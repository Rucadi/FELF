#ifndef FELF_RESOLVER_H
#define FELF_RESOLVER_H

#include "felf_arch.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *name;
    char *soname;
    char *path;
    char *kind;
    char *namespace_name;
    uint8_t *data;
    size_t size;
    uint64_t alignment;
    uint64_t offset;
    uint8_t sha256[32];
    /* zstd-compressed copy of data, or NULL when stored uncompressed. */
    uint8_t *zdata;
    size_t zsize;
    char **aliases;
    size_t alias_count;
    char **deps;
    size_t dep_count;
    size_t dep_cap;
    char *rpath;
    char *runpath;
    /* Symbol versions defined and required (see felf_elf_t). */
    char **verdefs;
    size_t verdef_count;
    char **verneed_files;
    char **verneed_versions;
    size_t verneed_count;
} felf_object_t;

typedef struct {
    felf_object_t *objects;
    size_t count;
    size_t cap;
    char **search_dirs;
    size_t search_count;
    size_t search_cap;
    /* FELF's glibc: its loader is bundled, and glibc's libraries are taken
     * from it, for every program; direct_map is then set (the loader maps
     * uncompressed libraries from the file).  NULL: each program's
     * PT_INTERP, whose canonical path is program_interp. */
    char *glibc_dir;
    char *program_interp;
    int direct_map;
    /* Bundle root relative to the output's directory, or NULL. */
    char *root;
    /* Name of the program run when argv[0] matches none (the first added). */
    char *default_program;
    /* "name=path-relative-to-root" per program, one per line, or NULL. */
    char *program_locations;
    /* Application environment entries, "NAME=VALUE". */
    char **env;
    size_t env_count;
    size_t env_cap;
    /* Host-preferred library patterns (see felf_host_libs.h).  A matching
     * dependency is embedded as a fallback when found and skipped when not. */
    char **host_libs;
    size_t host_lib_count;
    size_t host_lib_cap;
    /* Target architecture, taken from the first program.  Every object must
     * be built for it. */
    const felf_arch_t *arch;
    int strict;
} felf_resolver_t;

void felf_resolver_init(felf_resolver_t *r);
void felf_resolver_free(felf_resolver_t *r);

int felf_resolver_add_search_dir(felf_resolver_t *r, const char *dir,
                                 char *err, size_t errlen);

/* Add a program at PATH under NAME (NULL: the file's basename), the name
 * it is invoked by (argv[0], usually a symlink).  The first program is the
 * default.  Every program must use the same dynamic linker; the first one
 * selects it. */
int felf_resolver_add_program(felf_resolver_t *r, const char *path,
                              const char *name, char *err, size_t errlen);

/* Another name for the embedded program PROGRAM (a program name or one of
 * its aliases): invoked as NAME, it runs with NAME as argv[0], so a
 * multicall binary (coreutils, busybox) dispatches on it. */
int felf_resolver_add_program_alias(felf_resolver_t *r, const char *program,
                                    const char *name, char *err,
                                    size_t errlen);

int felf_resolver_add_audit(felf_resolver_t *r, const char *path,
                            char *err, size_t errlen);
int felf_resolver_add_runtime(felf_resolver_t *r, const char *path,
                              char *err, size_t errlen);
int felf_resolver_add_env(felf_resolver_t *r, const char *entry,
                          char *err, size_t errlen);
/* Embed the file at PATH as a non-ELF "resource" object called NAME. */
int felf_resolver_add_resource(felf_resolver_t *r, const char *path,
                               const char *name, char *err, size_t errlen);
int felf_resolver_add_host_lib(felf_resolver_t *r, const char *pattern,
                               char *err, size_t errlen);
/* Nonzero when the basename of NAME matches a host-preferred pattern. */
int felf_resolver_is_host_lib(const felf_resolver_t *r, const char *name);

/* Resolve every DT_NEEDED of the program and the included objects
 * transitively.  Failures are fatal. */
int felf_resolver_resolve(felf_resolver_t *r, char *err, size_t errlen);

/* Bundle the dependency closure of an object that stays OUTSIDE the
 * container (for example a Qt plugin kept as a file in a tree bundle).  The
 * object itself is not embedded unless something else needs it.  Returns 0
 * on success, 1 if the closure could not be resolved (nothing is added and a
 * warning is left in err), and -1 on a fatal error.  Call after
 * felf_resolver_resolve(). */
int felf_resolver_add_external(felf_resolver_t *r, const char *path,
                               char *err, size_t errlen);

/* Embed the shared object at PATH and its dependency closure.  A library
 * whose name is already bundled is skipped (the first one wins, as in a
 * search path) and 0 returned.  Returns 1, adding nothing and leaving a
 * warning in err, if PATH is not a shared library for the target or its
 * closure cannot be resolved, and -1 on a fatal error.  Call after
 * felf_resolver_resolve(). */
int felf_resolver_add_include(felf_resolver_t *r, const char *path,
                              char *err, size_t errlen);

/* Drop external objects that nothing needed.  Call before sorting. */
void felf_resolver_drop_external(felf_resolver_t *r);

/* Every symbol version an embedded object needs from another embedded
 * object must be defined there, as glibc's loader requires; for example a
 * program built against a newer glibc than the bundled one is rejected. */
int felf_resolver_check_versions(felf_resolver_t *r, char *err,
                                 size_t errlen);

/* Deterministic order: program, interpreter, then remaining objects by name. */
void felf_resolver_sort(felf_resolver_t *r);

felf_object_t *felf_resolver_find(felf_resolver_t *r, const char *name);
/* The default program. */
felf_object_t *felf_resolver_program(felf_resolver_t *r);
felf_object_t *felf_resolver_interp(felf_resolver_t *r);

#endif
