/*
 * FELF audit module (LD_AUDIT).
 *
 * This object is intentionally freestanding: it has no libc dependency and no
 * dynamic dependencies.  glibc loads LD_AUDIT objects before the application's
 * libc is usable.
 *
 * The bootstrap writes an index to descriptor FELF_AUDIT_INDEX_FD (998):
 * a header (flags, bundle root), name -> path entries, the host-preferred
 * library patterns, and the host library directories (see
 * felf_loader_protocol.h).  A path is /proc/self/fd/N, or FELF_MAP_PREFIX
 * and an offset for a library FELF's patched loader maps from the FELF file.
 *
 * la_objsearch() policy:
 *   - /proc/self/fd/... paths (bundled objects) are always allowed.
 *   - A host-preferred name (the graphics stack, felf_host_libs.h) is looked
 *     up on the host first, the way the host's ld.so would: LD_LIBRARY_PATH,
 *     /etc/ld.so.cache, default directories.  The bundled copy is only a
 *     fallback.
 *   - A name, or the basename of a path, that is in the index is redirected
 *     to the bundled memfd.  This also deduplicates a library that is loaded
 *     both by SONAME and by an absolute path.
 *   - Absolute paths under the bundle root (plugins kept as files in a tree
 *     bundle) are allowed.
 *   - Objects loaded from the host are tagged in la_objopen(); their own
 *     dependencies may come from the host too (a Mesa driver needs libLLVM,
 *     libexpat, ...), unless the name is bundled.
 *   - Anything else is denied in strict mode.  In permissive mode it is
 *     passed through, and the private loader's default directories (which
 *     belong to the bundled glibc, not the host) are replaced by the host
 *     search above.
 *
 * With FELF_DEBUG set when the FELF starts, every decision is logged to
 * stderr.
 *
 * This is enforcement, not authentication.  The FELF file is unsigned;
 * checksums detect corruption and are not signatures.
 */

#include "felf_host_libs.h"
#include "felf_arch.h"
#include "felf_loader_protocol.h"

/* <stdint.h> is available freestanding; libc headers are not. */
typedef uintptr_t felf_uintptr_t;
typedef uint64_t felf_uint64_t;
typedef uint32_t felf_uint32_t;
typedef uint16_t felf_uint16_t;
typedef uint8_t felf_uint8_t;

#define LA_SER_ORIG 0x01
#define LA_SER_CONFIG 0x08
#define LA_SER_DEFAULT 0x40

/* Cookie bit set on objects that were loaded from the host.  glibc
 * initializes each cookie to the (aligned) link_map address and never
 * interprets it. */
#define FELF_COOKIE_HOST 0x1ul

#define FELF_HOST_PATH_SIZE 512u
#define FELF_HOST_PATH_SLOTS 256u

/* The public prefix of glibc's struct link_map (<link.h>). */
struct felf_link_map
{
    felf_uintptr_t l_addr;
    char *l_name;
    void *l_ld;
    struct felf_link_map *l_next;
    struct felf_link_map *l_prev;
};

static felf_uint8_t index_buffer[FELF_AUDIT_BUFFER_SIZE];
static unsigned long index_size;
static int index_initialized;
static int index_valid;
static int index_strict = 1;
static int index_debug;
static unsigned long root_len;
static const felf_audit_index_entry_t *index_entries;
static const char *index_patterns;
static const char *index_libpath;
static const char *index_defaults;

/* Paths returned for host objects, so la_objopen() can tag them.  The
 * strings must outlive the call: glibc opens the returned name. */
static char host_paths[FELF_HOST_PATH_SLOTS][FELF_HOST_PATH_SIZE];
static unsigned int host_path_count;
static char host_scratch[FELF_HOST_PATH_SIZE];

static int cache_initialized;
static const char *cache_data;
static unsigned long cache_size;
static felf_uint32_t cache_nlibs;

/* Raw syscalls.  The audit module cannot call libc.  Only the *at() forms
 * are used, since aarch64 (asm-generic) has no open() or readlink(). */
