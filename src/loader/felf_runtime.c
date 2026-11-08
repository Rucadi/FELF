/*
 * libfelf_runtime.so — small runtime shim for FELF applications.
 *
 * The bootstrap puts this object first in LD_PRELOAD.  It does two things:
 *
 * 1. Executable path.  The program runs from an anonymous memfd, so the
 *    kernel reports /proc/self/exe as "/memfd:... (deleted)".  Toolkits use
 *    that link to find resources next to the executable (Qt's
 *    applicationDirPath(), qt.conf, KDE/Krita's installation prefix).  The
 *    shim reports the path of the FELF file that was actually executed
 *    instead.  Only /proc/{self,thread-self,<pid>}/exe is affected, and only
 *    while the real link points at a memfd.
 *
 *    The same applies to getauxval(AT_EXECFN): for the bootstrap's
 *    execveat() the kernel sets it to "/dev/fd/<N>", and Qt 6 prefers an
 *    absolute AT_EXECFN over /proc/self/exe.
 * 2. Child processes.  LD_PRELOAD/LD_AUDIT point at /proc/self/fd paths
 *    that only make sense inside this process; a child (a host helper, a
 *    browser, ffmpeg...) would otherwise load the bundled glibc with the
 *    host's loader.  The shim removes the handoff variables, restores the
 *    caller's original LD_* values (FELF_ORIG_*), and marks the bundled
 *    object descriptors close-on-exec.
 *
 * It is a libc-level interposer: direct syscalls bypass it.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "felf_loader_protocol.h"

#ifndef CLOSE_RANGE_CLOEXEC
#define CLOSE_RANGE_CLOEXEC (1U << 2)
#endif
#ifndef SYS_close_range
#define SYS_close_range 436
#endif

extern void __chk_fail(void) __attribute__((noreturn));

static char app_path[PATH_MAX];
static size_t app_len;
static int state;      /* 0 = uninitialized, 1 = initialized */
static int exe_is_memfd = -1;
static int debug_on;

static void debug(const char *fmt, ...)
{
    if (!debug_on)
        return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
        (void)!write(2, buf, (size_t)n < sizeof(buf) ? (size_t)n
                                                      : sizeof(buf) - 1);
}

/* The application may define its own getenv/setenv/unsetenv (bash does, for
 * its shell variables), and its definitions win over libc's here too.
 * Before main() they need not touch environ, so the shim edits environ
 * itself. */
static int env_is(const char *entry, const char *name)
{
    size_t n = strlen(name);
    return strncmp(entry, name, n) == 0 && entry[n] == '=';
}

static const char *env_get(const char *name)
{
    for (char **e = environ; e != NULL && *e != NULL; e++)
        if (env_is(*e, name))
            return *e + strlen(name) + 1;
    return NULL;
}

static void env_remove(const char *name)
{
    if (environ == NULL)
        return;
    char **out = environ;
    for (char **e = environ; *e != NULL; e++)
        if (!env_is(*e, name))
            *out++ = *e;
    *out = NULL;
}

static void init_once(void)
{
    if (state)
        return;
    state = 1;
    const char *dbg = env_get(FELF_ENV_DEBUG);
    debug_on = dbg != NULL && dbg[0] != '\0' && strcmp(dbg, "0") != 0;
    const char *app = env_get(FELF_ENV_APP_PATH);
    if (app != NULL && app[0] == '/' && strlen(app) < sizeof(app_path)) {
        memcpy(app_path, app, strlen(app) + 1);
        app_len = strlen(app_path);
    }
}

static int real_exe_is_memfd(void)
{
    if (exe_is_memfd >= 0)
        return exe_is_memfd;
    char buf[64];
    long n = syscall(SYS_readlinkat, AT_FDCWD, "/proc/self/exe", buf,
                     sizeof(buf) - 1);
    exe_is_memfd = n > 7 && memcmp(buf, "/memfd:", 7) == 0;
    return exe_is_memfd;
}

/* Is PATH this process's /proc/.../exe link? */
static int is_self_exe(const char *path)
{
    if (path == NULL || strncmp(path, "/proc/", 6) != 0)
        return 0;
    const char *p = path + 6;
    if (strncmp(p, "self/", 5) == 0)
        p += 5;
    else if (strncmp(p, "thread-self/", 12) == 0)
        p += 12;
    else {
        char pid[32];
        int n = snprintf(pid, sizeof(pid), "%d/", (int)getpid());
        if (n <= 0 || strncmp(p, pid, (size_t)n) != 0)
            return 0;
        p += n;
    }
    return strcmp(p, "exe") == 0;
}

static int should_fake(const char *path)
{
    init_once();
    return app_len != 0 && is_self_exe(path) && real_exe_is_memfd();
}

