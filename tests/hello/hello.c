#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    printf("Hello from FELF!\n");
    printf("argc=%d\n", argc);
    for (int i = 1; i < argc; i++)
        printf("arg%d=%s\n", i, argv[i]);
    const char *value = getenv("FELF_TEST_VALUE");
    printf("env=%s\n", value ? value : "(null)");
    fflush(stdout);
    return 0;
}
