# Vendored dependencies

CMake builds both libraries from these single-file sources
(`felf_sqlite`, `felf_zstd`); nothing is taken from the system, so the
static bootstrap and the CLI build the same everywhere.

| Library | Version | Files | License |
|---|---|---|---|
| SQLite | 3.53.3 | `sqlite/sqlite3.c`, `sqlite/sqlite3.h` | Public domain (see the header of `sqlite3.c`) |
| zstd | 1.5.7 | `zstd/zstd.c`, `zstd/zstd.h`, `zstd/zstd_errors.h` | BSD (`zstd/LICENSE`); zstd is dual BSD/GPLv2, FELF uses it under BSD |

These are the versions pinned by `flake.lock` (nixpkgs).

## Regenerating

SQLite: the amalgamation, built from the release's source tree.

```bash
unzip sqlite-src-3530300.zip && cd sqlite-src-3530300
./configure && make sqlite3.c          # needs tclsh
cp sqlite3.c sqlite3.h ../third_party/sqlite/
```

zstd: the single-file library from zstd's own script, without the
dictionary builder (FELF only compresses and decompresses; this drops the
`ZDICT_*` API).

```bash
cd zstd-1.5.7/build/single_file_libs
grep -v '#include "dictBuilder/' zstd-in.c > felf-zstd-in.c
./combine.sh -r ../../lib -x legacy/zstd_legacy.h -o zstd.c felf-zstd-in.c
cp zstd.c ../../lib/zstd.h ../../lib/zstd_errors.h ../../../third_party/zstd/
```
