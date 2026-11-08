#include <stdio.h>
#include <sys/resource.h>

/* Prints the soft open-file limit the application sees. */
int main(void)
{
    struct rlimit r;
    if (getrlimit(RLIMIT_NOFILE, &r) != 0)
        return 1;
    printf("nofile=%llu\n", (unsigned long long)r.rlim_cur);
    return 0;
}
