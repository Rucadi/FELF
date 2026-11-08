#ifndef FELF_AUDIT_INDEX_H
#define FELF_AUDIT_INDEX_H

#include "felf_storage.h"

#include <stddef.h>
#include <stdint.h>

/* Build the audit redirection index and install it at FELF_AUDIT_INDEX_FD.
 * object_fds and direct_offsets are indexed like db->objects: a library
 * mapped from the FELF file has its offset in direct_offsets (fd -1);
 * otherwise fd -1 means the object was not materialized.  ROOT is the
 * absolute bundle root (may be empty).
 * LD_LIBRARY_PATH is the caller's value (may be NULL); its directories are
 * searched first for host libraries. */
int felf_build_audit_index(const felf_db_t *db, const int *object_fds,
                           const uint64_t *direct_offsets,
                           int strict, const char *root, int debug,
                           const char *ld_library_path,
                           char *err, size_t errlen);

#endif
