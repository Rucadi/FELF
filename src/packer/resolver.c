#include "resolver.h"
#include "felf_format.h"
#include "elf_parser.h"
#include "felf_file.h"
#include "felf_host_libs.h"

#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return felf_strdup(slash ? slash + 1 : path);
}

static char *path_dirname(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL)
        return felf_strdup(".");
    if (slash == path)
        return felf_strdup("/");
    size_t n = (size_t)(slash - path);
    char *p = malloc(n + 1);
    if (p == NULL)
        return NULL;
    memcpy(p, path, n);
    p[n] = '\0';
    return p;
}

static int is_regular_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int add_string(char ***array, size_t *count, size_t *cap,
                      const char *value)
{
    for (size_t i = 0; i < *count; i++) {
        if (strcmp((*array)[i], value) == 0)
            return 0;
    }
    if (*count == *cap) {
        size_t newcap = *cap ? *cap * 2 : 8;
        char **p = realloc(*array, newcap * sizeof(**array));
        if (p == NULL)
            return -1;
        *array = p;
        *cap = newcap;
    }
    char *copy = felf_strdup(value);
    if (copy == NULL)
        return -1;
    (*array)[(*count)++] = copy;
    return 0;
}

void felf_resolver_init(felf_resolver_t *r)
{
    memset(r, 0, sizeof(*r));
}

static void object_free(felf_object_t *o)
{
    free(o->name);
    free(o->soname);
    free(o->path);
    free(o->kind);
    free(o->namespace_name);
    free(o->data);
    free(o->zdata);
    free(o->rpath);
    free(o->runpath);
    for (size_t i = 0; i < o->verdef_count; i++)
        free(o->verdefs[i]);
    free(o->verdefs);
    for (size_t i = 0; i < o->verneed_count; i++) {
        free(o->verneed_files[i]);
        free(o->verneed_versions[i]);
    }
    free(o->verneed_files);
    free(o->verneed_versions);
    for (size_t i = 0; i < o->alias_count; i++)
        free(o->aliases[i]);
    free(o->aliases);
    for (size_t i = 0; i < o->dep_count; i++)
        free(o->deps[i]);
    free(o->deps);
    memset(o, 0, sizeof(*o));
}

void felf_resolver_free(felf_resolver_t *r)
{
    for (size_t i = 0; i < r->count; i++)
        object_free(&r->objects[i]);
    free(r->objects);
    for (size_t i = 0; i < r->search_count; i++)
        free(r->search_dirs[i]);
    free(r->search_dirs);
    free(r->root);
    free(r->program_interp);
    free(r->default_program);
    free(r->program_locations);
    for (size_t i = 0; i < r->env_count; i++)
        free(r->env[i]);
    free(r->env);
    for (size_t i = 0; i < r->host_lib_count; i++)
        free(r->host_libs[i]);
    free(r->host_libs);
    memset(r, 0, sizeof(*r));
}

int felf_resolver_add_search_dir(felf_resolver_t *r, const char *dir,
                                 char *err, size_t errlen)
{
    if (dir == NULL || dir[0] == '\0')
        return 0;
    if (add_string(&r->search_dirs, &r->search_count, &r->search_cap,
                   dir) != 0) {
        felf_set_error(err, errlen, "out of memory adding search directory");
        return -1;
    }
    return 0;
}

static felf_object_t *find_by_path(felf_resolver_t *r, const char *path)
{
    for (size_t i = 0; i < r->count; i++) {
        if (strcmp(r->objects[i].path, path) == 0)
            return &r->objects[i];
    }
    return NULL;
}

felf_object_t *felf_resolver_find(felf_resolver_t *r, const char *name)
{
    for (size_t i = 0; i < r->count; i++) {
        felf_object_t *o = &r->objects[i];
        if (strcmp(o->name, name) == 0)
            return o;
        if (o->soname != NULL && strcmp(o->soname, name) == 0)
            return o;
        for (size_t j = 0; j < o->alias_count; j++) {
            if (strcmp(o->aliases[j], name) == 0)
                return o;
        }
    }
    return NULL;
}

felf_object_t *felf_resolver_program(felf_resolver_t *r)
{
    felf_object_t *first = NULL;
    for (size_t i = 0; i < r->count; i++) {
        if (strcmp(r->objects[i].kind, "program") != 0)
            continue;
        if (r->default_program != NULL &&
            strcmp(r->objects[i].name, r->default_program) == 0)
            return &r->objects[i];
        if (first == NULL)
            first = &r->objects[i];
    }
    return first;
}

