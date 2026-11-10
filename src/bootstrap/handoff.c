#define _GNU_SOURCE
#include "handoff.h"
#include "felf_loader_protocol.h"
#include "felf_format.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <zstd.h>

#ifndef SYS_memfd_create
#error "SYS_memfd_create is not available on this target"
#endif

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#ifndef MFD_EXEC
#define MFD_EXEC 0x0010U
#endif

/* The caller's open-file limit, saved the first time it is raised. */
static struct rlimit caller_nofile;
static rlim_t raised_nofile;

int felf_fd_room(int fd, char *err, size_t errlen)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur == RLIM_INFINITY ||
        (rlim_t)fd < rl.rlim_cur)
        return 0;
    rlim_t need = (rlim_t)fd + 1;
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < need) {
        felf_set_error(err, errlen,
                       "open-file limit too low: descriptor %d is needed, "
                       "hard limit is %llu (raise ulimit -Hn)", fd,
                       (unsigned long long)rl.rlim_max);
        return -1;
    }
    if (raised_nofile == 0)
        caller_nofile = rl;
    rl.rlim_cur = need;
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
        felf_set_error(err, errlen,
                       "cannot raise the open-file limit to %llu: %s",
                       (unsigned long long)need, strerror(errno));
        return -1;
    }
    raised_nofile = need;
    return 0;
}

int felf_restore_fd_limit(unsigned long long *from, unsigned long long *to)
{
    if (raised_nofile == 0)
        return 0;
    /* Descriptors already open stay valid above the restored limit. */
    setrlimit(RLIMIT_NOFILE, &caller_nofile);
    *from = (unsigned long long)caller_nofile.rlim_cur;
    *to = (unsigned long long)raised_nofile;
    return 1;
}

int felf_memfd_alloc(const char *name, uint64_t size, int min_fd,
                     char *err, size_t errlen)
{
    /* The name only shows up in /proc/<pid>/maps and readlink of the
     * descriptor ("/memfd:NAME (deleted)"); it makes debugging much easier.
     * The kernel limit is 249 bytes. */
    char label[200];
    snprintf(label, sizeof(label), "felf:%s", name ? name : "object");
    /* Since Linux 6.3 a memfd is only executable under vm.memfd_noexec=1
     * when MFD_EXEC asks for it; older kernels reject the flag (EINVAL).
     * vm.memfd_noexec=2 refuses it (EACCES): nothing can run from memory
     * there. */
    int fd = (int)syscall(SYS_memfd_create, label, MFD_CLOEXEC | MFD_EXEC);
    if (fd < 0 && errno == EINVAL)
        fd = (int)syscall(SYS_memfd_create, label, MFD_CLOEXEC);
    if (fd < 0) {
        felf_set_error(err, errlen, "memfd_create failed: %s%s",
                       strerror(errno), errno == EACCES
                           ? " (this system forbids executable memfds: "
                             "sysctl vm.memfd_noexec=2)" : "");
        return -1;
    }
    if (ftruncate(fd, (off_t)size) != 0) {
        felf_set_error(err, errlen, "sizing memfd failed: %s",
                       strerror(errno));
        close(fd);
        return -1;
    }
    /* F_DUPFD fails with EINVAL when MIN_FD is not below the soft limit,
     * and with EMFILE when nothing from MIN_FD up is free below it: only
     * then is the limit raised, one descriptor at a time. */
    int high;
    while ((high = fcntl(fd, F_DUPFD, min_fd)) < 0 &&
           (errno == EINVAL || errno == EMFILE)) {
        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) != 0 ||
            rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur > INT_MAX)
            break;
        int next = (rlim_t)min_fd > rl.rlim_cur ? min_fd : (int)rl.rlim_cur;
        if (felf_fd_room(next, err, errlen) != 0) {
            close(fd);
            return -1;
        }
        if (raised_nofile != (rlim_t)next + 1)
            break; /* not raised: report the F_DUPFD error */
    }
    if (high < 0)
        felf_set_error(err, errlen, "cannot duplicate memfd: %s",
                       strerror(errno));
    close(fd);
    return high;
}

