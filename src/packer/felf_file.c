#include "felf_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int felf_read_file(const char *path, uint8_t **out, size_t *out_size,
                   char *err, size_t errlen)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        felf_set_error(err, errlen,
                       "cannot open %s: %s", path, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        felf_set_error(err, errlen,
                       "cannot stat %s: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    if (st.st_size <= 0 ||
        (uint64_t)st.st_size > (uint64_t)2 * 1024 * 1024 * 1024) {
        felf_set_error(err, errlen, "%s is empty or too large", path);
        close(fd);
        return -1;
    }
    size_t size = (size_t)st.st_size;
    uint8_t *data = malloc(size);
    if (data == NULL) {
        felf_set_error(err, errlen, "out of memory reading %s", path);
        close(fd);
        return -1;
    }
    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, data + done, size - done);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            felf_set_error(err, errlen, "read %s failed: %s", path,
                           strerror(errno));
            free(data);
            close(fd);
            return -1;
        }
        if (n == 0)
            break;
        done += (size_t)n;
    }
    close(fd);
    if (done != size) {
        felf_set_error(err, errlen, "short read from %s", path);
        free(data);
        return -1;
    }
    *out = data;
    *out_size = size;
    return 0;
}

int felf_source_read(const felf_source_t *source, uint8_t **out,
                     size_t *out_size, char *err, size_t errlen)
{
    if (source->path != NULL)
        return felf_read_file(source->path, out, out_size, err, errlen);
    uint8_t *copy = source->size != 0 ? malloc(source->size) : NULL;
    if (copy == NULL) {
        felf_set_error(err, errlen, "out of memory reading %s",
                       source->name ? source->name : "a built-in file");
        return -1;
    }
    memcpy(copy, source->data, source->size);
    *out = copy;
    *out_size = source->size;
    return 0;
}

int felf_write_file(const char *path, const uint8_t *data, size_t size,
                    mode_t mode, char *err, size_t errlen)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) {
        felf_set_error(err, errlen, "cannot create %s: %s", path,
                       strerror(errno));
        return -1;
    }
    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, data + done, size - done);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        done += (size_t)n;
    }
    /* An existing file keeps its mode through open(): set it, within the
     * caller's umask.  close() reports delayed write errors (NFS, quotas). */
    mode_t mask = umask(0);
    umask(mask);
    int failed = done != size || fchmod(fd, mode & ~mask) < 0;
    if (close(fd) < 0)
        failed = 1;
    if (failed) {
        felf_set_error(err, errlen, "writing %s failed: %s", path,
                       strerror(errno));
        unlink(path);
        return -1;
    }
    return 0;
}

int felf_file_object_data(const felf_file_t *file,
                          const felf_db_object_t *object,
                          const uint8_t **data, size_t *size,
                          uint8_t **owned, char *err, size_t errlen)
{
    *owned = NULL;
    const uint8_t *stored;
    if (object->offset > file->size ||
        object->stored_size > file->size - object->offset) {
        felf_set_error(err, errlen, "object %s is outside the container",
                       object->name);
        return -1;
    }
    stored = file->data + object->offset;
    if (!object->compressed) {
        *data = stored;
        *size = (size_t)object->stored_size;
        return 0;
    }
    uint8_t *buf = malloc((size_t)object->size);
    if (buf == NULL) {
        felf_set_error(err, errlen, "out of memory decompressing %s",
                       object->name);
        return -1;
    }
    if (felf_db_object_unpack(object, stored, (size_t)object->stored_size,
                              buf, (size_t)object->size, err, errlen) != 0) {
        free(buf);
        return -1;
    }
    *owned = buf;
    *data = buf;
    *size = (size_t)object->size;
    return 0;
}

static void safe_name(const char *input, char *out, size_t outlen)
{
    if (input[0] == '\0' || strcmp(input, ".") == 0 ||
        strcmp(input, "..") == 0)
        input = "object";
    size_t o = 0;
    for (size_t i = 0; input[i] && o + 1 < outlen; i++) {
        unsigned char c = (unsigned char)input[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' ||
            c == '-' || c == '+')
            out[o++] = (char)c;
        else
            out[o++] = '_';
    }
    out[o] = '\0';
}