felf_object_t *felf_resolver_interp(felf_resolver_t *r)
{
    for (size_t i = 0; i < r->count; i++)
        if (strcmp(r->objects[i].kind, "interp") == 0)
            return &r->objects[i];
    return NULL;
}

static int add_alias(felf_object_t *o, const char *alias)
{
    if (alias == NULL || alias[0] == '\0')
        return 0;
    if (strcmp(o->name, alias) == 0)
        return 0;
    if (o->soname != NULL && strcmp(o->soname, alias) == 0)
        return 0;
    for (size_t i = 0; i < o->alias_count; i++)
        if (strcmp(o->aliases[i], alias) == 0)
            return 0;
    char **p = realloc(o->aliases, (o->alias_count + 1) * sizeof(*p));
    if (p == NULL)
        return -1;
    o->aliases = p;
    o->aliases[o->alias_count] = felf_strdup(alias);
    if (o->aliases[o->alias_count] == NULL)
        return -1;
    o->alias_count++;
    return 0;
}

/* Add the object in SOURCE, known as USE_PATH (and asked for as PATH).  If
 * an object with that path is already present, add the requested alias and
 * return it instead. */
static felf_object_t *add_object_from(felf_resolver_t *r,
                                      const felf_source_t *source,
                                      const char *use_path, const char *path,
                                      const char *kind,
                                      const char *requested_name,
                                      char *err, size_t errlen)
{
    felf_object_t *existing = find_by_path(r, use_path);
    if (existing != NULL) {
        /* An object kept outside the container that something embedded
         * depends on must be embedded after all. */
        if (strcmp(existing->kind, "external") == 0 &&
            strcmp(kind, "external") != 0) {
            char *k = felf_strdup(kind);
            if (k == NULL) {
                felf_set_error(err, errlen, "out of memory");
                return NULL;
            }
            free(existing->kind);
            existing->kind = k;
        }
        if (requested_name != NULL &&
            add_alias(existing, requested_name) != 0) {
            felf_set_error(err, errlen, "out of memory adding alias");
            return NULL;
        }
        return existing;
    }

    felf_elf_t elf;
    if (felf_elf_load_source(source, use_path, &elf, err, errlen) != 0)
        return NULL;
    if (r->arch != NULL && elf.ehdr.e_machine != r->arch->machine) {
        felf_set_error(err, errlen, "%s is not built for %s", use_path,
                       r->arch->name);
        felf_elf_free(&elf);
        return NULL;
    }

    if (r->count == r->cap) {
        size_t newcap = r->cap ? r->cap * 2 : 16;
        felf_object_t *p = realloc(r->objects, newcap * sizeof(*p));
        if (p == NULL) {
            felf_elf_free(&elf);
            felf_set_error(err, errlen, "out of memory adding object");
            return NULL;
        }
        r->objects = p;
        r->cap = newcap;
    }

    felf_object_t *o = &r->objects[r->count];
    memset(o, 0, sizeof(*o));
    o->path = felf_strdup(use_path);
    o->kind = felf_strdup(kind);
    o->namespace_name = felf_strdup("default");
    o->soname = felf_strdup(elf.soname);
    o->rpath = felf_strdup(elf.rpath);
    o->runpath = felf_strdup(elf.runpath);
    char *base = path_basename(use_path);
    if (strcmp(kind, "program") == 0 || strcmp(kind, "interp") == 0 ||
        strcmp(kind, "audit") == 0) {
        o->name = base;
        base = NULL;
    } else if (elf.soname != NULL) {
        o->name = felf_strdup(elf.soname);
    } else {
        o->name = base;
        base = NULL;
    }
    free(base);
    if (o->path == NULL || o->kind == NULL || o->namespace_name == NULL ||
        o->name == NULL) {
        object_free(o);
        felf_elf_free(&elf);
        felf_set_error(err, errlen, "out of memory adding object");
        return NULL;
    }
    o->size = elf.size;
    o->data = elf.data;
    elf.data = NULL;
    o->alignment = elf.max_align;
    /* The version lists move to the object. */
    o->verdefs = elf.verdefs;
    o->verdef_count = elf.verdef_count;
    o->verneed_files = elf.verneed_files;
    o->verneed_versions = elf.verneed_versions;
    o->verneed_count = elf.verneed_count;
    elf.verdefs = elf.verneed_files = elf.verneed_versions = NULL;
    elf.verdef_count = elf.verneed_count = 0;
    felf_sha256(o->data, o->size, o->sha256);
    for (size_t i = 0; i < elf.needed_count; i++) {
        if (add_string(&o->deps, &o->dep_count, &o->dep_cap,
                       elf.needed[i]) != 0) {
            object_free(o);
            felf_elf_free(&elf);
            felf_set_error(err, errlen, "out of memory adding dependency");
            return NULL;
        }
    }
    if (requested_name != NULL && add_alias(o, requested_name) != 0) {
        object_free(o);
        felf_elf_free(&elf);
        felf_set_error(err, errlen, "out of memory adding alias");
        return NULL;
    }
    add_alias(o, o->name);
    add_alias(o, o->soname);
    char *base_alias = path_basename(use_path);
    add_alias(o, base_alias);
    free(base_alias);
    char *original_base = path_basename(path);
    add_alias(o, original_base);
    free(original_base);
    felf_elf_free(&elf);
    r->count++;
    return o;
}

