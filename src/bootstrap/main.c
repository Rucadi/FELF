#define _GNU_SOURCE
#include "felf_format.h"
#include "felf_storage.h"
#include "felf_loader_protocol.h"
#include "audit_index.h"
#include "felf_host_libs.h"
#include "handoff.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYS_execveat
#error "SYS_execveat is not available on this target"
#endif

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif

static int debug_enabled;

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("felf: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void debug(const char *fmt, ...)
{
    if (!debug_enabled)
        return;
    va_list ap;
    va_start(ap, fmt);
    fputs("felf[bootstrap]: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int find_self(char *path, size_t pathlen)
{
    ssize_t n = readlink("/proc/self/exe", path, pathlen - 1);
    if (n > 0) {
        path[n] = '\0';
        return 0;
    }
    const char *execfn = (const char *)getauxval(AT_EXECFN);
    if (execfn != NULL && execfn[0] != '\0' && strlen(execfn) < pathlen) {
        strcpy(path, execfn);
        return 0;
    }
    return -1;
}

/* Compute the absolute bundle root: dirname(self)/rel, canonicalized. */
static int compute_root(const char *self, const char *rel, char *out,
                        size_t outlen)
{
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", self);
    char *slash = strrchr(dir, '/');
    if (slash == NULL)
        return -1;
    if (slash == dir)
        slash[1] = '\0';
    else
        *slash = '\0';
    char joined[PATH_MAX * 2];
    if (rel == NULL || rel[0] == '\0')
        snprintf(joined, sizeof(joined), "%s", dir);
    else
        snprintf(joined, sizeof(joined), "%s/%s", dir, rel);
    char resolved[PATH_MAX];
    if (realpath(joined, resolved) == NULL)
        return -1;
    if (strlen(resolved) >= outlen)
        return -1;
    snprintf(out, outlen, "%s", resolved);
    return 0;
}

static int append_preload(char **buf, size_t *len, size_t *cap, int fd)
{
    char piece[64];
    int n = snprintf(piece, sizeof(piece), "%s/proc/self/fd/%d",
                     *len ? ":" : "", fd);
    if (n < 0)
        return -1;
    size_t need = *len + (size_t)n + 1;
    if (need > *cap) {
        size_t newcap = *cap ? *cap * 2 : 256;
        while (newcap < need)
            newcap *= 2;
        char *p = realloc(*buf, newcap);
        if (p == NULL)
            return -1;
        *buf = p;
        *cap = newcap;
    }
    memcpy(*buf + *len, piece, (size_t)n);
    *len += (size_t)n;
    (*buf)[*len] = '\0';
    return 0;
}

static int is_executable_kind(const char *kind)
{
    return strcmp(kind, "program") == 0 || strcmp(kind, "interp") == 0 ||
           strcmp(kind, "shared") == 0 || strcmp(kind, "plugin") == 0 ||
           strcmp(kind, "audit") == 0 || strcmp(kind, "runtime") == 0;
}

static int is_library_kind(const char *kind)
{
    return strcmp(kind, "shared") == 0 || strcmp(kind, "plugin") == 0;
}

/* The bundle root when this file is a copy of the FELF at PROGRAM's place in
 * the tree (meta "program_locations"), e.g. libexec/gcc/.../ld after the
 * tree's symlinks were replaced by copies: SELF ends in "/<location>" and the
 * root is what precedes it.  Returns -1 when that does not apply. */
static int root_from_location(const felf_db_t *db, const char *program,
                              const char *self, char *out, size_t outlen)
{
    size_t name_len = strlen(program);
    size_t self_len = strlen(self);
    for (const char *p = db->program_locations; *p != '\0';) {
        size_t n = strcspn(p, "\n");
        if (n > name_len + 1 && strncmp(p, program, name_len) == 0 &&
            p[name_len] == '=') {
            const char *loc = p + name_len + 1;
            size_t loc_len = n - name_len - 1;
            if (self_len > loc_len + 1 &&
                strncmp(self + self_len - loc_len, loc, loc_len) == 0 &&
                self[self_len - loc_len - 1] == '/') {
                char prefix[PATH_MAX];
                size_t plen = self_len - loc_len - 1;
                if (plen == 0 || plen >= sizeof(prefix))
                    return -1;
                memcpy(prefix, self, plen);
                prefix[plen] = '\0';
                char resolved[PATH_MAX];
                if (realpath(prefix, resolved) == NULL ||
                    strlen(resolved) >= outlen)
                    return -1;
                snprintf(out, outlen, "%s", resolved);
                return 0;
            }
            return -1;
        }
        p += n;
        if (*p == '\n')
            p++;
    }
    return -1;
}

/* The locale environment the bundled glibc gets. */
typedef struct {
    /* LOCALE_ARCHIVE to set, or NULL to leave the caller's environment
     * alone. */
    const char *archive;
    /* 1 = ARCHIVE is the embedded one, which is only materialized then;
     * its /proc/self/fd path is filled in once it is. */
    int embedded;
    /* LANG to set when the caller set no locale at all, or NULL. */
    const char *default_locale;
    /* For FELF_DEBUG: the caller's archive, kept when ARCHIVE is NULL,
     * and why the archive was chosen. */
    const char *shown_var, *shown;
    char why[128];
} locale_choice_t;

static int locale_var_is_set(const char *name)
{
    const char *v = getenv(name);
    return v != NULL && v[0] != '\0';
}

/* Choose the bundled glibc's locale environment.  It ignores the host's
 * default locale location (like /etc/ld.so.cache), so the archive must be
 * named; every FELF carries one.
 *
 *   1. The caller's LOCALE_ARCHIVE_2_27 / LOCALE_ARCHIVE: keep it.
 *   2. The caller's locale is unset: the embedded archive, and LANG=C.UTF-8
 *      so that setlocale(LC_ALL, "") finds a UTF-8 locale.  Programs such
 *      as btop refuse to start without one, and a container has no
 *      /etc/locale.conf to name it.
 *   3. The caller's locale is C, POSIX or C.UTF-8: the embedded archive.
 *      Qt then switches to C.UTF-8, which distributions ship as a
 *      directory, not inside their archive; an explicit C is kept.
 *   4. Otherwise the host's archive (the user's locales; the format has
 *      been stable since glibc 2.27), then the embedded one.
 * HAVE_EMBEDDED says whether this FELF carries an archive. */
static locale_choice_t pick_locale(int have_embedded)
{
    locale_choice_t choice = {0};
    static const char *const caller_vars[] = {"LOCALE_ARCHIVE_2_27",
                                              "LOCALE_ARCHIVE"};
    for (size_t i = 0; i < 2; i++)
        if (getenv(caller_vars[i]) != NULL) {
            choice.shown_var = caller_vars[i];
            choice.shown = getenv(caller_vars[i]);
            snprintf(choice.why, sizeof(choice.why), "caller's, kept");
            return choice;
        }
    const char *loc = NULL;
    static const char *const vars[] = {"LC_ALL", "LC_CTYPE", "LANG"};
    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]) && loc == NULL; i++)
        if (locale_var_is_set(vars[i]))
            loc = getenv(vars[i]);
    int c_locale = loc == NULL || strcmp(loc, "C") == 0 ||
                   strcmp(loc, "POSIX") == 0 ||
                   strncmp(loc, "C.", 2) == 0;
    if (loc == NULL && have_embedded) {
        choice.embedded = 1;
        choice.default_locale = "C.UTF-8";
        snprintf(choice.why, sizeof(choice.why),
                 "embedded: the caller set no locale");
        return choice;
    }
    if (c_locale && have_embedded) {
        choice.embedded = 1;
        snprintf(choice.why, sizeof(choice.why),
                 "embedded: the caller's locale is %s", loc ? loc : "C");
        return choice;
    }
    static const char *const host[] = {
        "/usr/lib/locale/locale-archive",
        "/usr/lib64/locale/locale-archive",
        "/run/current-system/sw/lib/locale/locale-archive",
    };
    for (size_t i = 0; i < sizeof(host) / sizeof(host[0]); i++)
        if (access(host[i], R_OK) == 0) {
            choice.archive = host[i];
            snprintf(choice.why, sizeof(choice.why),
                     "host archive, for the caller's locale %s",
                     loc ? loc : "(unset)");
            return choice;
        }
    choice.embedded = have_embedded;
    snprintf(choice.why, sizeof(choice.why), "%s: no host archive for the "
             "caller's locale %s", have_embedded ? "embedded" : "none",
             loc ? loc : "(unset)");
    return choice;
}

