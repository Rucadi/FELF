#ifndef FELF_ELF_PARSER_H
#define FELF_ELF_PARSER_H

#include <elf.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *path;
    uint8_t *data;
    size_t size;
    Elf64_Ehdr ehdr;
    Elf64_Phdr *phdrs;
    size_t phnum;
    char *interp;
    char *soname;
    char *rpath;
    char *runpath;
    char **needed;
    size_t needed_count;
    /* Symbol versions it defines (DT_VERDEF, without the base entry), and
     * the ones it requires (DT_VERNEED, without weak ones): requirement i
     * is version verneed_versions[i] of the object verneed_files[i]. */
    char **verdefs;
    size_t verdef_count;
    char **verneed_files;
    char **verneed_versions;
    size_t verneed_count;
    uint64_t max_align;
} felf_elf_t;

/* Load and validate a little-endian ELF64 object for an architecture in
 * felf_arch.h. */
int felf_elf_load(const char *path, felf_elf_t *elf, char *err, size_t errlen);
void felf_elf_free(felf_elf_t *elf);

/* Convert a virtual address inside a PT_LOAD segment to a file offset. */
int felf_elf_vaddr_to_offset(const felf_elf_t *elf, uint64_t vaddr,
                             uint64_t *offset);

#endif
