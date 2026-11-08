#ifndef FELF_PACK_H
#define FELF_PACK_H

#include <stddef.h>

typedef struct {
    /* The default program, as PATH or NAME=PATH. */
    const char *input;
    /* More programs sharing the same libraries, as PATH or NAME=PATH.  Each
     * runs when the FELF is invoked as NAME (a symlink). */
    const char *const *programs;
    size_t program_count;
    /* More names for a program, as PROGRAM=NAME[,NAME...]: each runs
     * PROGRAM with that name as argv[0] (multicall binaries). */
    const char *const *aliases;
    size_t alias_count;
    const char *output;
    const char *bootstrap;
    const char *audit;
    const char *shim;
    /* FELF's glibc (its libraries and patched loader), bundled with every
     * program instead of its own; NULL: programs keep their own glibc. */
    const char *glibc_dir;
    const char *root;
    const char *const *includes;
    size_t include_count;
    /* Objects that stay outside the container; only their dependency
     * closure is bundled. */
    const char *const *plugins;
    size_t plugin_count;
    const char *const *env;
    size_t env_count;
    const char *const *lib_dirs;
    size_t lib_dir_count;
    /* Extra host-preferred patterns, added to the defaults unless
     * no_default_host_libs is set. */
    const char *const *host_libs;
    size_t host_lib_count;
    int no_default_host_libs;
    /* zstd level for object compression; 0 stores objects uncompressed. */
    int compress_level;
    /* glibc locale-archive embedded for the bundled glibc (felf's own,
     * src/packer/assets.h).  Every FELF embeds one; NULL is an error. */
    const char *locale_archive;
    /* Compress libraries too (an explicit --compress LEVEL): a smaller
     * file, but they are then copied into memory at start instead of
     * being mapped from the file. */
    int compress_libraries;
    int strict;
    int lazy;
} felf_pack_options_t;

int felf_pack(const felf_pack_options_t *options, char *err, size_t errlen);

#endif
