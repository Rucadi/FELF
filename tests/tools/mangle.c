/* Small helper for the corruption tests: edit a file in place so a FELF
 * can be damaged deterministically without shell tools.
 *
 *   mangle flip FILE OFFSET XOR
 *   mangle truncate FILE SIZE
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int parse_u64(const char *s, unsigned long long *out)
{
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "mangle: invalid number: %s\n", s);
        return -1;
    }
    *out = v;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) {
        fprintf(stderr,
                "usage: mangle flip FILE OFFSET XOR\n"
                "       mangle truncate FILE SIZE\n");
        return 2;
    }
    const char *mode = argv[1], *path = argv[2];
    unsigned long long a, b = 0;
    if (parse_u64(argv[3], &a) != 0)
        return 2;
    if (argc == 5 && parse_u64(argv[4], &b) != 0)
        return 2;

    if (strcmp(mode, "truncate") == 0) {
        if (truncate(path, (off_t)a) != 0) {
            fprintf(stderr, "mangle: truncate %s: %s\n", path,
                    strerror(errno));
            return 1;
        }
        return 0;
    }
    if (strcmp(mode, "flip") != 0) {
        fprintf(stderr, "mangle: unknown mode: %s\n", mode);
        return 2;
    }

    FILE *f = fopen(path, "r+b");
    if (f == NULL) {
        fprintf(stderr, "mangle: open %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (fseeko(f, (off_t)a, SEEK_SET) != 0) {
        fprintf(stderr, "mangle: seek %s: %s\n", path, strerror(errno));
        fclose(f);
        return 1;
    }
    int c = fgetc(f);
    if (c == EOF) {
        fprintf(stderr, "mangle: offset %llu is past the end of %s\n", a,
                path);
        fclose(f);
        return 1;
    }
    if (fseeko(f, (off_t)a, SEEK_SET) != 0 ||
        fputc(c ^ (int)(b & 0xff), f) == EOF) {
        fprintf(stderr, "mangle: write %s: %s\n", path, strerror(errno));
        fclose(f);
        return 1;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "mangle: close %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}
