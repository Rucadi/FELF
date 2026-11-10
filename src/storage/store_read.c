#include "felf_storage.h"
#include "felf_format.h"
#include "felf_loader_protocol.h"

#include <sqlite3.h>
#include <zstd.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Make room for one more element in ARRAY (COUNT used, *CAP allocated).
 * Returns the possibly moved array, or NULL when out of memory. */
static void *grow(void *array, size_t *cap, size_t count, size_t size)
{
    if (count < *cap)
        return array;
    size_t newcap = *cap ? *cap * 2 : 16;
    void *p = realloc(array, newcap * size);
    if (p != NULL)
        *cap = newcap;
    return p;
}

/* Read meta KEY into OUT.  A missing key is an error unless FALLBACK is
 * non-NULL, in which case OUT receives FALLBACK. */
static int read_meta(sqlite3 *db, const char *key, const char *fallback,
                     char *out, size_t outlen, char *err, size_t errlen)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM meta WHERE key = ?1", -1,
                           &stmt, NULL) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot prepare metadata query: %s",
                       sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    int rc = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *value = (const char *)sqlite3_column_text(stmt, 0);
        if (value == NULL || strlen(value) >= outlen) {
            felf_set_error(err, errlen, "metadata value for %s is invalid",
                           key);
            rc = -1;
        } else {
            snprintf(out, outlen, "%s", value);
        }
    } else if (fallback != NULL) {
        snprintf(out, outlen, "%s", fallback);
    } else {
        felf_set_error(err, errlen, "metadata key %s is missing", key);
        rc = -1;
    }
    sqlite3_finalize(stmt);
    return rc;
}

static int read_objects(felf_db_t *db, char *err, size_t errlen)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db->handle,
        "SELECT id, name, soname, kind, offset, size, alignment, sha256, "
        "compression, stored_size FROM objects ORDER BY id", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot query objects: %s",
                       sqlite3_errmsg(db->handle));
        return -1;
    }
    size_t cap = 0;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        felf_db_object_t *objects = grow(db->objects, &cap, db->object_count,
                                         sizeof(*objects));
        if (objects == NULL)
            goto oom;
        db->objects = objects;
        /* Counted before it is filled, so felf_db_close() frees a partially
         * read record. */
        felf_db_object_t *o = &db->objects[db->object_count++];
        memset(o, 0, sizeof(*o));
        o->id = sqlite3_column_int64(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *soname = (const char *)sqlite3_column_text(stmt, 2);
        const char *kind = (const char *)sqlite3_column_text(stmt, 3);
        o->offset = (uint64_t)sqlite3_column_int64(stmt, 4);
        o->size = (uint64_t)sqlite3_column_int64(stmt, 5);
        o->alignment = (uint64_t)sqlite3_column_int64(stmt, 6);
        o->stored_size = o->size;
        const char *comp = (const char *)sqlite3_column_text(stmt, 8);
        if (comp != NULL && strcmp(comp, "zstd") == 0) {
            o->compressed = 1;
            o->stored_size = (uint64_t)sqlite3_column_int64(stmt, 9);
        } else if (comp == NULL || strcmp(comp, "none") != 0) {
            felf_set_error(err, errlen,
                           "object %lld has an unknown compression",
                           (long long)o->id);
            goto fail;
        }
        const void *hash = sqlite3_column_blob(stmt, 7);
        int hash_len = sqlite3_column_bytes(stmt, 7);
        if (name == NULL || kind == NULL || o->offset == 0 ||
            o->size == 0 || o->stored_size == 0 || o->alignment == 0 ||
            hash == NULL || hash_len != 32) {
            felf_set_error(err, errlen, "invalid object record %lld",
                           (long long)o->id);
            goto fail;
        }
        /* The audit index finds a library by these names; one it cannot
         * hold would never load. */
        int library = strcmp(kind, "shared") == 0 ||
                      strcmp(kind, "plugin") == 0;
        const char *overlong =
            !library ? NULL
            : strlen(name) >= FELF_AUDIT_NAME_SIZE ? name
            : soname != NULL && strlen(soname) >= FELF_AUDIT_NAME_SIZE ? soname
            : NULL;
        if (overlong != NULL) {
            felf_set_error(err, errlen, "library name %s is too long (at "
                           "most %u bytes)", overlong,
                           FELF_AUDIT_NAME_SIZE - 1);
            goto fail;
        }
        memcpy(o->sha256, hash, 32);
        o->name = felf_strdup(name);
        o->soname = felf_strdup(soname);
        o->kind = felf_strdup(kind);
        if (o->name == NULL || o->kind == NULL ||
            (soname != NULL && o->soname == NULL))
            goto oom;
    }
    if (rc != SQLITE_DONE) {
        felf_set_error(err, errlen, "object query failed: %s",
                       sqlite3_errmsg(db->handle));
        goto fail;
    }
    sqlite3_finalize(stmt);
    return 0;

oom:
    felf_set_error(err, errlen, "out of memory reading objects");
