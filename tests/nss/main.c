/* Looks up argv[1] through NSS (getpwnam). */
#include <pwd.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    struct passwd *pw = getpwnam(argv[1]);
    if (pw == NULL) {
        printf("user %s: missing\n", argv[1]);
        return 1;
    }
    printf("user %s: uid=%u\n", pw->pw_name, (unsigned)pw->pw_uid);
    return 0;
}
