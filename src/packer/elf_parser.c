#include "elf_parser.h"
#include "felf_arch.h"
#include "felf_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int felf_elf_vaddr_to_offset(const felf_elf_t *elf, uint64_t vaddr,
                             uint64_t *offset)
{
    for (size_t i = 0; i < elf->phnum; i++) {
        const Elf64_Phdr *ph = &elf->phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_filesz == 0)
            continue;
        if (vaddr >= ph->p_vaddr && vaddr - ph->p_vaddr < ph->p_filesz) {
            uint64_t delta = vaddr - ph->p_vaddr;
            if (ph->p_offset > UINT64_MAX - delta)
                return -1;
            *offset = ph->p_offset + delta;
            return 0;
        }
    }
    return -1;
}

static int strtab_get(const uint8_t *strtab, uint64_t strsz, uint64_t offset,
                      const char **out, char *err, size_t errlen)
{
    if (offset >= strsz) {
        felf_set_error(err, errlen, "dynamic string offset is out of bounds");
        return -1;
    }
    const uint8_t *p = strtab + offset;
    size_t remaining = (size_t)(strsz - offset);
    if (memchr(p, '\0', remaining) == NULL) {
        felf_set_error(err, errlen, "dynamic string is not NUL-terminated");
        return -1;
    }
    *out = (const char *)p;
    return 0;
}

static int push_string(char ***array, size_t count, const char *s)
{
    char **p = realloc(*array, (count + 1) * sizeof(*p));
    if (p == NULL)
        return -1;
    *array = p;
    p[count] = strdup(s);
    return p[count] == NULL ? -1 : 0;
}

/* The bytes at VADDR (SIZE of them) in the file, or NULL. */
static const uint8_t *at_vaddr(const felf_elf_t *elf, uint64_t vaddr,
                               uint64_t size)
{
    uint64_t off;
    if (felf_elf_vaddr_to_offset(elf, vaddr, &off) != 0 || off > elf->size ||
        size > elf->size - off)
        return NULL;
    return elf->data + off;
}

/* DT_VERDEF and DT_VERNEED: the symbol versions the object defines and
 * the ones it needs from other objects. */
static int parse_versions(felf_elf_t *elf, const Elf64_Dyn *entries,
                          size_t count, const uint8_t *strtab, uint64_t strsz,
                          char *err, size_t errlen)
{
    uint64_t verdef = 0, verdefnum = 0, verneed = 0, verneednum = 0;
    for (size_t i = 0; i < count && entries[i].d_tag != DT_NULL; i++) {
        switch (entries[i].d_tag) {
        case DT_VERDEF: verdef = entries[i].d_un.d_ptr; break;
        case DT_VERDEFNUM: verdefnum = entries[i].d_un.d_val; break;
        case DT_VERNEED: verneed = entries[i].d_un.d_ptr; break;
        case DT_VERNEEDNUM: verneednum = entries[i].d_un.d_val; break;
        }
    }
    if ((verdefnum || verneednum) && strtab == NULL) {
        felf_set_error(err, errlen, "symbol versions without DT_STRTAB");
        return -1;
    }
    const char *s;
    uint64_t at = verdef;
    for (uint64_t i = 0; i < verdefnum && i < 65536; i++) {
        const Elf64_Verdef *vd = (const Elf64_Verdef *)
            at_vaddr(elf, at, sizeof(*vd));
        if (vd == NULL) {
            felf_set_error(err, errlen, "DT_VERDEF is out of bounds");
            return -1;
        }
        const Elf64_Verdaux *aux = (const Elf64_Verdaux *)
            at_vaddr(elf, at + vd->vd_aux, sizeof(*aux));
        if (vd->vd_cnt > 0 && (vd->vd_flags & VER_FLG_BASE) == 0) {
            if (aux == NULL ||
                strtab_get(strtab, strsz, aux->vda_name, &s, err,
                           errlen) != 0)
                return -1;
            if (push_string(&elf->verdefs, elf->verdef_count, s) != 0)
                goto oom;
            elf->verdef_count++;
        }
        if (vd->vd_next == 0)
            break;
        at += vd->vd_next;
    }
    at = verneed;
    for (uint64_t i = 0; i < verneednum && i < 65536; i++) {
        const Elf64_Verneed *vn = (const Elf64_Verneed *)
            at_vaddr(elf, at, sizeof(*vn));
        if (vn == NULL) {
            felf_set_error(err, errlen, "DT_VERNEED is out of bounds");
            return -1;
        }
        const char *file;
        if (strtab_get(strtab, strsz, vn->vn_file, &file, err, errlen) != 0)
            return -1;
        uint64_t aux_at = at + vn->vn_aux;
        for (unsigned j = 0; j < vn->vn_cnt; j++) {
            const Elf64_Vernaux *va = (const Elf64_Vernaux *)
                at_vaddr(elf, aux_at, sizeof(*va));
            if (va == NULL) {
                felf_set_error(err, errlen, "DT_VERNEED is out of bounds");
                return -1;
            }
            if (strtab_get(strtab, strsz, va->vna_name, &s, err,
                           errlen) != 0)
                return -1;
            if ((va->vna_flags & VER_FLG_WEAK) == 0) {
                if (push_string(&elf->verneed_files, elf->verneed_count,
                                file) != 0 ||
                    push_string(&elf->verneed_versions, elf->verneed_count,
                                s) != 0)
                    goto oom;
                elf->verneed_count++;
            }
            if (va->vna_next == 0)
                break;
            aux_at += va->vna_next;
        }
        if (vn->vn_next == 0)
            break;
        at += vn->vn_next;
    }
    return 0;
oom:
    felf_set_error(err, errlen, "out of memory reading symbol versions");
    return -1;
}

