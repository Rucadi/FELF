# FELF — Fat ELF

FELF packages an ordinary dynamically linked Linux ELF program, its private
glibc interpreter and shared libraries, and an embedded immutable SQLite
database into one executable file.

```text
program.felf
├── valid outer Linux ELF
│   └── statically linked musl bootstrap
├── page-aligned embedded ELF objects
├── read-only SQLite database
└── FELF footer
```

The result runs as a normal dynamically linked application, but resolves its
bundled runtime internally rather than from the host.  It does not require
Nix, root, package installation, or persistent extraction of libraries on the
target.

## Status

**Working on Linux / glibc for x86-64; aarch64 is built but untested.**

- `felf`: a static binary with `pack`, `inspect`, `list`, `verify`,
  `extract` and read-only `sql`.
- Static musl bootstrap; FELF format version 1, SQLite schema version 1.
- One glibc for every program: FELF's glibc (nixpkgs' 2.44), whose patched
  loader maps uncompressed libraries straight from the FELF file.
  Programs built for that glibc or an older one work, from Nix or from
  other distributions; newer ones are rejected at pack time.
- Libraries mapped from the file, everything else in zstd-compressed
  memfds; an explicit `--compress LEVEL` compresses the libraries too.
- Strict resolution through a freestanding `LD_AUDIT` module, with the host
  graphics stack preferred.
- Runtime shim: `/proc/self/exe` reports the FELF path, and child processes
  get the caller's original environment.
- Every FELF always embeds a glibc locale archive (C.UTF-8, en_US.UTF-8);
  a caller with no locale at all starts in `LANG=C.UTF-8`, so programs such
  as btop find a UTF-8 locale even in an empty container.
- Tree bundles: plugins stay as files, every library they need is embedded.
- Deterministic output; two identical packs compare byte-for-byte.
- An integration suite of over 150 checks (`nix build .#felf-tests`),
  including a `/nix`-masked execution test.

Not yet implemented: signatures, musl programs, architectures beyond x86-64
and aarch64, and transparent resource APIs.

## Quick start

```bash
nix develop
nix build .#felf

gcc -fPIE -pie hello.c -o hello
./result/bin/felf pack ./hello \
    --lazy \
    -o hello.felf

./result/bin/felf inspect hello.felf
./result/bin/felf verify hello.felf
./hello.felf
```

The packer bundles the loader named by the input's `PT_INTERP` and
resolves `DT_NEEDED` libraries from the input's `RPATH`/`RUNPATH`, then the
loader's directory and the standard library directories (`--lib-dir` adds
more).  The generated `hello.felf` does not reference any of these paths at
runtime.

Without flakes, `default.nix` builds the same derivations from the nixpkgs
revision pinned in `flake.lock`: `nix-build -A felf`, `nix-build -A
bundle-hello`, `nix-shell -A shells.default`.

### Prebuilt binaries