int felf_memfd_from_bytes(const char *name, const void *data, size_t size,
                          int min_fd, char *err, size_t errlen)
{
    int fd = felf_memfd_alloc(name, size, min_fd, err, errlen);
    if (fd < 0)
        return -1;
    /* pwrite() leaves the file offset at 0 for the reader. */
    const uint8_t *p = data;
    size_t done = 0;
    while (done < size) {
        ssize_t n = pwrite(fd, p + done, size - done, (off_t)done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            felf_set_error(err, errlen, "writing memfd failed: %s",
                           n < 0 ? strerror(errno) : "short write");
            close(fd);
            return -1;
        }
        done += (size_t)n;
    }
    return fd;
}

int felf_fill_jobs_add(const felf_db_object_t *o, const uint8_t *stored,
                       uint8_t *dst, felf_fill_job_t **jobs, size_t *count,
                       size_t *cap, char *err, size_t errlen)
{
    size_t size = (size_t)o->size;
    size_t stored_size = (size_t)o->stored_size;
    size_t off = 0, doff = 0;
    while (doff < size) {
        felf_fill_job_t job = {.name = o->name, .compressed = o->compressed,
                               .src = stored + off, .dst = dst + doff};
        if (o->compressed) {
            if (off >= stored_size) {
                felf_set_error(err, errlen,
                               "%s: truncated zstd data", o->name);
                return -1;
            }
            size_t fsz = ZSTD_findFrameCompressedSize(stored + off,
                                                      stored_size - off);
            if (ZSTD_isError(fsz)) {
                felf_set_error(err, errlen, "%s: zstd: %s", o->name,
                               ZSTD_getErrorName(fsz));
                return -1;
            }
            unsigned long long dsz =
                ZSTD_getFrameContentSize(stored + off, fsz);
            if (dsz == ZSTD_CONTENTSIZE_UNKNOWN ||
                dsz == ZSTD_CONTENTSIZE_ERROR || dsz > size - doff) {
                felf_set_error(err, errlen, "%s: invalid zstd frame", o->name);
                return -1;
            }
            job.src_len = fsz;
            job.dst_len = (size_t)dsz;
        } else {
            size_t n = size - doff < FELF_ZSTD_CHUNK ? size - doff
                                                     : FELF_ZSTD_CHUNK;
            job.src_len = job.dst_len = n;
        }
        if (*count == *cap) {
            size_t newcap = *cap ? *cap * 2 : 256;
            felf_fill_job_t *p = realloc(*jobs, newcap * sizeof(*p));
            if (p == NULL) {
                felf_set_error(err, errlen, "out of memory");
                return -1;
            }
            *jobs = p;
            *cap = newcap;
        }
        (*jobs)[(*count)++] = job;
        off += job.src_len;
        doff += job.dst_len;
        if (job.dst_len == 0) {
            felf_set_error(err, errlen, "%s: empty zstd frame", o->name);
            return -1;
        }
    }
    if ((o->compressed && off != stored_size) ||
        (!o->compressed && stored_size != size)) {
        felf_set_error(err, errlen, "%s: stored size does not match its data",
                       o->name);
        return -1;
    }
    return 0;
}

static int run_fill_job(const felf_fill_job_t *job, char *err, size_t errlen)
{
    if (!job->compressed) {
        memcpy(job->dst, job->src, job->dst_len);
        return 0;
    }
    size_t n = ZSTD_decompress(job->dst, job->dst_len, job->src,
                               job->src_len);
    if (ZSTD_isError(n) || n != job->dst_len) {
        felf_set_error(err, errlen, "%s: zstd: %s", job->name,
                       ZSTD_isError(n) ? ZSTD_getErrorName(n) : "short frame");
        return -1;
    }
    return 0;
}

typedef struct {
    const felf_fill_job_t *jobs;
    size_t count;
    size_t next;
    int failed;
    char err[256];
    pthread_mutex_t lock;
} fill_state_t;