fail:
    sqlite3_finalize(stmt);
    return -1;
}

const felf_db_object_t *felf_db_object_by_id(const felf_db_t *db, int64_t id)
{
    /* The packer numbers the objects 1, 2, ... in the order they are read. */
    if (id >= 1 && (uint64_t)id <= db->object_count &&
        db->objects[id - 1].id == id)
        return &db->objects[id - 1];
    for (size_t i = 0; i < db->object_count; i++)
        if (db->objects[i].id == id)
            return &db->objects[i];
    return NULL;
}

/* Aliases are needed by the audit index.  They are validated here so
 * neither the bootstrap nor the loader has to trust unchecked rows. */
static int read_aliases(felf_db_t *db, char *err, size_t errlen)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle,
            "SELECT alias, object_id FROM aliases ORDER BY alias", -1,
            &stmt, NULL) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot query aliases: %s",
                       sqlite3_errmsg(db->handle));
        return -1;
    }
    size_t cap = 0;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *alias = (const char *)sqlite3_column_text(stmt, 0);
        int64_t object_id = sqlite3_column_int64(stmt, 1);
        if (alias == NULL || strlen(alias) >= FELF_AUDIT_NAME_SIZE) {
            felf_set_error(err, errlen, "object alias %s is missing or too "
                           "long (at most %u bytes)", alias ? alias : "",
                           FELF_AUDIT_NAME_SIZE - 1);
            goto fail;
        }
        if (felf_db_object_by_id(db, object_id) == NULL) {
            felf_set_error(err, errlen,
                           "alias %s references missing object %lld", alias,
                           (long long)object_id);
            goto fail;
        }
        felf_db_alias_t *aliases = grow(db->aliases, &cap, db->alias_count,
                                        sizeof(*aliases));
        if (aliases == NULL)
            goto oom;
        db->aliases = aliases;
        felf_db_alias_t *a = &db->aliases[db->alias_count++];
        a->object_id = object_id;
        a->alias = felf_strdup(alias);
        if (a->alias == NULL)
            goto oom;
    }
    if (rc != SQLITE_DONE) {
        felf_set_error(err, errlen, "alias query failed: %s",
                       sqlite3_errmsg(db->handle));
        goto fail;
    }
    sqlite3_finalize(stmt);
    return 0;

oom:
    felf_set_error(err, errlen, "out of memory reading aliases");
fail:
    sqlite3_finalize(stmt);
    return -1;
}

static int read_environment(felf_db_t *db, char *err, size_t errlen)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle,
            "SELECT name, value FROM environment ORDER BY name", -1,
            &stmt, NULL) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot query environment: %s",
                       sqlite3_errmsg(db->handle));
        return -1;
    }
    size_t cap = 0;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        const char *value = (const char *)sqlite3_column_text(stmt, 1);
        if (name == NULL || value == NULL || name[0] == '\0' ||
            strchr(name, '=') != NULL) {
            felf_set_error(err, errlen, "invalid environment record");
            goto fail;
        }
        felf_db_env_t *env = grow(db->env, &cap, db->env_count,
                                  sizeof(*env));
        if (env == NULL)
            goto oom;
        db->env = env;
        felf_db_env_t *e = &db->env[db->env_count++];
        e->name = felf_strdup(name);
        e->value = felf_strdup(value);
        if (e->name == NULL || e->value == NULL)
            goto oom;
    }
    if (rc != SQLITE_DONE) {
        felf_set_error(err, errlen, "environment query failed: %s",
                       sqlite3_errmsg(db->handle));
        goto fail;
    }
    sqlite3_finalize(stmt);
    return 0;

oom:
    felf_set_error(err, errlen, "out of memory reading environment");
fail:
    sqlite3_finalize(stmt);
    return -1;
}

