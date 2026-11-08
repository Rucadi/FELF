# File format

Format version 1 (`src/include/felf_format.h`). Integers are little-endian.

## Layout

```text
┌────────────┐ 0                   the bootstrap, unchanged; the kernel only
│ bootstrap  │                     reads its ELF headers, so the rest is ignored
├────────────┤ page-aligned
│ payload    │                     each object at its largest PT_LOAD alignment
├────────────┤ page-aligned        (at least 4096)
│ SQLite     │
├────────────┤
│ footer     │ last 256 bytes
└────────────┘
```

## Footer

`felf_footer_t`, 256 bytes, packed:

| Field | Type | Value |
|---|---|---|
| `magic` | 8 B | `FELF\0\0\0\0` |
| `format_version` | u32 | 1 |
| `flags` | u32 | `0x1` strict, `0x10` lazy |
| `container_size` | u64 | file size |
| `db_offset`, `db_size` | u64 | the index |
| `payload_offset`, `payload_size` | u64 | the payload |
| `object_count` | u64 | rows in `objects` |
| `index_offset`, `index_size` | u64 | reserved, 0 |
| `reserved` | 168 B | 0 |
| `tail_magic` | 8 B | `FELFEND\0` |

The footer and the index must agree on strict, lazy and the object count.

## Index

An SQLite database: `src/storage/schema.sql`, `application_id` 1178942534,
`user_version` 1. Its header counters are fixed at 1, so its bytes depend
only on content. Query it with `felf sql FILE "SELECT …"`.

### `objects`: one row per embedded file

| Column | Meaning |
|---|---|
| `name` | SONAME for libraries, else the basename; for a program, the name it runs as |
| `soname` | `DT_SONAME` |
| `kind` | `program`, `interp` (ld.so), `shared`, `plugin` (from `--include`), `audit`, `runtime` (the shim), `resource` (locale archive) |
| `offset`, `alignment` | where the bytes are; `offset` is aligned to `alignment` |
| `size`, `sha256` | of the uncompressed bytes |
| `compression`, `stored_size` | `none`, or `zstd` and the stored size |

Compressed objects are independent zstd frames, one per 2 MiB, so they
decompress in parallel. An object that doesn't shrink is stored as is.

### Other tables

| Table | Contents |
|---|---|
| `aliases` | more names per object: SONAMEs, names it was found under, extra program names for the same file |
| `dependencies` | each object's `DT_NEEDED` |
| `environment` | `--env` values |
| `package` | name, version, architecture, ABI, creator |
| `meta` | settings, below |
| `resources` | reserved |

### `meta`

| Key | Meaning |
|---|---|
| `schema_version`, `format_version`, `created_by` | versions and writer |
| `strict`, `lazy` | as in the footer |
| `root` | bundle root, relative to the FELF's directory |
| `program_locations` | `name=path` per line: where each program sits in the tree, so a copy placed there finds the root |
| `default_program` | run when `argv[0]` names no program |
| `host_libs` | host-preferred patterns, one per line |
| `direct_map` | `1` when ld.so maps libraries from the file |

## Integrity

`felf verify` checks the footer, the layout and every object's SHA-256. The
bootstrap checks the footer and layout at every start, not the hashes.
Hashes catch corruption; they are not signatures ([security.md](security.md)).