#if defined(__x86_64__)
#define FELF_NR_READ 0
#define FELF_NR_WRITE 1
#define FELF_NR_CLOSE 3
#define FELF_NR_LSEEK 8
#define FELF_NR_MMAP 9
#define FELF_NR_OPENAT 257
#define FELF_NR_READLINKAT 267

static long
felf_syscall6 (long nr, long a, long b, long c, long d, long e, long f)
{
  register long rax __asm__ ("rax") = nr;
  register long rdi __asm__ ("rdi") = a;
  register long rsi __asm__ ("rsi") = b;
  register long rdx __asm__ ("rdx") = c;
  register long r10 __asm__ ("r10") = d;
  register long r8 __asm__ ("r8") = e;
  register long r9 __asm__ ("r9") = f;
  __asm__ volatile ("syscall"
                    : "+r" (rax)
                    : "r" (rdi), "r" (rsi), "r" (rdx), "r" (r10), "r" (r8),
                      "r" (r9)
                    : "rcx", "r11", "memory");
  return rax;
}
#elif defined(__aarch64__)
#define FELF_NR_OPENAT 56
#define FELF_NR_CLOSE 57
#define FELF_NR_LSEEK 62
#define FELF_NR_READ 63
#define FELF_NR_WRITE 64
#define FELF_NR_READLINKAT 78
#define FELF_NR_MMAP 222

static long
felf_syscall6 (long nr, long a, long b, long c, long d, long e, long f)
{
  register long x8 __asm__ ("x8") = nr;
  register long x0 __asm__ ("x0") = a;
  register long x1 __asm__ ("x1") = b;
  register long x2 __asm__ ("x2") = c;
  register long x3 __asm__ ("x3") = d;
  register long x4 __asm__ ("x4") = e;
  register long x5 __asm__ ("x5") = f;
  __asm__ volatile ("svc 0"
                    : "+r" (x0)
                    : "r" (x8), "r" (x1), "r" (x2), "r" (x3), "r" (x4),
                      "r" (x5)
                    : "memory");
  return x0;
}
#else
#error "the FELF audit module supports x86-64 and aarch64"
#endif

#define FELF_AT_FDCWD (-100)
#define FELF_O_CLOEXEC 02000000
#define FELF_O_PATH 010000000

static long
felf_syscall3 (long nr, long a, long b, long c)
{
  return felf_syscall6 (nr, a, b, c, 0, 0, 0);
}

static long
felf_read (int fd, void *buf, unsigned long count)
{
  return felf_syscall3 (FELF_NR_READ, fd, (long) buf, (long) count);
}

static void
felf_write (int fd, const void *buf, unsigned long count)
{
  felf_syscall3 (FELF_NR_WRITE, fd, (long) buf, (long) count);
}

/* Read-only (O_RDONLY is 0), close-on-exec, plus EXTRA_FLAGS. */
static int
felf_open (const char *path, long extra_flags)
{
  return (int) felf_syscall6 (FELF_NR_OPENAT, FELF_AT_FDCWD, (long) path,
                              FELF_O_CLOEXEC | extra_flags, 0, 0, 0);
}

static void
felf_close (int fd)
{
  felf_syscall3 (FELF_NR_CLOSE, fd, 0, 0);
}

static unsigned long
felf_strlen (const char *s)
{
  unsigned long n = 0;
  while (s[n] != '\0')
    n++;
  return n;
}

static void
felf_log (const char *what, const char *name, const char *result)
{
  if (!index_debug)
    return;
  static const char prefix[] = "felf[audit]: ";
  felf_write (2, prefix, sizeof (prefix) - 1);
  felf_write (2, what, felf_strlen (what));
  felf_write (2, " ", 1);
  felf_write (2, name, felf_strlen (name));
  if (result != 0)
    {
      felf_write (2, " -> ", 4);
      felf_write (2, result, felf_strlen (result));
    }
  felf_write (2, "\n", 1);
}