static int parse_dynamic(felf_elf_t *elf, char *err, size_t errlen)
{
    const Elf64_Phdr *dyn = NULL;
    for (size_t i = 0; i < elf->phnum; i++) {
        if (elf->phdrs[i].p_type == PT_DYNAMIC) {
            dyn = &elf->phdrs[i];
            break;
        }
    }
    if (dyn == NULL)
        return 0;
    if (dyn->p_filesz % sizeof(Elf64_Dyn) != 0 ||
        dyn->p_offset > elf->size ||
        dyn->p_filesz > elf->size - dyn->p_offset) {
        felf_set_error(err, errlen, "invalid PT_DYNAMIC range");
        return -1;
    }

    const Elf64_Dyn *entries = (const Elf64_Dyn *)(elf->data + dyn->p_offset);
    size_t count = (size_t)(dyn->p_filesz / sizeof(Elf64_Dyn));
    uint64_t strtab_vaddr = 0;
    uint64_t strsz = 0;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].d_tag == DT_STRTAB)
            strtab_vaddr = entries[i].d_un.d_ptr;
        else if (entries[i].d_tag == DT_STRSZ)
            strsz = entries[i].d_un.d_val;
        else if (entries[i].d_tag == DT_NULL)
            break;
    }
    if (strtab_vaddr == 0 || strsz == 0) {
        /* A dynamic object with no string table is legal only if it has no
         * NEEDED/SONAME/RPATH entries. */
        strsz = 0;
    }

    uint64_t strtab_off = 0;
    const uint8_t *strtab = NULL;
    if (strsz != 0) {
        if (felf_elf_vaddr_to_offset(elf, strtab_vaddr, &strtab_off) != 0 ||
            strtab_off > elf->size || strsz > elf->size - strtab_off) {
            felf_set_error(err, errlen,
                           "DT_STRTAB is outside a loadable segment");
            return -1;
        }
        strtab = elf->data + strtab_off;
    }

    size_t cap = 0;
    for (size_t i = 0; i < count && entries[i].d_tag != DT_NULL; i++) {
        char **slot;
        switch (entries[i].d_tag) {
        case DT_NEEDED:
            if (elf->needed_count == cap) {
                size_t newcap = cap ? cap * 2 : 8;
                char **p = realloc(elf->needed, newcap * sizeof(*p));
                if (p == NULL) {
                    felf_set_error(err, errlen, "out of memory in DT_NEEDED");
                    return -1;
                }
                elf->needed = p;
                cap = newcap;
            }
            elf->needed[elf->needed_count] = NULL;
            slot = &elf->needed[elf->needed_count++];
            break;
        case DT_SONAME: slot = &elf->soname; break;
        case DT_RPATH: slot = &elf->rpath; break;
        case DT_RUNPATH: slot = &elf->runpath; break;
        default: continue;
        }
        const char *s;
        if (strtab == NULL) {
            felf_set_error(err, errlen, "dynamic string without DT_STRTAB");
            return -1;
        }
        if (strtab_get(strtab, strsz, entries[i].d_un.d_val, &s,
                       err, errlen) != 0)
            return -1;
        free(*slot);
        *slot = strdup(s);
        if (*slot == NULL) {
            felf_set_error(err, errlen, "out of memory reading PT_DYNAMIC");
            return -1;
        }
    }
    return parse_versions(elf, entries, count, strtab, strsz, err, errlen);
}

