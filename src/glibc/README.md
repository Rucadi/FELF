# FELF's glibc

Every FELF carries this glibc in place of the program's own: glibc's
libraries, and its dynamic linker patched to map libraries straight from
the FELF file.  `felf` embeds it, so every `felf` build needs one.  There
are two ways to get it:

- **Handed in**: `-DFELF_GLIBC_DIR=<dir> -DFELF_GLIBC_VERSION=<version>`,
  a directory of an already patched glibc (`libc.so.6`, `ld-linux*`, the
  other `lib*.so.*`).  Nix does this: `nix/pkgs/felf-glibc.nix` applies
  `patches/0001-felf-loader.patch` to nixpkgs' glibc, `nix/pkgs/felf.nix`
  passes it, and the development shell exports both variables (and
  `FELF_LOCALE_ARCHIVE_FILE`), so `felf_build` and plain `cmake` use it.
- **Built here** (`FELF_GLIBC_DIR` empty, the default): CMake downloads
  glibc `FELF_GLIBC_BUILD_VERSION` (2.44, checksum-verified), applies
  `patches/0001-felf-loader.patch` and `patches/vanilla/*.patch`, builds and
  installs it into the build directory, and compiles the locale archive
  (`FELF_GLIBC_LOCALES`: C.UTF-8 and en_US.UTF-8) with that glibc's own
  `localedef`.  It needs what building glibc needs: gcc, GNU make, gawk,
  bison, python3, `patch` and Linux headers.  The first build takes a
  while; later builds reuse it.  `docker/ubuntu.Dockerfile` builds and
  tests FELF this way on Ubuntu.

| Variable | Meaning |
|---|---|
| `FELF_GLIBC_DIR`, `FELF_GLIBC_VERSION` | A prebuilt, patched glibc to embed instead of building one |
| `FELF_LOCALE_ARCHIVE_FILE` | The locale archive to embed (default: generated with the glibc built here; with a handed-in glibc, the host's) |
| `FELF_GLIBC_BUILD_VERSION` | glibc version to build (2.44) |
| `FELF_GLIBC_SOURCE` | URL, tarball or unpacked tree to build (default: ftpmirror.gnu.org) |
| `FELF_GLIBC_SOURCE_HASH` | `SHA256=...` of `FELF_GLIBC_SOURCE` (known for the default) |
| `FELF_GLIBC_CC` | Compiler for glibc (default: `gcc`) |
| `FELF_GLIBC_KERNEL_HEADERS` | Linux headers for `--with-headers` (default: the compiler's) |
| `FELF_GLIBC_LOCALES` | Locales in the generated archive |

## Patches

- `patches/0001-felf-loader.patch`: the FELF loader (below).  Applied to
  both builds.
- `patches/vanilla/`: applied only to the glibc built here.  They give a
  plain glibc what nixpkgs' own patches give Nix's, and what FELF relies
  on:
  - `0001-locale-archive-env.patch`: libc opens `LOCALE_ARCHIVE_2_27` /
    `LOCALE_ARCHIVE` (the bootstrap points them at the embedded archive);
  - `0002-no-host-loader-config.patch`: the loader reads neither the
    host's `/etc/ld.so.preload` nor its `/etc/ld.so.cache` (the audit
    module does host lookups itself).

## The loader patch

With stock glibc, every embedded library becomes a memfd at each start,
because glibc only maps an ELF that begins at offset zero of a file.  The
patch, against glibc 2.44 as nixpkgs builds it (`nix/pkgs/felf-glibc.nix`):

1. `_dl_map_new_object()`: after `LD_AUDIT` has renamed the object, a name
   `felf-map-1:<hex offset>` (which FELF's audit module returns for an
   uncompressed library) is mapped by `felf_map_object()` from
   `/proc/self/fd/999`, the FELF file the bootstrap left open.  A
   `dlopen`'ed object is its own loader there, as in the normal path.
2. `_dl_map_object_from_fd()` takes the image's offset in the file: the
   program-header reads and every segment `mmap` add it (`struct
   loadcmd`'s new `file_offset`, `dl_pt_load_iterator`'s `file_offset`).
3. The object's file identity becomes (device −1, offset), so two
   libraries of one FELF file are never taken for the same file.

It does not touch relocation, TLS, symbol lookup or versioning, and it
changes no structure that `libc.so.6` shares with the loader
(`struct link_map` and `struct r_file_id` are as they were).  Nix takes
only `ld-linux` from the patched build and the stock `libc.so.6` and other
libraries of the same nixpkgs glibc; the CMake build takes all of them
from its one patched build.

Porting to another glibc: apply it to that glibc's source with nixpkgs'
patches applied, fix what does not apply in `elf/dl-load.c`,
`elf/dl-load.h` and `elf/dl-map-segments.h`, and run `nix build
.#felf-tests`.