/* Add the file at PATH, known by its canonical path. */
static felf_object_t *add_object(felf_resolver_t *r, const char *path,
                                 const char *kind, const char *requested_name,
                                 char *err, size_t errlen)
{
    char canonical[PATH_MAX];
    const char *use_path = realpath(path, canonical) ? canonical : path;
    felf_source_t source = {.path = use_path};
    return add_object_from(r, &source, use_path, path, kind, requested_name,
                           err, errlen);
}

/* Add SOURCE: a file, or a piece built into felf, whose path is then
 * "<felf>/NAME" (no file has it). */
static felf_object_t *add_source(felf_resolver_t *r,
                                 const felf_source_t *source,
                                 const char *kind, const char *requested_name,
                                 char *err, size_t errlen)
{
    if (source->path != NULL)
        return add_object(r, source->path, kind, requested_name, err, errlen);
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "<felf>/%s", source->name);
    return add_object_from(r, source, path, path, kind, requested_name,
                           err, errlen);
}

/* The file NAME of FELF's glibc, when it is an ELF object for the target
 * (as is_candidate() asks of a file in a search directory). */
static const felf_source_t *glibc_file(const felf_resolver_t *r,
                                       const char *name)
{
    for (size_t i = 0; i < r->glibc_count; i++) {
        const felf_source_t *s = &r->glibc[i];
        if (strcmp(s->name, name) != 0)
            continue;
        const uint8_t *d = s->data;
        return s->size >= sizeof(Elf64_Ehdr) &&
               memcmp(d, ELFMAG, SELFMAG) == 0 &&
               d[EI_CLASS] == ELFCLASS64 && d[EI_DATA] == ELFDATA2LSB &&
               (d[18] | d[19] << 8) == r->arch->machine ? s : NULL;
    }
    return NULL;
}

/* Nonzero when DIR/NAME is a loadable ELF object for the target; the path
 * is left in OUT_PATH.  Anything else in a search directory (a 32-bit or
 * foreign-architecture library, a linker script) is not a match. */
static int is_candidate(const felf_resolver_t *r, const char *dir,
                        const char *name, char *out_path, size_t out_len)
{
    int n = snprintf(out_path, out_len, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= out_len || !is_regular_file(out_path))
        return 0;
    char ignored[256];
    felf_elf_t check;
    if (felf_elf_load(out_path, &check, ignored, sizeof(ignored)) != 0)
        return 0;
    int match = check.ehdr.e_machine == r->arch->machine;
    felf_elf_free(&check);
    return match;
}

/* Expand $ORIGIN, $LIB, and $PLATFORM (also as ${...}) in a search path
 * component. */
static char *expand_token(const char *component, const char *requester_dir,
                          const char *platform)
{
    const struct {
        const char *token;
        const char *value;
    } tokens[] = {
        {"$ORIGIN", requester_dir}, {"${ORIGIN}", requester_dir},
        {"$LIB", "lib"},            {"${LIB}", "lib"},
        {"$PLATFORM", platform},     {"${PLATFORM}", platform},
    };
    size_t cap = strlen(component) + 1, len = 0;
    char *out = malloc(cap);
    for (const char *p = component; out != NULL && *p != '\0';) {
        const char *rep = NULL;
        size_t token_len = 1;
        for (size_t t = 0; t < sizeof(tokens) / sizeof(tokens[0]); t++) {
            size_t n = strlen(tokens[t].token);
            if (strncmp(p, tokens[t].token, n) == 0) {
                rep = tokens[t].value;
                token_len = n;
                break;
            }
        }
        size_t rep_len = rep ? strlen(rep) : 1;
        if (len + rep_len + 1 > cap) {
            cap = (len + rep_len + 1) * 2;
            char *q = realloc(out, cap);
            if (q == NULL)
                free(out);
            out = q;
            if (out == NULL)
                break;
        }
        memcpy(out + len, rep ? rep : p, rep_len);
        len += rep_len;
        p += token_len;
    }
    if (out != NULL)
        out[len] = '\0';
    return out;
}

