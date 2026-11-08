# File format

Format version 1 (`FELF_FORMAT_VERSION`, `src/include/felf_format.h`).  All
integers are little-endian.

## Layout

| Region | Where | Contents |
|---|---|---|
| Bootstrap | offset 0 | The static bootstrap executable, unchanged.  The kernel only looks at its ELF headers and segments, so whatever follows it is ignored. |
| Payload | page-aligned after the bootstrap | The stored bytes of every object, each at an offset aligned to the object's largest `PT_LOAD` alignment (at least 4096). |
| Database | page-aligned after the payload | An SQLite database (below). |
| Footer | the last 256 bytes | Locates everything else. |

`felf_outer_elf_validate()` checks that the file starts with an executable
ELF and that the regions do not overlap it.

## Footer

`felf_footer_t`, 256 bytes, packed:

| Field | Type | Meaning |
|---|---|---|
| `magic` | 8 bytes | `FELF\0\0\0\0` |
| `format_version` | u32 | 1 |
| `flags` | u32 | `0x1` strict, `0x10` lazy loading |
| `container_size` | u64 | Size of the whole file |
| `db_offset`, `db_size` | u64 | The database |
| `payload_offset`, `payload_size` | u64 | The payload region |
| `object_count` | u64 | Rows in `objects` |
| `index_offset`, `index_size` | u64 | Reserved, 0 |
| `reserved` | 168 bytes | 0 |
| `tail_magic` | 8 bytes | `FELFEND\0` |

The bootstrap and the CLI cross-check the footer against the database:
strict and lazy must agree, and `object_count` must match.

## Database

The schema is `src/storage/schema.sql` (`application_id` 1178942534,
`user_version` 1); `felf sql FILE "SELECT ..."` queries it.  Its header's
file change counter, schema cookie and version-valid-for are 1, so the
bytes depend only on the content.

### `objects`

One row per embedded file.

| Column | Meaning |
|---|---|
| `name` | Unique name: the SONAME for libraries, the basename otherwise; a program's name is the one it runs as |
| `soname` | `DT_SONAME`, if any |
| `kind` | `program`, `interp` (the dynamic linker), `shared` (a library something links against), `plugin` (a library only loaded with `dlopen`, from `--include`), `audit`, `runtime` (the shim), `resource` (the locale archive) |
| `offset` | File offset of the stored bytes, in the payload region |
| `size`, `sha256` | Size and SHA-256 of the object's uncompressed bytes |
| `compression`, `stored_size` | `none`, or `zstd` with the number of stored bytes |
| `alignment` | The object's largest segment alignment; `offset` is aligned to it |

A compressed object is a sequence of independent zstd frames, one per
2 MiB of the object (`FELF_ZSTD_CHUNK`), so it can be decompressed in
parallel.  An object that does not shrink is stored uncompressed.

### Other tables

| Table | Contents |
|---|---|
| `aliases` | More names per object: the SONAME, file names it was found under, the names of extra programs that are the same file (a multicall binary such as coreutils) |
| `dependencies` | Each object's `DT_NEEDED` names |
| `environment` | Variables set for the application (`--env`); values may use `$FELF_ROOT`, `$FELF_APP` and the caller's `$NAME`, and `$$` is a literal `$` |
| `package` | Name, version, architecture, ABI and the creating tool |
| `resources` | Reserved |
| `meta` | Settings, below |

### `meta` keys

| Key | Meaning |
|---|---|
| `schema_version`, `format_version`, `created_by` | Versions and the tool that wrote the file |
| `strict`, `lazy` | As in the footer |
| `root` | The bundle root, relative to the FELF file's directory (tree bundles) |
| `program_locations` | `name=path` per line: where each program sits in the tree, relative to the root, so a copy of the FELF placed there finds the root |
| `default_program` | The program run when `argv[0]` names none |
| `host_libs` | Patterns of libraries taken from the host first, one per line |
| `direct_map` | `1` when the bundled loader maps libraries from the file |

## Integrity

`felf verify` checks the footer, the layout, and the SHA-256 of every
object.  The bootstrap checks the footer and layout at every start but not
the hashes.  Checksums detect corruption; they are not signatures
([security.md](security.md)).