static int
felf_memcmp (const void *a, const void *b, unsigned long n)
{
  const felf_uint8_t *pa = a;
  const felf_uint8_t *pb = b;
  for (unsigned long i = 0; i < n; i++)
    if (pa[i] != pb[i])
      return 1;
  return 0;
}

static int
felf_strcmp (const char *a, const char *b)
{
  while (*a != '\0' && *a == *b)
    {
      a++;
      b++;
    }
  return (unsigned char) *a - (unsigned char) *b;
}

static int
felf_has_prefix (const char *s, const char *prefix)
{
  while (*prefix != '\0')
    {
      if (*s++ != *prefix++)
        return 0;
    }
  return 1;
}

static const char *
felf_basename (const char *path)
{
  const char *base = path;
  for (const char *p = path; *p != '\0'; p++)
    if (*p == '/')
      base = p + 1;
  return base;
}

/* Force NUL termination of COUNT fixed-size strings. */
static void
felf_terminate (char *strings, felf_uint32_t count, unsigned int size)
{
  for (felf_uint32_t i = 0; i < count; i++)
    strings[(unsigned long) i * size + size - 1] = '\0';
}

static void
felf_index_init (void)
{
  if (index_initialized)
    return;
  index_initialized = 1;

  while (index_size < sizeof (index_buffer))
    {
      long n = felf_read (FELF_AUDIT_INDEX_FD, index_buffer + index_size,
                          sizeof (index_buffer) - index_size);
      if (n <= 0)
        break;
      index_size += (unsigned long) n;
    }
  if (index_size < sizeof (felf_audit_index_header_t))
    return;

  felf_audit_index_header_t *header =
      (felf_audit_index_header_t *) index_buffer;
  if (felf_memcmp (header->magic, FELF_AUDIT_INDEX_MAGIC, 8) != 0
      || header->version != FELF_AUDIT_INDEX_VERSION
      || header->entry_count > FELF_AUDIT_MAX_ENTRIES
      || header->pattern_count > FELF_AUDIT_MAX_PATTERNS
      || header->libpath_count > FELF_AUDIT_MAX_DIRS
      || header->default_count > FELF_AUDIT_MAX_DIRS)
    return;

  unsigned long need = sizeof (*header)
      + (unsigned long) header->entry_count * sizeof (felf_audit_index_entry_t)
      + (unsigned long) header->pattern_count * FELF_AUDIT_NAME_SIZE
      + ((unsigned long) header->libpath_count + header->default_count)
            * FELF_AUDIT_DIR_SIZE;
  if (need > index_size)
    return;

  char *p = (char *) index_buffer + sizeof (*header);
  index_entries = (const felf_audit_index_entry_t *) p;
  p += (unsigned long) header->entry_count * sizeof (felf_audit_index_entry_t);
  felf_terminate (p, header->pattern_count, FELF_AUDIT_NAME_SIZE);
  index_patterns = p;
  p += (unsigned long) header->pattern_count * FELF_AUDIT_NAME_SIZE;
  felf_terminate (p, header->libpath_count, FELF_AUDIT_DIR_SIZE);
  index_libpath = p;
  p += (unsigned long) header->libpath_count * FELF_AUDIT_DIR_SIZE;
  felf_terminate (p, header->default_count, FELF_AUDIT_DIR_SIZE);
  index_defaults = p;

  header->root[FELF_AUDIT_ROOT_SIZE - 1] = '\0';
  root_len = felf_strlen (header->root);
  /* "/" as a root would allow everything; treat it as no root. */
  if (root_len == 1)
    root_len = 0;
  index_strict = (header->flags & FELF_AUDIT_FLAG_STRICT) != 0;
  index_debug = (header->flags & FELF_AUDIT_FLAG_DEBUG) != 0;
  index_valid = 1;
}

static const felf_audit_index_header_t *
felf_header (void)
{
  return (const felf_audit_index_header_t *) index_buffer;
}

