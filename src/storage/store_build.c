#include "felf_storage.h"
#include "felf_format.h"
#include "resolver.h"
#include <sqlite3.h>
#include "schema_sql.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Capacity of each felf_db_t field that a meta value is read into. */
#define FIELD_SIZE(field) sizeof(((felf_db_t *)0)->field)

static int exec_sql(sqlite3 *db, const char *sql, char *err, size_t errlen)
{
    char *sqlite_err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &sqlite_err);
    if (rc != SQLITE_OK) {
        felf_set_error(err, errlen, "SQLite error: %s",
                       sqlite_err ? sqlite_err : sqlite3_errmsg(db));
        sqlite3_free(sqlite_err);
        return -1;
    }
    return 0;
}

static int bind_text(sqlite3_stmt *stmt, int index, const char *value)
{
    return sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
}

/* Insert meta KEY = VALUE.  MAX is the reader's buffer size for the key
 * (0: no limit), so a value the bootstrap cannot read fails at pack time. */
static int insert_meta(sqlite3 *db, const char *key, const char *value,
                       size_t max, char *err, size_t errlen)
{
    if (max != 0 && strlen(value) >= max) {
        felf_set_error(err, errlen, "%s is too long (%zu bytes, at most %zu)",
                       key, strlen(value), max - 1);
        return -1;
    }
    sqlite3_stmt *stmt = NULL;
    int ok = sqlite3_prepare_v2(db,
                 "INSERT INTO meta(key, value) VALUES(?1, ?2)", -1, &stmt,
                 NULL) == SQLITE_OK &&
             bind_text(stmt, 1, key) == SQLITE_OK &&
             bind_text(stmt, 2, value) == SQLITE_OK &&
             sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok) {
        felf_set_error(err, errlen, "cannot insert %s metadata: %s", key,
                       sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

static int prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt,
                   char *err, size_t errlen)
{
    if (sqlite3_prepare_v2(db, sql, -1, stmt, NULL) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot prepare insert: %s",
                       sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

static int insert_objects(sqlite3 *db, felf_resolver_t *r, char *err,
                          size_t errlen)
{
    sqlite3_stmt *ins = NULL;
    if (prepare(db,
            "INSERT INTO objects(id, namespace, name, soname, kind, "
            "offset, size, alignment, sha256, compression, stored_size) "
            "VALUES(?1, 'default', ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
            &ins, err, errlen) != 0)
        return -1;
    for (size_t i = 0; i < r->count; i++) {
        felf_object_t *o = &r->objects[i];
        sqlite3_reset(ins);
        sqlite3_clear_bindings(ins);
        /* Unbound parameters are NULL. */
        sqlite3_bind_int64(ins, 1, (sqlite3_int64)(i + 1));
        bind_text(ins, 2, o->name);
        if (o->soname != NULL)
            bind_text(ins, 3, o->soname);
        bind_text(ins, 4, o->kind);
        sqlite3_bind_int64(ins, 5, (sqlite3_int64)o->offset);
        sqlite3_bind_int64(ins, 6, (sqlite3_int64)o->size);
        sqlite3_bind_int64(ins, 7, (sqlite3_int64)o->alignment);
        sqlite3_bind_blob(ins, 8, o->sha256, 32, SQLITE_TRANSIENT);
        /* The stored bytes are the zstd copy when there is one. */
        bind_text(ins, 9, o->zdata ? "zstd" : "none");
        if (o->zdata)
            sqlite3_bind_int64(ins, 10, (sqlite3_int64)o->zsize);
        if (sqlite3_step(ins) != SQLITE_DONE) {
            felf_set_error(err, errlen, "cannot insert object %s: %s",
                           o->name, sqlite3_errmsg(db));
            sqlite3_finalize(ins);
            return -1;
        }
    }
    sqlite3_finalize(ins);
    return 0;
}

/* Insert (object id, string) rows for every entry of each object's
 * aliases (DEPS == 0) or dependencies (DEPS != 0). */
static int insert_links(sqlite3 *db, felf_resolver_t *r, int deps,
                        char *err, size_t errlen)
{
    sqlite3_stmt *stmt = NULL;
    if (prepare(db, deps
            ? "INSERT OR IGNORE INTO dependencies(object_id, "
              "dependency_name, namespace) VALUES(?1, ?2, 'default')"
            : "INSERT OR IGNORE INTO aliases(object_id, alias, namespace) "
              "VALUES(?1, ?2, 'default')", &stmt, err, errlen) != 0)
        return -1;
    for (size_t i = 0; i < r->count; i++) {
        const felf_object_t *o = &r->objects[i];
        char *const *names = deps ? o->deps : o->aliases;
        size_t count = deps ? o->dep_count : o->alias_count;
        for (size_t j = 0; j < count; j++) {
            sqlite3_reset(stmt);
            sqlite3_bind_int64(stmt, 1, (sqlite3_int64)(i + 1));
            bind_text(stmt, 2, names[j]);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                felf_set_error(err, errlen, "cannot insert %s %s: %s",
                               deps ? "dependency" : "alias", names[j],
                               sqlite3_errmsg(db));
                sqlite3_finalize(stmt);
                return -1;
            }
        }
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int insert_environment(sqlite3 *db, felf_resolver_t *r, char *err,
                              size_t errlen)
{
    sqlite3_stmt *env = NULL;
    if (prepare(db,
            "INSERT OR REPLACE INTO environment(name, value) VALUES(?1, ?2)",
            &env, err, errlen) != 0)
        return -1;
    for (size_t i = 0; i < r->env_count; i++) {
        const char *entry = r->env[i];
        const char *eq = strchr(entry, '=');
        if (eq == NULL || eq == entry) {
            felf_set_error(err, errlen, "invalid environment entry %s",
                           entry);
            sqlite3_finalize(env);
            return -1;
        }
        sqlite3_reset(env);
        sqlite3_bind_text(env, 1, entry, (int)(eq - entry), SQLITE_TRANSIENT);
        bind_text(env, 2, eq + 1);
        if (sqlite3_step(env) != SQLITE_DONE) {
            felf_set_error(err, errlen, "cannot insert environment %s: %s",
                           entry, sqlite3_errmsg(db));
            sqlite3_finalize(env);
            return -1;
        }
    }
    sqlite3_finalize(env);
    return 0;
}

/* The host-preferred patterns, one per line. */
static char *join_host_libs(const felf_resolver_t *r)
{
    size_t len = 1;
    for (size_t i = 0; i < r->host_lib_count; i++)
        len += strlen(r->host_libs[i]) + 1;
    char *joined = malloc(len);
    if (joined == NULL)
        return NULL;
    size_t at = 0;
    for (size_t i = 0; i < r->host_lib_count; i++)
        at += (size_t)sprintf(joined + at, "%s\n", r->host_libs[i]);
    joined[at] = '\0';
    return joined;
}

/* Big-endian u32 at offset AT of the database header. */
static void put_be32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

/* SQLite counts the writes in the database header (the file change
 * counter, the schema cookie): set them to 1, so the bytes depend only on
 * the content, however it was written. */
static void felf_db_normalize(uint8_t *db, size_t size)
{
    if (size < 100)
        return;
    put_be32(db + 24, 1); /* file change counter */
    put_be32(db + 40, 1); /* schema cookie */
    put_be32(db + 92, 1); /* version-valid-for: equal to the counter */
}

int felf_db_build(felf_resolver_t *r, const char *package_name,
                  const char *package_version, const char *created_by,
                  int strict, int lazy, uint8_t **out, size_t *out_size,
                  char *err, size_t errlen)
{
    /* Built in memory and serialized: no temporary file. */
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(":memory:", &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        NULL) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot create SQLite database: %s",
                       db ? sqlite3_errmsg(db) : "unknown error");
        goto fail;
    }
    if (exec_sql(db, "PRAGMA temp_store = MEMORY;", err, errlen) != 0 ||
        exec_sql(db, FELF_SCHEMA_SQL, err, errlen) != 0)
        goto fail;

    char format[16];
    snprintf(format, sizeof(format), "%u", FELF_FORMAT_VERSION);
    if (insert_meta(db, "schema_version", "1", 0, err, errlen) != 0 ||
        insert_meta(db, "format_version", format, 0, err, errlen) != 0 ||
        insert_meta(db, "strict", strict ? "1" : "0", 0, err, errlen) != 0 ||
        insert_meta(db, "lazy", lazy ? "1" : "0", 0, err, errlen) != 0 ||
        insert_meta(db, "created_by", created_by, 0, err, errlen) != 0)
        goto fail;

    sqlite3_stmt *pkg = NULL;
    if (prepare(db,
            "INSERT INTO package(id, name, version, architecture, abi, "
            "created_by, format_version, strict, namespace) "
            "VALUES(1, ?1, ?2, ?6, 'glibc', ?3, ?4, ?5, 'default')",
            &pkg, err, errlen) != 0)
        goto fail;
    bind_text(pkg, 1, package_name);
    bind_text(pkg, 2, package_version);
    bind_text(pkg, 3, created_by);
    sqlite3_bind_int(pkg, 4, (int)FELF_FORMAT_VERSION);
    sqlite3_bind_int(pkg, 5, strict ? 1 : 0);
    bind_text(pkg, 6, r->arch != NULL ? r->arch->name : "unknown");
    int pkg_ok = sqlite3_step(pkg) == SQLITE_DONE;
    sqlite3_finalize(pkg);
    if (!pkg_ok) {
        felf_set_error(err, errlen, "cannot insert package record: %s",
                       sqlite3_errmsg(db));
        goto fail;
    }

    if (insert_objects(db, r, err, errlen) != 0 ||
        insert_links(db, r, 0, err, errlen) != 0 ||
        insert_links(db, r, 1, err, errlen) != 0 ||
        insert_environment(db, r, err, errlen) != 0)
        goto fail;

    if (r->host_lib_count > 0) {
        char *joined = join_host_libs(r);
        if (joined == NULL) {
            felf_set_error(err, errlen, "out of memory");
            goto fail;
        }
        int rc = insert_meta(db, "host_libs", joined, FIELD_SIZE(host_libs),
                             err, errlen);
        free(joined);
        if (rc != 0)
            goto fail;
    }
    if (r->program_locations != NULL && r->program_locations[0] != '\0' &&
        insert_meta(db, "program_locations", r->program_locations,
                    FIELD_SIZE(program_locations), err, errlen) != 0)
        goto fail;
    if (r->default_program != NULL &&
        insert_meta(db, "default_program", r->default_program,
                    FIELD_SIZE(default_program), err, errlen) != 0)
        goto fail;
    if (r->direct_map &&
        insert_meta(db, "direct_map", "1", 0, err, errlen) != 0)
        goto fail;
    if (r->root != NULL &&
        insert_meta(db, "root", r->root, FIELD_SIZE(root), err, errlen) != 0)
        goto fail;

    if (exec_sql(db, "VACUUM;", err, errlen) != 0)
        goto fail;
    sqlite3_int64 size = 0;
    unsigned char *image = sqlite3_serialize(db, "main", &size, 0);
    uint8_t *copy = image != NULL && size > 0 ? malloc((size_t)size) : NULL;
    if (copy == NULL) {
        sqlite3_free(image);
        felf_set_error(err, errlen, "cannot serialize SQLite database");
        goto fail;
    }
    memcpy(copy, image, (size_t)size);
    sqlite3_free(image);
    sqlite3_close(db);
    felf_db_normalize(copy, (size_t)size);
    *out = copy;
    *out_size = (size_t)size;
    return 0;

fail:
    sqlite3_close(db);
    return -1;
}