static void *fill_worker(void *arg)
{
    fill_state_t *st = arg;
    for (;;) {
        pthread_mutex_lock(&st->lock);
        size_t i = st->next++;
        int stop = st->failed || i >= st->count;
        pthread_mutex_unlock(&st->lock);
        if (stop)
            return NULL;
        char local[256] = {0};
        if (run_fill_job(&st->jobs[i], local, sizeof(local)) != 0) {
            pthread_mutex_lock(&st->lock);
            if (!st->failed)
                snprintf(st->err, sizeof(st->err), "%s", local);
            st->failed = 1;
            pthread_mutex_unlock(&st->lock);
            return NULL;
        }
    }
}

int felf_fill_all(const felf_fill_job_t *jobs, size_t count, char *err,
                  size_t errlen)
{
    fill_state_t st = {.jobs = jobs, .count = count};
    pthread_mutex_init(&st.lock, NULL);
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    size_t n = cpus > 0 ? (size_t)cpus : 1;
    if (n > 16)
        n = 16;
    if (n > count)
        n = count ? count : 1;
    pthread_t threads[16];
    size_t started = 0;
    /* The calling thread is one of the workers. */
    for (; started + 1 < n; started++)
        if (pthread_create(&threads[started], NULL, fill_worker, &st) != 0)
            break;
    fill_worker(&st);
    for (size_t i = 0; i < started; i++)
        pthread_join(threads[i], NULL);
    pthread_mutex_destroy(&st.lock);
    if (st.failed) {
        felf_set_error(err, errlen, "%s", st.err);
        return -1;
    }
    return 0;
}

int felf_patch_program_interp(int program_fd, int interp_fd,
                              char *err, size_t errlen)
{
    Elf64_Ehdr eh;
    if (pread(program_fd, &eh, sizeof(eh), 0) != (ssize_t)sizeof(eh)) {
        felf_set_error(err, errlen, "cannot read program header");
        return -1;
    }
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB) {
        felf_set_error(err, errlen,
                       "embedded program is not ELF64 little-endian");
        return -1;
    }
    if (eh.e_phoff > (uint64_t)INT64_MAX ||
        eh.e_phnum == 0 || eh.e_phnum > 4096 ||
        eh.e_phentsize != sizeof(Elf64_Phdr)) {
        felf_set_error(err, errlen,
                       "embedded program has invalid program headers");
        return -1;
    }
    for (unsigned i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr ph;
        off_t off = (off_t)eh.e_phoff + (off_t)i * (off_t)sizeof(ph);
        if (pread(program_fd, &ph, sizeof(ph), off) != (ssize_t)sizeof(ph)) {
            felf_set_error(err, errlen,
                           "cannot read embedded program headers");
            return -1;
        }
        if (ph.p_type != PT_INTERP)
            continue;
        char path[64];
        int n = snprintf(path, sizeof(path), "/proc/self/fd/%d", interp_fd);
        if (n < 0 || (size_t)n + 1 > ph.p_filesz) {
            felf_set_error(err, errlen,
                           "PT_INTERP slot is too small for the FELF interpreter "
                           "path (%d bytes available)", (int)ph.p_filesz);
            return -1;
        }
        uint8_t *buf = calloc(1, (size_t)ph.p_filesz);
        if (buf == NULL) {
            felf_set_error(err, errlen, "out of memory patching PT_INTERP");
            return -1;
        }
        memcpy(buf, path, (size_t)n);
        ssize_t written = pwrite(program_fd, buf, (size_t)ph.p_filesz,
                                 (off_t)ph.p_offset);
        free(buf);
        if (written != (ssize_t)ph.p_filesz) {
            felf_set_error(err, errlen,
                           "cannot patch embedded program PT_INTERP");
            return -1;
        }
        return 0;
    }
    felf_set_error(err, errlen, "embedded program has no PT_INTERP");
    return -1;
}

static int is_ld_variable(const char *entry)
{
    return entry[0] == 'L' && entry[1] == 'D' && entry[2] == '_';
}

