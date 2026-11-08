#ifndef FELF_LOADER_PROTOCOL_H
#define FELF_LOADER_PROTOCOL_H

#include <stdint.h>

/*
 * FELF loader handoff protocol, version 2.
 *
 * The static bootstrap does not jump into glibc internals and does not
 * fabricate an auxiliary vector.  It materializes the embedded program and
 * private interpreter as memfds, rewrites the program's PT_INTERP string to
 * /proc/self/fd/<interpreter-fd>, and calls:
 *
 *     execveat(program_fd, "", argv, envp, AT_EMPTY_PATH)
 *
 * The kernel therefore performs a normal ELF exec.  Bundled libraries are
 * reached through /proc/self/fd/<fd> paths, either from LD_PRELOAD or from the
 * audit index (lazy mode).  The audit module is always loaded: it also looks
 * up host-preferred libraries (the graphics stack) on the host.
 *
 * Descriptor rules:
 *   - Every object descriptor is >= FELF_FD_BASE.
 *   - FELF_AUDIT_INDEX_FD holds the audit index.
 *   - FELF_CONTAINER_FD holds the FELF file when libraries are mapped
 *     from it directly (see FELF_MAP_PREFIX).
 *   - Descriptors survive the bootstrap's exec.  The optional runtime shim
 *     marks every descriptor >= FELF_AUDIT_INDEX_FD close-on-exec once the
 *     application starts, so child processes do not inherit them.
 *
 * Environment passed to the application:
 *   LD_PRELOAD, LD_AUDIT         rebuilt by the bootstrap
 *   FELF_APP_PATH                absolute path of the FELF file that ran
 *   FELF_ROOT                    absolute bundle root (see meta key "root")
 *   FELF_ORIG_<name>             a host LD_* variable removed by the bootstrap
 *   FELF_HANDOFF_VERSION, FELF_STRICT
 *   LOCALE_ARCHIVE               glibc locale data (host or embedded) when the
 *                                caller set neither LOCALE_ARCHIVE nor
 *                                LOCALE_ARCHIVE_2_27; FELF_LOCALE_ARCHIVE_SET=1
 *   LANG                         C.UTF-8 when the caller set no locale at all;
 *                                the application then starts in UTF-8
 * The runtime shim removes the FELF/LD variables again (except FELF_ROOT,
 * which tools such as the bundled GCC driver read in child processes) and
 * restores the FELF_ORIG_* values, so children see the user's original
 * environment.
 */

#define FELF_HANDOFF_VERSION 2u
#define FELF_FD_BASE 1000

#define FELF_ENV_HANDOFF_VERSION "FELF_HANDOFF_VERSION"
#define FELF_ENV_STRICT "FELF_STRICT"
#define FELF_ENV_APP_PATH "FELF_APP_PATH"
#define FELF_ENV_ROOT "FELF_ROOT"
#define FELF_ENV_ORIG_PREFIX "FELF_ORIG_"
#define FELF_ENV_DEBUG "FELF_DEBUG"
/* FELF_INSTALL_LINKS=DIR: the bootstrap makes DIR/<name> a symlink to the
 * FELF for each program name and alias, and exits instead of running one. */
#define FELF_ENV_INSTALL_LINKS "FELF_INSTALL_LINKS"
/* Set when the bootstrap chose LOCALE_ARCHIVE itself; the runtime shim then
 * maps the archive and removes the variable for child processes. */
#define FELF_ENV_LOCALE_SET "FELF_LOCALE_ARCHIVE_SET"
#define FELF_ENV_LOCALE_ARCHIVE "LOCALE_ARCHIVE"

/* The audit module is freestanding and cannot use getenv() before libc is
 * available, so the index descriptor is a documented fixed descriptor. */
#define FELF_AUDIT_INDEX_FD 998

/* Direct mapping (FELF's patched loader, src/glibc/patches/): a library stored
 * uncompressed and page-aligned is not materialized.  Its
 * audit index path is FELF_MAP_PREFIX followed by its offset in the FELF
 * file in lowercase hexadecimal; the loader maps it from the FELF file,
 * which the bootstrap leaves open on FELF_CONTAINER_FD.  The packer
 * recognizes such a loader by FELF_MAP_PREFIX in its bytes. */
#define FELF_MAP_PREFIX "felf-map-1:"
#define FELF_CONTAINER_FD 999

#define FELF_AUDIT_INDEX_MAGIC "FELFA3\0\0"
#define FELF_AUDIT_INDEX_VERSION 3u
#define FELF_AUDIT_NAME_SIZE 128u
#define FELF_AUDIT_PATH_SIZE 32u
#define FELF_AUDIT_ROOT_SIZE 1024u
#define FELF_AUDIT_DIR_SIZE 256u
#define FELF_AUDIT_MAX_DIRS 64u
#define FELF_AUDIT_MAX_PATTERNS 256u
#define FELF_AUDIT_MAX_ENTRIES 65536u
/* The audit module reads the index into a static buffer of this size.  A
 * larger index cannot be read, and the module would fail closed. */
#define FELF_AUDIT_BUFFER_SIZE 4194304u

#define FELF_AUDIT_FLAG_STRICT 0x1u
#define FELF_AUDIT_FLAG_DEBUG 0x2u

/*
 * Audit index layout, in this order:
 *
 *   header
 *   entry_count     x felf_audit_index_entry_t   bundled name -> memfd path
 *   pattern_count   x char[FELF_AUDIT_NAME_SIZE] host-preferred patterns
 *   libpath_count   x char[FELF_AUDIT_DIR_SIZE]  caller's LD_LIBRARY_PATH
 *   default_count   x char[FELF_AUDIT_DIR_SIZE]  host default directories
 *
 * The audit module searches the host like the host's ld.so would:
 * LD_LIBRARY_PATH directories, then /etc/ld.so.cache, then the defaults.
 */
typedef struct __attribute__((packed)) {
    uint8_t magic[8];
    uint32_t version;
    uint32_t entry_count;
    uint32_t flags;
    uint32_t pattern_count;
    uint32_t libpath_count;
    uint32_t default_count;
    /* Absolute bundle root, NUL-terminated, or empty.  In strict mode an
     * absolute path under this directory may be loaded directly (plugins
     * that live in a tree bundle). */
    char root[FELF_AUDIT_ROOT_SIZE];
} felf_audit_index_header_t;

typedef struct __attribute__((packed)) {
    char name[FELF_AUDIT_NAME_SIZE];
    char path[FELF_AUDIT_PATH_SIZE];
} felf_audit_index_entry_t;

_Static_assert(sizeof(felf_audit_index_header_t) == 32 + FELF_AUDIT_ROOT_SIZE,
               "FELF audit index header layout");
_Static_assert(sizeof(felf_audit_index_entry_t) == 160,
               "FELF audit index entry must be 160 bytes");

#endif
