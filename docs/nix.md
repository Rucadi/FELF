# Nix

`flake.nix` and the root `default.nix` (no flakes; nixpkgs pinned from
`flake.lock`) expose the package set in `nix/default.nix`.

## Packages

| Attribute | File | What it is |
|---|---|---|
| `felf` | `nix/pkgs/felf.nix` | The CLI, static (musl), embedding the three below |
| `felf-bootstrap` | `nix/pkgs/felf-bootstrap.nix` | The static bootstrap |
| `felf-runtime` | `nix/pkgs/felf-runtime.nix` | The audit module and runtime shim, built against glibc |
| `felf-glibc` | `nix/pkgs/felf-glibc.nix` | FELF's glibc: nixpkgs' glibc libraries and its loader built with `src/glibc/patches/0001-felf-loader.patch` (a full glibc build, once) |
| `felf-locales` | `nix/default.nix` | The locale archive (C.UTF-8, en_US.UTF-8) every FELF embeds |
| `bundle-hello` | `nix/default.nix` | `pkgs.hello` as a FELF tree |
| `bundle-gcc` | `nix/bundles/gcc.nix` | A relocatable GCC toolchain: driver, `cc1`, `as`, `ld`... in one FELF |
| `bundle-gcc-tarball` | `nix/default.nix` | `bundle-gcc` as a tarball |
| `bundle-krita` | `nix/bundles/krita.nix` | Krita with its Qt plugins |
| `oci-coreutils-bash` | `nix/examples/oci-coreutils-bash.nix` | An OCI image whose only executable is a FELF with bash and coreutils; every command is a symlink to it |

`nix-shell` (via the root `shell.nix`), `nix-shell -A shells.default` or
`nix develop` gives a development shell.  It provides two helpers:

```bash
felf_build                 # cmake configure + build in build/host
felf_test                  # ctest in build/host
felf_test -L run -j 8      # arguments are passed to ctest
FELF_BUILD_DIR=build/dev felf_build
```

`felf_build` passes any extra arguments to the CMake configure step.  The
shell exports `FELF_GLIBC_DIR`, `FELF_GLIBC_VERSION` and
`FELF_LOCALE_ARCHIVE_FILE` (the `felf-glibc` and `felf-locales` the package
embeds); CMake reads them from the environment, so a plain `cmake` in the
shell uses them too instead of building glibc itself
(`src/glibc/README.md`).  Entering the shell the first time builds
`felf-glibc` (a full glibc build).  The shell's `CC` is gcc; clang and the
musl compiler (`x86_64-unknown-linux-musl-gcc`, for static builds) are on
`PATH` by name.

## `lib.bundle`: a package as a FELF

`nix/lib/bundle.nix` turns a derivation into a FELF.  By default it builds
a *tree*: a copy of the package in which `bin/<program>` is the FELF, and
data and plugins stay as ordinary files next to it.  Libraries the FELF
embeds are removed from the top of `lib/`.

```nix
bundle {
  package = pkgs.hello;
}
```

| Argument | Default | Meaning |
|---|---|---|
| `package` | (required) | The derivation |
| `binary` | `bin/<meta.mainProgram>` | The program, relative to the package |
| `programName` | the binary's name | Its name under `bin/` |
| `tree` | `true` | `false`: a single self-contained executable |
| `allPrograms` | `false` | Bundle every dynamically linked executable in `bin/` into the same FELF; the others become symlinks to it |
| `programs` | `{ }` | More programs: `{ name = "path/in/package"; }` |
| `closureLibs` | `true` | Also embed every shared library of the package's runtime closure, so libraries it `dlopen`s are found |
| `includes` | `[ ]` | More shared objects to embed |
| `libDirs` | `[ ]` | More library directories |
| `compressLibraries` | `false` | `true` compresses the libraries too; they then become memfds at start |
| `compress` | `null` | zstd level or `"none"` for objects copied into memory; `null` is felf's default |
| `strict` | `true` | `false` lets unknown names come from the host (GUI programs may need it for host graphics drivers) |
| `lazy` | `true` | Load libraries on demand through the audit module |
| `env` | `{ }` | Variables for the application; values may use `$FELF_ROOT`, `$FELF_APP`, `$NAME` |
| `extraTrees` | `[ ]` | More trees merged into the output: `{ src; dest; }` |
| `qtPluginsFrom`, `qtConf` | `[ ]`, `false` | Gather Qt plugin and QML directories named in the given files (wrapper scripts) into the tree, and write `bin/qt.conf` |
| `keepEmbeddedLibs` | `false` | Keep `lib/*.so*` even when embedded |
| `extraArgs` | `[ ]` | More `felf pack` arguments |

**Wrapper scripts.**  A nixpkgs program is often a `makeWrapper` script.
The bundler follows its `exec "..."` line, through nested wrappers, to the
real executable and bundles that, at its place in the tree (so it finds
files next to itself).  The wrapper and `bin/<name>` become symlinks to the
FELF.  The wrapper's environment is not kept; use `env` when a program
needs variables.

**Store paths.**  Paths compiled into a program as `/nix/store/...` are not
redirected and do not exist on other hosts; the packer scrubs them from
every embedded object ([packing.md](packing.md)).

## `nix bundle`

The flake is a bundler (`nix/bundler.nix`): a package becomes a tree with
every program of its `bin/` in one FELF, packed as
`<name>-<system>.tar.gz` in the current directory.

```bash
nix bundle --bundler .# nixpkgs#jq
tar -xzf jq-*.tar.gz
./jq-*/bin/jq --version
```

Without flakes: `nix-build -E '(import ./. { }).bundler (import <nixpkgs> { }).jq'`.

## `lib.tarball`

`nix/lib/tarball.nix` packs a tree into `<name>-<system>.tar[.gz]` that
extracts into `<name>/`.  It keeps the tree's relative symlinks, refuses
absolute ones and ones that leave the tree, and is reproducible (sorted
entries, fixed times and owners).

## Checks

`nix flake check` builds:

| Check | Tests |
|---|---|
| `felf-tests` | The `tests/` CTest suite against the built `felf`, plus that `felf` is static |
| `felf-reproducibility` | Two packs of one input are identical |
| `bundle-hello-test` | The hello bundle runs, with its libraries mapped from the file |
| `bundle-gcc-test`, `bundle-gcc-tarball-test` | The bundled GCC compiles and links C, C++ and LTO programs from its own tree |
| `oci-coreutils-bash-test` | The commands run with only the image's root file system on `PATH`, and its layer holds one FELF plus symlinks (the root file system may not reference the store) |
| `bundler-test` | `nix bundle` of coreutils: every command runs from the extracted tarball, and `bin/` holds one FELF plus symlinks |
