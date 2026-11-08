# bundle.nix — package a normal Nix derivation as a FELF.
#
# Tree mode (default):
#
#   $out/
#   ├── bin/<programName>   # the FELF: program, glibc, and every shared
#   │                       # library the program or its plugins need
#   ├── bin/qt.conf         # optional
#   ├── share/...           # original resources
#   └── lib/<plugin dirs>   # plugins stay as ordinary files
#
# Shared libraries the program links against are embedded in the FELF and
# removed from the top level of lib/.  Plugins (anything under a lib/
# subdirectory, Qt plugin and QML directories, ...) stay as real files,
# because plugin systems enumerate and mmap them; the libraries *they* need
# are embedded too, so they load without the Nix store.
#
# The FELF sets the application environment itself (no launcher script),
# and the embedded runtime shim makes /proc/self/exe report bin/<programName>
# so toolkits find qt.conf and ../share next to it.
#
# Set `tree = false` for a single self-contained executable.
{
  pkgs,
  felf,
}:
{
  package,

  # Relative path of the real ELF inside the package.  Defaults to
  # bin/<meta.mainProgram>.
  binary ? null,

  # Name of the executable under $out/bin.  Defaults to the ELF's basename.
  programName ? null,

  name ? "${package.name}-felf",

  tree ? true,

  # Strict: only bundled objects and files under the tree may be loaded.
  # GUI applications usually need strict = false so host graphics drivers
  # (libGLX_*/libEGL_*/Vulkan ICDs) can still be dlopen'ed.  Bundled names
  # always win in both modes.
  strict ? true,

  # Resolve bundled libraries on demand through the audit module instead of
  # preloading all of them.
  lazy ? true,

  # Additional DT_NEEDED search directories.
  libDirs ? [ ],

  # Shared objects to embed explicitly (e.g. libraries dlopen'ed by SONAME).
  includes ? [ ],

  # Also embed every shared library in the package's runtime closure, so
  # libraries it dlopen()s (by SONAME or by store path) are found even
  # though nothing links against them.  Where two have the same name, the
  # one the program links against wins, then the first in store-path order.
  # Libraries whose own dependencies cannot be resolved are left out with a
  # warning.  Lazy loading keeps the unused ones out of memory.
  closureLibs ? true,

  # Merge additional trees into the output: [ { src; dest; } ], dest
  # relative to $out.
  extraTrees ? [ ],

  # Files (usually the package's Nix wrapper scripts) to scan for
  # /nix/store/.../lib/qt-6/plugins and .../lib/qt-6/qml directories.  Those
  # directories are merged into $out/lib/qt-6/{plugins,qml}.
  qtPluginsFrom ? [ ],

  # Write bin/qt.conf so Qt uses the tree's plugin and QML directories.
  qtConf ? false,

  # Application environment, set by the FELF at startup and overriding the
  # caller's values.  Values may use $FELF_ROOT (the tree root), $FELF_APP,
  # and $NAME for the caller's variables; "$$" is a literal "$".
  env ? { },

  # Keep top-level lib/*.so* files even when they are embedded.
  keepEmbeddedLibs ? false,

  # More programs from the same package, sharing the FELF's libraries:
  # { name = "relative/path/to/elf"; }.  Each is embedded once and reached
  # through bin/<name>, a symlink to the FELF (the FELF runs the program
  # named like the symlink).  The original file, and a Nix wrapper at
  # bin/<name>, are replaced.  All programs must use the same glibc.
  programs ? { },

  # Also bundle every other dynamically linked executable in the package's
  # bin/ as a program of the same FELF, and replace it by a symlink to the
  # FELF.  Names that resolve to an already bundled file (bin/sh -> bash,
  # coreutils' multicall symlinks) become aliases of it.  Scripts and static
  # executables are left as they are.
  allPrograms ? false,

  # Compression: null is felf's default (libraries mapped uncompressed
  # from the file, the rest zstd level 19).  A zstd level (1-22) compresses
  # everything, the libraries too: a smaller file, but every library is then
  # decompressed into a memfd at start.  "none" compresses nothing.
  compress ? null,

  # Extra arguments for `felf pack`.
  extraArgs ? [ ],
}:
let
  inherit (pkgs) lib;

  defaultBinary = package.meta.mainProgram or package.pname or package.name;

  binaryRel =
    if binary == null then "bin/${defaultBinary}"
    else if lib.hasInfix "/" binary then binary
    else "bin/${binary}";

  executableName =
    if programName == null then baseNameOf binaryRel else programName;

  q = lib.escapeShellArg;

  programArgs = lib.concatStringsSep " " (lib.mapAttrsToList
    (pname: rel:
      # $out must stay outside the quotes so the shell expands it.
      if tree then ''--program ${q pname}="$out"/${q rel}''
      else "--program ${q "${pname}=${package}/${rel}"}")
    programs);

  # Replace each extra program by a symlink to the FELF.  In a tree, a
  # program whose file is already named after it keeps its place (a
  # compiler driver execs libexec/.../cc1 by path); others get bin/<name>.
  programLinks = lib.concatStrings (lib.mapAttrsToList (pname: rel:
    if tree && baseNameOf rel == pname then ''
      rm -f "$out/${rel}"
      ln -s "$(realpath --relative-to="$(dirname "$out/${rel}")" \
        "$out/bin/${executableName}")" "$out/${rel}"
    '' else ''
      ${lib.optionalString tree ''rm -f "$out/${rel}"''}
      rm -f "$out/bin/${pname}"
      ln -s ${q executableName} "$out/bin/${pname}"
    '') programs);


  # allPrograms: find the other dynamic executables at build time.  Each
  # becomes --program NAME=PATH with PATH the file it resolves to (its copy
  # in the tree), so names of the same file are aliases of one program.
  autoPrograms = lib.optionalString allPrograms ''
    auto=()
    : > auto-programs
    for f in ${q (toString package)}/bin/*; do
      name=$(basename "$f")
      case "$name" in
        ${lib.concatMapStringsSep "|" q
          ([ executableName (baseNameOf binaryRel) ] ++ lib.attrNames programs)}) continue ;;
      esac
      # A wrapper script contributes the program it runs.  Otherwise the
      # file it resolves to, so names of one file are aliases.
      resolve_program "$f"
      if [ "$WRAPPED" = 1 ]; then real=$RESOLVED; else real=$(realpath "$f"); fi
      [ -f "$real" ] && [ -x "$real" ] && is_dynamic_elf "$real" || continue
      keep_chain
      src=$real
      ${lib.optionalString tree ''src=$(tree_path "$real")''}
      auto+=(--program "$name=$src")
      echo "$name" >> auto-programs
    done
    echo "felf bundle: $(wc -l < auto-programs) more programs from bin/"
  '';
  autoProgramArgs = lib.optionalString allPrograms ''"''${auto[@]}"'';
  autoProgramLinks = lib.optionalString allPrograms ''
    while IFS= read -r name; do
      rm -f "$out/bin/$name"
      ln -s ${q executableName} "$out/bin/$name"
    done < auto-programs
  '';

  pkg = q (toString package);

  # Build-time helpers.  A program in nixpkgs is often a wrapper script
  # (makeWrapper) that sets up an environment and execs the real program.
  # The bundle takes the real program and leaves the wrapper's environment
  # out: setting up an environment is the package's business, not FELF's.
  programHelpers = ''
    is_dynamic_elf() {
      readelf -lW "$1" 2>/dev/null | grep -q "program interpreter"
    }
    # A path inside the package as the path of its copy in the tree.
    tree_path() {
      case "$1" in
        ${pkg}/*) printf '%s\n' "$out/''${1#${toString package}/}" ;;
        *) printf '%s\n' "$1" ;;
      esac
    }
    # resolve_program PATH: sets RESOLVED to the executable PATH runs and
    # WRAPPED=1 if a wrapper script was followed.  Through a wrapper,
    # RESOLVED is the last path inside the package on the way (a program
    # may find its data next to itself, e.g. Firefox in lib/firefox/), and
    # CHAIN holds the package paths that led there; keep_chain records them
    # in wrapper-chain, and in the tree they become symlinks to the FELF.
    resolve_program() {
      local p=$1 l n t
      WRAPPED=0 CHAIN=()
      while :; do
        while [ -L "$p" ]; do
          l=$(readlink "$p")
          case $l in /*) n=$l ;; *) n=$(dirname "$p")/$l ;; esac
          n=$(realpath -ms "$n")
          case $n in ${pkg}/*) ;; *) break ;; esac
          CHAIN+=("$p")
          p=$n
        done
        [ "$(head -c 2 "$p" 2>/dev/null)" = '#!' ] || break
        t=$(sed -n 's/^exec \(-a [^ ]* \)\{0,1\}"\(\/[^"]*\)".*/\2/p' "$p" |
            tail -n 1)
        [ -n "$t" ] || break
        WRAPPED=1
        CHAIN+=("$p")
        p=$t
      done
      RESOLVED=$p
    }
    keep_chain() {
      local p
      [ "$WRAPPED" = 1 ] || return 0
      for p in "''${CHAIN[@]}"; do
        case $p in
          ${pkg}/*) printf '%s\n' "''${p#${toString package}/}" >> wrapper-chain ;;
        esac
      done
    }
    : > wrapper-chain

    # The main program, and where the FELF goes ($felf_rel in the tree).
    resolve_program ${q "${package}/${binaryRel}"}
    main=$RESOLVED
    if ! is_dynamic_elf "$main"; then
      echo "felf bundle: ${binaryRel} is not a dynamically linked executable" \
        "(set binary = \"...\" to the real program)" >&2
      exit 1
    fi
    felf_rel=${q "bin/${executableName}"}
    keep_chain
    if [ "$WRAPPED" = 1 ]; then
      case $main in ${pkg}/*) felf_rel=''${main#${toString package}/} ;; esac
      echo "felf bundle: ${binaryRel} is a wrapper script; bundling" \
        "''${main#${toString package}/} (the wrapper's environment is not kept)"
    fi
  '';

  # closureLibs: the closure's shared libraries, as --include arguments in
  # an @FILE.  In a tree the package's own files are already there (as
  # plugins).  Only ELF files: lib*.so can also be a linker script.
  closure = pkgs.closureInfo { rootPaths = [ package ]; };
  closureList = lib.optionalString closureLibs ''
    while IFS= read -r p; do
      ${lib.optionalString tree ''[ "$p" = ${pkg} ] && continue''}
      [ -d "$p" ] || continue
      find "$p" -type f \( -name '*.so' -o -name '*.so.*' \) | LC_ALL=C sort
    done < ${closure}/store-paths | while IFS= read -r f; do
      IFS= read -r -N 4 magic < "$f" || true
      [ "$magic" = $'\177ELF' ] && printf -- '--include=%s\n' "$f"
    done > closure-libs || true
    echo "felf bundle: $(wc -l < closure-libs) shared objects in the closure"
  '';
  closureArgs = lib.optionalString closureLibs "@closure-libs";

  commonArgs = lib.concatStringsSep " " (
  lib.optional (!strict) "--permissive"
  ++ lib.optional (compress != null) "--compress ${q (toString compress)}"
  ++ lib.optional lazy "--lazy"
  ++ map (path: "--include ${q path}") includes
  ++ map (dir: "--lib-dir ${q dir}") libDirs
  ++ lib.mapAttrsToList (k: v: "--env ${q "${k}=${v}"}") env
  ++ map q extraArgs);

  extraTreeCommands = lib.concatMapStringsSep "\n" (t: ''
    mkdir -p "$out/${t.dest}"
    cp -aL ${q t.src}/. "$out/${t.dest}/"
    chmod -R u+w "$out/${t.dest}"
  '') extraTrees;

  qtHarvest = lib.optionalString (qtPluginsFrom != [ ]) ''
    re="/nix/store/[^'\":[:space:]]+/lib/qt-6/(plugins|qml)"
    for f in ${lib.concatMapStringsSep " " q qtPluginsFrom}; do
      tr '\0' '\n' < "$f" | grep -oE "$re" || true
    done | sort -u > qt-dirs
    while IFS= read -r dir; do
      [ -d "$dir" ] || continue
      case "$dir" in
        */plugins) dest="$out/lib/qt-6/plugins" ;;
        *) dest="$out/lib/qt-6/qml" ;;
      esac
      echo "felf bundle: merging $dir"
      mkdir -p "$dest"
      chmod -R u+w "$dest"
      # First directory wins on conflicts, like a search path.
      cp -aL --update=none "$dir/." "$dest/"
    done < qt-dirs
    chmod -R u+w "$out/lib/qt-6" 2>/dev/null || true
  '';

  qtConfFile = ''
    cat > "$out/bin/qt.conf" <<'EOF'
    [Paths]
    Prefix=..
    Plugins=lib/qt-6/plugins
    QmlImports=lib/qt-6/qml
    Qml2Imports=lib/qt-6/qml
    EOF
  '';

  removeEmbeddedLibs = lib.optionalString (!keepEmbeddedLibs) ''
    if [ -d "$out/lib" ]; then
      felf sql "$out/bin/${executableName}" \
        "SELECT lower(hex(sha256)) FROM objects WHERE kind IN ('shared','plugin')" \
        | tail -n +2 | sort -u > embedded-hashes
      find "$out/lib" -maxdepth 1 -type f \( -name '*.so' -o -name '*.so.*' \) \
        -print0 | while IFS= read -r -d "" f; do
          h=$(sha256sum "$f" | cut -d' ' -f1)
          if grep -qx "$h" embedded-hashes; then
            rm -f "$f"
          fi
        done
    fi
  '';
