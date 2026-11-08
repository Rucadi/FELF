#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the mapping holding ADDR comes from: "felf" (the FELF file SELF
 * itself), "memfd", or "file". */
static const char *origin(const void *addr, const char *self)
{
    static char path[PATH_MAX];
    FILE *maps = fopen("/proc/self/maps", "r");
    if (maps == NULL)
        return "unknown";
    const char *result = "unknown";
    char line[PATH_MAX + 128];
    while (fgets(line, sizeof(line), maps) != NULL) {
        uintptr_t start, end;
        int n = 0;
        if (sscanf(line, "%lx-%lx %*s %*s %*s %*s %n", &start, &end, &n) < 2 ||
            (uintptr_t)addr < start || (uintptr_t)addr >= end)
            continue;
        snprintf(path, sizeof(path), "%s", line + n);
        path[strcspn(path, "\n")] = '\0';
        if (strcmp(path, self) == 0)
            result = "felf";
        else if (strstr(path, "memfd:") != NULL)
            result = "memfd";
        else
            result = "file";
        break;
    }
    fclose(maps);
    return result;
}

int main(void)
{
    char self[PATH_MAX];
    if (realpath("/proc/self/exe", self) == NULL)
        return 1;

    Dl_info info;
    if (dladdr((void *)&printf, &info) == 0)
        return 1;
    printf("libc=%s name=%s\n", origin(info.dli_fbase, self), info.dli_fname);

    void *handle = dlopen("libplugin.so", RTLD_NOW);
    if (handle == NULL) {
        printf("plugin failed: %s\n", dlerror());
        return 1;
    }
    int (*value)(void) = (int (*)(void))dlsym(handle, "plugin_value");
    if (value == NULL || dladdr((void *)value, &info) == 0)
        return 1;
    printf("plugin=%s value=%d\n", origin(info.dli_fbase, self), value());
    return 0;
}
