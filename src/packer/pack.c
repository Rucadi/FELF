#define _GNU_SOURCE
#include "pack.h"
#include "elf_parser.h"
#include "felf_arch.h"
#include "felf_file.h"
#include "felf_format.h"
#include "felf_host_libs.h"
#include "felf_loader_protocol.h"
#include "resolver.h"
#include "store_build.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zstd.h>

#ifndef FELF_VERSION_STRING
#define FELF_VERSION_STRING "0.2.0"
#endif

/* zstd-compress every object at LEVEL, except, with KEEP_LIBRARIES, the
 * libraries FELF's loader maps straight from the file: those stay
 * uncompressed, and the objects that become memfds are compressed.  Each
 * object is compressed by one single-threaded context, so the output does
 * not depend on the machine; objects are spread over worker threads.  An
 * object whose compressed form is not smaller stays uncompressed. */
typedef struct {
    felf_resolver_t *r;
    int level;
    int keep_libraries;
    size_t next;
    int failed;
    pthread_mutex_t lock;
} compress_job_t;

static void *compress_worker(void *arg)
{
    compress_job_t *job = arg;
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (cctx == NULL) {
        pthread_mutex_lock(&job->lock);
        job->failed = 1;
        pthread_mutex_unlock(&job->lock);
        return NULL;
    }
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, job->level);
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0);
    for (;;) {
        pthread_mutex_lock(&job->lock);
        size_t i = job->next++;
        int stop = job->failed || i >= job->r->count;
        pthread_mutex_unlock(&job->lock);
        if (stop)
            break;
        felf_object_t *o = &job->r->objects[i];
        if (job->keep_libraries &&
            (strcmp(o->kind, "shared") == 0 || strcmp(o->kind, "plugin") == 0))
            continue;
        /* One frame per FELF_ZSTD_CHUNK bytes. */
        size_t chunks = (o->size + FELF_ZSTD_CHUNK - 1) / FELF_ZSTD_CHUNK;
        size_t bound = chunks * ZSTD_compressBound(FELF_ZSTD_CHUNK);
        uint8_t *buf = malloc(bound);
        size_t n = 0;
        int bad = buf == NULL;
        for (size_t off = 0; !bad && off < o->size; off += FELF_ZSTD_CHUNK) {
            size_t len = o->size - off < FELF_ZSTD_CHUNK ? o->size - off
                                                          : FELF_ZSTD_CHUNK;
            size_t m = ZSTD_compress2(cctx, buf + n, bound - n,
                                      o->data + off, len);
            if (ZSTD_isError(m))
                bad = 1;
            else
                n += m;
        }
        if (bad) {
            free(buf);
            pthread_mutex_lock(&job->lock);
            job->failed = 1;
            pthread_mutex_unlock(&job->lock);
            break;
        }
        if (n < o->size) {
            uint8_t *shrunk = realloc(buf, n);
            o->zdata = shrunk ? shrunk : buf;
            o->zsize = n;
        } else {
            free(buf);
        }
    }
    ZSTD_freeCCtx(cctx);
    return NULL;
}

static int compress_objects(felf_resolver_t *r, int level,
                            int keep_libraries, char *err,
                            size_t errlen)
{
    compress_job_t job = {.r = r, .level = level,
                          .keep_libraries = keep_libraries};
    pthread_mutex_init(&job.lock, NULL);
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    size_t nthreads = cpus > 0 ? (size_t)cpus : 1;
    if (nthreads > 32)
        nthreads = 32;
    if (nthreads > r->count)
        nthreads = r->count ? r->count : 1;
    pthread_t threads[32];
    size_t started = 0;
    for (; started < nthreads; started++)
        if (pthread_create(&threads[started], NULL, compress_worker,
                           &job) != 0)
            break;
    if (started == 0)
        compress_worker(&job);
    for (size_t i = 0; i < started; i++)
        pthread_join(threads[i], NULL);
    pthread_mutex_destroy(&job.lock);
    if (job.failed) {
        felf_set_error(err, errlen, "zstd compression failed");
        return -1;
    }
    return 0;
}

/* Record where each program sits in the tree, relative to the bundle root
 * (OUTPUT's directory + ROOT): "name=libexec/gcc/.../cc1", one per line.
 * The bootstrap uses it to find the root from a copy of the FELF at that
 * place, when the tree's symlinks were replaced by copies. */
