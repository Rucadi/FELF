#include <langinfo.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* With "default", uses the locale the environment names, like btop and Qt:
 * setlocale(LC_ALL, "") must find a UTF-8 locale.  Otherwise switches to
 * C.UTF-8 explicitly. */
int main(int argc, char **argv)
{
    const char *r = (argc > 1 && strcmp(argv[1], "default") == 0)
                        ? setlocale(LC_ALL, "")
                        : setlocale(LC_ALL, "C.UTF-8");
    printf("setlocale=%s codeset=%s\n", r ? r : "(null)",
           nl_langinfo(CODESET));
    /* The category glibc selects from the current environment; the full
     * setlocale() string varies with the caller's other LC_* settings. */
    printf("ctype=%s\n", setlocale(LC_CTYPE, NULL));
    const char *a = getenv("LOCALE_ARCHIVE");
    printf("archive=%s\n", a ? a : "(unset)");
    const char *a2 = getenv("LOCALE_ARCHIVE_2_27");
    printf("archive2=%s\n", a2 ? a2 : "(unset)");
    const char *l = getenv("LANG");
    printf("lang=%s\n", l ? l : "(unset)");
    return 0;
}
