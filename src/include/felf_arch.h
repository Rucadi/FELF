#ifndef FELF_ARCH_H
#define FELF_ARCH_H

/*
 * Supported target architectures (Linux, glibc, little-endian ELF64).
 *
 * The packer takes the target from the program's e_machine and requires
 * every embedded object, the bootstrap included, to match it.  The
 * runtime pieces (bootstrap, audit module) are built per architecture and
 * use the FELF_HOST_* values for the one they are compiled for.
 *
 * This header is used by the freestanding audit module: no libc.
 */

#define FELF_EM_X86_64 62
#define FELF_EM_AARCH64 183

typedef struct {
    unsigned short machine;
    /* package.architecture and $PLATFORM in RPATH/RUNPATH. */
    const char *name;
    /* Basename of the glibc dynamic linker. */
    const char *interp;
    /* Debian/Ubuntu multiarch directory name under /lib and /usr/lib. */
    const char *multiarch;
} felf_arch_t;

/* The supported architectures, terminated by an entry with machine 0. */
static inline const felf_arch_t *felf_arches(void)
{
    static const felf_arch_t arches[] = {
        {FELF_EM_X86_64, "x86_64", "ld-linux-x86-64.so.2", "x86_64-linux-gnu"},
        {FELF_EM_AARCH64, "aarch64", "ld-linux-aarch64.so.1",
         "aarch64-linux-gnu"},
        {0, 0, 0, 0},
    };
    return arches;
}

/* The architecture for ELF e_machine MACHINE, or NULL if unsupported. */
static inline const felf_arch_t *felf_arch_find(unsigned machine)
{
    for (const felf_arch_t *a = felf_arches(); a->machine != 0; a++)
        if (a->machine == machine)
            return a;
    return 0;
}

/* The architecture called NAME ("x86_64", "aarch64"), or NULL. */
static inline const felf_arch_t *felf_arch_by_name(const char *name)
{
    for (const felf_arch_t *a = felf_arches(); a->machine != 0; a++) {
        const char *p = a->name, *q = name;
        while (*p != '\0' && *p == *q) {
            p++;
            q++;
        }
        if (*p == *q)
            return a;
    }
    return 0;
}

/* The architecture this file is compiled for.  FELF_HOST_LDCACHE_FLAGS are
 * the /etc/ld.so.cache entry flags of a 64-bit glibc library on it:
 * FLAG_ELF_LIBC6 (0x0003) | FLAG_X8664_LIB64 (0x0300) or
 * FLAG_AARCH64_LIB64 (0x0a00). */
#if defined(__x86_64__)
#define FELF_HOST_MACHINE FELF_EM_X86_64
#define FELF_HOST_MULTIARCH "x86_64-linux-gnu"
#define FELF_HOST_LDCACHE_FLAGS 0x0303u
#elif defined(__aarch64__)
#define FELF_HOST_MACHINE FELF_EM_AARCH64
#define FELF_HOST_MULTIARCH "aarch64-linux-gnu"
#define FELF_HOST_LDCACHE_FLAGS 0x0a03u
#endif
/* Without a FELF_HOST_* definition (another host), only the packer and
 * inspector can be built: they work on any target in felf_arch_find(). */

#endif