static int is_locale_archive(const felf_db_object_t *o)
{
    return strcmp(o->kind, "resource") == 0 &&
           strcmp(o->name, "locale-archive") == 0;
}

static int is_host_lib(const felf_db_t *db, const char *name)
{
    return name != NULL && felf_host_list_match(db->host_libs, name);
}

/* Choose the program to run.  A FELF may hold several programs that share
 * its libraries; each is reached through a symlink named after it, so the
 * basename of argv[0] selects it.  Otherwise the default program runs (the
 * only one, or meta "default_program"). */
static int select_program(const felf_db_t *db, const char *argv0,
                          const felf_db_object_t **out)
{
    *out = NULL;
    const char *invoked = NULL;
    if (argv0 != NULL) {
        invoked = strrchr(argv0, '/');
        invoked = invoked ? invoked + 1 : argv0;
        if (invoked[0] == '-') /* login shells */
            invoked++;
    }
    const felf_db_object_t *by_name = NULL, *fallback = NULL;
    size_t programs = 0;
    for (size_t i = 0; i < db->object_count; i++) {
        const felf_db_object_t *o = &db->objects[i];
        if (strcmp(o->kind, "program") != 0)
            continue;
        programs++;
        if (invoked != NULL && strcmp(o->name, invoked) == 0)
            by_name = o;
        if (db->default_program[0] != '\0'
                ? strcmp(o->name, db->default_program) == 0
                : fallback == NULL)
            fallback = o;
    }
    /* Another name of a multicall program (coreutils' "ls"): an alias of a
     * program object.  argv[0] is passed on unchanged for it to dispatch. */
    for (size_t i = 0; by_name == NULL && invoked != NULL &&
                       i < db->alias_count; i++) {
        if (strcmp(db->aliases[i].alias, invoked) != 0)
            continue;
        const felf_db_object_t *o =
            felf_db_object_by_id(db, db->aliases[i].object_id);
        if (o != NULL && strcmp(o->kind, "program") == 0)
            by_name = o;
    }
    if (programs > 1 && db->default_program[0] == '\0')
        fallback = NULL;
    *out = by_name ? by_name : fallback;
    if (*out == NULL) {
        fail("no program named '%s' in this FELF and no default program",
             invoked ? invoked : "");
        return -1;
    }
    debug("program=%s (invoked as %s, %zu program%s)", (*out)->name,
          invoked ? invoked : "?", programs, programs == 1 ? "" : "s");
    return 0;
}