int felf_db_open_memory(const uint8_t *data, size_t size, felf_db_t *db,
                        char *err, size_t errlen)
{
    memset(db, 0, sizeof(*db));
    if (size == 0 || size > (size_t)INT32_MAX) {
        felf_set_error(err, errlen, "invalid SQLite database size");
        return -1;
    }
    if (sqlite3_open_v2(":memory:", &db->handle,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        NULL) != SQLITE_OK) {
        felf_set_error(err, errlen,
                       "cannot create in-memory SQLite database: %s",
                       db->handle ? sqlite3_errmsg(db->handle)
                                  : "unknown error");
        goto fail;
    }
    unsigned char *buf = (unsigned char *)(uintptr_t)data;
    if (sqlite3_deserialize(db->handle, "main", buf, size, size,
                            SQLITE_DESERIALIZE_READONLY) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot deserialize embedded database: %s",
                       sqlite3_errmsg(db->handle));
        goto fail;
    }
    /* The database comes from a file anyone may have written: its schema
     * gets no say in what SQL functions run, and its pages are checked. */
    sqlite3_db_config(db->handle, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0,
                      (int *)NULL);
    sqlite3_db_config(db->handle, SQLITE_DBCONFIG_DEFENSIVE, 1, (int *)NULL);
    char *sqlite_err = NULL;
    if (sqlite3_exec(db->handle,
                     "PRAGMA query_only = ON; PRAGMA cell_size_check = ON;",
                     NULL, NULL,
                     &sqlite_err) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot set database read-only: %s",
                       sqlite_err ? sqlite_err : sqlite3_errmsg(db->handle));
        sqlite3_free(sqlite_err);
        goto fail;
    }

    char schema[32], format[32], strict[8], lazy[8];
    char direct_map[8];
    if (read_meta(db->handle, "schema_version", NULL, schema, sizeof(schema),
                  err, errlen) != 0)
        goto fail;
    if (strcmp(schema, "1") != 0) {
        felf_set_error(err, errlen, "unsupported FELF schema version %s",
                       schema);
        goto fail;
    }
    if (read_meta(db->handle, "format_version", NULL, format, sizeof(format),
                  err, errlen) != 0)
        goto fail;
    if (strtoul(format, NULL, 10) != FELF_FORMAT_VERSION) {
        felf_set_error(err, errlen, "database format version does not match");
        goto fail;
    }
    if (read_meta(db->handle, "strict", NULL, strict, sizeof(strict),
                  err, errlen) != 0 ||
        read_meta(db->handle, "lazy", "0", lazy, sizeof(lazy),
                  err, errlen) != 0 ||
        read_meta(db->handle, "direct_map", "0", direct_map,
                  sizeof(direct_map),
                  err, errlen) != 0 ||
        read_meta(db->handle, "root", "", db->root, sizeof(db->root),
                  err, errlen) != 0 ||
        read_meta(db->handle, "host_libs", "", db->host_libs,
                  sizeof(db->host_libs), err, errlen) != 0 ||
        read_meta(db->handle, "default_program", "", db->default_program,
                  sizeof(db->default_program), err, errlen) != 0 ||
        read_meta(db->handle, "program_locations", "",
                  db->program_locations, sizeof(db->program_locations),
                  err, errlen) != 0)
        goto fail;
    db->strict = strcmp(strict, "1") == 0;
    db->lazy = strcmp(lazy, "1") == 0;
    db->direct_map = strcmp(direct_map, "1") == 0;

    if (read_objects(db, err, errlen) != 0 ||
        read_aliases(db, err, errlen) != 0 ||
        read_environment(db, err, errlen) != 0)
        goto fail;
    return 0;

fail:
    felf_db_close(db);
    return -1;
}

void felf_db_close(felf_db_t *db)
{
    if (db == NULL)
        return;
    for (size_t i = 0; i < db->object_count; i++) {
        free(db->objects[i].name);
        free(db->objects[i].soname);
        free(db->objects[i].kind);
    }
    free(db->objects);
    for (size_t i = 0; i < db->alias_count; i++)
        free(db->aliases[i].alias);
    free(db->aliases);
    for (size_t i = 0; i < db->env_count; i++) {
        free(db->env[i].name);
        free(db->env[i].value);
    }
    free(db->env);
    if (db->handle != NULL)
        sqlite3_close(db->handle);
    memset(db, 0, sizeof(*db));
}

int felf_db_validate_layout(const felf_db_t *db, uint64_t container_size,
                            uint64_t payload_offset, uint64_t payload_size,
                            char *err, size_t errlen)
{
    if (payload_offset > container_size ||
        payload_size > container_size - payload_offset) {
        felf_set_error(err, errlen, "payload region is outside the container");
        return -1;
    }
    uint64_t payload_end = payload_offset + payload_size;
    for (size_t i = 0; i < db->object_count; i++) {
        const felf_db_object_t *o = &db->objects[i];
        if (o->offset == 0 || o->offset < payload_offset ||
            o->offset > payload_end ||
            o->stored_size > payload_end - o->offset) {
            felf_set_error(err, errlen,
                           "object %s is outside the payload region", o->name);
            return -1;
        }
        if ((o->offset % o->alignment) != 0) {
            felf_set_error(err, errlen, "object %s is not aligned", o->name);
            return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */

int felf_db_object_unpack(const felf_db_object_t *o, const uint8_t *src,
                          size_t srclen, uint8_t *dst, size_t dstlen,
                          char *err, size_t errlen)
{
    if (srclen != o->stored_size || dstlen != o->size) {
        felf_set_error(err, errlen, "object %s: size mismatch", o->name);
        return -1;
    }
    if (!o->compressed) {
        memcpy(dst, src, srclen);
        return 0;
    }
    size_t n = ZSTD_decompress(dst, dstlen, src, srclen);
    if (ZSTD_isError(n)) {
        felf_set_error(err, errlen, "object %s: zstd: %s", o->name,
                       ZSTD_getErrorName(n));
        return -1;
    }
    if (n != dstlen) {
        felf_set_error(err, errlen, "object %s: decompressed to %zu bytes, "
                       "expected %zu", o->name, n, dstlen);
        return -1;
    }
    return 0;
}