static int search_requester_paths(felf_resolver_t *r, const char *name,
                                  const char *requester_path,
                                  const char *rpath, const char *runpath,
                                  char *out_path, size_t out_len)
{
    const char *path = runpath != NULL ? runpath : rpath;
    if (path == NULL || path[0] == '\0')
        return 0;
    char *requester_dir = path_dirname(requester_path);
    char *copy = felf_strdup(path);
    int found = 0;
    char *save = NULL;
    for (char *comp = requester_dir && copy ? strtok_r(copy, ":", &save)
                                            : NULL;
         comp != NULL && !found; comp = strtok_r(NULL, ":", &save)) {
        char *dir = expand_token(comp, requester_dir, r->arch->name);
        if (dir == NULL)
            continue;
        found = is_candidate(r, dir, name, out_path, out_len);
        free(dir);
    }
    free(copy);
    free(requester_dir);
    return found;
}

static int search_dirs(felf_resolver_t *r, const char *name,
                       char *out_path, size_t out_len)
{
    for (size_t i = 0; i < r->search_count; i++)
        if (is_candidate(r, r->search_dirs[i], name, out_path, out_len))
            return 1;
    return 0;
}

/* Add the standard directory set: the interpreter's directory and the
 * conventional host directories. */
static int add_standard_dirs(felf_resolver_t *r, const char *interp,
                             char *err, size_t errlen)
{
    if (interp != NULL && interp[0] == '/') {
        char *dir = path_dirname(interp);
        if (dir != NULL) {
            if (felf_resolver_add_search_dir(r, dir, err, errlen) != 0) {
                free(dir);
                return -1;
            }
            char parent[PATH_MAX];
            snprintf(parent, sizeof(parent), "%s/../lib", dir);
            char resolved[PATH_MAX];
            if (realpath(parent, resolved) != NULL)
                felf_resolver_add_search_dir(r, resolved, err, errlen);
            free(dir);
        }
    }
    static const char *const dirs[] = {
        "/lib", "/lib64", "/usr/lib", "/usr/lib64",
        "/lib/%s", "/usr/lib/%s",
        "/usr/local/lib", "/usr/local/lib64"
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        char dir[PATH_MAX];
        /* %s: the multiarch directory (x86_64-linux-gnu, ...). */
        snprintf(dir, sizeof(dir), dirs[i], r->arch->multiarch);
        if (felf_resolver_add_search_dir(r, dir, err, errlen) != 0)
            return -1;
    }
    return 0;
}

static int resolve_name(felf_resolver_t *r, felf_object_t *requester,
                        const char *name, char *err, size_t errlen)
{
    felf_object_t *known = felf_resolver_find(r, name);
    if (known != NULL) {
        if (strcmp(known->kind, "external") == 0) {
            char *k = felf_strdup("shared");
            if (k == NULL) {
                felf_set_error(err, errlen, "out of memory");
                return -1;
            }
            free(known->kind);
            known->kind = k;
        }
        return 0;
    }

    char path[PATH_MAX];
    path[0] = '\0';
    const felf_source_t *glibc = NULL;

    if (strchr(name, '/') != NULL) {
        if (name[0] == '/') {
            snprintf(path, sizeof(path), "%s", name);
        } else {
            char *dir = path_dirname(requester->path);
            if (dir != NULL) {
                snprintf(path, sizeof(path), "%s/%s", dir, name);
                free(dir);
            }
        }
        if (!is_regular_file(path)) {
            felf_set_error(err, errlen, "cannot resolve dependency %s", name);
            return -1;
        }
    } else {
        /* glibc's libraries come from FELF's glibc, whatever the
         * requester's RUNPATH says. */
        if ((r->direct_map && (glibc = glibc_file(r, name)) != NULL) ||
            search_requester_paths(r, name, requester->path,
                                   requester->rpath, requester->runpath,
                                   path, sizeof(path)) ||
            search_dirs(r, name, path, sizeof(path))) {
            /* found: added below */
        } else if (felf_resolver_is_host_lib(r, name)) {
            /* Host-preferred: the runtime finds it on the host.  Nothing
             * to embed as a fallback. */
            return 0;
        } else {
            felf_set_error(err, errlen,
                           "cannot resolve dependency %s (needed by %s)",
                           name, requester->name);
            return -1;
        }
    }

    felf_object_t *o =
        glibc != NULL ? add_source(r, glibc, "shared", name, err, errlen)
                      : add_object(r, path, "shared", name, err, errlen);
    if (o == NULL)
        return -1;
    if (add_alias(o, name) != 0) {
        felf_set_error(err, errlen, "out of memory adding alias");
        return -1;
    }
    return 0;
}