/* Make DIR/NAME a symlink to TARGET.  An existing DIR/NAME is left alone:
 * it counts as done when it already is this FELF (SELF_ST, the FELF file
 * itself or a link to it), and as skipped otherwise. */
static void install_link(const char *dir, const char *name,
                         const char *target, const struct stat *self_st,
                         size_t *made, size_t *done, size_t *skipped)
{
    char link[PATH_MAX];
    if (snprintf(link, sizeof(link), "%s/%s", dir, name) >=
        (int)sizeof(link)) {
        fail("%s/%s: path too long", dir, name);
        (*skipped)++;
        return;
    }
    struct stat st;
    if (lstat(link, &st) == 0) {
        if (stat(link, &st) == 0 && st.st_dev == self_st->st_dev &&
            st.st_ino == self_st->st_ino) {
            (*done)++;
        } else {
            fail("%s exists and is not this FELF; not replaced", link);
            (*skipped)++;
        }
        return;
    }
    if (symlink(target, link) != 0) {
        fail("cannot create %s: %s", link, strerror(errno));
        (*skipped)++;
        return;
    }
    debug("%s -> %s", link, target);
    (*made)++;
}

/* FELF_INSTALL_LINKS=DIR: instead of running a program, make DIR/<name> a
 * symlink to this FELF for every program and every alias of one (the names
 * of a multicall program), like busybox --install -s.  The link is
 * relative when DIR is the FELF's own directory, so the two move together.
 * Returns the exit status: 1 when a name could not be linked. */
