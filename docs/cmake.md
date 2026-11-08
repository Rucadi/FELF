# CMake

`cmake/FELF.cmake` packs a project's executables into FELFs as part of its
build.  It does not build FELF: it uses a `felf` already installed, or
downloads the static `felf` of a GitHub release at configure time.

## Getting the module

Each release attaches `FELF.cmake`, with the SHA-256 of that release's
`felf` binaries written into it.  Download it with a pinned hash (from the
release's `SHA256SUMS`), and every binary it fetches is pinned too:

```cmake
set(felf_module "${CMAKE_BINARY_DIR}/FELF.cmake")
if(NOT EXISTS "${felf_module}")
    file(DOWNLOAD
        https://github.com/Rucadi/FELF/releases/download/v0.1.0/FELF.cmake
        "${felf_module}"
        EXPECTED_HASH SHA256=<FELF.cmake's hash from SHA256SUMS>)
endif()
include("${felf_module}")
```

Or copy it into the project (`cmake/FELF.cmake`) and `include()` it.  A copy
taken from the source tree has no checksums in it, so it checks the binary
against the release's `SHA256SUMS` instead.

## A project, start to finish

```cmake
cmake_minimum_required(VERSION 3.21)
project(myapp C)

include(cmake/FELF.cmake)          # or the file(DOWNLOAD) above

add_library(mylib SHARED lib.c)
add_executable(myapp main.c)
target_link_libraries(myapp PRIVATE mylib)

felf_pack(myapp-felf
    TARGET myapp
    LAZY
    INSTALL_DESTINATION bin)
```

```bash
cmake -S . -B build
cmake --build build                       # build/myapp.felf
cmake --install build --prefix dist       # dist/bin/myapp.felf
```

The first configure downloads felf into `build/_felf/<version>/felf` when
none is installed.  The build compiles the program as usual, then packs
`build/myapp.felf`, which runs on any Linux of the same architecture
without installing anything.  Only CMake and the project's own toolchain
are needed; felf is never built.

A program with plugins, a second executable and data files:

```cmake
felf_pack(myapp-felf
    TARGET myapp
    INCLUDES myplugin                   # dlopen'ed: list it, it is not linked
    PROGRAMS myapp-cli=myapp_cli        # a second program in the same FELF
    ENV MYAPP_DATA=$FELF_ROOT/share/myapp
    ROOT ..                             # the FELF in bin/, data in ../share
    COMPRESS 19                         # smaller file, libraries in memfds
    INSTALL_DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/myapp)
```

`myapp_cli` and `myplugin` are targets of the project.  The installed
`dist/` is the tree to ship (as a tarball, for example); invoked through a
symlink named `myapp-cli`, the FELF runs the second program.

## `felf_pack`

```cmake
add_library(greet SHARED greet.c)
add_library(extra SHARED extra.c)          # dlopen'ed at run time
add_executable(app main.c)
target_link_libraries(app PRIVATE greet)

felf_pack(app-felf
    TARGET app
    INCLUDES extra
    ENV APP_DATA=$FELF_ROOT/share/app
    LAZY
    INSTALL_DESTINATION bin)
```

`felf_pack(<name> ...)` adds the custom target `<name>`, built by default,
which writes `<binary dir>/<output name>.felf` from the target's file
(the property `FELF_OUTPUT` of `<name>` holds the path).  The FELF is
packed again whenever the program or a library target it names changes.

| Argument | `felf pack` option |
|---|---|
| `TARGET <target>` or `PROGRAM <path>` | The main program |
| `NAME <name>` | `NAME=` prefix of the main program |
| `OUTPUT <path>` | `-o`; relative to the current binary directory |
| `PROGRAMS <name>=<target\|path>...` | `--program` |
| `ALIASES <program>=<names>...` | `--alias` |
| `INCLUDES <target\|path>...` | `--include` |
| `PLUGINS <target\|path>...` | `--plugin` |
| `LIB_DIRS <dir>...` | `--lib-dir` |
| `HOST_LIBS <pattern>...` | `--host-lib` |
| `ENV <NAME=VALUE>...` | `--env`; `$FELF_ROOT`, `$FELF_APP` and `$VAR` are passed unexpanded |
| `ROOT <rel>` | `--root` |
| `COMPRESS <level\|none>` | `--compress`: a level compresses the libraries too |
| `LAZY`, `PERMISSIVE`, `NO_DEFAULT_HOST_LIBS` | the flags of the same name |
| `EXTRA_ARGS <arg>...` | passed as they are |
| `DEPENDS <file\|target>...` | more dependencies of the pack step |
| `EXCLUDE_FROM_ALL` | build it only when asked for |
| `INSTALL_DESTINATION <dir>` [`INSTALL_COMPONENT <c>`] | `install(PROGRAMS)` the FELF |

The project's own shared libraries are found through the build tree's
`RUNPATH`, which CMake sets.  With `CMAKE_SKIP_BUILD_RPATH`, name their
directories in `LIB_DIRS`.

## Which felf

| Variable | Default | Meaning |
|---|---|---|
| `FELF_EXECUTABLE` | `felf` on `PATH` | The felf to use; none found means download |
| `FELF_DOWNLOAD` | `ON` | Download when there is no felf |
| `FELF_VERSION` | the module's release | Release to download |
| `FELF_DOWNLOAD_DIR` | `<build>/_felf` | Where downloads are kept (`<version>/felf`) |
| `FELF_RELEASE_URL` | GitHub releases of Rucadi/FELF | Base URL, for a mirror |

The felf found is the imported target `felf::felf`, and its version is in
`FELF_FOUND_VERSION`.  Releases exist for x86-64 and aarch64 Linux.

## Limits

- **glibc.**  Programs are packed with FELF's glibc (2.44 for 0.1.0), so
  they must be built against that glibc or an older one: any current
  distribution's toolchain works, and `felf pack` rejects a program that
  needs newer symbols.
- **Architecture.**  A felf packs for its own architecture.  For a
  cross-compiled program, run felf with `FELF_BOOTSTRAP`, `FELF_AUDIT` and
  `FELF_SHIM` naming builds for the target ([packing.md](packing.md));
  `felf_pack` warns when `CMAKE_CROSSCOMPILING` is set.
- **Data files.**  A FELF carries code, not the files a program opens by
  path.  Install those next to it (`share/...`) and point the program at
  them with `ENV` and `$FELF_ROOT`, with `ROOT ..` for a FELF in `bin/`.
