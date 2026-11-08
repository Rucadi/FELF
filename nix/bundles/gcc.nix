# A relocatable GCC toolchain as a FELF tree.
#
#   $out/bin/gcc                       the FELF: gcc, g++, c++, cpp, cc1,
#                                      cc1plus, collect2, lto1, lto-wrapper,
#                                      as and ld, sharing one set of libraries
#   $out/bin/{g++,c++,cpp}             symlinks to bin/gcc
#   $out/libexec/gcc/<triple>/<ver>/   cc1, cc1plus, collect2, lto1,
#                                      lto-wrapper, as, ld: symlinks to the
#                                      FELF, where the driver looks for them
#   $out/lib/gcc/<triple>/<ver>/specs  defaults for the bundled toolchain
#   $out/sysroot/{include,lib}         glibc + Linux headers, crt files,
#                                      libc/libstdc++/libgcc_s to link with
#
# The GCC driver relocates itself from argv[0], so the tree works wherever it
# is unpacked.  Everything Nix's cc-wrapper would add on the command line
# comes from the specs file instead, which GCC reads from its lib directory:
#
#   - glibc headers:        -idirafter $FELF_ROOT/sysroot/include
#   - crt files, libraries:  $FELF_ROOT/sysroot/lib (start files and -L)
#   - dynamic linker:        dynamicLinker (the distribution path), not the
#                            Nix store path GCC was built with
#
# $FELF_ROOT is set by the FELF for every process it starts.
#
# Programs built with it link against the bundled glibc, so they need a host
# glibc at least as new as the symbol versions they use.  libstdc++ is linked
# statically by default (staticLibstdcxx).
{
  pkgs,
  # lib/bundle.nix applied to the FELF tools (nix/default.nix: bundle).
  bundle,
  gcc ? pkgs.gcc.cc,
  binutils ? pkgs.binutils-unwrapped,
  glibc ? pkgs.glibc,
  linuxHeaders ? pkgs.linuxHeaders,
  # Interpreter written into the programs this toolchain links (the
  # distributions' path; the flake passes the one for the platform).
  dynamicLinker ? "/lib64/ld-linux-x86-64.so.2",
  # Link libstdc++ statically into C++ programs.  The host's libstdc++ is
  # usually older than GCC's (Debian 12 and Ubuntu 22.04 lack the symbol
  # versions GCC 16 emits), so dynamically linked C++ programs would only
  # run on hosts as new as the toolchain.
  staticLibstdcxx ? true,
}:
let
  inherit (pkgs) lib;
  triple = pkgs.stdenv.hostPlatform.config;
  version = gcc.version;
  libexec = "libexec/gcc/${triple}/${version}";
  # The glibc loader's file name as a sed -E regex.
  loaderRe = builtins.replaceStrings [ "." ] [ "\\." ]
    (baseNameOf pkgs.stdenv.cc.bintools.dynamicLinker);
  gccLib = "lib/gcc/${triple}/${version}";

  toolchain = pkgs.runCommand "gcc-${version}-toolchain" {
    nativeBuildInputs = [ pkgs.gawk ];
  } ''
    set -euo pipefail
    mkdir -p "$out"
    cp -a ${gcc}/. "$out/"
    chmod -R u+w "$out"
    rm -rf "$out/nix-support" "$out/share"
    # Only the tools bundled below are kept; the rest would be ELF files
    # with a Nix store interpreter.
    rm -f "$out"/bin/${triple}-* "$out"/bin/gcov* "$out"/bin/lto-dump \
      "$out"/bin/gcc-ar "$out"/bin/gcc-nm "$out"/bin/gcc-ranlib \
      "$out/${libexec}/g++-mapper-server"

    # The driver runs as, collect2 runs ld; both look in libexec first.
    cp ${binutils}/bin/as "$out/${libexec}/as"
    cp ${binutils}/bin/ld.bfd "$out/${libexec}/ld"

    # Sysroot: the same glibc as the bundle's, so headers, crt files and
    # libc.so.6 agree with what the FELF runs on.
    mkdir -p "$out/sysroot/include" "$out/sysroot/lib"
    # glibc's include/ links linux/, asm/, ... into the kernel headers.
    cp -rL ${glibc.dev}/include/. "$out/sysroot/include/"
    chmod -R u+w "$out/sysroot/include"
    cp -rL --update=none ${linuxHeaders}/include/. "$out/sysroot/include/"
    cp -a ${glibc}/lib/*.so* ${glibc}/lib/*.o ${glibc}/lib/*.a \
      "$out/sysroot/lib/" 2>/dev/null || true
    cp -a ${gcc.lib}/lib/*.so* "$out/sysroot/lib/"
    cp -a ${gcc}/lib/libstdc++*.a ${gcc}/lib/libsupc++.a "$out/sysroot/lib/"
    rm -f "$out"/sysroot/lib/*-gdb.py
    chmod -R u+w "$out/sysroot"
    ${lib.optionalString staticLibstdcxx ''
      # -lstdc++ resolves to the archive (the .so.6 stays for -shared users
      # who link it explicitly by path).
      rm -f "$out/sysroot/lib/libstdc++.so"
      echo "INPUT ( libstdc++.a )" > "$out/sysroot/lib/libstdc++.so"
    ''}
    # Linker scripts (libc.so, libm.so, ...) name Nix store paths; bare
    # names are searched in the -L directories instead.
    for f in "$out"/sysroot/lib/*.so; do
      [ -L "$f" ] && continue
      if ! head -c 4 "$f" | grep -q ELF; then
        sed -i -E 's|/nix/store/[^ )]*/lib/||g' "$f"
      fi
    done

    # specs: GCC's built-in specs plus the sysroot, with the distribution's
    # dynamic linker.
    ${gcc}/bin/gcc -dumpspecs > specs.in
    sed -E 's|/nix/store/[^ }:;]*/lib(64)?/${loaderRe}|${dynamicLinker}|g' \
      specs.in > specs.ld
    awk '
      /^\*cpp_unique_options:$/ { print; getline;
        print $0 " -idirafter %:getenv(FELF_ROOT /sysroot/include)"; next }
      /^\*startfile_prefix_spec:$/ { print; getline;
        print "%:getenv(FELF_ROOT /sysroot/lib/)"; next }
      /^\*link:$/ { print; getline;
        print $0 " -L%:getenv(FELF_ROOT /sysroot/lib)"; next }
      { print }
    ' specs.ld > "$out/${gccLib}/specs"
    grep -q "FELF_ROOT /sysroot/include" "$out/${gccLib}/specs"
    grep -q "FELF_ROOT /sysroot/lib/" "$out/${gccLib}/specs"
    if grep -qE "/nix/store/[^ }:;]*${loaderRe}" "$out/${gccLib}/specs"; then
      echo "gcc.nix: store dynamic linker left in specs" >&2
      exit 1
    fi
  '';
in
bundle {
  package = toolchain;
  name = "gcc-${version}-felf";
  binary = "bin/gcc";
  programName = "gcc";
  programs = {
    "g++" = "bin/g++";
    "c++" = "bin/c++";
    cpp = "bin/cpp";
    cc1 = "${libexec}/cc1";
    cc1plus = "${libexec}/cc1plus";
    collect2 = "${libexec}/collect2";
    lto1 = "${libexec}/lto1";
    lto-wrapper = "${libexec}/lto-wrapper";
    as = "${libexec}/as";
    ld = "${libexec}/ld";
  };
  # The toolchain needs nothing from the host.  Files under the tree (the
  # LTO plugin) may still be loaded.
  strict = true;
  lazy = true;
  # sysroot/lib and lib/ hold the libraries programs are linked against;
  # they must stay even though the same bytes are embedded.
  keepEmbeddedLibs = true;
}
