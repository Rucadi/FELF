/* dlopens each argument and calls its many_value(). */
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        void *handle = dlopen(argv[i], RTLD_NOW);
        int (*value)(void) =
            handle ? (int (*)(void))dlsym(handle, "many_value") : NULL;
        if (value == NULL) {
            printf("%s: %s\n", argv[i], dlerror());
            return 1;
        }
        printf("%s: value=%d\n", argv[i], value());
    }
    return 0;
}