Each [GitHub release](https://github.com/Rucadi/FELF/releases) carries the
static `felf` CLI for x86-64 and aarch64, with `SHA256SUMS`; it needs
nothing on the host:

```bash
curl -LO https://github.com/Rucadi/FELF/releases/download/v0.1.0/felf-0.1.0-x86_64-linux
chmod +x felf-0.1.0-x86_64-linux
./felf-0.1.0-x86_64-linux --version
```

Releases are built by `.github/workflows/release.yml` when a tag `vX.Y.Z`
matching `felfVersion` in `nix/default.nix` is pushed.

### CMake projects

`cmake/FELF.cmake` (also attached to each release) packs a project's
programs as part of its build, with a released felf it downloads at
configure time when none is installed ([docs/cmake.md](docs/cmake.md)):

```cmake
include(FELF.cmake)
add_executable(app main.c)
felf_pack(app-felf TARGET app LAZY INSTALL_DESTINATION bin)   # app.felf
```

### Bundling a normal Nix package

`nix/lib/bundle.nix` turns a normal Nix derivation into a FELF **tree** by
default:

```text
$out/bin/<program>     the FELF: program + glibc + every shared library the
                       program or any plugin in the tree needs
$out/bin/qt.conf       optional
$out/share/...         original resources
$out/lib/<subdirs>/    plugins, unchanged, as ordinary files
```

Libraries the program links against are embedded and removed from the top
of `lib/`.  Everything under a `lib/` subdirectory is treated as a plugin:
the file stays (plugin systems enumerate and `mmap` plugin files), and its
dependency closure is embedded, so it loads without the Nix store.  The
FELF sets the application environment itself, and the runtime shim makes
`/proc/self/exe` point at `bin/<program>`, so Qt finds `qt.conf` and
`../share`.  Set `tree = false` for a single self-contained file.

```nix
let
  flake = builtins.getFlake (toString ./.);
  pkgs = import flake.inputs.nixpkgs { system = "x86_64-linux"; };
  bundle = flake.lib.x86_64-linux.bundle;
  # Without flakes: (import ./default.nix { }).bundle
in
bundle {
  package = pkgs.hello;
  # binary = "hello";              # default: meta.mainProgram
  # programName = "hello";         # name under $out/bin
  # tree = true;                   # default; false = single file
  # compress = 19;                # a level: smaller, libraries in memfds
  # strict = true;                 # false: unknown names fall back to host
  # lazy = true;
  # libDirs = [ "${pkgs.openssl.out}/lib" ];
  # closureLibs = true;            # embed every .so of the runtime closure
  # includes = [ "${pkgs.sdl3.lib}/lib/libSDL3.so.0" ];   # dlopen'ed by name
  # extraTrees = [ { src = ...; dest = "lib/qt-6/plugins"; } ];
  # qtPluginsFrom = [ "${pkg}/bin/app" ];  # harvest Qt plugin/QML dirs
  # qtConf = true;
  # env = { QT_PLUGIN_PATH = "$FELF_ROOT/lib/qt-6/plugins"; };
}
```

The flake includes a worked example:

```bash
nix build .#bundle-hello
./result/bin/hello
```

`env` values may use `$FELF_ROOT` (the tree root), `$FELF_APP` (the FELF
file), and `$NAME` for any variable of the caller's environment; `$$` is a
literal `$`.  These values override the caller's, which matters on NixOS,
where `QT_PLUGIN_PATH` and friends point at host Qt plugins.

### `nix bundle`: any package as a .tar.gz

The flake is a `nix bundle` bundler.  It turns a package into a FELF tree
and leaves `<name>-<system>.tar.gz` in the current directory:

```bash
nix bundle --bundler .# nixpkgs#jq     # in a checkout; elsewhere: --bundler /path/to/felf#
tar -xzf jq-1.8.2-x86_64-linux.tar.gz
./jq-1.8.2/bin/jq --version
```

The archive extracts into `<name>/`: `bin/` holds one FELF with every
dynamically linked executable of the package's `bin/` (`allPrograms`; the
others are symlinks to it, multicall names such as coreutils' or `sh` for
`bash` become aliases), and the rest of the package (`share/`, plugins under
`lib/`) is kept as data next to it.  The main program is the package's
`meta.mainProgram`, else its `pname`.  Without flakes:
`nix-build -E '(import ./. { }).bundler (import <nixpkgs> { }).jq'`.

A program behind a nixpkgs wrapper script (`makeWrapper`, as for
`nixpkgs#firefox`) is bundled as the executable the wrapper runs, at its
place in the tree (`lib/firefox/firefox`, so it finds its files next to
itself); the wrapper and `bin/<name>` become symlinks to the FELF.  The
wrapper's environment is not kept: setting one up is the package's
business, not FELF's (use `env` in `nix/lib/bundle.nix` when a program
needs variables).

