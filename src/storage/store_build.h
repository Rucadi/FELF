#ifndef FELF_STORE_BUILD_H
#define FELF_STORE_BUILD_H

#include "resolver.h"

#include <stddef.h>
#include <stdint.h>

int felf_db_build_file(felf_resolver_t *r, const char *path,
                       const char *package_name, const char *package_version,
                       const char *created_by, int strict,
                       int lazy, char *err, size_t errlen);

/* SQLite counts the writes in the database header (the file change
 * counter, the schema cookie): set them to 1, so the bytes depend only on
 * the content, however it was written.  SIZE is the database's. */
void felf_db_normalize(uint8_t *db, size_t size);

#endif