static int record_program_locations(felf_resolver_t *r, const char *output,
                                    const char *root, char *err,
                                    size_t errlen)
{
    char dir[PATH_MAX], joined[PATH_MAX * 2], abs_root[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", output);
    char *slash = strrchr(dir, '/');
    if (slash == NULL)
        snprintf(dir, sizeof(dir), ".");
    else if (slash == dir)
        dir[1] = '\0';
    else
        *slash = '\0';
    snprintf(joined, sizeof(joined), "%s/%s", dir, root);
    if (realpath(joined, abs_root) == NULL)
        return 0; /* the tree does not exist yet: nothing to record */
    size_t root_len = strlen(abs_root);
    size_t cap = 1, len = 0;
    for (size_t i = 0; i < r->count; i++)
        cap += strlen(r->objects[i].name) + strlen(r->objects[i].path) + 2;
    char *out = calloc(1, cap);
    if (out == NULL) {
        felf_set_error(err, errlen, "out of memory");
        return -1;
    }
    for (size_t i = 0; i < r->count; i++) {
        const felf_object_t *o = &r->objects[i];
        if (strcmp(o->kind, "program") != 0 ||
            strncmp(o->path, abs_root, root_len) != 0 ||
            o->path[root_len] != '/')
            continue;
        len += (size_t)snprintf(out + len, cap - len, "%s=%s\n", o->name,
                                o->path + root_len + 1);
    }
    free(r->program_locations);
    r->program_locations = out;
    return 0;
}

/* SPEC is PATH or NAME=PATH (NAME has no '/'). */
static int add_program_spec(felf_resolver_t *r, const char *spec, char *err,
                            size_t errlen)
{
    const char *eq = strchr(spec, '=');
    if (eq == NULL || memchr(spec, '/', (size_t)(eq - spec)) != NULL)
        return felf_resolver_add_program(r, spec, NULL, err, errlen);
    char name[256];
    size_t n = (size_t)(eq - spec);
    if (n == 0 || n >= sizeof(name)) {
        felf_set_error(err, errlen, "invalid program name in %s", spec);
        return -1;
    }
    memcpy(name, spec, n);
    name[n] = '\0';
    return felf_resolver_add_program(r, eq + 1, name, err, errlen);
}

/* SPEC is PROGRAM=NAME[,NAME...]. */
static int add_alias_spec(felf_resolver_t *r, const char *spec, char *err,
                          size_t errlen)
{
    const char *eq = strchr(spec, '=');
    if (eq == NULL || eq == spec || eq[1] == '\0') {
        felf_set_error(err, errlen,
                       "--alias expects PROGRAM=NAME[,NAME...]: %s", spec);
        return -1;
    }
    char *program = strndup(spec, (size_t)(eq - spec));
    char *names = strdup(eq + 1);
    int rc = program != NULL && names != NULL ? 0 : -1;
    if (rc != 0)
        felf_set_error(err, errlen, "out of memory");
    char *save = NULL;
    for (char *name = rc == 0 ? strtok_r(names, ",", &save) : NULL;
         rc == 0 && name != NULL; name = strtok_r(NULL, ",", &save))
        rc = felf_resolver_add_program_alias(r, program, name, err, errlen);
    free(program);
    free(names);
    return rc;
}

/* Bundle the dependency closure of a plugin kept outside the container.
 * An unresolvable closure is a warning (counted in *WARNED), not an error. */
static int add_plugin(felf_resolver_t *r, const char *path, size_t *warned,
                      char *err, size_t errlen)
{
    char local[512] = {0};
    int rc = felf_resolver_add_external(r, path, local, sizeof(local));
    if (rc < 0) {
        felf_set_error(err, errlen, "%s", local);
        return -1;
    }
    if (rc > 0) {
        fprintf(stderr, "felf: warning: plugin %s: %s\n", path, local);
        (*warned)++;
    }
    return 0;
}

/* Nix treats every /nix/store/<hash> string in a file as a dependency, and
 * the embedded objects are full of them (PT_INTERP, RUNPATHs, glibc's own
 * paths), though a FELF reads none: the bootstrap rewrites PT_INTERP, and
 * the audit module resolves every library.  Compression does not hide
 * them (zstd stores many strings as literals).  Rewrite each hash to 'e's,
 * as nixpkgs' nuke-refs does: the length stays, and 'e' is not in Nix's
 * base-32 alphabet, so no reference is left. */
static void scrub_store_paths(uint8_t *data, size_t size)
{
    static const char prefix[] = "/nix/store/";
    static const char base32[] = "0123456789abcdfghijklmnpqrsvwxyz";
    const size_t plen = sizeof(prefix) - 1, hlen = 32;
    uint8_t *p = data;
    while ((p = memmem(p, size - (size_t)(p - data), prefix, plen)) != NULL) {
        p += plen;
        if ((size_t)(data + size - p) < hlen)
            break;
        size_t i = 0;
        while (i < hlen && p[i] != '\0' && strchr(base32, p[i]) != NULL)
            i++;
        if (i == hlen)
            memset(p, 'e', hlen);
    }
}

static void scrub_objects(felf_resolver_t *r)
{
    for (size_t i = 0; i < r->count; i++) {
        felf_object_t *o = &r->objects[i];
        scrub_store_paths(o->data, o->size);
        felf_sha256(o->data, o->size, o->sha256);
    }
}

/* A library that cannot be included is a warning (counted in *WARNED). */
static int add_include(felf_resolver_t *r, const char *path, size_t *warned,
                       char *err, size_t errlen)
{
    char local[512] = {0};
    int rc = felf_resolver_add_include(r, path, local, sizeof(local));
    if (rc < 0) {
        felf_set_error(err, errlen, "%s", local);
        return -1;
    }
    if (rc > 0) {
        fprintf(stderr, "felf: warning: not bundling %s: %s\n", path, local);
        (*warned)++;
    }
    return 0;
}

static int build_database(felf_resolver_t *r, int strict,
                          int lazy, uint8_t **out, size_t *out_size,
                          char *err, size_t errlen)
{
    const char *tmpdir = getenv("TMPDIR");
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s/felf-db-XXXXXX",
             tmpdir != NULL && tmpdir[0] != '\0' ? tmpdir : "/tmp");
    int fd = mkstemp(tmp);
    if (fd < 0) {
        felf_set_error(err, errlen, "cannot create temporary database: %s",
                       strerror(errno));
        return -1;
    }
    close(fd);
    unlink(tmp);
    felf_object_t *program = felf_resolver_program(r);
    const char *package_name = program ? program->name : "program";
    if (felf_db_build_file(r, tmp, package_name, FELF_VERSION_STRING,
                           "felf " FELF_VERSION_STRING, strict,
                           lazy, err, errlen) != 0) {
        unlink(tmp);
        return -1;
    }
    int rc = felf_read_file(tmp, out, out_size, err, errlen);
    unlink(tmp);
    if (rc == 0)
        felf_db_normalize(*out, *out_size);
    return rc;
}

