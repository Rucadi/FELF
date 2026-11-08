/* Loads a plugin by absolute path, like Qt does for files in a tree bundle. */
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    void *h = dlopen(argv[1], RTLD_NOW);
    if (h == NULL) {
        printf("tree plugin failed: %s\n", dlerror());
        return 1;
    }
    int (*value)(void) = (int (*)(void))dlsym(h, "tree_value");
    printf("tree value=%d\n", value ? value() : -1);
    return 0;
}