static const felf_audit_index_entry_t *
felf_index_lookup (const char *name)
{
  if (!index_valid)
    return 0;
  const char *base = felf_basename (name);
  for (felf_uint32_t i = 0; i < felf_header ()->entry_count; i++)
    {
      const felf_audit_index_entry_t *entry = &index_entries[i];
      int terminated = 0;
      for (unsigned int j = 0; j < FELF_AUDIT_PATH_SIZE; j++)
        if (entry->path[j] == '\0')
          {
            terminated = 1;
            break;
          }
      if (!terminated)
        continue;
      /* An absolute or search-path candidate is matched by basename, so the
         bundled object wins even when the caller names a build-store path or
         a tree copy of the same library. */
      if (felf_strcmp (entry->name, base) == 0)
        return entry;
    }
  return 0;
}

static int
felf_under_root (const char *name)
{
  if (!index_valid || root_len == 0)
    return 0;
  if (!felf_has_prefix (name, felf_header ()->root))
    return 0;
  return name[root_len] == '/';
}

/* Like felf_under_root(), but for a path that names the tree through a
   symlink or "..": the bundle root is canonical, and callers such as the GCC
   driver deliberately do not resolve links.  The kernel resolves the path:
   open it with O_PATH and read /proc/self/fd/<fd>. */
static int
felf_under_root_resolved (const char *name)
{
  if (felf_under_root (name))
    return 1;
  if (!index_valid || root_len == 0 || name[0] != '/')
    return 0;
  int fd = felf_open (name, FELF_O_PATH);
  if (fd < 0)
    return 0;
  char link[32] = "/proc/self/fd/";
  char digits[12];
  int nd = 0;
  for (unsigned int v = (unsigned int) fd; nd == 0 || v != 0; v /= 10)
    digits[nd++] = (char) ('0' + v % 10);
  int pos = 14;
  while (nd > 0)
    link[pos++] = digits[--nd];
  link[pos] = '\0';
  static char resolved[4096];
  long n = felf_syscall6 (FELF_NR_READLINKAT, FELF_AT_FDCWD, (long) link,
                          (long) resolved, sizeof (resolved) - 1, 0, 0);
  felf_close (fd);
  if (n <= 0)
    return 0;
  resolved[n] = '\0';
  return felf_under_root (resolved);
}

static int
felf_host_pattern (const char *base)
{
  for (felf_uint32_t i = 0; i < felf_header ()->pattern_count; i++)
    if (felf_host_pattern_match (index_patterns
                                     + (unsigned long) i * FELF_AUDIT_NAME_SIZE,
                                 base))
      return 1;
  return 0;
}

/* Nonzero when PATH is a little-endian ELF64 file for this architecture. */
static int
felf_elf64_ok (const char *path)
{
  int fd = felf_open (path, 0);
  if (fd < 0)
    return 0;
  felf_uint8_t ident[20];
  long n = felf_read (fd, ident, sizeof (ident));
  felf_close (fd);
  if (n != (long) sizeof (ident))
    return 0;
  felf_uint16_t machine = (felf_uint16_t) (ident[18] | (ident[19] << 8));
  return ident[0] == 0x7f && ident[1] == 'E' && ident[2] == 'L'
         && ident[3] == 'F' && ident[4] == 2 && ident[5] == 1
         && machine == FELF_HOST_MACHINE;
}

/* Keep PATH for la_objopen() and return the stored copy. */
static const char *
felf_remember (const char *path)
{
  for (unsigned int i = 0; i < host_path_count; i++)
    if (felf_strcmp (host_paths[i], path) == 0)
      return host_paths[i];
  if (host_path_count == FELF_HOST_PATH_SLOTS)
    {
      /* Table full: the object loads but is not tagged as a host object. */
      if (path != host_scratch)
        {
          unsigned long n = felf_strlen (path);
          for (unsigned long i = 0; i <= n; i++)
            host_scratch[i] = path[i];
        }
      return host_scratch;
    }
  char *slot = host_paths[host_path_count++];
  unsigned long n = felf_strlen (path);
  for (unsigned long i = 0; i <= n; i++)
    slot[i] = path[i];
  return slot;
}

