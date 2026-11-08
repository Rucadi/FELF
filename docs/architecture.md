# Architecture

## Components

| Component | Source | What it is |
|---|---|---|
| `felf` CLI | `src/packer/`, `src/storage/store_build.c` | Packs, inspects, verifies and extracts FELFs.  A static (musl) binary that embeds everything below. |
| Bootstrap | `src/bootstrap/` | A static musl executable at the start of every FELF.  The kernel runs it; it prepares the embedded objects and executes the program. |
| Audit module | `src/loader/felf_audit.c` | A freestanding `LD_AUDIT` module.  Decides, for every library the program loads, which copy is used. |
| Runtime shim | `src/loader/felf_runtime.c` | A small `LD_PRELOAD` library.  Makes `/proc/self/exe` name the FELF file and gives child processes the caller's environment. |
| FELF's glibc | `nix/pkgs/felf-glibc.nix`, `src/glibc/patches/` | nixpkgs' glibc libraries with a dynamic linker patched to map libraries straight from the FELF file.  Bundled with every program. |
| Format code | `src/format/`, `src/include/` | Footer, SHA-256, shared definitions and the bootstrap/loader protocol. |
| Storage | `src/storage/` | The SQLite database inside each FELF: schema, writer, reader. |
| Vendored libraries | `third_party/` | SQLite and zstd, built from source into the CLI and the bootstrap. |

## A FELF file

```text
+---------------------------+  offset 0
| bootstrap (static ELF)    |  what the kernel executes
+---------------------------+  page-aligned
| payload: object bytes     |  every object, each aligned
|   program, loader, libs,  |  to its largest segment alignment
|   audit module, shim, ... |
+---------------------------+  page-aligned
| SQLite database           |  metadata: objects, aliases, dependencies,
|                           |  environment, settings
+---------------------------+
| footer (256 bytes)        |  locates the database and the payload
+---------------------------+  end of file
```

Details are in [format.md](format.md).

## Packing

`felf pack PROGRAM -o OUT` ([packing.md](packing.md)):

1. Takes FELF's glibc (embedded in `felf`) as the program's runtime: its
   dynamic linker becomes the FELF's interpreter, and glibc's libraries are
   taken from it, not from the program's own glibc.
2. Resolves the program's `DT_NEEDED` dependencies transitively, through
   each object's `RUNPATH`/`RPATH`, the extra `--lib-dir` directories and
   the standard library directories.
3. Adds requested extras: more programs (`--program`), libraries
   (`--include`), the dependency closures of plugins kept outside the FELF
   (`--plugin`), a locale archive.
4. Checks that every symbol version an object needs is defined by the
   object that provides it.
5. Compresses the objects that will be copied into memory at start, keeps
   libraries uncompressed so they can be mapped from the file, and writes
   the bootstrap, payload, database and footer.

## Running

When the FELF is executed ([runtime.md](runtime.md)):

1. The kernel runs the bootstrap.  It reads the footer and the database
   and picks the program (several programs can share one FELF; `argv[0]`
   selects).
2. It copies, or decompresses, the program, the loader, the audit module
   and the shim into memfds.  Libraries stored uncompressed are not copied.
3. It writes the audit index, a table from library names to where each
   library is: a memfd (`/proc/self/fd/N`) or an offset in the FELF file.
4. It rewrites the program's interpreter to the loader's memfd, sets
   `LD_AUDIT` and `LD_PRELOAD`, and executes the program memfd with
   `execveat()`.
5. The kernel starts FELF's loader.  For every library, the audit module
   answers with a bundled copy, a host copy (the graphics stack), or a
   refusal.  The patched loader maps libraries that live in the FELF file
   directly from it.
6. Before `main()`, the shim restores the caller's environment.

## Libraries kept outside the FELF

A *tree* bundle (`nix/lib/bundle.nix`) is a directory: the FELF in `bin/`
and the rest of the package (data, plugins) as ordinary files next to it.
The packer records the *bundle root*, relative to the FELF, so that
plugins under it can be loaded by path in strict mode, and the libraries
they link against are embedded in the FELF.
