#ifndef FELF_STORE_BUILD_H
#define FELF_STORE_BUILD_H

#include "resolver.h"

#include <stddef.h>
#include <stdint.h>

/* Build the FELF's database for R in memory.  *OUT (freed by the caller)
 * receives its bytes, which depend only on the content: the counters
 * SQLite keeps in the header are reset. */
int felf_db_build(felf_resolver_t *r, const char *package_name,
                  const char *package_version, const char *created_by,
                  int strict, int lazy, uint8_t **out, size_t *out_size,
                  char *err, size_t errlen);

#endif
