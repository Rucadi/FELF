#ifndef FELF_FILE_H
#define FELF_FILE_H

#include "felf_format.h"
#include "felf_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Read all of a non-empty regular file (at most 2 GiB) into *OUT, which the
 * caller frees. */
int felf_read_file(const char *path, uint8_t **out, size_t *out_size,
                   char *err, size_t errlen);
/* Create or truncate PATH with MODE and write DATA; PATH is removed again
 * on failure. */
int felf_write_file(const char *path, const uint8_t *data, size_t size,
                    mode_t mode, char *err, size_t errlen);

typedef struct {
    uint8_t *data;
    size_t size;
    felf_footer_t footer;
    felf_db_t db;
    char package_name[256];
    char package_version[128];
    char abi[32];
    char architecture[32];
} felf_file_t;

int felf_file_open(const char *path, int verify_hashes, felf_file_t *file,
                   char *err, size_t errlen);
void felf_file_close(felf_file_t *file);

/* Return the object's uncompressed bytes.  For an uncompressed object the
 * pointer is inside the mapped container and stays valid until
 * felf_file_close(), and *owned is NULL.  A compressed object is
 * decompressed into *owned, which the caller frees. */
int felf_file_object_data(const felf_file_t *file,
                          const felf_db_object_t *object,
                          const uint8_t **data, size_t *size,
                          uint8_t **owned, char *err, size_t errlen);

/* The file name object INDEX of DB has in a directory written by felf
 * extract: its name with characters other than
 * [A-Za-z0-9._+-] replaced by '_', and ".<id>" appended when that name is
 * another object's too, or is felf.sqlite.  Only the database decides it. */
void felf_file_object_path(const felf_db_t *db, size_t index,
                           char *out, size_t outlen);

/* The database's name in an extracted directory. */
#define FELF_EXTRACT_DB "felf.sqlite"

#endif
