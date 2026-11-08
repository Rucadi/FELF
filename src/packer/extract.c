#include "commands.h"
#include "felf_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Write every object, decompressed, and the database into OUTPUT, named by
 * felf_file_object_path(). */
int felf_cmd_extract(const char *path, const char *output,
                     char *err, size_t errlen)
{
    felf_file_t file;
    if (felf_file_open(path, 0, &file, err, errlen) != 0)
        return -1;
    if (mkdir(output, 0755) < 0 && errno != EEXIST) {
        felf_set_error(err, errlen, "cannot create %s: %s", output,
                       strerror(errno));
        felf_file_close(&file);
        return -1;
    }
    struct stat st;
    if (stat(output, &st) < 0 || !S_ISDIR(st.st_mode)) {
        felf_set_error(err, errlen, "%s is not a directory", output);
        felf_file_close(&file);
        return -1;
    }

    for (size_t i = 0; i < file.db.object_count; i++) {
        const felf_db_object_t *o = &file.db.objects[i];
        const uint8_t *data;
        size_t size;
        uint8_t *owned;
        if (felf_file_object_data(&file, o, &data, &size, &owned,
                                  err, errlen) != 0) {
            felf_file_close(&file);
            return -1;
        }
        char name[512], target[4096];
        felf_file_object_path(&file.db, i, name, sizeof(name));
        snprintf(target, sizeof(target), "%s/%s", output, name);
        mode_t mode = (strcmp(o->kind, "shared") == 0 ||
                       strcmp(o->kind, "plugin") == 0 ||
                       strcmp(o->kind, "resource") == 0)
                          ? 0644 : 0755;
        int wrc = felf_write_file(target, data, size, mode, err, errlen);
        free(owned);
        if (wrc != 0) {
            felf_file_close(&file);
            return -1;
        }
    }

    char db_path[4096];
    snprintf(db_path, sizeof(db_path), "%s/" FELF_EXTRACT_DB, output);
    if (felf_write_file(db_path,
                        file.data + file.footer.db_offset,
                        (size_t)file.footer.db_size, 0644, err, errlen) != 0) {
        felf_file_close(&file);
        return -1;
    }
    printf("extracted %zu objects and " FELF_EXTRACT_DB " to %s\n",
           file.db.object_count, output);
    felf_file_close(&file);
    return 0;
}