/* Variables owned by the handoff protocol; never inherited from the caller. */
static int is_felf_variable(const char *entry)
{
    static const char *const names[] = {
        FELF_ENV_HANDOFF_VERSION "=",
        FELF_ENV_STRICT "=",
        FELF_ENV_APP_PATH "=",
        FELF_ENV_ROOT "=",
        FELF_ENV_LOCALE_SET "=",
        FELF_ENV_ORIG_PREFIX,
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t n = strlen(names[i]);
        if (strncmp(entry, names[i], n) == 0)
            return 1;
    }
    return 0;
}

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} env_list_t;

static int env_push(env_list_t *l, char *owned)
{
    if (owned == NULL)
        return -1;
    if (l->count + 1 >= l->cap) {
        size_t newcap = l->cap ? l->cap * 2 : 64;
        char **p = realloc(l->items, newcap * sizeof(*p));
        if (p == NULL) {
            free(owned);
            return -1;
        }
        l->items = p;
        l->cap = newcap;
    }
    l->items[l->count++] = owned;
    l->items[l->count] = NULL;
    return 0;
}

static char *join3(const char *a, const char *b, const char *c)
{
    size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
    char *s = malloc(la + lb + lc + 1);
    if (s == NULL)
        return NULL;
    memcpy(s, a, la);
    memcpy(s + la, b, lb);
    memcpy(s + la + lb, c, lc + 1);
    return s;
}

/* Remove NAME from the list, if present. */
static void env_remove(env_list_t *l, const char *name)
{
    size_t n = strlen(name);
    size_t out = 0;
    for (size_t i = 0; i < l->count; i++) {
        if (strncmp(l->items[i], name, n) == 0 && l->items[i][n] == '=') {
            free(l->items[i]);
            continue;
        }
        l->items[out++] = l->items[i];
    }
    l->count = out;
    if (l->items != NULL)
        l->items[out] = NULL;
}

static const char *lookup_env(char **envp, const char *name, size_t n)
{
    for (char **e = envp; *e != NULL; e++)
        if (strncmp(*e, name, n) == 0 && (*e)[n] == '=')
            return *e + n + 1;
    return NULL;
}

/* Is NAME set to a non-empty value in the built environment?  An empty
 * locale variable means "unset" to glibc, so an empty value does not count
 * as the caller having chosen a locale. */
static int env_has_value(const env_list_t *l, const char *name)
{
    size_t n = strlen(name);
    for (size_t i = 0; i < l->count; i++)
        if (strncmp(l->items[i], name, n) == 0 && l->items[i][n] == '=' &&
            l->items[i][n + 1] != '\0')
            return 1;
    return 0;
}

/* Expand $FELF_ROOT, $FELF_APP, $NAME (from the caller's environment) and
 * $$.  Unknown variables expand to the empty string. */
static char *expand_value(const char *value, char **envp, const char *app,
                          const char *root)
{
    size_t cap = strlen(value) + 256, len = 0;
    char *out = malloc(cap);
    if (out == NULL)
        return NULL;
    for (const char *p = value; *p != '\0';) {
        const char *rep = NULL;
        size_t rep_len = 0, consumed = 1;
        char single = *p;
        if (p[0] == '$' && p[1] == '$') {
            single = '$';
            consumed = 2;
        } else if (p[0] == '$' && (p[1] == '_' ||
                                   (p[1] >= 'A' && p[1] <= 'Z') ||
                                   (p[1] >= 'a' && p[1] <= 'z'))) {
            size_t n = 1;
            while (p[1 + n] == '_' || (p[1 + n] >= 'A' && p[1 + n] <= 'Z') ||
                   (p[1 + n] >= 'a' && p[1 + n] <= 'z') ||
                   (p[1 + n] >= '0' && p[1 + n] <= '9'))
                n++;
            if (n == 9 && strncmp(p + 1, "FELF_ROOT", 9) == 0)
                rep = root;
            else if (n == 8 && strncmp(p + 1, "FELF_APP", 8) == 0)
                rep = app;
            else
                rep = lookup_env(envp, p + 1, n);
            if (rep == NULL)
                rep = "";
            rep_len = strlen(rep);
            consumed = 1 + n;
        }
        size_t need = len + (rep ? rep_len : 1) + 1;
        if (need > cap) {
            while (cap < need)
                cap *= 2;
            char *q = realloc(out, cap);
            if (q == NULL) {
                free(out);
                return NULL;
            }
            out = q;
        }
        if (rep != NULL) {
            memcpy(out + len, rep, rep_len);
            len += rep_len;
        } else {
            out[len++] = single;
        }
        p += consumed;
    }
    out[len] = '\0';
    return out;
}

