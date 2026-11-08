#include "commands.h"
#include "felf_arch.h"
#include "felf_file.h"
#include "felf_host_libs.h"

#include <sqlite3.h>
#include <elf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_sqlite(const felf_db_t *db, char *err, size_t errlen)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, "PRAGMA integrity_check", -1,
                           &stmt, NULL) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot run SQLite integrity check: %s",
                       sqlite3_errmsg(db->handle));
        return -1;
    }
    int rc = sqlite3_step(stmt);
    const char *result = rc == SQLITE_ROW
                             ? (const char *)sqlite3_column_text(stmt, 0)
                             : NULL;
    if (result == NULL || strcmp(result, "ok") != 0) {
        felf_set_error(err, errlen, "SQLite integrity check failed");
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);

    /* A dependency on a host-preferred library may be left out: the
     * runtime looks it up on the host. */
    if (sqlite3_prepare_v2(db->handle,
            "SELECT DISTINCT d.dependency_name FROM dependencies d "
            "LEFT JOIN objects o ON o.namespace=d.namespace AND "
            "(o.name=d.dependency_name OR o.soname=d.dependency_name) "
            "LEFT JOIN aliases a ON a.namespace=d.namespace AND "
            "a.alias=d.dependency_name "
            "WHERE o.id IS NULL AND a.object_id IS NULL "
            "ORDER BY d.dependency_name", -1,
            &stmt, NULL) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot validate dependency graph: %s",
                       sqlite3_errmsg(db->handle));
        return -1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        const char *base = name ? strrchr(name, '/') : NULL;
        if (name == NULL ||
            !felf_host_list_match(db->host_libs, base ? base + 1 : name)) {
            felf_set_error(err, errlen,
                           "dependency %s is not in the container",
                           name ? name : "(null)");
            sqlite3_finalize(stmt);
            return -1;
        }
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        felf_set_error(err, errlen, "cannot validate dependency graph: %s",
                       sqlite3_errmsg(db->handle));
        return -1;
    }

    if (sqlite3_prepare_v2(db->handle,
            "SELECT COUNT(*) FROM aliases a LEFT JOIN objects o "
            "ON o.id=a.object_id WHERE o.id IS NULL", -1,
            &stmt, NULL) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot validate aliases: %s",
                       sqlite3_errmsg(db->handle));
        return -1;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW ||
        sqlite3_column_int64(stmt, 0) != 0) {
        felf_set_error(err, errlen,
                       "alias table contains dangling object IDs");
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int check_elf_object(const felf_arch_t *arch,
                            const felf_db_object_t *o, const uint8_t *data,
                            size_t size, char *err, size_t errlen)
{
    if (size < sizeof(Elf64_Ehdr)) {
        felf_set_error(err, errlen,
                       "object %s is too small for ELF64", o->name);
        return -1;
    }
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)data;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 ||
        eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB ||
        eh->e_machine != arch->machine) {
        felf_set_error(err, errlen,
                       "object %s is not little-endian ELF64 for %s", o->name,
                       arch->name);
        return -1;
    }
    if (strcmp(o->kind, "program") == 0) {
        if (eh->e_type != ET_DYN && eh->e_type != ET_EXEC) {
            felf_set_error(err, errlen,
                           "program %s has invalid e_type", o->name);
            return -1;
        }
    } else if (eh->e_type != ET_DYN) {
        felf_set_error(err, errlen, "object %s is not ET_DYN", o->name);
        return -1;
    }
    uint64_t ph_end = eh->e_phoff +
                      (uint64_t)eh->e_phnum * eh->e_phentsize;
    if (eh->e_phentsize != sizeof(Elf64_Phdr) ||
        eh->e_phnum == 0 || eh->e_phnum > 4096 ||
        ph_end < eh->e_phoff || ph_end > size) {
        felf_set_error(err, errlen, "object %s has invalid program headers",
                       o->name);
        return -1;
    }
    return 0;
}

