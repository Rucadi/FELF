/* A program that replaces getenv/setenv/unsetenv, as bash does for its own
 * shell variables.  The runtime shim must edit environ itself instead of
 * calling back into the application. */
#include <stdio.h>
#include <string.h>

extern char **environ;

char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    (void)name;
    (void)value;
    (void)overwrite;
    return 0;
}

int unsetenv(const char *name)
{
    (void)name;
    return 0;
}

int main(void)
{
    for (char **e = environ; *e != NULL; e++)
        if (strncmp(*e, "LD_", 3) == 0 || strncmp(*e, "FELF_", 5) == 0)
            puts(*e);
    puts("done");
    return 0;
}
