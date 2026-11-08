#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "code") == 0)
        return 42;
    if (argc > 1 && strcmp(argv[1], "signal") == 0)
        raise(SIGTERM);
    char buf[128];
    if (fgets(buf, sizeof(buf), stdin) != NULL)
        printf("stdin=%s", buf);
    return 0;
}