static int add_program_object(felf_resolver_t *r, const char *path,
                              const char *program_name,
                              char *err, size_t errlen);

int felf_resolver_add_program_alias(felf_resolver_t *r, const char *program,
                                    const char *name, char *err,
                                    size_t errlen)
{
    felf_object_t *o = felf_resolver_find(r, program);
    if (o == NULL || strcmp(o->kind, "program") != 0) {
        felf_set_error(err, errlen, "--alias: no program named %s", program);
        return -1;
    }
    if (name[0] == '\0' || strchr(name, '/') != NULL) {
        felf_set_error(err, errlen, "invalid program name '%s'", name);
        return -1;
    }
    felf_object_t *known = felf_resolver_find(r, name);
    if (known != NULL && known != o) {
        felf_set_error(err, errlen, "a program or object named %s already "
                       "exists in the container", name);
        return -1;
    }
    if (add_alias(o, name) != 0) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    return 0;
}

int felf_resolver_add_program(felf_resolver_t *r, const char *path,
                              const char *name, char *err, size_t errlen)
{
    char *base = path_basename(path);
    if (base == NULL) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    char *program_name = felf_strdup(name != NULL ? name : base);
    free(base);
    if (program_name == NULL) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    if (program_name[0] == '\0' || strchr(program_name, '/') != NULL) {
        felf_set_error(err, errlen, "invalid program name '%s'", program_name);
        free(program_name);
        return -1;
    }
    for (size_t i = 0; i < r->count; i++) {
        if (strcmp(r->objects[i].name, program_name) == 0) {
            felf_set_error(err, errlen, "a program or object named %s already "
                           "exists in the container", program_name);
            free(program_name);
            return -1;
        }
    }
    /* A multicall binary (coreutils, busybox): another name for a program
     * that is already embedded becomes an alias of it.  The program then
     * runs with that name as argv[0] and dispatches on it. */
    char canonical[PATH_MAX];
    felf_object_t *same =
        find_by_path(r, realpath(path, canonical) ? canonical : path);
    if (same != NULL && strcmp(same->kind, "program") == 0) {
        felf_object_t *known = felf_resolver_find(r, program_name);
        int rc = 0;
        if (known != NULL && known != same) {
            felf_set_error(err, errlen, "a program or object named %s already "
                           "exists in the container", program_name);
            rc = -1;
        } else if (add_alias(same, program_name) != 0) {
            felf_set_error(err, errlen, "out of memory");
            rc = -1;
        }
        free(program_name);
        return rc;
    }
    int rc = add_program_object(r, path, program_name, err, errlen);
    if (rc == 0 && r->default_program == NULL) {
        r->default_program = program_name;
        program_name = NULL;
    }
    free(program_name);
    return rc;
}