in
pkgs.runCommand name {
  nativeBuildInputs = [
    felf pkgs.coreutils pkgs.findutils pkgs.gnugrep pkgs.binutils
  ];
  meta = {
    description = "FELF-packaged ${package.name}${lib.optionalString tree " tree"}";
    mainProgram = executableName;
    platforms = [ "x86_64-linux" "aarch64-linux" ];
  };
  passthru = {
    inherit package;
    felf = {
      inherit tree strict lazy compress libDirs includes
        closureLibs
        extraTrees qtPluginsFrom qtConf env keepEmbeddedLibs programs allPrograms;
    };
  };
} (
  if tree then ''
    set -euo pipefail
    ${programHelpers}
    mkdir -p "$out"
    cp -aL ${q (toString package)}/. "$out/"
    chmod -R u+w "$out"
    ${extraTreeCommands}
    ${qtHarvest}

    # Every shared object that stays in the tree is a plugin: keep the file,
    # embed its dependency closure.
    main_src=$(tree_path "$main")
    find "$out" -type f \( -name '*.so' -o -name '*.so.*' \) \
      ! -path "$main_src" | LC_ALL=C sort | sed "s/^/--plugin=/" > plugin-list
    echo "felf bundle: $(wc -l < plugin-list) plugin files in the tree"

    ${autoPrograms}
    ${closureList}
    # The FELF takes the program's place, $felf_rel (bin/<name> unless the
    # program was behind a wrapper); the bundle root is the tree.
    felf_dir=$(dirname "$felf_rel")
    mkdir -p "$out/bin" "$out/$felf_dir"
    root=$(realpath -m --relative-to="$out/$felf_dir" "$out")
    felf pack ${q executableName}="$main_src" ${commonArgs} \
      ${programArgs} ${autoProgramArgs} --root "$root" \
      ${closureArgs} \
      @plugin-list \
      -o "$out/$felf_dir/.${executableName}.felf"
    if [ "${binaryRel}" != "bin/${executableName}" ]; then
      rm -f "$out/${binaryRel}"
    fi
    mv "$out/$felf_dir/.${executableName}.felf" "$out/$felf_rel"
    chmod 0755 "$out/$felf_rel"

    # bin/<name>, and the wrapper scripts and symlinks that led to a wrapped
    # program, become symlinks to the FELF.
    link_to_felf() {
      rm -f "$out/$1"
      ln -s "$(realpath -m --relative-to="$(dirname "$out/$1")" \
        "$out/$felf_rel")" "$out/$1"
    }
    if [ "$felf_rel" != ${q "bin/${executableName}"} ]; then
      link_to_felf ${q "bin/${executableName}"}
    fi
    sort -u wrapper-chain | while IFS= read -r p; do
      [ "$p" = "$felf_rel" ] || link_to_felf "$p"
    done
    ${programLinks}
    ${autoProgramLinks}

    ${removeEmbeddedLibs}
    ${lib.optionalString qtConf qtConfFile}
    felf inspect "$out/bin/${executableName}" > inspect.txt
    head -n 20 inspect.txt
  '' else ''
    set -euo pipefail
    ${programHelpers}
    mkdir -p "$out/bin"
    ${autoPrograms}
    ${closureList}
    felf pack ${q executableName}="$main" ${commonArgs} \
      ${programArgs} ${autoProgramArgs} \
      ${closureArgs} \
      -o "$out/bin/${executableName}"
    ${programLinks}
    ${autoProgramLinks}
  ''
)
