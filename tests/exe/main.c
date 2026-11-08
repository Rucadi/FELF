/* Checks what the application sees once it runs inside a FELF. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char link[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", link, sizeof(link) - 1);
    if (n < 0)
        n = 0;
    link[n] = '\0';
    printf("readlink=%s\n", link);

    char *real = realpath("/proc/self/exe", NULL);
    printf("realpath=%s\n", real ? real : "(null)");
    free(real);

    const char *execfn = (const char *)getauxval(AT_EXECFN);
    printf("execfn=%s\n", execfn ? execfn : "(null)");

    char pidpath[64];
    snprintf(pidpath, sizeof(pidpath), "/proc/%d/exe", (int)getpid());
    real = realpath(pidpath, NULL);
    printf("pid-realpath=%s\n", real ? real : "(null)");
    free(real);

    const char *names[] = {"LD_PRELOAD", "LD_AUDIT", "LD_LIBRARY_PATH",
                           "FELF_APP_PATH", "FELF_ROOT", "APP_DATA",
                           "APP_HOME", "APP_LITERAL"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *v = getenv(names[i]);
        printf("%s=%s\n", names[i], v ? v : "(unset)");
    }

    int inherited = 0;
    for (int fd = 998; fd < 1200; fd++) {
        int flags = fcntl(fd, F_GETFD);
        if (flags >= 0 && !(flags & FD_CLOEXEC))
            inherited++;
    }
    printf("inheritable-felf-fds=%d\n", inherited);

    if (argc > 1) {
        /* Run a host child and show what it inherits. */
        fflush(stdout);
        execvp(argv[1], argv + 1);
        perror("execvp");
        return 1;
    }
    return 0;
}