/* Add the program object and, for the first program, its interpreter.  */
static int add_program_object(felf_resolver_t *r, const char *path,
                              const char *program_name,
                              char *err, size_t errlen)
{
    felf_elf_t elf;
    if (felf_elf_load(path, &elf, err, errlen) != 0)
        return -1;
    if (elf.ehdr.e_type != ET_DYN && elf.ehdr.e_type != ET_EXEC) {
        felf_set_error(err, errlen, "program is not an executable ELF");
        felf_elf_free(&elf);
        return -1;
    }
    if (elf.interp == NULL) {
        felf_set_error(err, errlen,
                       "%s has no PT_INTERP; FELF requires a dynamically linked "
                       "executable", path);
        felf_elf_free(&elf);
        return -1;
    }
    /* The bootstrap writes the loader's /proc/self/fd path over PT_INTERP
     * at every start; descriptors start at FELF_FD_BASE. */
    if (strlen(elf.interp) < strlen("/proc/self/fd/1000")) {
        felf_set_error(err, errlen,
                       "%s: PT_INTERP \"%s\" is too short for FELF to "
                       "rewrite (link with a --dynamic-linker path of at "
                       "least %zu characters)", path, elf.interp,
                       strlen("/proc/self/fd/1000"));
        felf_elf_free(&elf);
        return -1;
    }
    char interp[PATH_MAX];
    snprintf(interp, sizeof(interp), "%s", elf.interp);
    /* The first program selects the target; add_object() checks the rest. */
    if (r->arch == NULL)
        r->arch = felf_arch_find(elf.ehdr.e_machine);
    felf_elf_free(&elf);

    size_t before = r->count;
    felf_object_t *program = add_object(r, path, "program", NULL,
                                        err, errlen);
    if (program == NULL)
        return -1;
    if (r->count == before) {
        felf_set_error(err, errlen, "%s is already in the container", path);
        return -1;
    }
    char *renamed = felf_strdup(program_name);
    if (renamed == NULL) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    free(program->name);
    program->name = renamed;

    /* FELF's glibc, when it has a loader for this architecture, replaces
     * every program's own: its loader is bundled, glibc's libraries are
     * taken from it (resolve_name), and the loader maps the uncompressed
     * libraries from the file. */
    const felf_source_t *glibc_loader = glibc_file(r, r->arch->interp);
    if (glibc_loader != NULL) {
        if (r->direct_map)
            return 0;
        felf_object_t *li = add_source(r, glibc_loader, "interp", NULL,
                                       err, errlen);
        if (li == NULL)
            return -1;
        add_alias(li, "ld-linux.so.2");
        r->direct_map = 1;
        return add_standard_dirs(r, NULL, err, errlen);
    }

    /* Otherwise the program's PT_INTERP: the FELF bundles that dynamic
     * linker, and every program must use the same one. */
    if (!is_regular_file(interp)) {
        felf_set_error(err, errlen,
                       "cannot find the dynamic linker %s", interp);
        return -1;
    }
    char canonical[PATH_MAX];
    const char *use = realpath(interp, canonical) ? canonical : interp;
    if (r->program_interp != NULL) {
        if (strcmp(use, r->program_interp) != 0) {
            felf_set_error(err, errlen,
                           "program %s uses dynamic linker %s, but the container "
                           "already uses %s; all programs must share one",
                           program_name, use, r->program_interp);
            return -1;
        }
        return 0;
    }
    r->program_interp = felf_strdup(use);
    if (r->program_interp == NULL) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    felf_object_t *li = add_object(r, interp, "interp", NULL, err, errlen);
    if (li == NULL)
        return -1;
    add_alias(li, r->arch->interp);
    add_alias(li, "ld-linux.so.2");

    if (add_standard_dirs(r, interp, err, errlen) != 0)
        return -1;
    return 0;
}


int felf_resolver_add_audit(felf_resolver_t *r, const felf_source_t *source,
                            char *err, size_t errlen)
{
    return add_source(r, source, "audit", NULL, err, errlen) ? 0 : -1;
}

int felf_resolver_add_runtime(felf_resolver_t *r, const felf_source_t *source,
                              char *err, size_t errlen)
{
    return add_source(r, source, "runtime", NULL, err, errlen) ? 0 : -1;
}

int felf_resolver_add_env(felf_resolver_t *r, const char *entry,
                          char *err, size_t errlen)
{
    const char *eq = strchr(entry, '=');
    if (eq == NULL || eq == entry) {
        felf_set_error(err, errlen, "environment entry must be NAME=VALUE: %s",
                       entry);
        return -1;
    }
    /* Later entries for the same name replace earlier ones. */
    size_t name_len = (size_t)(eq - entry);
    for (size_t i = 0; i < r->env_count; i++) {
        if (strncmp(r->env[i], entry, name_len) == 0 &&
            r->env[i][name_len] == '=') {
            char *copy = felf_strdup(entry);
            if (copy == NULL) {
                felf_set_error(err, errlen, "out of memory");
                return -1;
            }
            free(r->env[i]);
            r->env[i] = copy;
            return 0;
        }
    }
    if (add_string(&r->env, &r->env_count, &r->env_cap, entry) != 0) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    return 0;
}

