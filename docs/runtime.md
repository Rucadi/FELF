# Runtime

How a FELF starts and loads its libraries.  The descriptor numbers and
environment variables below are the handoff protocol between the
bootstrap, the audit module and the shim
(`src/include/felf_loader_protocol.h`).  `FELF_DEBUG=1` makes all three log
their decisions to stderr.

## Bootstrap

The kernel executes the bootstrap (`src/bootstrap/main.c`), a static musl
binary.  It:

1. **Finds itself** through `/proc/self/exe` (or `AT_EXECFN`), and requires
   `/proc/self/fd`.
2. **Reads the container**: maps the file, checks the outer ELF, the footer
   and the layout, and opens the database in memory (nothing is written to
   disk).
3. **Selects the program**: the one whose name or alias is the basename of
   `argv[0]`, else the default program.  Only that program is
   materialized.
4. **Finds the bundle root**: when the file sits at a program's recorded
   place in a tree (a copy of the FELF there), the root follows from that;
   otherwise from the stored root, relative to the file.
5. **Raises the soft open-file limit** if the objects need more
   descriptors than it allows, and restores the caller's value before the
   exec.
6. **Materializes objects**: every object that needs a file of its own (the
   program, the loader, the audit module, the shim, the locale archive, and
   libraries that are compressed) becomes a memfd, at
   descriptor 1000 or above (`FELF_FD_BASE`).  The memfds are filled in
   parallel, one job per 2 MiB, on up to 16 threads.  Uncompressed
   libraries are not copied when the loader maps them from the file; the
   bootstrap then keeps the FELF file open on descriptor 999
   (`FELF_CONTAINER_FD`).
7. **Writes the audit index** to descriptor 998 (`FELF_AUDIT_INDEX_FD`):
   the strict flag, the bundle root, a table from every library name
   (name, SONAME, aliases) to its location, the host-preferred patterns, the
   caller's `LD_LIBRARY_PATH` directories, and the host's library
   directories.  A location is `/proc/self/fd/N` for a memfd, or
   `felf-map-1:<hex offset>` for a library mapped from the file.
8. **Builds the environment**:
   - the caller's `LD_*` variables are removed, so they cannot redirect the
     bundled loader, and kept as `FELF_ORIG_<name>`;
   - the FELF's `environment` table is applied;
   - `LD_AUDIT` names the audit module's memfd; `LD_PRELOAD` the shim (and,
     without `--lazy`, the memfd libraries);
   - `FELF_APP_PATH` (the FELF file), `FELF_ROOT`, `FELF_STRICT`,
     `FELF_HANDOFF_VERSION`;
   - `LOCALE_ARCHIVE`, when the caller sets none: the always-embedded
     archive if the caller's locale is C, C.UTF-8 or unset, else the host's
     archive and then the embedded one.  When the caller set no locale at
     all it also sets `LANG=C.UTF-8`, so `setlocale(LC_ALL, "")` finds a
     UTF-8 locale (btop and Qt need it; an empty container has no locale
     configuration).
9. **Executes the program**: its `PT_INTERP` (in the memfd copy) is
   rewritten to `/proc/self/fd/<loader>`, and the program memfd is run with
   `execveat(fd, "", argv, envp, AT_EMPTY_PATH)`.  The kernel then performs
   a normal ELF exec and starts the bundled loader.

With `FELF_INSTALL_LINKS=DIR` set, the bootstrap stops after reading the
container: it makes `DIR/NAME` a symlink to the FELF for every program and
every alias of a program, and exits without running one.  A link is
relative (the FELF's basename) when DIR is the FELF's own directory, and
the FELF's absolute path otherwise.  An existing `DIR/NAME` is left alone:
already this FELF, it counts as done; anything else is reported, and the
exit status is 1.

## Audit module

`src/loader/felf_audit.c` is freestanding (raw system calls, no libc): it is
loaded before the application's libc can be used.  Its `la_objsearch()`
decides every library lookup:

1. `/proc/self/fd/...` paths (the bundled objects) are allowed.
2. A host-preferred name (the graphics stack: libglvnd, Mesa, Vulkan,
   DRM/GBM, VA-API/VDPAU, NVIDIA/CUDA; `src/include/felf_host_libs.h` and
   `--host-lib`) is looked up on the host first, as the host's loader would:
   the caller's `LD_LIBRARY_PATH`, `/etc/ld.so.cache`, the default and
   multiarch directories, `/run/opengl-driver/lib`.  Every candidate is
   checked to be an ELF for the FELF's architecture.  A bundled copy is
   only the fallback.
3. A name, or the basename of a path, that is in the index is answered with
   the bundled copy.  A library is therefore loaded once whether it is
   requested by SONAME or by an absolute build path.
4. An absolute path under the bundle root is allowed (plugins kept as files
   in a tree bundle).
5. Objects loaded from the host are tagged (`la_objopen()`); their own
   dependencies may come from the host too (a Mesa driver needs libLLVM),
   unless the name is bundled.
6. Anything else is denied in strict mode.  In permissive mode the host is
   searched instead of the bundled glibc's default directories.

## Direct mapping

FELF's dynamic linker is glibc's, patched (`src/glibc/patches/`,
[`../loader/README.md`](../loader/README.md)) to accept a library name of
the form `felf-map-1:<hex offset>`, which the audit module returns for a
library stored uncompressed in the FELF file.  The loader opens
`/proc/self/fd/999` and maps the ELF image at that offset like a library
on disk: program headers and segments are read and mapped relative to the
offset.  Each such object's file identity is (device −1, offset), so two
libraries of one file are never taken for the same one, and a library
requested again under another name is found again.

The mapping is ordinary file-backed `mmap`: nothing is copied at start,
pages are read on first use, unused libraries cost nothing, and processes
running the same FELF share the pages through the page cache.  Memfd and
mapped objects work side by side in one process.

## Runtime shim

`src/loader/felf_runtime.c`, preloaded into the application:

- **`/proc/self/exe`.**  The program actually runs from a memfd.
  `readlink()`, `realpath()` and `getauxval(AT_EXECFN)` of
  `/proc/self/exe` (and `/proc/<own pid>/exe`) report the FELF file
  instead, so programs find resources next to themselves.
- **Environment.**  Before `main()` it removes `LD_PRELOAD`, `LD_AUDIT`
  and the FELF handoff variables, and turns every `FELF_ORIG_<name>` back
  into `<name>`, so child processes see the caller's environment.
  `FELF_ROOT` stays.  It edits `environ` directly: a program may define its
  own `setenv`/`unsetenv` (bash does), and those would win over libc's.
- **Locale.**  When the bootstrap chose `LOCALE_ARCHIVE`, the shim makes
  glibc open the archive (glibc keeps it mapped) before removing the
  variable, which names a descriptor children do not have.  `LANG`, when
  the bootstrap defaulted it, stays for the application and its children.
- **Descriptors.**  Every descriptor from 998 up is made close-on-exec, so
  child processes do not inherit the FELF's objects.  They stay open for
  the application's own later `dlopen` calls.