static ssize_t fake_readlink(char *buf, size_t bufsiz)
{
    size_t n = app_len < bufsiz ? app_len : bufsiz;
    memcpy(buf, app_path, n);
    return (ssize_t)n;
}

__attribute__((constructor)) static void felf_runtime_init(void)
{
    init_once();

    /* Remove the handoff variables and restore the caller's LD_*. */
    env_remove("LD_PRELOAD");
    env_remove("LD_AUDIT");
    env_remove(FELF_ENV_APP_PATH);
    env_remove(FELF_ENV_HANDOFF_VERSION);
    env_remove(FELF_ENV_STRICT);
    if (env_get(FELF_ENV_LOCALE_SET) != NULL) {
        /* The bootstrap chose it, and it names a descriptor that children
         * do not inherit, so it is removed for them.  glibc reads it when
         * it first opens the archive, and keeps the archive mapped: open it
         * now, while the variable is still set. */
        locale_t l = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
        if (l != (locale_t)0)
            freelocale(l);
        env_remove(FELF_ENV_LOCALE_ARCHIVE);
        env_remove("LOCALE_ARCHIVE_2_27");
        env_remove(FELF_ENV_LOCALE_SET);
    }
    /* FELF_ORIG_NAME=VALUE becomes NAME=VALUE again: the entry is the same
     * string without the prefix. */
    size_t prefix = strlen(FELF_ENV_ORIG_PREFIX);
    for (char **e = environ; e != NULL && *e != NULL; e++)
        if (strncmp(*e, FELF_ENV_ORIG_PREFIX, prefix) == 0 &&
            (*e)[prefix] != '=' && strchr(*e + prefix, '=') != NULL)
            *e += prefix;

    /* Children must not inherit the bundled objects or the audit index. */
    if (syscall(SYS_close_range, (unsigned)FELF_AUDIT_INDEX_FD, ~0U,
                CLOSE_RANGE_CLOEXEC) != 0) {
        struct rlimit rl;
        int limit = 65536;
        if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < 65536)
            limit = (int)rl.rlim_cur;
        for (int fd = FELF_AUDIT_INDEX_FD; fd < limit; fd++) {
            int flags = fcntl(fd, F_GETFD);
            if (flags >= 0)
                fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }
    debug("felf[runtime]: app=%s exe-is-memfd=%d\n",
          app_len ? app_path : "(none)", real_exe_is_memfd());
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    if (should_fake(path))
        return fake_readlink(buf, bufsiz);
    return (ssize_t)syscall(SYS_readlinkat, AT_FDCWD, path, buf, bufsiz);
}

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz)
{
    if (path[0] == '/' && should_fake(path))
        return fake_readlink(buf, bufsiz);
    return (ssize_t)syscall(SYS_readlinkat, dirfd, path, buf, bufsiz);
}

ssize_t __readlink_chk(const char *path, char *buf, size_t len,
                       size_t buflen)
{
    if (len > buflen)
        __chk_fail();
    return readlink(path, buf, len);
}

ssize_t __readlinkat_chk(int dirfd, const char *path, char *buf, size_t len,
                         size_t buflen)
{
    if (len > buflen)
        __chk_fail();
    return readlinkat(dirfd, path, buf, len);
}

static char *(*next_realpath)(const char *, char *);

char *realpath(const char *path, char *resolved)
{
    if (next_realpath == NULL) {
        /* Plain dlsym() can return the GLIBC_2.2.5 compatibility version,
         * which rejects a NULL buffer.  Ask for the current one. */
        void *sym = dlvsym(RTLD_NEXT, "realpath", "GLIBC_2.3");
        if (sym == NULL)
            sym = dlsym(RTLD_NEXT, "realpath");
        memcpy(&next_realpath, &sym, sizeof(sym));
    }
    if (next_realpath == NULL) {
        errno = ENOSYS;
        return NULL;
    }
    if (should_fake(path))
        return next_realpath(app_path, resolved);
    return next_realpath(path, resolved);
}

char *canonicalize_file_name(const char *path)
{
    return realpath(path, NULL);
}

char *__realpath_chk(const char *path, char *resolved, size_t resolvedlen)
{
    if (resolvedlen < PATH_MAX)
        __chk_fail();
    return realpath(path, resolved);
}

static unsigned long (*next_getauxval)(unsigned long);

unsigned long getauxval(unsigned long type)
{
    if (next_getauxval == NULL) {
        void *sym = dlsym(RTLD_NEXT, "getauxval");
        memcpy(&next_getauxval, &sym, sizeof(sym));
    }
    if (type == AT_EXECFN) {
        init_once();
        if (app_len != 0 && real_exe_is_memfd())
            return (unsigned long)app_path;
    }
    if (next_getauxval == NULL) {
        errno = ENOENT;
        return 0;
    }
    return next_getauxval(type);
}

unsigned long __getauxval(unsigned long type)
{
    return getauxval(type);
}
