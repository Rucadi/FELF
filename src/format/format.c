#include "felf_format.h"

#include <elf.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *felf_kind_name(felf_object_kind_t kind)
{
    switch (kind) {
    case FELF_KIND_PROGRAM: return "program";
    case FELF_KIND_INTERP: return "interp";
    case FELF_KIND_SHARED: return "shared";
    case FELF_KIND_PLUGIN: return "plugin";
    case FELF_KIND_AUDIT: return "audit";
    case FELF_KIND_RESOURCE: return "resource";
    case FELF_KIND_RUNTIME: return "runtime";
    }
    return "unknown";
}

void felf_set_error(char *err, size_t errlen, const char *fmt, ...)
{
    if (err == NULL || errlen == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

char *felf_strdup(const char *s)
{
    if (s == NULL)
        return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p != NULL)
        memcpy(p, s, n);
    return p;
}

uint64_t felf_align_up(uint64_t value, uint64_t alignment)
{
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        return 0;
    uint64_t mask = alignment - 1;
    if (value > UINT64_MAX - mask)
        return 0;
    return (value + mask) & ~mask;
}

int felf_range_valid(uint64_t offset, uint64_t length, uint64_t limit,
                     char *err, size_t errlen)
{
    if (offset > limit || length > limit - offset) {
        felf_set_error(err, errlen, "range is outside the container");
        return -1;
    }
    return 0;
}

void felf_footer_init(felf_footer_t *footer)
{
    memset(footer, 0, sizeof(*footer));
    memcpy(footer->magic, felf_magic, sizeof(felf_magic));
    memcpy(footer->tail_magic, felf_tail_magic, sizeof(felf_tail_magic));
    footer->format_version = FELF_FORMAT_VERSION;
}

void felf_footer_write(const felf_footer_t *footer, uint8_t *dest)
{
    memcpy(dest, footer, sizeof(*footer));
}

int felf_footer_read(const uint8_t *file, uint64_t file_size,
                     felf_footer_t *out, char *err, size_t errlen)
{
    if (file_size < FELF_FOOTER_SIZE) {
        felf_set_error(err, errlen,
                       "file is too small to contain a FELF footer");
        return -1;
    }
    memcpy(out, file + file_size - FELF_FOOTER_SIZE, sizeof(*out));
    return felf_footer_validate(out, file_size, err, errlen);
}

int felf_footer_validate(const felf_footer_t *footer, uint64_t file_size,
                         char *err, size_t errlen)
{
    if (memcmp(footer->magic, felf_magic, sizeof(felf_magic)) != 0 ||
        memcmp(footer->tail_magic, felf_tail_magic,
               sizeof(felf_tail_magic)) != 0) {
        felf_set_error(err, errlen, "FELF magic not found");
        return -1;
    }
    if (footer->format_version != FELF_FORMAT_VERSION) {
        felf_set_error(err, errlen, "unsupported FELF format version %u",
                   footer->format_version);
        return -1;
    }
    if (footer->container_size != file_size) {
        felf_set_error(err, errlen,
                   "container size mismatch (footer says %llu, file is %llu)",
                   (unsigned long long)footer->container_size,
                   (unsigned long long)file_size);
        return -1;
    }
    if (felf_range_valid(footer->db_offset, footer->db_size,
                         file_size - FELF_FOOTER_SIZE, err, errlen) != 0)
        return -1;
    if (footer->db_size == 0) {
        felf_set_error(err, errlen, "container has no SQLite database");
        return -1;
    }
    if (felf_range_valid(footer->payload_offset, footer->payload_size,
                         file_size - FELF_FOOTER_SIZE, err, errlen) != 0)
        return -1;
    if (footer->payload_size != 0) {
        if (footer->payload_offset < sizeof(uint64_t) ||
            footer->payload_offset > footer->db_offset) {
            felf_set_error(err, errlen,
                           "payload region is not before the database");
            return -1;
        }
    }
    if (footer->payload_offset > UINT64_MAX - footer->payload_size) {
        felf_set_error(err, errlen, "payload range overflows");
        return -1;
    }
    if (footer->db_offset < footer->payload_offset + footer->payload_size) {
        felf_set_error(err, errlen,
                       "database overlaps the ELF payload region");
        return -1;
    }
    if (footer->index_size != 0 &&
        felf_range_valid(footer->index_offset, footer->index_size,
                         file_size - FELF_FOOTER_SIZE, err, errlen) != 0)
        return -1;
    return 0;
}

int felf_outer_elf_validate(const uint8_t *file, uint64_t file_size,
                            char *err, size_t errlen)
{
    if (file_size < EI_NIDENT + sizeof(Elf64_Ehdr)) {
        felf_set_error(err, errlen, "file is too small for an ELF64 header");
        return -1;
    }
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)file;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) {
        felf_set_error(err, errlen, "outer file is not an ELF file");
        return -1;
    }
    if (eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB) {
        felf_set_error(err, errlen, "outer file is not little-endian ELF64");
        return -1;
    }
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) {
        felf_set_error(err, errlen, "outer ELF is not EXEC or DYN");
        return -1;
    }
    if (eh->e_ehsize != sizeof(Elf64_Ehdr) ||
        eh->e_phentsize != sizeof(Elf64_Phdr)) {
        felf_set_error(err, errlen,
                       "outer ELF has an unsupported header layout");
        return -1;
    }
    if (eh->e_phnum == 0 || eh->e_phnum > 4096) {
        felf_set_error(err, errlen,
                       "outer ELF has an invalid program header count");
        return -1;
    }
    uint64_t ph_end = eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize;
    if (felf_range_valid(eh->e_phoff, ph_end - eh->e_phoff, file_size,
                         err, errlen) != 0)
        return -1;
    return 0;
}
