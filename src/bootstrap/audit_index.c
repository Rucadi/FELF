#define _GNU_SOURCE
#include "audit_index.h"
#include "felf_arch.h"
#include "felf_format.h"
#include "felf_loader_protocol.h"
#include "handoff.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The path the audit module returns for an object: its memfd, or a
 * reference to it in the FELF file when the loader maps it from there. */
static void object_path(char *out, size_t outlen, int fd, uint64_t offset)
{
    if (offset != 0)
        snprintf(out, outlen, FELF_MAP_PREFIX "%llx",
                 (unsigned long long)offset);
    else
        snprintf(out, outlen, "/proc/self/fd/%d", fd);
}

/* A name the index will hold, in the order the names were found: of the
 * objects sharing a name, the first one found keeps it. */
typedef struct {
    const char *name;
    size_t object;
    size_t order;
} index_name_t;

static void add_name(index_name_t *names, size_t *count, const char *name,
                     size_t object)
{
    if (name == NULL || name[0] == '\0' ||
        strlen(name) >= FELF_AUDIT_NAME_SIZE)
        return;
    names[*count] = (index_name_t){name, object, *count};
    (*count)++;
}

static int name_order(const void *pa, const void *pb)
{
    const index_name_t *a = pa, *b = pb;
    int c = strcmp(a->name, b->name);
    return c != 0 ? c : (a->order > b->order) - (a->order < b->order);
}

/* Append DIR if it is an absolute, existing directory not yet listed. */
static void add_host_dir(char *dirs, uint32_t *count, const char *dir)
{
    if (dir[0] != '/' || strchr(dir, '$') != NULL ||
        strlen(dir) >= FELF_AUDIT_DIR_SIZE || *count >= FELF_AUDIT_MAX_DIRS)
        return;
    for (uint32_t i = 0; i < *count; i++)
        if (strcmp(dirs + (size_t)i * FELF_AUDIT_DIR_SIZE, dir) == 0)
            return;
    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
        return;
    snprintf(dirs + (size_t)*count * FELF_AUDIT_DIR_SIZE, FELF_AUDIT_DIR_SIZE,
             "%s", dir);
    (*count)++;
}

static int is_library(const felf_db_object_t *o)
{
    return strcmp(o->kind, "shared") == 0 || strcmp(o->kind, "plugin") == 0;
}

