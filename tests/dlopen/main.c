#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc > 1) {
        void *absolute = dlopen(argv[1], RTLD_NOW);
        if (absolute == NULL) {
            printf("absolute plugin failed: %s\n", dlerror());
            return 1;
        }
        int (*absolute_value)(void) =
            (int (*)(void))dlsym(absolute, "plugin_value");
        printf("absolute plugin value=%d\n",
               absolute_value ? absolute_value() : -1);
        dlclose(absolute);
    }

    void *handle = dlopen("libplugin.so", RTLD_NOW);
    if (handle == NULL) {
        printf("plugin failed: %s\n", dlerror());
        return 1;
    }
    int (*value)(void) = (int (*)(void))dlsym(handle, "plugin_value");
    if (value == NULL) {
        printf("symbol failed: %s\n", dlerror());
        return 1;
    }
    printf("plugin value=%d\n", value());

    void *host = dlopen("libm.so.6", RTLD_NOW);
    printf("host libm=%s\n", host ? "LOADED" : "DENIED");
    if (host != NULL)
        dlclose(host);
    dlclose(handle);
    return 0;
}
