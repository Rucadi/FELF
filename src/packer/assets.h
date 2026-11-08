#ifndef FELF_ASSETS_H
#define FELF_ASSETS_H

#include <stddef.h>

/* The runtime pieces every FELF carries, embedded in the packer at build
 * time: the static bootstrap, the audit module, and the runtime shim. */
typedef struct {
    const char *name;        /* file name, also the object's name */
    const char *env;         /* variable that overrides it with a path */
    const unsigned char *data;
    size_t size;
} felf_asset_t;

extern const felf_asset_t felf_asset_bootstrap;
extern const felf_asset_t felf_asset_audit;
extern const felf_asset_t felf_asset_shim;

/* FELF's glibc (nix/pkgs/felf-glibc.nix): glibc's libraries and its dynamic
 * linker patched to map libraries straight from the FELF file.  Every
 * program is bundled with it instead of its own glibc.  The list ends with
 * an entry whose name is NULL.  It is never empty: the build requires
 * FELF_GLIBC_DIR. */
extern const felf_asset_t felf_glibc_assets[];
extern const char felf_glibc_version[];

/* The glibc locale-archive every FELF carries for FELF's glibc, which has
 * no usable host locale location of its own.  It is not optional: the
 * bootstrap points LOCALE_ARCHIVE at it when the caller's locale is C or
 * unset, or the host has no archive, and names C.UTF-8 when the caller set
 * no locale at all.  There is no option to omit or replace it. */
extern const felf_asset_t felf_asset_locale_archive;

#endif