/* Read the bootstrap the kernel runs, the outer ELF of the FELF. */
static int read_bootstrap(const char *path, uint8_t **out, size_t *size,
                          char *err, size_t errlen)
{
    if (path == NULL || path[0] == '\0') {
        felf_set_error(err, errlen, "no FELF bootstrap executable was found");
        return -1;
    }
    if (felf_read_file(path, out, size, err, errlen) != 0)
        return -1;
    if (felf_outer_elf_validate(*out, *size, err, errlen) != 0) {
        free(*out);
        *out = NULL;
        return -1;
    }
    return 0;
}

/* The bootstrap must be built for the architecture of the programs it
 * starts. */
static int check_bootstrap_arch(const uint8_t *bootstrap, const char *path,
                                const felf_arch_t *arch, char *err,
                                size_t errlen)
{
    const Elf64_Ehdr *outer = (const Elf64_Ehdr *)bootstrap;
    if (outer->e_machine == arch->machine)
        return 0;
    const felf_arch_t *b = felf_arch_find(outer->e_machine);
    felf_set_error(err, errlen, "bootstrap %s is built for %s, the "
                   "programs for %s", path,
                   b ? b->name : "another architecture", arch->name);
    return -1;
}

/* Give every object an offset in the payload region, which starts at the
 * first page after the bootstrap: each object aligned to its largest
 * segment alignment, so the loader can map it in place. */
static int layout_objects(felf_resolver_t *r, size_t bootstrap_size,
                          uint64_t *payload_offset, uint64_t *payload_end,
                          char *err, size_t errlen)
{
    uint64_t cursor = felf_align_up(bootstrap_size, FELF_ALIGNMENT);
    if (cursor == 0) {
        felf_set_error(err, errlen, "bootstrap size overflows alignment");
        return -1;
    }
    *payload_offset = cursor;
    for (size_t i = 0; i < r->count; i++) {
        felf_object_t *o = &r->objects[i];
        uint64_t align = o->alignment > FELF_ALIGNMENT
                             ? o->alignment : FELF_ALIGNMENT;
        uint64_t off = felf_align_up(cursor, align);
        uint64_t stored = o->zdata ? o->zsize : o->size;
        if (off == 0 || off > UINT64_MAX - stored) {
            felf_set_error(err, errlen, "object layout overflows");
            return -1;
        }
        o->offset = off;
        cursor = off + stored;
    }
    *payload_end = cursor;
    return 0;
}

