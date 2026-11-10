/* A host NSS module that knows one user, felf-nss-user (uid 4242). */
#include <nss.h>
#include <pwd.h>
#include <stddef.h>
#include <string.h>

enum nss_status _nss_felftest_getpwnam_r(const char *name, struct passwd *pw,
                                         char *buf, size_t len, int *err)
{
    (void)buf;
    (void)len;
    (void)err;
    if (strcmp(name, "felf-nss-user") != 0)
        return NSS_STATUS_NOTFOUND;
    pw->pw_name = (char *)"felf-nss-user";
    pw->pw_passwd = (char *)"x";
    pw->pw_uid = 4242;
    pw->pw_gid = 4242;
    pw->pw_gecos = (char *)"";
    pw->pw_dir = (char *)"/nonexistent";
    pw->pw_shell = (char *)"/bin/sh";
    return NSS_STATUS_SUCCESS;
}