static int check_objects(const felf_file_t *file, char *err, size_t errlen)
{
    const felf_arch_t *arch = felf_arch_by_name(file->architecture);
    if (arch == NULL) {
        felf_set_error(err, errlen, "unsupported architecture %s",
                       file->architecture);
        return -1;
    }
    size_t programs = 0, interps = 0;
    for (size_t i = 0; i < file->db.object_count; i++) {
        const felf_db_object_t *o = &file->db.objects[i];
        if (strcmp(o->kind, "program") == 0)
            programs++;
        else if (strcmp(o->kind, "interp") == 0)
            interps++;
        else if (strcmp(o->kind, "shared") != 0 &&
                 strcmp(o->kind, "plugin") != 0 &&
                 strcmp(o->kind, "audit") != 0 &&
                 strcmp(o->kind, "runtime") != 0 &&
                 strcmp(o->kind, "resource") != 0) {
            felf_set_error(err, errlen,
                           "object %s has an unknown kind", o->name);
            return -1;
        }
        if (strcmp(o->kind, "resource") == 0)
            continue;
        const uint8_t *data;
        size_t size;
        uint8_t *owned;
        if (felf_file_object_data(file, o, &data, &size, &owned,
                                  err, errlen) != 0)
            return -1;
        int bad = check_elf_object(arch, o, data, size, err, errlen);
        free(owned);
        if (bad != 0)
            return -1;
    }
    if (programs == 0 || interps != 1) {
        felf_set_error(err, errlen,
                       "container must contain at least one program and exactly "
                       "one interpreter (found %zu and %zu)", programs, interps);
        return -1;
    }
    if (programs > 1) {
        int found = 0;
        for (size_t i = 0; i < file->db.object_count; i++)
            if (strcmp(file->db.objects[i].kind, "program") == 0 &&
                strcmp(file->db.objects[i].name,
                       file->db.default_program) == 0)
                found = 1;
        if (!found) {
            felf_set_error(err, errlen,
                           "container has %zu programs but default program '%s' "
                           "is not one of them", programs,
                           file->db.default_program);
            return -1;
        }
    }

    for (size_t i = 0; i < file->db.object_count; i++) {
        const felf_db_object_t *a = &file->db.objects[i];
        for (size_t j = i + 1; j < file->db.object_count; j++) {
            const felf_db_object_t *b = &file->db.objects[j];
            if (a->offset < b->offset + b->stored_size &&
                b->offset < a->offset + a->stored_size) {
                felf_set_error(err, errlen, "objects %s and %s overlap",
                               a->name, b->name);
                return -1;
            }
        }
    }
    return 0;
}

int felf_cmd_verify(const char *path, char *err, size_t errlen)
{
    felf_file_t file;
    if (felf_file_open(path, 1, &file, err, errlen) != 0)
        return -1;
    if (felf_db_validate_layout(&file.db, file.size,
                                file.footer.payload_offset,
                                file.footer.payload_size,
                                err, errlen) != 0)
        goto fail;
    if ((file.footer.db_offset % FELF_ALIGNMENT) != 0) {
        felf_set_error(err, errlen, "database is not page-aligned");
        goto fail;
    }
    if ((file.footer.payload_offset % FELF_ALIGNMENT) != 0) {
        felf_set_error(err, errlen, "payload region is not page-aligned");
        goto fail;
    }
    if (file.footer.object_count != file.db.object_count) {
        felf_set_error(err, errlen,
                       "footer object count does not match database");
        goto fail;
    }
    if (((file.footer.flags & FELF_FLAG_STRICT) != 0) != file.db.strict) {
        felf_set_error(err, errlen,
                       "footer strict flag does not match database");
        goto fail;
    }
    if (((file.footer.flags & FELF_FLAG_LAZY) != 0) != file.db.lazy) {
        felf_set_error(err, errlen,
                       "footer lazy flag does not match database");
        goto fail;
    }
    if (file.db.lazy) {
        int audit_found = 0;
        for (size_t i = 0; i < file.db.object_count; i++)
            if (strcmp(file.db.objects[i].kind, "audit") == 0)
                audit_found = 1;
        if (!audit_found) {
            felf_set_error(err, errlen,
                           "lazy container has no embedded audit module");
            goto fail;
        }
    }
    if (check_sqlite(&file.db, err, errlen) != 0)
        goto fail;
    if (check_objects(&file, err, errlen) != 0)
        goto fail;
    felf_file_close(&file);
    return 0;

fail:
    felf_file_close(&file);
    return -1;
}