/* DIR + "/" + NAME into host_scratch.  Returns 0 if it does not fit. */
static int
felf_join (const char *dir, const char *name)
{
  unsigned long dl = felf_strlen (dir);
  unsigned long nl = felf_strlen (name);
  if (dl + 1 + nl + 1 > sizeof (host_scratch))
    return 0;
  for (unsigned long i = 0; i < dl; i++)
    host_scratch[i] = dir[i];
  host_scratch[dl] = '/';
  for (unsigned long i = 0; i <= nl; i++)
    host_scratch[dl + 1 + i] = name[i];
  return 1;
}

static const char *
felf_search_dirs (const char *dirs, felf_uint32_t count, const char *name)
{
  for (felf_uint32_t i = 0; i < count; i++)
    if (felf_join (dirs + (unsigned long) i * FELF_AUDIT_DIR_SIZE, name)
        && felf_elf64_ok (host_scratch))
      return felf_remember (host_scratch);
  return 0;
}

/* Map /etc/ld.so.cache (glibc "glibc-ld.so.cache1.1" format, optionally
 * behind the old "ld.so-1.7.0" header). */
static void
felf_cache_init (void)
{
  if (cache_initialized)
    return;
  cache_initialized = 1;
  int fd = felf_open ("/etc/ld.so.cache", 0);
  if (fd < 0)
    return;
  long size = felf_syscall3 (FELF_NR_LSEEK, fd, 0, 2); /* SEEK_END */
  if (size < 48)
    {
      felf_close (fd);
      return;
    }
  /* mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0) */
  long map = felf_syscall6 (FELF_NR_MMAP, 0, size, 1, 2, fd, 0);
  felf_close (fd);
  if (map < 0 && map > -4096)
    return;
  const char *base = (const char *) map;
  unsigned long off = 0;
  if (felf_memcmp (base, "ld.so-1.7.0", 11) == 0)
    {
      felf_uint32_t old_n = *(const felf_uint32_t *) (base + 12);
      off = 16 + (unsigned long) old_n * 12;
      off = (off + 7) & ~7ul;
    }
  if (off > (unsigned long) size || (unsigned long) size - off < 48
      || felf_memcmp (base + off, "glibc-ld.so.cache1.1", 20) != 0)
    return;
  felf_uint32_t n = *(const felf_uint32_t *) (base + off + 20);
  unsigned long avail = (unsigned long) size - off;
  if ((unsigned long) n > (avail - 48) / 24)
    return;
  cache_data = base + off;
  cache_size = avail;
  cache_nlibs = n;
}

/* Compare NAME with the NUL-terminated cache string at OFF. */
static int
felf_cache_str_eq (felf_uint32_t off, const char *name)
{
  for (unsigned long i = off;; i++, name++)
    {
      if (i >= cache_size)
        return 0;
      if (cache_data[i] != *name)
        return 0;
      if (*name == '\0')
        return 1;
    }
}

static const char *
felf_cache_lookup (const char *name)
{
  felf_cache_init ();
  for (felf_uint32_t i = 0; i < cache_nlibs; i++)
    {
      const felf_uint8_t *e =
          (const felf_uint8_t *) cache_data + 48 + (unsigned long) i * 24;
      felf_uint32_t flags = *(const felf_uint32_t *) e;
      felf_uint32_t key = *(const felf_uint32_t *) (e + 4);
      felf_uint32_t value = *(const felf_uint32_t *) (e + 8);
      felf_uint64_t hwcap = *(const felf_uint64_t *) (e + 16);
      /* A 64-bit glibc library for this architecture, baseline (no
         glibc-hwcaps). */
      if (flags != FELF_HOST_LDCACHE_FLAGS || hwcap != 0 || value >= cache_size)
        continue;
      if (!felf_cache_str_eq (key, name))
        continue;
      unsigned long len = 0;
      while (value + len < cache_size && cache_data[value + len] != '\0')
        len++;
      if (value + len >= cache_size || len + 1 > sizeof (host_scratch))
        continue;
      for (unsigned long j = 0; j <= len; j++)
        host_scratch[j] = cache_data[value + j];
      if (felf_elf64_ok (host_scratch))
        return felf_remember (host_scratch);
    }
  return 0;
}