int felf_resolver_add_resource(felf_resolver_t *r,
                               const felf_source_t *source,
                               const char *name, char *err, size_t errlen)
{
    const char *path = source->path ? source->path : name;
    uint8_t *data;
    size_t size;
    if (felf_source_read(source, &data, &size, err, errlen) != 0)
        return -1;
    if (r->count == r->cap) {
        size_t newcap = r->cap ? r->cap * 2 : 16;
        felf_object_t *p = realloc(r->objects, newcap * sizeof(*p));
        if (p == NULL) {
            free(data);
            felf_set_error(err, errlen, "out of memory adding resource");
            return -1;
        }
        r->objects = p;
        r->cap = newcap;
    }
    felf_object_t *o = &r->objects[r->count];
    memset(o, 0, sizeof(*o));
    o->path = felf_strdup(path);
    o->kind = felf_strdup("resource");
    o->namespace_name = felf_strdup("default");
    o->name = felf_strdup(name);
    if (o->path == NULL || o->kind == NULL || o->namespace_name == NULL ||
        o->name == NULL) {
        object_free(o);
        free(data);
        felf_set_error(err, errlen, "out of memory adding resource");
        return -1;
    }
    o->data = data;
    o->size = size;
    /* Page-aligned, like every object in the payload region. */
    o->alignment = 4096;
    felf_sha256(o->data, o->size, o->sha256);
    r->count++;
    return 0;
}

int felf_resolver_add_host_lib(felf_resolver_t *r, const char *pattern,
                               char *err, size_t errlen)
{
    if (pattern == NULL || pattern[0] == '\0' || strchr(pattern, '/') ||
        strchr(pattern, '\n') ||
        (strchr(pattern, '*') && strchr(pattern, '*')[1] != '\0')) {
        felf_set_error(err, errlen,
                       "host library pattern must be a file name, optionally "
                       "ending in '*': %s", pattern ? pattern : "");
        return -1;
    }
    if (add_string(&r->host_libs, &r->host_lib_count, &r->host_lib_cap,
                   pattern) != 0) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    return 0;
}

int felf_resolver_is_host_lib(const felf_resolver_t *r, const char *name)
{
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    for (size_t i = 0; i < r->host_lib_count; i++)
        if (felf_host_pattern_match(r->host_libs[i], base))
            return 1;
    return 0;
}

static int kind_rank(const char *kind)
{
    if (strcmp(kind, "program") == 0) return 0;
    if (strcmp(kind, "interp") == 0) return 1;
    return 2;
}

static int object_order(const void *pa, const void *pb)
{
    const felf_object_t *a = pa, *b = pb;
    int ra = kind_rank(a->kind), rb = kind_rank(b->kind);
    /* Names are unique (the objects table requires it), so the order is
     * total and qsort() is deterministic. */
    return ra != rb ? ra - rb : strcmp(a->name, b->name);
}

void felf_resolver_sort(felf_resolver_t *r)
{
    if (r->count > 1)
        qsort(r->objects, r->count, sizeof(*r->objects), object_order);
}

/* Resolve the dependencies of every object from index START onwards.
 * Objects appended while resolving are processed too (worklist order). */
static int resolve_from(felf_resolver_t *r, size_t start, char *err,
                        size_t errlen)
{
    for (size_t i = start; i < r->count; i++) {
        if (strcmp(r->objects[i].kind, "audit") == 0)
            continue;
        /* resolve_name() can append objects and reallocate r->objects.
         * Copy the dependency name and re-read the object by index after
         * the call; never keep an object pointer across it. */
        size_t dep_count = r->objects[i].dep_count;
        for (size_t j = 0; j < dep_count; j++) {
            char *dep = felf_strdup(r->objects[i].deps[j]);
            if (dep == NULL) {
                felf_set_error(err, errlen, "out of memory resolving deps");
                return -1;
            }
            int rc = resolve_name(r, &r->objects[i], dep, err, errlen);
            free(dep);
            if (rc != 0)
                return -1;
        }
    }
    return 0;
}

int felf_resolver_resolve(felf_resolver_t *r, char *err, size_t errlen)
{
    return resolve_from(r, 0, err, errlen);
}

