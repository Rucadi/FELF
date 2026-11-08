# Packing

```bash
felf pack [NAME=]PROGRAM -o OUT [options]
```

`felf --help` lists the options.  An argument `@FILE` is replaced by the
lines of FILE, one argument per line (empty lines and `#` comments are
skipped), for argument lists too long for a command line:

```text
--include=/path/to/libfoo.so.1
--plugin=/path/to/tree/lib/plugins/libbar.so
```

## Options

| Option | Effect |
|---|---|
| `-o PATH` | The FELF to write (written to a temporary file and renamed) |
| `--program [NAME=]PATH` | Another program in the same FELF, run when the FELF is invoked as NAME (a symlink to it).  Programs share the libraries.  A second name for an already embedded file becomes an alias, so multicall binaries keep one copy. |
| `--alias PROGRAM=NAME[,NAME...]` | More names for an embedded program (a multicall binary such as coreutils or busybox): invoked as NAME, it runs with NAME as `argv[0]`.  PROGRAM is a program name or one of its aliases; NAME must not name another object. |
| `--compress-libraries` | Compress the libraries too, for a smaller file.  They are then decompressed into memfds at start instead of mapped from the file. |
| `--compress LEVEL\|none` | zstd level for the objects that are copied into memory at start (default 19).  Libraries FELF's loader maps from the file are not compressed unless `--compress-libraries` is given. |
| `--permissive` | Names the FELF does not bundle may be loaded from the host.  The default is strict: only bundled objects, files under the bundle root, and the host graphics stack. |
| `--lazy` | Libraries are loaded on demand by name through the audit module.  Without it, libraries kept in memfds are also preloaded with `LD_PRELOAD`. |
| `--include PATH` | Embed a shared object and its dependencies, for libraries the program only `dlopen`s.  A library whose name is already bundled is skipped; one that is not a library for the target or whose dependencies cannot be found is a warning. |
| `--plugin PATH` | A library that stays a file outside the FELF (a plugin in a tree bundle): its dependencies are embedded.  Unresolvable dependencies are a warning. |
| `--root REL` | The top of the directory tree the FELF is part of, relative to the FELF's directory (default: that directory); for example `..` for `tree/bin/app`.  Resolved from the FELF's location at each start, so the tree can be moved.  `$FELF_ROOT` points there, and strict mode allows loading files under it (plugins kept as files). |
| `--env NAME=VALUE` | A variable set for the application, overriding the caller's |
| `--lib-dir DIR` | Another directory to find the libraries to embed in, searched after the standard ones (see below).  Used at pack time only. |
| `--host-lib PATTERN` | Prefer the host's copy of matching libraries (a name, or a prefix ending in `*`) |
| `--no-default-host-libs` | Do not prefer the host's graphics stack |

The bootstrap, audit module and shim are built into `felf`;
`FELF_BOOTSTRAP`, `FELF_AUDIT` and `FELF_SHIM` name other files to use
instead (for example builds for another architecture).

## The runtime: FELF's glibc

`felf` carries FELF's glibc (`felf --version` names it): nixpkgs' glibc
libraries and its dynamic linker patched to map libraries from the FELF file
(`src/glibc/patches/`).  When it has a loader for the program's architecture,
every program is bundled with it:

- its dynamic linker is the FELF's interpreter, whatever the program's own
  `PT_INTERP` names (the program's loader need not exist on the packing
  machine);
- a library found in FELF's glibc (`libc.so.6`, `libm.so.6`, ...) is
  taken from there before any other directory.

glibc is backward compatible, so programs built for that glibc or an older
one work, from Nix or from any distribution.  The version check below
rejects programs that need a newer one.

Every FELF also embeds felf's glibc locale archive (C.UTF-8 and
en_US.UTF-8); this is not optional.  The bundled glibc has no usable host
locale location, and the bootstrap names C.UTF-8 when the caller set no
locale, so `setlocale(LC_ALL, "")` works in an empty container.  See
[runtime.md](runtime.md).

## Finding dependencies

For each `DT_NEEDED` name, in order:

1. a name already bundled is reused (one copy per name);
2. FELF's glibc, for glibc's libraries;
3. the requesting object's `RUNPATH` (or `RPATH` when it has none), with
   `$ORIGIN`, `$LIB` and `$PLATFORM` expanded;
4. the loader's directory and the standard directories (`/lib`, `/lib64`,
   `/usr/lib`, `/usr/lib64`, the multiarch directories, `/usr/local/lib`);
5. the `--lib-dir` directories.

Only regular files that are ELF objects for the target architecture match;
32-bit or foreign libraries and linker scripts are skipped.  A name that
cannot be found is an error, unless it matches a host-preferred pattern:
then the host provides it at run time.

The architecture comes from the first program.  Every embedded object, and
the bootstrap, must be built for it.

## Checks

- **Symbol versions.**  Every version an embedded object requires
  (`DT_VERNEED`, except weak ones) from another embedded object must be
  defined there (`DT_VERDEF`), as glibc's loader would require.  A program
  built against a newer glibc fails with `... needs version GLIBC_x of
  libc.so.6, which the bundled libc.so.6 ... does not define`.
- **Names.**  Object names are unique; a program name may not contain `/`.

## Storage decisions

Every object's bytes are in the file's payload region; the database only
holds metadata, including each object's compression.  With FELF's glibc,
the database records `direct_map`, and:

- libraries (`shared`, `plugin`) are stored uncompressed, at offsets
  aligned for `mmap`, so the loader maps them from the file (with
  `--compress-libraries` they are compressed and become memfds instead);
- everything else (program, loader, audit module, shim, locale archive) is
  compressed with `--compress`.

In every object, `/nix/store/<hash>` strings are rewritten to
`/nix/store/eeee...` (as nixpkgs' `nuke-refs` does), so Nix sees no
dependencies in a FELF.  Compression would not hide them: zstd keeps many
strings as literal bytes.  Those paths no longer resolve, even on a host
that has them; a FELF does not need them (the bootstrap rewrites the
program's `PT_INTERP`, and the audit module resolves every library).  A
path the program itself opens, such as a data directory compiled in as
`/nix/store/...`, did not exist on other hosts anyway.

## Other commands

| Command | Does |
|---|---|
| `felf inspect FILE [--json]` | Summary: modes, programs, objects and their compression |
| `felf list FILE [--json]` | The objects |
| `felf verify FILE` | Checks the layout and every object's SHA-256 |
| `felf extract FILE --output DIR` | Writes every object, decompressed, and the database (`felf.sqlite`) into DIR |
| `felf sql FILE "SELECT ..."` | Runs a read-only query on the database |

## Extracting

`felf extract FILE --output DIR` writes the database as `DIR/felf.sqlite`
and every object, decompressed, as `DIR/<name>`: the object's name with
characters other than `[A-Za-z0-9._+-]` replaced by `_`, and `.<id>`
(its row in `objects`) appended when that name is not unique or is
`felf.sqlite`.  The database alone decides the names, so nothing else is
written.