static int install_links(const felf_db_t *db, const char *self,
                         const char *dir)
{
    struct stat self_st, dir_st;
    if (stat(self, &self_st) != 0) {
        fail("cannot stat %s: %s", self, strerror(errno));
        return 1;
    }
    if (stat(dir, &dir_st) != 0 || !S_ISDIR(dir_st.st_mode)) {
        fail("%s=%s is not a directory", FELF_ENV_INSTALL_LINKS, dir);
        return 1;
    }
    const char *target = self;
    char self_dir[PATH_MAX], real_dir[PATH_MAX];
    snprintf(self_dir, sizeof(self_dir), "%s", self);
    char *slash = strrchr(self_dir, '/');
    if (slash != NULL && slash != self_dir) {
        *slash = '\0';
        if (realpath(dir, real_dir) != NULL &&
            strcmp(real_dir, self_dir) == 0)
            target = self + (slash + 1 - self_dir);
    }
    size_t made = 0, done = 0, skipped = 0;
    for (size_t i = 0; i < db->object_count; i++)
        if (strcmp(db->objects[i].kind, "program") == 0)
            install_link(dir, db->objects[i].name, target, &self_st,
                         &made, &done, &skipped);
    for (size_t i = 0; i < db->alias_count; i++) {
        const felf_db_object_t *o =
            felf_db_object_by_id(db, db->aliases[i].object_id);
        if (o != NULL && strcmp(o->kind, "program") == 0)
            install_link(dir, db->aliases[i].alias, target, &self_st,
                         &made, &done, &skipped);
    }
    fprintf(stderr, "felf: %zu link%s created in %s, %zu already there, "
            "%zu not created\n", made, made == 1 ? "" : "s", dir, done,
            skipped);
    return skipped != 0;
}