/* Write the FELF: BOOTSTRAP, the objects at their offsets, DB and the
 * footer, into a temporary file that is opened again to check it before it
 * is renamed to OUTPUT. */
static int write_container(const char *output, const uint8_t *bootstrap,
                           size_t bootstrap_size, const felf_resolver_t *r,
                           uint64_t payload_offset, uint64_t payload_end,
                           const uint8_t *db, size_t db_size, uint32_t flags,
                           char *err, size_t errlen)
{
    if (db_size > INT32_MAX) {
        felf_set_error(err, errlen, "database is too large");
        return -1;
    }
    uint64_t db_offset = felf_align_up(payload_end, FELF_ALIGNMENT);
    if (db_offset == 0 || db_offset > UINT64_MAX - db_size - FELF_FOOTER_SIZE) {
        felf_set_error(err, errlen, "container size overflows");
        return -1;
    }
    uint64_t container_size = db_offset + db_size + FELF_FOOTER_SIZE;
    if (container_size > (uint64_t)SIZE_MAX) {
        felf_set_error(err, errlen, "container is too large for this host");
        return -1;
    }
    uint8_t *image = calloc(1, (size_t)container_size);
    if (image == NULL) {
        felf_set_error(err, errlen, "out of memory assembling container");
        return -1;
    }
    memcpy(image, bootstrap, bootstrap_size);
    for (size_t i = 0; i < r->count; i++) {
        const felf_object_t *o = &r->objects[i];
        uint64_t stored = o->zdata ? o->zsize : o->size;
        if (o->offset > container_size ||
            stored > container_size - o->offset) {
            felf_set_error(err, errlen, "internal object layout error");
            free(image);
            return -1;
        }
        memcpy(image + o->offset, o->zdata ? o->zdata : o->data,
               (size_t)stored);
    }
    memcpy(image + db_offset, db, db_size);

    felf_footer_t footer;
    felf_footer_init(&footer);
    footer.flags = flags;
    footer.container_size = container_size;
    footer.db_offset = db_offset;
    footer.db_size = db_size;
    footer.payload_offset = payload_offset;
    footer.payload_size = payload_end - payload_offset;
    footer.object_count = r->count;
    felf_footer_write(&footer, image + container_size - FELF_FOOTER_SIZE);

    char tmp_output[4096];
    int n = snprintf(tmp_output, sizeof(tmp_output), "%s.tmp.%ld",
                     output, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(tmp_output)) {
        felf_set_error(err, errlen, "output path is too long");
        free(image);
        return -1;
    }
    int rc = felf_write_file(tmp_output, image, (size_t)container_size,
                             0755, err, errlen);
    free(image);
    if (rc != 0)
        return -1;

    /* Verify the exact bytes that will become the output. */
    felf_file_t check;
    if (felf_file_open(tmp_output, 0, &check, err, errlen) != 0) {
        unlink(tmp_output);
        return -1;
    }
    felf_file_close(&check);
    if (rename(tmp_output, output) < 0) {
        felf_set_error(err, errlen,
                       "cannot rename output: %s", strerror(errno));
        unlink(tmp_output);
        return -1;
    }
    return 0;
}

