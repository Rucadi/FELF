#ifndef FELF_FORMAT_H
#define FELF_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FELF_FORMAT_VERSION 1u
#define FELF_FOOTER_SIZE 256u
#define FELF_ALIGNMENT 4096u
/* Compressed objects are a sequence of independent zstd frames, one per
 * FELF_ZSTD_CHUNK bytes of the object (the last one shorter), so the
 * bootstrap can decompress them in parallel. */
#define FELF_ZSTD_CHUNK (2u * 1024u * 1024u)
#define FELF_MAX_NAME 255u

#define FELF_FLAG_STRICT 0x00000001u
#define FELF_FLAG_HAS_RESOURCES 0x00000004u
#define FELF_FLAG_LAZY 0x00000010u

/* The footer is deliberately fixed-size and at the very end of the file so
 * that a static bootstrap can find it with a single bounded read. */
typedef struct __attribute__((packed)) {
    uint8_t magic[8];
    uint32_t format_version;
    uint32_t flags;
    uint64_t container_size;
    uint64_t db_offset;
    uint64_t db_size;
    uint64_t payload_offset;
    uint64_t payload_size;
    uint64_t object_count;
    uint64_t index_offset;
    uint64_t index_size;
    uint8_t reserved[168];
    uint8_t tail_magic[8];
} felf_footer_t;

_Static_assert(sizeof(felf_footer_t) == FELF_FOOTER_SIZE,
               "FELF footer must be exactly 256 bytes");

static const uint8_t felf_magic[8] = {'F', 'E', 'L', 'F', 0, 0, 0, 0};
static const uint8_t felf_tail_magic[8] = {'F', 'E', 'L', 'F', 'E', 'N', 'D', 0};

typedef enum {
    FELF_KIND_PROGRAM = 0,
    FELF_KIND_INTERP = 1,
    FELF_KIND_SHARED = 2,
    FELF_KIND_PLUGIN = 3,
    FELF_KIND_AUDIT = 4,
    FELF_KIND_RESOURCE = 5,
    FELF_KIND_RUNTIME = 6
} felf_object_kind_t;

const char *felf_kind_name(felf_object_kind_t kind);

/* Format an error message into ERR (when ERR is non-NULL and ERRLEN is
 * non-zero).  Every component reports errors through such a pair. */
void felf_set_error(char *err, size_t errlen, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
/* strdup() that maps NULL to NULL. */
char *felf_strdup(const char *s);

/* All functions return 0 on success and -1 on failure.  Error messages are
 * always NUL-terminated when err is non-NULL and errlen is non-zero. */
int felf_footer_read(const uint8_t *file, uint64_t file_size,
                     felf_footer_t *out, char *err, size_t errlen);
int felf_footer_validate(const felf_footer_t *footer, uint64_t file_size,
                         char *err, size_t errlen);
void felf_footer_init(felf_footer_t *footer);
void felf_footer_write(const felf_footer_t *footer, uint8_t *dest);

uint64_t felf_align_up(uint64_t value, uint64_t alignment);
int felf_range_valid(uint64_t offset, uint64_t length, uint64_t limit,
                     char *err, size_t errlen);

/* Outer ELF sanity.  This is not a full ELF parser; it establishes that the
 * file can be executed as an ELF and that the footer does not overlap it. */
int felf_outer_elf_validate(const uint8_t *file, uint64_t file_size,
                            char *err, size_t errlen);

void felf_sha256(const void *data, size_t len, uint8_t out[32]);
void felf_sha256_hex(const uint8_t digest[32], char out[65]);

#ifdef __cplusplus
}
#endif

#endif
