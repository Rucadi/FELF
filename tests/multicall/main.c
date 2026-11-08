/* A multicall binary, like busybox or coreutils: it dispatches on the name
 * it was invoked as.  Several names for this one file become FELF aliases
 * (--alias) or programs (--program). */
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *base = strrchr(argv[0], '/');
    printf("applet=%s\n", base != NULL ? base + 1 : argv[0]);
    return argc > 99;
}