int felf_resolver_add_external(felf_resolver_t *r, const char *path,
                               char *err, size_t errlen)
{
    char canonical[PATH_MAX];
    const char *use_path = realpath(path, canonical) ? canonical : path;
    if (find_by_path(r, use_path) != NULL)
        return 0;
    /* Remember which objects were external so a failed closure can be
     * rolled back exactly: resolve_name() may promote them to shared. */
    size_t snapshot = r->count;
    unsigned char *was_external = calloc(snapshot ? snapshot : 1, 1);
    if (was_external == NULL) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    for (size_t i = 0; i < snapshot; i++)
        was_external[i] = strcmp(r->objects[i].kind, "external") == 0;

    int rc = 0;
    char local[512] = {0};
    if (add_object(r, use_path, "external", NULL, local,
                   sizeof(local)) == NULL ||
        resolve_from(r, snapshot, local, sizeof(local)) != 0) {
        for (size_t i = snapshot; i < r->count; i++)
            object_free(&r->objects[i]);
        r->count = snapshot;
        for (size_t i = 0; i < snapshot; i++) {
            if (was_external[i] &&
                strcmp(r->objects[i].kind, "external") != 0) {
                char *k = felf_strdup("external");
                if (k == NULL) {
                    free(was_external);
                    felf_set_error(err, errlen, "out of memory");
                    return -1;
                }
                free(r->objects[i].kind);
                r->objects[i].kind = k;
            }
        }
        felf_set_error(err, errlen, "%s", local);
        rc = 1;
    }
    free(was_external);
    return rc;
}

int felf_resolver_add_include(felf_resolver_t *r, const char *path,
                              char *err, size_t errlen)
{
    char canonical[PATH_MAX];
    const char *use_path = realpath(path, canonical) ? canonical : path;
    if (find_by_path(r, use_path) != NULL)
        return 0;
    felf_elf_t elf;
    if (felf_elf_load(use_path, &elf, err, errlen) != 0)
        return 1;
    /* A shared library: not an executable that happens to be PIE. */
    int usable = elf.ehdr.e_type == ET_DYN &&
                 elf.ehdr.e_machine == r->arch->machine &&
                 (elf.soname != NULL || elf.interp == NULL);
    char *name = NULL;
    if (usable)
        name = elf.soname ? felf_strdup(elf.soname) : path_basename(use_path);
    felf_elf_free(&elf);
    if (!usable) {
        felf_set_error(err, errlen, "not a shared library for %s",
                       r->arch->name);
        return 1;
    }
    if (name == NULL) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    int taken = felf_resolver_find(r, name) != NULL;
    free(name);
    if (taken)
        return 0;

    int rc = felf_resolver_add_external(r, use_path, err, errlen);
    if (rc != 0)
        return rc;
    felf_object_t *o = find_by_path(r, use_path);
    if (o != NULL && strcmp(o->kind, "external") == 0) {
        char *k = felf_strdup("plugin");
        if (k == NULL) {
            felf_set_error(err, errlen, "out of memory");
            return -1;
        }
        free(o->kind);
        o->kind = k;
    }
    return 0;
}

int felf_resolver_check_versions(felf_resolver_t *r, char *err, size_t errlen)
{
    for (size_t i = 0; i < r->count; i++) {
        const felf_object_t *o = &r->objects[i];
        if (strcmp(o->kind, "external") == 0)
            continue;
        for (size_t j = 0; j < o->verneed_count; j++) {
            const felf_object_t *t = felf_resolver_find(r, o->verneed_files[j]);
            /* Not bundled (a host library), or without version information
             * (glibc's loader only warns then). */
            if (t == NULL || strcmp(t->kind, "external") == 0 ||
                t->verdef_count == 0)
                continue;
            int found = 0;
            for (size_t k = 0; k < t->verdef_count && !found; k++)
                found = strcmp(t->verdefs[k], o->verneed_versions[j]) == 0;
            if (!found) {
                felf_set_error(err, errlen,
                               "%s needs version %s of %s, which the bundled "
                               "%s (%s) does not define",
                               o->path, o->verneed_versions[j],
                               o->verneed_files[j], t->name, t->path);
                return -1;
            }
        }
    }
    return 0;
}

void felf_resolver_drop_external(felf_resolver_t *r)
{
    size_t out = 0;
    for (size_t i = 0; i < r->count; i++) {
        if (strcmp(r->objects[i].kind, "external") == 0) {
            object_free(&r->objects[i]);
            continue;
        }
        if (out != i)
            r->objects[out] = r->objects[i];
        out++;
    }
    r->count = out;
}