Every shared library in the package's runtime closure is embedded
(`closureLibs`, on by default), not only those the program links
against, so libraries it `dlopen()`s by name or by store path — audio
(PulseAudio, PipeWire), codecs (ffmpeg), libva, glibc's gconv modules —
load on a host that has none of them.  This costs size, memory and start
time: every start decompresses every embedded library into memory, loaded
or not (Firefox: a 184 MB FELF instead of 27 MB, and `firefox --version`
peaks at 770 MB instead of 100 MB and takes 0.28 s instead of 0.05 s).
Where two libraries have the same
name, the one the program links against wins; one whose own dependencies
cannot be found is left out with a warning.  Set `closureLibs = false` to
embed only the link-time closure plus `includes`.

Data the program finds relative to its executable or through an
environment variable is used from the tree.  Paths compiled into the
program as `/nix/store/...` (for example gettext's translations, or
fontconfig's configuration: set `FONTCONFIG_FILE=/etc/fonts/fonts.conf`)
are not redirected, and other scripts in `bin/` are left as they are.

### Krita

```bash
nix build .#bundle-krita
./result/bin/krita            # FELF_DEBUG=1 ./result/bin/krita to trace
```

Krita is the stress test.  Its Qt plugin and QML directories are taken from
the nixpkgs wrapper (`qtPluginsFrom`), copied into the tree, and their
dependencies embedded.  It runs in permissive mode so host GL/Vulkan drivers
can still load.

### GCC

```bash
nix build .#bundle-gcc
export PATH=$PWD/result/bin:$PATH
gcc -O2 hello.c -o hello && ./hello
```

`nix/bundles/gcc.nix` builds a relocatable GCC toolchain tree.  The driver,
`cc1`, `cc1plus`, `collect2`, `lto1`, `lto-wrapper` and binutils' `as` and `ld` are
one multi-program FELF (`bin/gcc`); `bin/g++`, `bin/c++`, `bin/cpp` and the
`libexec/gcc/<triple>/<version>/` tools are symlinks to it, where the driver
looks for them.  `sysroot/` holds glibc and Linux headers, crt files and the
libc/libstdc++/libgcc_s to link against (the same glibc the FELF runs on).

What Nix's compiler wrapper would add on the command line comes from a
`specs` file in GCC's lib directory, which GCC reads automatically:
`-idirafter $FELF_ROOT/sysroot/include`, the sysroot's `lib/` for start
files and `-L`, and the distribution's dynamic linker
(`/lib64/ld-linux-x86-64.so.2` or `/lib/ld-linux-aarch64.so.1`, the
`dynamicLinker` argument) instead of the
Nix store loader GCC was built with.  The GCC driver relocates itself from
`argv[0]`, so the tree works wherever it is unpacked.

To ship it, use the tarball:

```bash
nix build .#bundle-gcc-tarball
tar -xf result/gcc-16.2.0-x86_64-linux.tar -C /opt   # -> /opt/gcc-16.2.0/
export PATH=/opt/gcc-16.2.0/bin:$PATH
```

`nix/lib/tarball.nix` packs any tree this way: one top-level directory,
the tree's relative symlinks kept (the FELF is stored once), owner-writable files,
root:root and fixed timestamps (reproducible).  The build fails if an entry
is absolute or a symlink leads outside the directory, so extracting it
cannot write anywhere else.

If the tree is copied some other way, keep its symlinks (`cp -a`, `tar`
without `-h`).  A copy that dereferences them still works (the packer
records where each program sits, so a copy of the FELF at
`libexec/.../ld` finds the tree root from its own path), but every program
then becomes a full copy of the 183 MB FELF.

libstdc++ is linked statically into C++ programs by default
(`staticLibstdcxx`): the hosts' libstdc++ is usually older than GCC 16's
(`GLIBCXX_3.4.32` is missing on Debian 12 and Ubuntu 22.04).

The sysroot is glibc 2.44, so that is what programs are compiled against.
The glibc a program needs at runtime depends on what it uses: with GCC 16's
default `-std=gnu23`, glibc 2.44's headers map `strtol`/`sscanf` & co. to
the `__isoc23_*` variants (`GLIBC_2.38`), and the static libstdc++ uses
`arc4random` (2.36) and `__isoc23_strtoul` (2.38).  Such programs run on
Ubuntu 24.04 but not on Debian 12 or Ubuntu 22.04; `-std=gnu17` C programs
that avoid newer functions do.  Static linking (`-static`) is not set up:
the sysroot has no `libc.a`.

### CLI

```bash
felf pack INPUT -o OUTPUT [options]
felf inspect FILE [--json]
felf list FILE [--json]
felf verify FILE
felf extract FILE --output DIR
felf sql FILE "SELECT ..."
felf --help
felf --version
```

Pack options:

```text
-o, --output PATH          Output FELF executable
    --program [NAME=]PATH  Add another program (run via a symlink NAME)
    --alias PROG=NAME,...  More argv[0] names for a multicall PROG
    --compress LEVEL|none  Compress everything, libraries too, at zstd
                           LEVEL 1-22; none: nothing (see Compression)
    --permissive           Unknown names fall back to the host (default: strict)
    --lazy                 Resolve bundled SONAMEs on demand via audit
    --include PATH         Also bundle a shared object (names already
                           bundled are skipped; failures are warnings)
    --plugin PATH          Keep PATH outside, bundle its dependencies
    --root REL             Top of the tree the FELF is part of, relative
                           to the FELF's directory (e.g. .. for bin/app)
    --env NAME=VALUE       Application environment ($FELF_ROOT, $VAR)
    --lib-dir DIR          Also look for libraries to embed in DIR (pack time)
    --host-lib PATTERN     Prefer the host copy (name or prefix*)
    --no-default-host-libs Do not prefer the host graphics stack
```

An argument `@FILE` stands for the lines of FILE, one argument per line
(`--include=/path/libfoo.so.1`; empty lines and `#` comments are skipped),
for argument lists too long for a command line.

The bootstrap, the audit module and the runtime shim that every FELF
carries are built into `felf` itself.  `FELF_BOOTSTRAP`, `FELF_AUDIT` and
`FELF_SHIM` name other files to use instead, for example the aarch64 builds
when packing for aarch64 on x86-64.

`FELF_DEBUG=1` makes the bootstrap, audit module, and shim log what they
resolve and why.

### Several programs in one FELF

Programs that use the same glibc can share one FELF, so their libraries are
embedded once.  Each extra program is added with `--program [NAME=]PATH`
(the positional input is the default program, and also accepts
`NAME=PATH`); NAME defaults to the file's basename.  The bootstrap runs the
program whose name is the basename of `argv[0]`, so each program is reached
through a symlink named after it; any other name runs the default program.
Only the selected program is materialized.

```bash
felf pack hello=./hello --program ./tool --program cli=./cli-real -o bin/hello
ln -s hello bin/tool
ln -s hello bin/cli
```

`nix/lib/bundle.nix` does this with `programs = { name = "relative/path"; }`: the
Krita bundle's `bin/kritarunner` and `bin/krita_version` are symlinks to
`bin/krita`.  `felf inspect` lists the programs and marks the default.

A multicall binary (coreutils, busybox) dispatches on `argv[0]` itself.
`--alias PROGRAM=NAME[,NAME...]` gives it its names: the binary is embedded
once and runs with NAME as `argv[0]`.  (Naming the same file again with
`--program NAME=PATH` does the same, one name at a time.)

```bash
felf pack bash=$BASH --program coreutils=$CU/bin/coreutils \
    --alias coreutils=ls,cat,cp -o bin/bash
felf pack ./busybox --alias "busybox=$(./busybox --list | paste -sd,)" \
    -o bin/busybox
```

Run a FELF with `FELF_INSTALL_LINKS=DIR` to make the symlinks: it creates
`DIR/NAME` → the FELF for every program and alias, and runs nothing (like
`busybox --install -s`).  A link next to the FELF is relative (`ls ->
bash`), one elsewhere points to the FELF's absolute path.  Existing files
are not replaced; it exits 1 if a name could not be linked, and running it
again is harmless.

```bash
FELF_INSTALL_LINKS=bin bin/bash    # bin/coreutils, bin/ls, bin/cat, bin/cp
```

### OCI image: one FELF, everything else a symlink

`nix/examples/oci-coreutils-bash.nix` builds a single-layer image whose only
regular file is `/bin/bash`, a FELF holding bash, all of coreutils and
their libraries.  The other 110 entries in `/bin` (`sh`, `ls`, `cat`, ...)
are symlinks to it, and the image has no `/nix`.

```bash
nix build .#oci-coreutils-bash
docker load < result              # or: podman load < result
docker run --rm -it felf-coreutils-bash
docker run --rm felf-coreutils-bash -c 'ls -l /bin | head; echo hi | sha256sum'
```

The FELF derivation sets `allowedReferences = [ ]`, so the build fails if
the FELF would drag a store closure into the layer.
`nix build .#oci-coreutils-bash-test` checks the layer and runs the
commands.

### Architectures

FELF targets Linux/glibc on x86-64 and aarch64 (`src/include/felf_arch.h`).
The packer takes the target from the program's `e_machine`; every embedded
object and the bootstrap must match it, and search directories skip
libraries built for another architecture.  The bootstrap and audit module
are built per architecture; packing for aarch64 on an x86-64 machine is
meant to work when `FELF_BOOTSTRAP`, `FELF_AUDIT` and `FELF_SHIM` name the aarch64
builds, but has not been tested yet.
The flake builds every package for both `x86_64-linux` and
`aarch64-linux` (for example `.#packages.aarch64-linux.oci-coreutils-bash`).

### Host libraries and user plugins

The graphics stack always comes from the host: libglvnd (`libGL`, `libEGL`,
`libGLX`, `libGLdispatch`, `libOpenGL`, GLES), vendor drivers (`libGLX_*`,
`libEGL_*`, `libnvidia-*`, `libgallium-*`), Vulkan (`libvulkan.so.1`,
`libvulkan_*`), DRM/GBM, VA-API/VDPAU and CUDA.  The list is in
`src/include/felf_host_libs.h`; `--host-lib` adds patterns.

At runtime the audit module looks such a name up the way the host's ld.so
would (caller's `LD_LIBRARY_PATH`, `/etc/ld.so.cache`, the default and
multiarch directories, and `/run/opengl-driver/lib`), checking that each
candidate is an ELF for the FELF's architecture.  If the packer found a copy at pack time it is
embedded and used **only** when the host has none (NixOS keeps libglvnd in
the store, so this fallback is what runs there).  Objects loaded from the
host may pull their own dependencies (libLLVM, libexpat, ...) from the host,
even in strict mode, except names that are bundled: one copy per SONAME.

A plugin is never embedded.  `--plugin` only embeds the plugin's
dependencies that the program itself does not already cover.  Any plugin,
including one a user drops in later from anywhere on disk, links against
the embedded libraries; in strict mode only plugins under the bundle root
may load, in permissive mode (`--permissive`) any path may.

The bundled glibc is the one the host libraries run against, so a host
library built for a newer glibc than the bundled one will not load.

## How it runs

1. The kernel executes the static bootstrap.
2. The bootstrap validates the footer, SQLite database, and object records.
3. It materializes the embedded program and private `ld-linux` as memfds.
4. It rewrites the program's `PT_INTERP` to `/proc/self/fd/<loader>`.
5. It calls `execveat(program_fd, "", argv, envp, AT_EMPTY_PATH)`.
6. The kernel performs a normal ELF exec and starts the private glibc loader.
7. Bundled libraries resolve either from `LD_PRELOAD` or, in `--lazy` mode,
   through the audit module's name-to-`/proc/self/fd` index.  Plugins under
   the bundle root load from their files.
8. Strict mode denies everything else; permissive mode lets unknown names
   fall back to the host.
9. The runtime shim (first in `LD_PRELOAD`) fixes `/proc/self/exe`, removes
   `LD_AUDIT`/`LD_PRELOAD`, restores the caller's `LD_*` variables, and marks
   FELF descriptors close-on-exec, so child processes are unaffected.

No auxiliary vector is fabricated.  No jump into glibc internals is performed.
No persistent library extraction occurs.

## How libraries are loaded

Every FELF carries FELF's glibc, not the program's own: nixpkgs' glibc
libraries with a dynamic linker patched to map a library straight from the
FELF file (`src/glibc/patches/0001-felf-loader.patch`; `nix/pkgs/felf-glibc.nix`
builds it, and `felf` embeds it: `felf --version` names the version).  glibc
is backward compatible, so any program built for that glibc or an older one
works, a Debian binary as well as a Nix package.  The packer rejects a
program, or a library, that needs a symbol version FELF's glibc does not
define (`... needs version GLIBC_2.45 of libc.so.6 ...`).

One FELF mixes both ways of loading:

- **Mapped from the file:** by default, libraries
  are stored uncompressed and page-aligned, and the loader maps them like
  any shared library on disk.  Nothing is copied at start, a library that is
  never loaded costs nothing, and processes running the same FELF share its
  pages.
- **Memfds:** everything that has to be a file of its own (the program, the
  loader, the audit module, the shim, the locale archive) is compressed and
  copied into a memfd at each start, as is every library when
  `--compress LEVEL` is given.

The packer rewrites the `/nix/store` hashes in every embedded object to
`eeee…`, as nixpkgs' `nuke-refs` does, so Nix sees no dependencies in a
FELF (an OCI layer stays self-contained); those store paths therefore never
resolve, even on a host that has them.

How it works: the audit module answers a lookup for a mapped library with
`felf-map-1:<offset>` instead of a `/proc/self/fd/N` path; the patched
loader recognizes the prefix and maps the object at that offset of the FELF
file, which the bootstrap leaves open on descriptor 999.  Objects get the
file identity (device −1, offset), so two libraries in one file are never
taken for the same one.  Only `ld-linux` is patched: it changes nothing the
loader shares with `libc.so.6`.

`felf` itself is a static (musl) binary; it embeds the bootstrap, the audit
module and shim (`nix/pkgs/felf-runtime.nix`), and FELF's glibc.

## Storage

Every object's bytes are in page-aligned payload regions of the file; SQLite
holds only metadata: offsets, sizes, alignment, hashes, and whether each
object is compressed.  With FELF's loader (above) the uncompressed libraries
are mapped from the file; everything else is materialized as memfds.

### Compression

By default, libraries FELF's loader maps from the file are stored
uncompressed and every other object is compressed at zstd level 19.
An explicit `--compress LEVEL` (zstd, 1-22) compresses everything, the
libraries too, which trades start-up time and shared pages for a smaller
file: the libraries then become memfds.  `--compress none` stores every
object as it is (`compress` in `nix/lib/bundle.nix`).  A compressed object
is a sequence of independent zstd frames of 2 MiB (`FELF_ZSTD_CHUNK`).  `size` and `sha256` stay
those of the uncompressed object.  An object that does not shrink is stored
as is.

The bootstrap creates every memfd in order, maps it, and fills them all in
parallel, one job per frame (or per 2 MiB of an uncompressed object), on up
to 16 threads.  Every start decompresses all objects it materializes.
Measured on a 16-core machine:

| | uncompressed | zstd |
|---|---|---|
| GCC FELF | 183 MB | 58 MB |
| Krita FELF | 573 MB | 168 MB |
| `gcc --version` | 0.010 s | 0.013 s |
| compile hello (5 FELF starts) | 0.088 s | 0.102 s |
| `krita --version` | 0.54 s | 0.40 s |

Krita starts faster compressed because the parallel fill replaced a
sequential copy.  On fewer cores the decompression cost grows accordingly.

## Testing

```bash
nix flake check
nix develop          # or nix-shell, for the tools used below
```

Local development, from the development shell:

```bash
felf_build                                  # configure + build build/host
felf_test                                   # ctest --output-on-failure
felf_test -L run                            # one label
felf_test -R direct -V                      # one group, verbose
```

`felf_build` passes extra arguments to CMake configure, and
`FELF_BUILD_DIR=build/other` selects another build directory.  Without Nix,
plain CMake builds everything, FELF's patched glibc included (it downloads
glibc 2.44 and builds it once; see [`src/glibc/README.md`](src/glibc/README.md)
for what that needs):

