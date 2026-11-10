#include "commands.h"
#include "felf_file.h"

#include <sqlite3.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* sqlite3_stmt_readonly() counts ATTACH as read-only, yet ATTACH creates
 * the file it names.  Only the FELF's own database may be read. */
static int deny_attach(void *denied, int action, const char *a,
                       const char *b, const char *db, const char *trigger)
{
    (void)a;
    (void)b;
    (void)db;
    (void)trigger;
    if (action == SQLITE_ATTACH || action == SQLITE_DETACH) {
        *(int *)denied = 1;
        return SQLITE_DENY;
    }
    return SQLITE_OK;
}

int felf_cmd_sql(const char *path, const char *query,
                 char *err, size_t errlen)
{
    felf_file_t file;
    if (felf_file_open(path, 0, &file, err, errlen) != 0)
        return -1;
    int attach_denied = 0;
    sqlite3_set_authorizer(file.db.handle, deny_attach, &attach_denied);
    sqlite3_stmt *stmt = NULL;
    const char *tail = NULL;
    int rc = sqlite3_prepare_v2(file.db.handle, query, -1, &stmt, &tail);
    if (rc != SQLITE_OK) {
        if (attach_denied)
            felf_set_error(err, errlen,
                           "ATTACH and DETACH are not allowed; only the "
                           "FELF's own database can be queried");
        else
            felf_set_error(err, errlen,
                           "SQL error: %s", sqlite3_errmsg(file.db.handle));
        felf_file_close(&file);
        return -1;
    }
    while (tail != NULL && (*tail == ' ' || *tail == '\t' ||
                            *tail == '\r' || *tail == '\n'))
        tail++;
    if (tail != NULL && *tail != '\0') {
        felf_set_error(err, errlen,
                       "only one read-only SQL statement is allowed");
        sqlite3_finalize(stmt);
        felf_file_close(&file);
        return -1;
    }
    if (!sqlite3_stmt_readonly(stmt)) {
        felf_set_error(err, errlen, "query is not read-only");
        sqlite3_finalize(stmt);
        felf_file_close(&file);
        return -1;
    }

    int columns = sqlite3_column_count(stmt);
    for (int i = 0; i < columns; i++) {
        if (i)
            putchar('\t');
        fputs(sqlite3_column_name(stmt, i), stdout);
    }
    putchar('\n');
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        for (int i = 0; i < columns; i++) {
            if (i)
                putchar('\t');
            switch (sqlite3_column_type(stmt, i)) {
            case SQLITE_NULL:
                fputs("NULL", stdout);
                break;
            case SQLITE_INTEGER:
                printf("%lld", (long long)sqlite3_column_int64(stmt, i));
                break;
            case SQLITE_FLOAT:
                printf("%.17g", sqlite3_column_double(stmt, i));
                break;
            case SQLITE_BLOB: {
                const unsigned char *p = sqlite3_column_blob(stmt, i);
                int n = sqlite3_column_bytes(stmt, i);
                fputs("x'", stdout);
                for (int j = 0; j < n; j++)
                    printf("%02x", p[j]);
                putchar('\'');
                break;
            }
            default: {
                const char *text = (const char *)sqlite3_column_text(stmt, i);
                fputs(text ? text : "", stdout);
                break;
            }
            }
        }
        putchar('\n');
    }
    if (rc != SQLITE_DONE) {
        felf_set_error(err, errlen, "SQL execution failed: %s",
                       sqlite3_errmsg(file.db.handle));
        sqlite3_finalize(stmt);
        felf_file_close(&file);
        return -1;
    }
    sqlite3_finalize(stmt);
    felf_file_close(&file);
    return 0;
}