int felf_pack(const felf_pack_options_t *options, char *err, size_t errlen)
{
    if (options->input == NULL || options->output == NULL) {
        felf_set_error(err, errlen, "pack requires an input and output path");
        return -1;
    }
    /* Not optional: the bundled glibc has no locale location on the host,
     * and applications such as btop refuse to start without a UTF-8
     * locale. */
    if (options->locale_archive == NULL) {
        felf_set_error(err, errlen,
                       "a FELF must embed a glibc locale archive");
        return -1;
    }
    uint8_t *bootstrap = NULL;
    size_t bootstrap_size = 0;
    if (read_bootstrap(options->bootstrap, &bootstrap, &bootstrap_size,
                       err, errlen) != 0)
        return -1;

    felf_resolver_t r;
    felf_resolver_init(&r);
    r.strict = options->strict;
    if (options->glibc_dir != NULL) {
        r.glibc_dir = strdup(options->glibc_dir);
        if (r.glibc_dir == NULL) {
            felf_set_error(err, errlen, "out of memory");
            goto fail;
        }
    }

    if (add_program_spec(&r, options->input, err, errlen) != 0)
        goto fail;
    for (size_t i = 0; i < options->program_count; i++)
        if (add_program_spec(&r, options->programs[i], err, errlen) != 0)
            goto fail;
    for (size_t i = 0; i < options->alias_count; i++)
        if (add_alias_spec(&r, options->aliases[i], err, errlen) != 0)
            goto fail;
    for (size_t i = 0; i < options->lib_dir_count; i++) {
        if (felf_resolver_add_search_dir(&r, options->lib_dirs[i],
                                         err, errlen) != 0)
            goto fail;
    }
    if (!options->no_default_host_libs) {
        for (size_t i = 0; i < FELF_DEFAULT_HOST_LIB_COUNT; i++)
            if (felf_resolver_add_host_lib(&r, felf_default_host_libs[i],
                                           err, errlen) != 0)
                goto fail;
    }
    for (size_t i = 0; i < options->host_lib_count; i++) {
        if (felf_resolver_add_host_lib(&r, options->host_libs[i],
                                       err, errlen) != 0)
            goto fail;
    }
    /* The audit module is embedded in every mode: it implements the
     * host-library lookup as well as strict-mode enforcement. */
    if (options->audit != NULL) {
        if (felf_resolver_add_audit(&r, options->audit, err, errlen) != 0)
            goto fail;
    }
    if (options->shim != NULL) {
        if (felf_resolver_add_runtime(&r, options->shim, err, errlen) != 0)
            goto fail;
    }
    if (felf_resolver_add_resource(&r, options->locale_archive,
                                   "locale-archive", err, errlen) != 0)
        goto fail;
    for (size_t i = 0; i < options->env_count; i++) {
        if (felf_resolver_add_env(&r, options->env[i], err, errlen) != 0)
            goto fail;
    }
    if (options->root != NULL) {
        if (options->root[0] == '/') {
            felf_set_error(err, errlen,
                           "--root must be relative to the output's directory");
            goto fail;
        }
        r.root = strdup(options->root);
        if (r.root == NULL) {
            felf_set_error(err, errlen, "out of memory");
            goto fail;
        }
        if (record_program_locations(&r, options->output, options->root,
                                     err, errlen) != 0)
            goto fail;
    }
    if (felf_resolver_resolve(&r, err, errlen) != 0)
        goto fail;

    /* Included libraries come after the programs' own closure, so a
     * program keeps the libraries it links against. */
    size_t warned = 0;
    for (size_t i = 0; i < options->include_count; i++)
        if (add_include(&r, options->includes[i], &warned, err, errlen) != 0)
            goto fail;
    if (warned)
        fprintf(stderr, "felf: warning: %zu included librar%s not bundled\n",
                warned, warned == 1 ? "y is" : "ies are");
    warned = 0;
    for (size_t i = 0; i < options->plugin_count; i++)
        if (add_plugin(&r, options->plugins[i], &warned, err, errlen) != 0)
            goto fail;
    if (warned)
        fprintf(stderr,
                "felf: warning: %zu plugin(s) have unresolved dependencies "
                "and will fail to load in strict mode\n", warned);
    if (felf_resolver_check_versions(&r, err, errlen) != 0)
        goto fail;
    felf_resolver_drop_external(&r);
    felf_resolver_sort(&r);
    if (felf_resolver_program(&r) == NULL ||
        felf_resolver_interp(&r) == NULL) {
        felf_set_error(err, errlen,
                       "resolved container has no program/interpreter");
        goto fail;
    }
    if (check_bootstrap_arch(bootstrap, options->bootstrap, r.arch,
                             err, errlen) != 0)
        goto fail;

    scrub_objects(&r);
    if (options->compress_level > 0 &&
        compress_objects(&r, options->compress_level,
                         r.direct_map && !options->compress_libraries,
                         err, errlen) != 0)
        goto fail;

    uint64_t payload_offset, payload_end;
    if (layout_objects(&r, bootstrap_size, &payload_offset, &payload_end,
                       err, errlen) != 0)
        goto fail;
    uint8_t *db = NULL;
    size_t db_size = 0;
    if (build_database(&r, options->strict,
                       options->lazy, &db, &db_size, err, errlen) != 0)
        goto fail;
    uint32_t flags = (options->strict ? FELF_FLAG_STRICT : 0) |
                     (options->lazy ? FELF_FLAG_LAZY : 0);
    int rc = write_container(options->output, bootstrap, bootstrap_size, &r,
                             payload_offset, payload_end, db, db_size, flags,
                             err, errlen);
    free(db);
    if (rc != 0)
        goto fail;
    free(bootstrap);
    felf_resolver_free(&r);
    return 0;

fail:
    free(bootstrap);
    felf_resolver_free(&r);
    return -1;
}