```bash
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

[`docker/ubuntu.Dockerfile`](docker/ubuntu.Dockerfile) does exactly that on
Ubuntu 26.04, with no Nix: it builds the static bootstrap with musl, then
everything else with CMake building FELF's glibc, and runs the suite.

```bash
docker build -f docker/ubuntu.Dockerfile -t felf:ubuntu .
```

The tests are ordinary CTest tests: fixture programs are CMake targets and
packed files are CTest fixtures, so `ctest -N` lists them and a failed setup
skips its dependents instead of hiding the failure.  Tests that need a
static bootstrap or a static busybox are only registered when the
corresponding option is set (`FELF_TEST_STATIC_BOOTSTRAP`,
`FELF_TEST_BUSYBOX`); see
[`tests/README.md`](tests/README.md).  The suite does not claim
cross-distribution support that was not executed.

Every `felf` embeds FELF's glibc and a glibc locale archive.  Built here,
CMake makes both; `-DFELF_GLIBC_DIR=... -DFELF_GLIBC_VERSION=...` (and
`-DFELF_LOCALE_ARCHIVE_FILE=...`) hand in prebuilt ones instead.  The
development shell exports all three (Nix's), and CMake reads them from the
environment.

SQLite and zstd are vendored in `third_party/` and built from source; no
system libraries are needed besides a C compiler and libc.

## Repository map

| Path | Purpose |
|---|---|
| `src/bootstrap/` | Static musl bootstrap and handoff |
| `src/format/` | Footer, validation, SHA-256 |
| `src/storage/` | SQLite read/write paths and schema |
| `src/packer/` | ELF parsing, dependency resolution, CLI |
| `src/loader/` | Audit module and runtime shim |
| `src/glibc/` | FELF's glibc: its patches, and the CMake build used when `FELF_GLIBC_DIR` is not given |
| `src/include/` | Public format, storage, and handoff headers |
| `nix/` | Nix package set: `pkgs/` (felf, bootstrap, runtime), `lib/` (`bundle.nix`, `tarball.nix`), `bundles/` (GCC, Krita), `examples/` (OCI image), `tests/` |
| `tests/` | Integration fixtures and CTest suite |
| `docker/` | `ubuntu.Dockerfile`: the build and tests without Nix |
| `cmake/` | `FELF.cmake`: `felf_pack()` for CMake projects |
| `docs/` | Architecture, format, packing, runtime, CMake, Nix, security |
| `flake.nix`, `default.nix` | Entry points (flake, and without flakes) for `nix/` |
| `third_party/` | Vendored SQLite and zstd sources |

## Documentation

- [`docs/README.md`](docs/README.md): index
- [`docs/architecture.md`](docs/architecture.md): components, packing and
  start-up
- [`docs/format.md`](docs/format.md): file layout, footer, database
- [`docs/packing.md`](docs/packing.md): `felf pack` and dependency resolution
- [`docs/runtime.md`](docs/runtime.md): bootstrap, audit module, shim,
  direct mapping
- [`docs/nix.md`](docs/nix.md): packages, `bundle.nix`, `nix bundle`, checks
- [`docs/security.md`](docs/security.md): guarantees and limits
- [`src/glibc/README.md`](src/glibc/README.md): FELF's glibc and the loader patch

## Licensing

The project license has not been selected.  Bundling glibc creates LGPL
redistribution and source-availability obligations.  The optional glibc patch,
if shipped, would additionally require publishing its corresponding source and
modifications.  This is tracked as an open decision.

## Security note

SHA-256 checksums detect corruption.  They do **not** authenticate the file
against deliberate tampering.  FELF version 1 has no signatures.  Strict mode
is a library-resolution policy, not a filesystem sandbox.
