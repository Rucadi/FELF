#ifndef FELF_STORAGE_H
#define FELF_STORAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct sqlite3 sqlite3;

typedef struct {
    int64_t id;
    char *name;
    char *soname;
    char *kind;
    /* Where the stored bytes are in the file (the payload region). */
    uint64_t offset;
    /* Uncompressed size; sha256 is over the uncompressed bytes. */
    uint64_t size;
    /* Bytes stored at offset. */
    uint64_t stored_size;
    /* 1 = the stored bytes are zstd-compressed. */
    int compressed;
    uint64_t alignment;
    uint8_t sha256[32];
} felf_db_object_t;

typedef struct {
    char *alias;
    int64_t object_id;
} felf_db_alias_t;

typedef struct {
    char *name;
    char *value;
} felf_db_env_t;

typedef struct {
    sqlite3 *handle;
    felf_db_object_t *objects;
    size_t object_count;
    felf_db_alias_t *aliases;
    size_t alias_count;
    felf_db_env_t *env;
    size_t env_count;
    /* Bundle root relative to the FELF file's directory ("" = that dir). */
    char root[1024];
    /* Host-preferred library patterns, one per line (meta "host_libs"). */
    char host_libs[8192];
    /* Program run when argv[0] matches no program name (meta
     * "default_program"; empty in containers packed before multi-program
     * support). */
    char default_program[256];
    /* Where each program sits relative to the root, "name=path" per line
     * (meta "program_locations"; tree bundles only). */
    char program_locations[16384];
    int strict;
    int lazy;
    /* The bundled loader maps uncompressed libraries straight from the
     * file (meta "direct_map"; FELF's glibc). */
    int direct_map;
} felf_db_t;

/* Open a bounded byte range as an immutable, in-memory SQLite database.
 * No temporary file is created and the source buffer is not modified. */
int felf_db_open_memory(const uint8_t *data, size_t size, felf_db_t *db,
                        char *err, size_t errlen);
void felf_db_close(felf_db_t *db);

/* The object with this id, or NULL. */
const felf_db_object_t *felf_db_object_by_id(const felf_db_t *db, int64_t id);

/* Validate that every object record points at a bounded, aligned part of
 * the payload region. */
int felf_db_validate_layout(const felf_db_t *db, uint64_t container_size,
                            uint64_t payload_offset, uint64_t payload_size,
                            char *err, size_t errlen);

/* Copy (or zstd-decompress) the stored bytes SRC of O, SRCLEN ==
 * o->stored_size, into DST, DSTLEN == o->size. */
int felf_db_object_unpack(const felf_db_object_t *o, const uint8_t *src,
                          size_t srclen, uint8_t *dst, size_t dstlen,
                          char *err, size_t errlen);

#endif
