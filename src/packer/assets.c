/* CMake passes the files' paths as FELF_*_FILE. */
#include "assets.h"

static const unsigned char bootstrap[] = {
#embed FELF_BOOTSTRAP_FILE
};
static const unsigned char audit[] = {
#embed FELF_AUDIT_FILE
};
static const unsigned char shim[] = {
#embed FELF_SHIM_FILE
};

const felf_asset_t felf_asset_bootstrap = {
    "felf-bootstrap", "FELF_BOOTSTRAP", bootstrap, sizeof(bootstrap)};
const felf_asset_t felf_asset_audit = {
    "libfelf_audit.so", "FELF_AUDIT", audit, sizeof(audit)};
const felf_asset_t felf_asset_shim = {
    "libfelf_runtime.so", "FELF_SHIM", shim, sizeof(shim)};

static const unsigned char locale_archive[] = {
#embed FELF_LOCALE_ARCHIVE_FILE
};
const felf_asset_t felf_asset_locale_archive = {
    "locale-archive", NULL, locale_archive, sizeof(locale_archive)};
