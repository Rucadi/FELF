# Development shell: compilers and tools, the static busybox the tests use,
# and the felf_build / felf_test helpers.  It exports FELF's glibc and
# locale archive (the same ones nix/pkgs/felf.nix embeds), which every felf
# build requires; CMake reads them from the environment.
{
  pkgs,
  felf-glibc,
  felf-locales,
}:
let
  static = pkgs.pkgsStatic;

  # Extra compilers on PATH without their setup hooks: as shell packages
  # their hooks make them the shell's compiler (CC, NIX_CC), and the stdenv
  # gcc then puts glibc's headers on -isystem, which breaks C++.  The
  # shell's CC stays its stdenv's gcc; clang and the musl compiler
  # (x86_64-unknown-linux-musl-gcc ...) are there to call by name.
  bare = cc: pkgs.runCommand "${cc.name}-bin" { } ''
    mkdir -p $out
    ln -s ${cc}/bin $out/bin
  '';

  # Both helpers work on the checkout they are run from.  FELF_BUILD_DIR
  # selects another build directory (default: build/host).
  felfBuild = pkgs.writeShellScriptBin "felf_build" ''
    set -euo pipefail

    root=$(git rev-parse --show-toplevel 2>/dev/null || pwd)
    if [ ! -f "$root/CMakeLists.txt" ]; then
        echo "felf_build: $root is not the FELF source tree" >&2
        exit 1
    fi

    build_dir=''${FELF_BUILD_DIR:-build/host}
    case "$build_dir" in
        /*) ;;
        *) build_dir="$root/$build_dir" ;;
    esac

    cmake -S "$root" -B "$build_dir" -G Ninja "$@"
    cmake --build "$build_dir"
  '';

  felfTest = pkgs.writeShellScriptBin "felf_test" ''
    set -euo pipefail

    root=$(git rev-parse --show-toplevel 2>/dev/null || pwd)
    build_dir=''${FELF_BUILD_DIR:-build/host}
    case "$build_dir" in
        /*) ;;
        *) build_dir="$root/$build_dir" ;;
    esac

    if [ ! -f "$build_dir/CMakeCache.txt" ]; then
        echo "felf_test: $build_dir is not configured; run felf_build first" >&2
        exit 1
    fi

    ctest --test-dir "$build_dir" --output-on-failure "$@"
  '';
in
pkgs.mkShell {
  packages = with pkgs; [
    felfBuild felfTest
    gcc (bare clang) binutils cmake ninja python3 sqlite sqlite-interactive zstd
    pkg-config patchelf gdb strace gnumake git file jq curl unzip
    tcl glibc.bin gcc.cc.lib libgcc
    (bare static.stdenv.cc) static.busybox
    # Building FELF's glibc with CMake (FELF_GLIBC_DIR unset).
    bison gawk
  ];
  shellHook = ''
    echo "FELF development shell"
    echo "  gcc:      $(gcc --version | head -1)"
    echo "  glibc:    $(ldd --version | head -1)"
    echo "  sqlite:   $(sqlite3 --version)"
    echo "  runtime:  FELF's glibc $FELF_GLIBC_VERSION"
    echo "  build:    felf_build [cmake args]"
    echo "  test:     felf_test [ctest args]"
  '';

  FELF_GLIBC_DIR = "${felf-glibc}/lib";
  FELF_GLIBC_VERSION = felf-glibc.version;
  FELF_LOCALE_ARCHIVE_FILE = "${felf-locales}/lib/locale/locale-archive";
  FELF_TEST_BUSYBOX = "${static.busybox}/bin/busybox";
}