int felf_elf_load(const char *path, felf_elf_t *elf, char *err, size_t errlen)
{
    memset(elf, 0, sizeof(*elf));
    if (felf_read_file(path, &elf->data, &elf->size, err, errlen) != 0)
        return -1;
    elf->path = strdup(path);
    if (elf->path == NULL) {
        felf_set_error(err, errlen, "out of memory");
        felf_elf_free(elf);
        return -1;
    }
    if (elf->size < sizeof(Elf64_Ehdr)) {
        felf_set_error(err, errlen, "%s is too small for ELF64", path);
        felf_elf_free(elf);
        return -1;
    }
    memcpy(&elf->ehdr, elf->data, sizeof(elf->ehdr));
    if (memcmp(elf->ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        felf_set_error(err, errlen, "%s is not an ELF file", path);
        felf_elf_free(elf);
        return -1;
    }
    if (elf->ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        elf->ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        felf_arch_find(elf->ehdr.e_machine) == NULL) {
        felf_set_error(err, errlen, "%s is not little-endian ELF64 for a "
                       "supported architecture (x86-64, aarch64)", path);
        felf_elf_free(elf);
        return -1;
    }
    if (elf->ehdr.e_type != ET_EXEC && elf->ehdr.e_type != ET_DYN) {
        felf_set_error(err, errlen, "%s is not an executable or shared object",
                       path);
        felf_elf_free(elf);
        return -1;
    }
    if (elf->ehdr.e_phentsize != sizeof(Elf64_Phdr) ||
        elf->ehdr.e_phnum == 0 || elf->ehdr.e_phnum > 4096) {
        felf_set_error(err, errlen,
                       "%s has an invalid program header table", path);
        felf_elf_free(elf);
        return -1;
    }
    uint64_t phbytes = (uint64_t)elf->ehdr.e_phnum * sizeof(Elf64_Phdr);
    if (elf->ehdr.e_phoff > elf->size ||
        phbytes > elf->size - elf->ehdr.e_phoff) {
        felf_set_error(err, errlen,
                       "%s program headers are out of bounds", path);
        felf_elf_free(elf);
        return -1;
    }
    elf->phnum = elf->ehdr.e_phnum;
    elf->phdrs = malloc(elf->phnum * sizeof(*elf->phdrs));
    if (elf->phdrs == NULL) {
        felf_set_error(err, errlen, "out of memory");
        felf_elf_free(elf);
        return -1;
    }
    memcpy(elf->phdrs, elf->data + elf->ehdr.e_phoff,
           elf->phnum * sizeof(*elf->phdrs));
    elf->max_align = 4096;
    for (size_t i = 0; i < elf->phnum; i++) {
        const Elf64_Phdr *ph = &elf->phdrs[i];
        if (ph->p_filesz != 0 &&
            (ph->p_offset > elf->size ||
             ph->p_filesz > elf->size - ph->p_offset)) {
            felf_set_error(err, errlen,
                           "%s has a segment outside the file", path);
            felf_elf_free(elf);
            return -1;
        }
        if (ph->p_type == PT_LOAD && ph->p_align > elf->max_align &&
            ph->p_align <= 2 * 1024 * 1024)
            elf->max_align = ph->p_align;
        if (ph->p_type == PT_INTERP) {
            if (ph->p_filesz == 0 || ph->p_filesz > 4096 ||
                ph->p_offset > elf->size ||
                ph->p_filesz > elf->size - ph->p_offset) {
                felf_set_error(err, errlen,
                               "%s has an invalid PT_INTERP", path);
                felf_elf_free(elf);
                return -1;
            }
            char *interp = malloc((size_t)ph->p_filesz + 1);
            if (interp == NULL) {
                felf_set_error(err, errlen, "out of memory");
                felf_elf_free(elf);
                return -1;
            }
            memcpy(interp, elf->data + ph->p_offset, ph->p_filesz);
            interp[ph->p_filesz] = '\0';
            if (memchr(interp, '\0', ph->p_filesz) == NULL) {
                felf_set_error(err, errlen,
                               "%s PT_INTERP is not NUL-terminated",
                               path);
                free(interp);
                felf_elf_free(elf);
                return -1;
            }
            elf->interp = interp;
        }
    }
    if (parse_dynamic(elf, err, errlen) != 0) {
        felf_elf_free(elf);
        return -1;
    }
    return 0;
}

void felf_elf_free(felf_elf_t *elf)
{
    if (elf == NULL)
        return;
    free(elf->path);
    free(elf->data);
    free(elf->phdrs);
    free(elf->interp);
    free(elf->soname);
    free(elf->rpath);
    free(elf->runpath);
    for (size_t i = 0; i < elf->needed_count; i++)
        free(elf->needed[i]);
    free(elf->needed);
    for (size_t i = 0; i < elf->verdef_count; i++)
        free(elf->verdefs[i]);
    free(elf->verdefs);
    for (size_t i = 0; i < elf->verneed_count; i++) {
        free(elf->verneed_files[i]);
        free(elf->verneed_versions[i]);
    }
    free(elf->verneed_files);
    free(elf->verneed_versions);
    memset(elf, 0, sizeof(*elf));
}

