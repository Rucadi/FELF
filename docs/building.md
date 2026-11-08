# Building and testing

FELF builds with plain CMake; Nix is a convenience that caches the slow
part, the glibc build.

## With CMake

```bash
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

This downloads glibc 2.44, patches and builds it once, then builds
everything else. It needs what building glibc needs: gcc, make, gawk,
bison, python3, `patch`, Linux headers ([src/glibc/README.md](../src/glibc/README.md)).
SQLite and zstd are vendored.

To reuse a prebuilt glibc instead, pass `-DFELF_GLIBC_DIR=…
-DFELF_GLIBC_VERSION=…` (and `-DFELF_LOCALE_ARCHIVE_FILE=…`); CMake also
reads them from the environment.

[`docker/ubuntu.Dockerfile`](../docker/ubuntu.Dockerfile) runs exactly this
on Ubuntu 26.04:

```bash
docker build -f docker/ubuntu.Dockerfile -t felf:ubuntu .
```

## With Nix

```bash
nix flake check      # build everything, run every check
nix develop          # shell with FELF's glibc prebuilt
felf_build           # configure + build build/host
felf_test -L run     # ctest, arguments passed through
```

`FELF_BUILD_DIR` picks another build directory. The shell exports
`FELF_GLIBC_DIR`, `FELF_GLIBC_VERSION` and `FELF_LOCALE_ARCHIVE_FILE`, so
plain `cmake` there skips the glibc build. clang and a musl compiler are on
`PATH` by name.

## Tests

Plain CTest: fixture programs are CMake targets and packed files are CTest
fixtures, so `ctest -N` lists everything and a failed setup skips its
dependents. Tests needing a static bootstrap or busybox run only with
`FELF_TEST_STATIC_BOOTSTRAP` / `FELF_TEST_BUSYBOX`
([tests/README.md](../tests/README.md)).

## Repository map

| Path | Contents |
|---|---|
| `src/bootstrap/` | the bootstrap |
| `src/loader/` | audit module and shim |
| `src/packer/` | the CLI, ELF parsing, dependency resolution |
| `src/storage/`, `src/format/` | the SQLite index, footer, SHA-256 |
| `src/glibc/` | the loader patch and the glibc build |
| `src/include/` | format and protocol headers |
| `cmake/` | `FELF.cmake` for user projects |
| `tests/` | the CTest suite |
| `nix/` | packages, the OCI example, checks |
| `docker/` | the Ubuntu build |
| `third_party/` | SQLite, zstd |
