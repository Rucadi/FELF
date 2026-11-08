#ifndef FELF_BOOTSTRAP_HANDOFF_H
#define FELF_BOOTSTRAP_HANDOFF_H

#include "felf_storage.h"

#include <stddef.h>

/* Copy DATA into a new memfd named after NAME and return a descriptor
 * >= MIN_FD (F_DUPFD, so it is inherited across exec). */
int felf_memfd_from_bytes(const char *name, const void *data, size_t size,
                          int min_fd, char *err, size_t errlen);
/* Object memfds are created and sized by felf_memfd_alloc() in order (so
 * descriptor numbers are deterministic), mapped, and then filled by jobs:
 * one per zstd frame of a compressed object, one per FELF_ZSTD_CHUNK of an
 * uncompressed one.  felf_fill_all() runs the jobs on all CPUs. */
typedef struct {
    const char *name;
    const uint8_t *src;
    size_t src_len;
    uint8_t *dst;
    size_t dst_len;
    int compressed;
} felf_fill_job_t;

int felf_memfd_alloc(const char *name, uint64_t size, int min_fd,
                     char *err, size_t errlen);
/* Append the jobs that write O's stored bytes into DST (o->size bytes). */
int felf_fill_jobs_add(const felf_db_object_t *o, const uint8_t *stored,
                       uint8_t *dst, felf_fill_job_t **jobs, size_t *count,
                       size_t *cap, char *err, size_t errlen);
int felf_fill_all(const felf_fill_job_t *jobs, size_t count, char *err,
                  size_t errlen);
int felf_patch_program_interp(int program_fd, int interp_fd,
                              char *err, size_t errlen);
/* Build the application environment: caller environment minus LD_* (kept
 * as FELF_ORIG_*), plus the database environment table (expanded), plus the
 * handoff variables.  Every string in *out_env is heap-allocated. */
int felf_build_environment(char **envp, const felf_db_t *db,
                           const char *preload_paths, const char *audit_path,
                           const char *app_path, const char *root,
                           int strict, const char *locale_archive,
                           const char *default_locale, char ***out_env,
                           char *err, size_t errlen);
void felf_free_environment(char **env);

#endif