static int single_kind(const felf_db_t *db, const char *kind,
                       const felf_db_object_t **out)
{
    *out = NULL;
    for (size_t i = 0; i < db->object_count; i++) {
        if (strcmp(db->objects[i].kind, kind) != 0)
            continue;
        if (*out != NULL) {
            fail("more than one %s object", kind);
            return -1;
        }
        *out = &db->objects[i];
    }
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    const char *dbg = getenv(FELF_ENV_DEBUG);
    debug_enabled = dbg != NULL && dbg[0] != '\0' && strcmp(dbg, "0") != 0;

    /* glibc ignores LD_AUDIT and LD_PRELOAD paths in a set-user-ID
     * process, so the program could not find its libraries; and nothing
     * here is written to be driven by another user's environment. */
    if (getuid() != geteuid() || getgid() != getegid()) {
        fail("set-user-ID and set-group-ID FELF files are not supported");
        return 127;
    }

    char self[PATH_MAX];
    if (find_self(self, sizeof(self)) != 0) {
        fail("cannot locate the running executable; /proc is unavailable and "
             "AT_EXECFN is unusable");
        return 127;
    }
    if (self[0] != '/') {
        char resolved[PATH_MAX];
        if (realpath(self, resolved) != NULL &&
            strlen(resolved) < sizeof(self))
            strcpy(self, resolved);
    }
    if (access("/proc/self/fd", X_OK) != 0) {
        fail("/proc/self/fd is required by this FELF handoff version");
        return 127;
    }

    /* /proc/self/exe is the file the kernel is running even when SELF has
     * been replaced or deleted since.  This one descriptor is read here and
     * handed to the loader below, so both see the same bytes. */
    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        fd = open(self, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fail("cannot open %s: %s", self, strerror(errno));
        return 127;
    }
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size <= 0) {
        fail("cannot stat %s: %s", self, strerror(errno));
        close(fd);
        return 127;
    }
    uint64_t file_size = (uint64_t)st.st_size;
    uint8_t *file = mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE,
                         fd, 0);
    if (file == MAP_FAILED) {
        fail("cannot map %s: %s", self, strerror(errno));
        close(fd);
        return 127;
    }

    char err[512] = {0};
    if (felf_outer_elf_validate(file, file_size, err, sizeof(err)) != 0) {
        fail("%s", err);
        return 127;
    }
    felf_footer_t footer;
    if (felf_footer_read(file, file_size, &footer, err, sizeof(err)) != 0) {
        fail("%s", err);
        return 127;
    }

    felf_db_t db;
    if (felf_db_open_memory(file + footer.db_offset, (size_t)footer.db_size,
                            &db, err, sizeof(err)) != 0) {
        fail("%s", err);
        return 127;
    }
    int rc = 127;
    int strict = (footer.flags & FELF_FLAG_STRICT) != 0;
    int lazy = (footer.flags & FELF_FLAG_LAZY) != 0;
    char *preload = NULL;
    char **new_env = NULL;
    int *object_fds = NULL;
    /* Offset of each library mapped from this file, 0 for the others. */
    uint64_t *direct_offsets = NULL;
    felf_fill_job_t *fill_jobs = NULL;
    size_t fill_count = 0, fill_cap = 0;
    /* Writable mappings of the object memfds while they are filled. */
    struct { void *p; size_t n; } *maps = NULL;
    size_t map_count = 0;

    if (felf_db_validate_layout(&db, file_size, footer.payload_offset,
                                footer.payload_size, err, sizeof(err)) != 0) {
        fail("%s", err);
        goto out;
    }
    if (strict != db.strict) {
        fail("footer and database disagree about strict mode");
        goto out;
    }
    if (lazy != db.lazy) {
        fail("footer and database disagree about lazy loading");
        goto out;
    }
    if (db.object_count != footer.object_count) {
        fail("object count mismatch between footer and database");
        goto out;
    }
    const char *links = getenv(FELF_ENV_INSTALL_LINKS);
    if (links != NULL && links[0] != '\0') {
        rc = install_links(&db, self, links);
        goto out;
    }

    const felf_db_object_t *program, *interp, *audit, *runtime;
    if (select_program(&db, argc > 0 ? argv[0] : NULL, &program) != 0 ||
        single_kind(&db, "interp", &interp) != 0 ||
        single_kind(&db, "audit", &audit) != 0 ||
        single_kind(&db, "runtime", &runtime) != 0)
        goto out;
    if (program == NULL || interp == NULL) {
        fail("container does not contain a program and exactly one "
             "interpreter");
        goto out;
    }
    if (lazy && audit == NULL) {
        fail("lazy mode requires an embedded audit module");
        goto out;
    }

    char root[PATH_MAX];
    if (root_from_location(&db, program->name, self, root,
                           sizeof(root)) != 0 &&
        compute_root(self, db.root, root, sizeof(root)) != 0) {
        fail("cannot resolve bundle root '%s' relative to %s", db.root,
             self);
        goto out;
    }
    debug("app=%s root=%s strict=%d lazy=%d objects=%zu", self, root,
          strict, lazy, db.object_count);

    /* Every object descriptor is >= FELF_FD_BASE, and 998 and 999 are
     * fixed.  Containers commonly start with a soft open-file limit of
     * 1024, enough unless many objects become memfds: the soft limit is
     * raised only for a descriptor above it (felf_fd_room, also called by
     * felf_memfd_alloc), and the caller's value is restored before exec, so
     * the application sees the limit it was started with. */

    /* Reserve the audit-index descriptor so F_DUPFD cannot hand it out.
     * The real index replaces this placeholder below. */
    if (audit != NULL) {
        if (felf_fd_room(FELF_AUDIT_INDEX_FD, err, sizeof(err)) != 0) {
            fail("%s", err);
            goto out;
        }
        int reserve = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (reserve >= 0) {
            if (dup2(reserve, FELF_AUDIT_INDEX_FD) >= 0)
                fcntl(FELF_AUDIT_INDEX_FD, F_SETFD, FD_CLOEXEC);
            close(reserve);
        }
    }

    object_fds = malloc((db.object_count ? db.object_count : 1) *
                        sizeof(*object_fds));
    if (object_fds == NULL) {
        fail("out of memory");
        goto out;
    }
    maps = malloc((db.object_count ? db.object_count : 1) * sizeof(*maps));
    direct_offsets = calloc(db.object_count ? db.object_count : 1,
                            sizeof(*direct_offsets));
    if (maps == NULL || direct_offsets == NULL) {
        fail("out of memory");
        goto out;
    }
    uint64_t page_size = (uint64_t)sysconf(_SC_PAGESIZE);
    size_t direct_count = 0;
    int program_fd = -1, interp_fd = -1, audit_fd = -1, runtime_fd = -1;
    int locale_fd = -1;
    size_t preload_len = 0, preload_cap = 0;

    /* The locale is chosen first: the embedded archive (about 3 MB
     * decompressed) is only materialized when it is used. */
    int have_locale = 0;
    for (size_t i = 0; i < db.object_count; i++)
        if (is_locale_archive(&db.objects[i]))
            have_locale = 1;
    locale_choice_t locale = pick_locale(have_locale);

    for (size_t i = 0; i < db.object_count; i++) {
        const felf_db_object_t *o = &db.objects[i];
        object_fds[i] = -1;
        int is_locale = is_locale_archive(o);
        if (!is_executable_kind(o->kind) && !(is_locale && locale.embedded))
            continue;
        /* Only the selected program is materialized. */
        if (strcmp(o->kind, "program") == 0 && o != program)
            continue;
        /* FELF's loader maps an uncompressed, page-aligned library straight
         * from this file (FELF_MAP_PREFIX): no memfd. */
        if (db.direct_map && audit != NULL && is_library_kind(o->kind) &&
            !o->compressed && o->offset % page_size == 0) {
            direct_offsets[i] = o->offset;
            direct_count++;
            continue;
        }
        if (o->offset > file_size ||
            o->stored_size > file_size - o->offset) {
            fail("object %s is outside the container", o->name);
            goto out;
        }
        const uint8_t *data = file + o->offset;
        /* Every object descriptor is >= FELF_FD_BASE (see protocol).  The
         * memfds are created here, in order; their contents are copied or
         * decompressed below, in parallel. */
        int objfd = felf_memfd_alloc(o->name, o->size, FELF_FD_BASE, err,
                                     sizeof(err));
        if (objfd < 0) {
            fail("%s: %s", o->name, err);
            goto out;
        }
        void *map = mmap(NULL, (size_t)o->size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, objfd, 0);
        if (map == MAP_FAILED) {
            fail("%s: mapping memfd failed: %s", o->name, strerror(errno));
            goto out;
        }
        maps[map_count].p = map;
        maps[map_count++].n = (size_t)o->size;
        if (felf_fill_jobs_add(o, data, map, &fill_jobs, &fill_count,
                               &fill_cap, err, sizeof(err)) != 0) {
            fail("%s", err);
            goto out;
        }
        object_fds[i] = objfd;
        if (is_locale) {
            locale_fd = objfd;
            continue;
        }
        if (o == program)
            program_fd = objfd;
        else if (o == interp)
            interp_fd = objfd;
        else if (o == audit)
            audit_fd = objfd;
        else if (o == runtime)
            runtime_fd = objfd;
        /* Host-preferred objects are never preloaded: the audit module
         * resolves them on the host and falls back to this memfd. */
        else if (!lazy && is_library_kind(o->kind) &&
                 !(audit != NULL && (is_host_lib(&db, o->name) ||
                                     is_host_lib(&db, o->soname))) &&
                 append_preload(&preload, &preload_len, &preload_cap,
                                objfd) != 0) {
            fail("out of memory building LD_PRELOAD");
            goto out;
        }
    }

    if (felf_fill_all(fill_jobs, fill_count, err, sizeof(err)) != 0) {
        fail("%s", err);
        goto out;
    }
    if (direct_count > 0) {
        /* The loader opens /proc/self/fd/FELF_CONTAINER_FD for each mapped
         * library; the runtime shim makes it close-on-exec.  dup2() leaves
         * the copy open across exec; FD itself is close-on-exec. */
        if (felf_fd_room(FELF_CONTAINER_FD, err, sizeof(err)) != 0) {
            fail("%s", err);
            goto out;
        }
        if (dup2(fd, FELF_CONTAINER_FD) < 0) {
            fail("cannot keep %s open for its loader: %s", self,
                 strerror(errno));
            goto out;
        }
        debug("%zu libraries are mapped from the FELF file", direct_count);
    }
    for (size_t i = 0; i < map_count; i++)
        munmap(maps[i].p, maps[i].n);
    map_count = 0;

    if (runtime_fd >= 0) {
        /* The runtime shim must precede libc in LD_PRELOAD so its
         * interposed symbols win. */
        char *with_shim;
        if (asprintf(&with_shim, "/proc/self/fd/%d%s%s", runtime_fd,
                     preload ? ":" : "", preload ? preload : "") < 0) {
            fail("out of memory adding the runtime shim to LD_PRELOAD");
            goto out;
        }
        free(preload);
        preload = with_shim;
    }

    if (felf_patch_program_interp(program_fd, interp_fd,
                                  err, sizeof(err)) != 0) {
        fail("%s", err);
        goto out;
    }
    char audit_path[64] = {0};
    if (audit != NULL) {
        snprintf(audit_path, sizeof(audit_path), "/proc/self/fd/%d",
                 audit_fd);
        if (felf_build_audit_index(&db, object_fds, direct_offsets,
                                   strict, root,
                                   debug_enabled,
                                   getenv("LD_LIBRARY_PATH"),
                                   err, sizeof(err)) != 0) {
            fail("%s", err);
            goto out;
        }
    }
    debug("LD_PRELOAD=%s", preload ? preload : "");
    debug("LD_AUDIT=%s", audit_path);

    char locale_buf[64];
    if (locale.embedded && locale_fd >= 0) {
        snprintf(locale_buf, sizeof(locale_buf), "/proc/self/fd/%d",
                 locale_fd);
        locale.archive = locale_buf;
    }
    if (locale.archive != NULL || locale.shown == NULL)
        debug("LOCALE_ARCHIVE=%s (%s)",
              locale.archive ? locale.archive : "unset", locale.why);
    else
        debug("%s=%s (%s)", locale.shown_var, locale.shown, locale.why);
    if (locale.default_locale != NULL)
        debug("LANG=%s (no caller locale)", locale.default_locale);
    if (felf_build_environment(envp, &db, preload, audit_path, self, root,
                               strict, locale.archive,
                               locale.default_locale, &new_env, err,
                               sizeof(err)) != 0) {
        fail("%s", err);
        goto out;
    }
    if (debug_enabled) {
        for (size_t i = 0; i < db.env_count; i++)
            for (char **e = new_env; *e != NULL; e++) {
                size_t n = strlen(db.env[i].name);
                if (strncmp(*e, db.env[i].name, n) == 0 && (*e)[n] == '=')
                    debug("env %s", *e);
            }
    }

    /* The database and container mapping are not needed after this point.
     * The memfds contain the executable bytes and remain open for the
     * kernel and the private loader. */
    felf_db_close(&db);

    unsigned long long nofile_from, nofile_to;
    if (felf_restore_fd_limit(&nofile_from, &nofile_to))
        debug("raised open-file soft limit %llu -> %llu, restored for exec",
              nofile_from, nofile_to);
    syscall(SYS_execveat, program_fd, "", argv, new_env, AT_EMPTY_PATH);
    int saved = errno;
    /* Fallback for kernels without execveat: /proc/self/fd/<program> is a
     * normal path and preserves the patched interpreter path. */
    if (saved == ENOSYS) {
        char program_path[64];
        snprintf(program_path, sizeof(program_path), "/proc/self/fd/%d",
                 program_fd);
        execve(program_path, argv, new_env);
        saved = errno;
    }
    fail("cannot execute embedded program: %s%s", strerror(saved),
         saved == EACCES ? " (does this system forbid running programs "
                           "from memory? sysctl vm.memfd_noexec, SELinux)"
                         : "");
    return 127;

out:
    close(fd);
    free(preload);
    free(object_fds);
    free(direct_offsets);
    free(fill_jobs);
    for (size_t i = 0; i < map_count; i++)
        munmap(maps[i].p, maps[i].n);
    free(maps);
    felf_free_environment(new_env);
    felf_db_close(&db);
    return rc;
}