void felf_file_object_path(const felf_db_t *db, size_t index,
                           char *out, size_t outlen)
{
    char base[448], other[448];
    safe_name(db->objects[index].name, base, sizeof(base));
    int clash = strcmp(base, FELF_EXTRACT_DB) == 0;
    for (size_t i = 0; !clash && i < db->object_count; i++) {
        if (i == index)
            continue;
        safe_name(db->objects[i].name, other, sizeof(other));
        clash = strcmp(base, other) == 0;
    }
    if (clash)
        snprintf(out, outlen, "%s.%lld", base,
                 (long long)db->objects[index].id);
    else
        snprintf(out, outlen, "%s", base);
}

int felf_file_open(const char *path, int verify_hashes, felf_file_t *file,
                   char *err, size_t errlen)
{
    memset(file, 0, sizeof(*file));
    if (felf_read_file(path, &file->data, &file->size, err, errlen) != 0)
        return -1;
    if (felf_outer_elf_validate(file->data, file->size, err, errlen) != 0)
        goto fail;
    if (felf_footer_read(file->data, file->size, &file->footer,
                         err, errlen) != 0)
        goto fail;
    if (felf_db_open_memory(file->data + file->footer.db_offset,
                            (size_t)file->footer.db_size, &file->db,
                            err, errlen) != 0)
        goto fail;
    if (felf_db_validate_layout(&file->db, file->size,
                                file->footer.payload_offset,
                                file->footer.payload_size,
                                err, errlen) != 0)
        goto fail;
    if (file->db.object_count != file->footer.object_count) {
        felf_set_error(err, errlen,
                       "object count mismatch between footer and DB");
        goto fail;
    }
    if (verify_hashes) {
        for (size_t i = 0; i < file->db.object_count; i++) {
            felf_db_object_t *o = &file->db.objects[i];
            const uint8_t *data;
            size_t size;
            uint8_t *owned;
            if (felf_file_object_data(file, o, &data, &size, &owned,
                                      err, errlen) != 0)
                goto fail;
            uint8_t hash[32];
            felf_sha256(data, size, hash);
            free(owned);
            if (memcmp(hash, o->sha256, 32) != 0) {
                felf_set_error(err, errlen, "checksum mismatch for object %s",
                               o->name);
                goto fail;
            }
        }
    }

    /* Read package metadata.  Failure here is a malformed container. */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(file->db.handle,
            "SELECT name, version, architecture, abi FROM package WHERE id=1",
            -1, &stmt, NULL) != SQLITE_OK) {
        felf_set_error(err, errlen, "cannot read package metadata: %s",
                       sqlite3_errmsg(file->db.handle));
        goto fail;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        felf_set_error(err, errlen, "package metadata is missing");
        sqlite3_finalize(stmt);
        goto fail;
    }
    const char *name = (const char *)sqlite3_column_text(stmt, 0);
    const char *version = (const char *)sqlite3_column_text(stmt, 1);
    const char *arch = (const char *)sqlite3_column_text(stmt, 2);
    const char *abi = (const char *)sqlite3_column_text(stmt, 3);
    if (name == NULL || version == NULL || arch == NULL || abi == NULL ||
        strlen(name) >= sizeof(file->package_name) ||
        strlen(version) >= sizeof(file->package_version) ||
        strlen(arch) >= sizeof(file->architecture) ||
        strlen(abi) >= sizeof(file->abi)) {
        felf_set_error(err, errlen, "package metadata is invalid");
        sqlite3_finalize(stmt);
        goto fail;
    }
    snprintf(file->package_name, sizeof(file->package_name), "%s", name);
    snprintf(file->package_version, sizeof(file->package_version), "%s",
             version);
    snprintf(file->architecture, sizeof(file->architecture), "%s", arch);
    snprintf(file->abi, sizeof(file->abi), "%s", abi);
    sqlite3_finalize(stmt);
    return 0;

fail:
    felf_file_close(file);
    return -1;
}

void felf_file_close(felf_file_t *file)
{
    if (file == NULL)
        return;
    felf_db_close(&file->db);
    free(file->data);
    memset(file, 0, sizeof(*file));
}
