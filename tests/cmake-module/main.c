#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

const char *greet(void);

int main(int argc, char **argv)
{
    (void)argc;
    void *handle = dlopen("libextra.so", RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    const char *(*extra)(void) = (const char *(*)(void))dlsym(handle, "extra");
    const char *env = getenv("FELF_CMAKE_TEST");
    printf("argv0=%s %s %s env=%s\n", argv[0], greet(), extra(),
           env ? env : "(unset)");
    return 0;
}