int felf_build_audit_index(const felf_db_t *db, const int *object_fds,
                           const uint64_t *direct_offsets,
                           int strict, const char *root, int debug,
                           const char *ld_library_path,
                           char *err, size_t errlen)
{
    /* Every name an object can be asked for by, then sorted, so the audit
     * module can search them however many there are. */
    size_t max_entries = db->object_count * 2 + db->alias_count;
    index_name_t *names = malloc((max_entries ? max_entries : 1) *
                                 sizeof(*names));
    size_t bytes = sizeof(felf_audit_index_header_t) +
                   max_entries * sizeof(felf_audit_index_entry_t) +
                   FELF_AUDIT_MAX_PATTERNS * FELF_AUDIT_NAME_SIZE +
                   2 * FELF_AUDIT_MAX_DIRS * FELF_AUDIT_DIR_SIZE;
    uint8_t *buffer = calloc(1, bytes);
    if (names == NULL || buffer == NULL || max_entries > UINT32_MAX) {
        free(names);
        free(buffer);
        felf_set_error(err, errlen, "out of memory building audit index");
        return -1;
    }
    felf_audit_index_header_t *header = (felf_audit_index_header_t *)buffer;
    memcpy(header->magic, FELF_AUDIT_INDEX_MAGIC, 8);
    header->version = FELF_AUDIT_INDEX_VERSION;
    header->flags = (strict ? FELF_AUDIT_FLAG_STRICT : 0u) |
                    (debug ? FELF_AUDIT_FLAG_DEBUG : 0u);
    if (root != NULL) {
        if (strlen(root) >= sizeof(header->root)) {
            free(names);
            free(buffer);
            felf_set_error(err, errlen, "bundle root path is too long");
            return -1;
        }
        snprintf(header->root, sizeof(header->root), "%s", root);
    }
    felf_audit_index_entry_t *entries =
        (felf_audit_index_entry_t *)(buffer + sizeof(*header));

    size_t found = 0;
    for (size_t i = 0; i < db->object_count; i++) {
        const felf_db_object_t *object = &db->objects[i];
        if ((object_fds[i] < 0 && direct_offsets[i] == 0) ||
            !is_library(object))
            continue;
        add_name(names, &found, object->name, i);
        add_name(names, &found, object->soname, i);
    }
    for (size_t i = 0; i < db->alias_count; i++) {
        const felf_db_object_t *object =
            felf_db_object_by_id(db, db->aliases[i].object_id);
        if (object == NULL || !is_library(object))
            continue;
        size_t index = (size_t)(object - db->objects);
        if (object_fds[index] < 0 && direct_offsets[index] == 0)
            continue;
        add_name(names, &found, db->aliases[i].alias, index);
    }
    qsort(names, found, sizeof(*names), name_order);
    uint32_t count = 0;
    for (size_t i = 0; i < found; i++) {
        if (i > 0 && strcmp(names[i].name, names[i - 1].name) == 0)
            continue;
        felf_audit_index_entry_t *entry = &entries[count++];
        snprintf(entry->name, sizeof(entry->name), "%s", names[i].name);
        object_path(entry->path, sizeof(entry->path),
                    object_fds[names[i].object],
                    direct_offsets[names[i].object]);
    }
    free(names);
    header->entry_count = count;
    char *tail = (char *)(entries + count);

    /* Host-preferred patterns, one per line in the database. */
    uint32_t patterns = 0;
    for (const char *p = db->host_libs; *p != '\0';) {
        size_t n = strcspn(p, "\n");
        if (n > 0 && n < FELF_AUDIT_NAME_SIZE &&
            patterns < FELF_AUDIT_MAX_PATTERNS) {
            memcpy(tail, p, n);
            tail += FELF_AUDIT_NAME_SIZE;
            patterns++;
        }
        p += n;
        if (*p == '\n')
            p++;
    }
    header->pattern_count = patterns;

    char *dirs_start = tail;
    uint32_t libpath = 0;
    if (ld_library_path != NULL) {
        char copy[8192];
        snprintf(copy, sizeof(copy), "%s", ld_library_path);
        char *save = NULL;
        for (char *d = strtok_r(copy, ":;", &save); d != NULL;
             d = strtok_r(NULL, ":;", &save))
            add_host_dir(dirs_start, &libpath, d);
    }
    header->libpath_count = libpath;
    char *defaults_start = dirs_start + (size_t)libpath * FELF_AUDIT_DIR_SIZE;
    uint32_t defaults = 0;
    /* glibc's 64-bit defaults, the Debian/Ubuntu multiarch directories,
     * distributions that keep 64-bit libraries in /usr/lib, and the NixOS
     * graphics driver directory.  The audit module checks the ELF class and
     * machine of every candidate, so foreign directories are harmless. */
    static const char *const default_dirs[] = {
        "/lib64", "/usr/lib64",
        "/lib/" FELF_HOST_MULTIARCH, "/usr/lib/" FELF_HOST_MULTIARCH,
        "/lib", "/usr/lib", "/usr/local/lib",
        "/run/opengl-driver/lib",
    };
    for (size_t i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); i++)
        add_host_dir(defaults_start, &defaults, default_dirs[i]);
    header->default_count = defaults;
    tail = defaults_start + (size_t)defaults * FELF_AUDIT_DIR_SIZE;
    bytes = (size_t)(tail - (char *)buffer);

    int fd = felf_memfd_from_bytes("audit-index", buffer, bytes,
                                   FELF_AUDIT_INDEX_FD, err, errlen);
    free(buffer);
    if (fd < 0)
        return -1;
    if (fd != FELF_AUDIT_INDEX_FD) {
        if (dup2(fd, FELF_AUDIT_INDEX_FD) < 0) {
            felf_set_error(err, errlen,
                           "cannot install audit index descriptor: %s",
                           strerror(errno));
            close(fd);
            return -1;
        }
        close(fd);
    }
    if (fcntl(FELF_AUDIT_INDEX_FD, F_SETFD, 0) < 0) {
        felf_set_error(err, errlen, "cannot clear CLOEXEC on audit index: %s",
                       strerror(errno));
        return -1;
    }
    return 0;
}