int felf_build_environment(char **envp, const felf_db_t *db,
                           const char *preload_paths, const char *audit_path,
                           const char *app_path, const char *root,
                           int strict, const char *locale_archive,
                           const char *default_locale, char ***out_env,
                           char *err, size_t errlen)
{
    env_list_t l = {0};
    /* 1. The caller's environment.  LD_* variables could redirect the
     *    private loader; they are removed and preserved as FELF_ORIG_*, so
     *    the runtime shim can restore them for child processes. */
    for (char **e = envp; *e != NULL; e++) {
        if (is_felf_variable(*e))
            continue;
        int rc = is_ld_variable(*e)
                     ? env_push(&l, join3(FELF_ENV_ORIG_PREFIX, *e, ""))
                     : env_push(&l, join3(*e, "", ""));
        if (rc != 0)
            goto oom;
    }
    /* 2. Application environment from the database; overrides the caller. */
    for (size_t i = 0; db != NULL && i < db->env_count; i++) {
        char *value = expand_value(db->env[i].value, envp, app_path, root);
        if (value == NULL)
            goto oom;
        env_remove(&l, db->env[i].name);
        char *entry = join3(db->env[i].name, "=", value);
        free(value);
        if (env_push(&l, entry) != 0)
            goto oom;
    }
    /* 3. Handoff variables. */
    if (preload_paths != NULL && preload_paths[0] != '\0' &&
        env_push(&l, join3("LD_PRELOAD=", preload_paths, "")) != 0)
        goto oom;
    /* The audit module is used in every mode that has one.  In permissive
     * mode it still redirects every known bundled object to its memfd; it
     * only lets unknown names fall through to the host. */
    if (audit_path != NULL && audit_path[0] != '\0' &&
        env_push(&l, join3("LD_AUDIT=", audit_path, "")) != 0)
        goto oom;
    if (app_path != NULL && app_path[0] != '\0' &&
        env_push(&l, join3(FELF_ENV_APP_PATH "=", app_path, "")) != 0)
        goto oom;
    if (root != NULL && root[0] != '\0' &&
        env_push(&l, join3(FELF_ENV_ROOT "=", root, "")) != 0)
        goto oom;
    /* 4. glibc locale data for the bundled glibc, which does not read the
     *    host's default location.  Only when the caller set none.  A
     *    caller with no locale at all also gets LANG=C.UTF-8, so that
     *    setlocale(LC_ALL, "") yields a UTF-8 locale (btop and Qt need
     *    this; a bare container has no locale configuration). */
    if (locale_archive != NULL &&
        (env_push(&l, join3(FELF_ENV_LOCALE_ARCHIVE "=", locale_archive,
                            "")) != 0 ||
         env_push(&l, join3(FELF_ENV_LOCALE_SET "=1", "", "")) != 0))
        goto oom;
    if (default_locale != NULL && !env_has_value(&l, "LC_ALL") &&
        !env_has_value(&l, "LC_CTYPE") && !env_has_value(&l, "LANG") &&
        env_push(&l, join3("LANG=", default_locale, "")) != 0)
        goto oom;
    if (env_push(&l, join3(FELF_ENV_HANDOFF_VERSION "=2", "", "")) != 0 ||
        env_push(&l, join3(FELF_ENV_STRICT "=", strict ? "1" : "0", "")) != 0)
        goto oom;
    *out_env = l.items;
    return 0;

oom:
    felf_free_environment(l.items);
    felf_set_error(err, errlen, "out of memory building environment");
    return -1;
}

void felf_free_environment(char **env)
{
    if (env == NULL)
        return;
    for (char **e = env; *e != NULL; e++)
        free(*e);
    free(env);
}