/* Find NAME on the host: LD_LIBRARY_PATH, ld.so.cache, default dirs. */
static const char *
felf_host_find (const char *name)
{
  const felf_audit_index_header_t *h = felf_header ();
  const char *path = felf_search_dirs (index_libpath, h->libpath_count, name);
  if (path == 0)
    path = felf_cache_lookup (name);
  if (path == 0)
    path = felf_search_dirs (index_defaults, h->default_count, name);
  return path;
}

static int
felf_is_host_object (const char *path)
{
  if (path[0] == '\0' || felf_has_prefix (path, "/proc/self/fd/"))
    return 0;
  /* Host objects have absolute paths; a library mapped from the FELF file
     keeps the name it was requested by. */
  if (felf_basename (path) == path)
    return 0;
  for (unsigned int i = 0; i < host_path_count; i++)
    if (felf_strcmp (host_paths[i], path) == 0)
      return 1;
  return !felf_under_root (path) && felf_host_pattern (felf_basename (path));
}

unsigned int
la_version (unsigned int version)
{
  felf_index_init ();
  return version;
}

unsigned int
la_objopen (struct felf_link_map *map, long lmid, felf_uintptr_t *cookie)
{
  (void) lmid;
  felf_index_init ();
  if (index_valid && map != 0 && map->l_name != 0 && cookie != 0
      && felf_is_host_object (map->l_name))
    {
      *cookie |= FELF_COOKIE_HOST;
      felf_log ("host object", map->l_name, 0);
    }
  /* No symbol binding callbacks. */
  return 0;
}

const char *
la_objsearch (const char *name, felf_uintptr_t *cookie, unsigned int code)
{
  felf_index_init ();
  if (name == 0)
    return 0;

  /* The inherited /proc/self/fd/ namespace holds the bundled objects.  This
     covers LD_PRELOAD, LD_AUDIT, and the paths returned below. */
  if (felf_has_prefix (name, "/proc/self/fd/"))
    return name;

  if (!index_valid)
    {
      felf_log ("deny (no index)", name, 0);
      return 0;
    }

  const char *base = felf_basename (name);
  int has_slash = base != name;
  int host_pattern = felf_host_pattern (base);
  int from_host = cookie != 0 && (*cookie & FELF_COOKIE_HOST) != 0;

  if (host_pattern)
    {
      if (code == LA_SER_ORIG && !has_slash)
        {
          const char *path = felf_host_find (base);
          if (path != 0)
            {
              felf_log ("host", name, path);
              return path;
            }
        }
      else if (has_slash)
        {
          /* An explicit host path or a search candidate: never swap it for
             the bundled fallback. */
          felf_log ("host", name, 0);
          return name;
        }
    }

  const felf_audit_index_entry_t *entry = felf_index_lookup (name);
  if (entry != 0)
    {
      felf_log (host_pattern ? "bundled (host fallback)" : "bundled", name,
                entry->path);
      return entry->path;
    }

  if (felf_under_root_resolved (name))
    {
      felf_log ("tree", name, 0);
      return name;
    }

  if (index_strict && !host_pattern && !from_host)
    {
      felf_log ("deny", name, 0);
      return 0;
    }

  /* The private loader's default directories belong to the bundled glibc;
     search the host's instead. */
  if (code == LA_SER_CONFIG || code == LA_SER_DEFAULT)
    {
      const char *path = felf_host_find (base);
      if (path != 0)
        {
          felf_log ("host", name, path);
          return path;
        }
    }
  if (code == LA_SER_ORIG)
    felf_log ("search", name, 0);
  return name;
}
