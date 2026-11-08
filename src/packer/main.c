#define _GNU_SOURCE
#include "assets.h"
#include "commands.h"
#include "felf_file.h"
#include "pack.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef FELF_VERSION_STRING
#define FELF_VERSION_STRING "0.3.0"
#endif

/* zstd level used unless --compress says otherwise. */
#define DEFAULT_COMPRESS_LEVEL 19

static void usage(FILE *out)
{
    fprintf(out,
        "felf %s — Fat ELF packer and inspector\n"
        "\n"
        "Usage:\n"
        "  felf pack [NAME=]INPUT -o OUTPUT [options]\n"
        "  felf inspect FILE [--json]\n"
        "  felf list FILE [--json]\n"
        "  felf verify FILE\n"
        "  felf extract FILE --output DIR\n"
        "  felf sql FILE \"SELECT ...\"\n"
        "  felf --help\n"
        "  felf --version\n"
        "\n"
        "An argument @FILE is replaced by the lines of FILE, one argument per\n"
        "line (empty lines and lines starting with # are skipped).\n"
        "\n"
        "Pack options:\n"
        "  -o, --output PATH          Output FELF executable\n"
        "      --program [NAME=]PATH  Add another program sharing the libraries;\n"
        "                             it runs when invoked as NAME (a symlink)\n"
        "      --alias PROGRAM=NAME[,NAME...]\n"
        "                             More names for PROGRAM, run with that name\n"
        "                             as argv[0] (multicall binaries; see below)\n"
        "      --compress LEVEL|none  Compress everything, the libraries too, at\n"
        "                             zstd level 1-22; none compresses nothing.\n"
        "                             Without it, libraries are mapped from the\n"
        "                             file and the rest is compressed at level %d\n"
        "      --permissive           Let names the FELF does not bundle fall\n"
        "                             back to the host (default: strict)\n"
        "      --lazy                 Resolve bundled objects on demand via audit\n"
        "      --include PATH         Also bundle a shared object; one whose name\n"
        "                             is already bundled is skipped, and one that\n"
        "                             cannot be bundled is a warning\n"
        "      --plugin PATH          Keep PATH outside the FELF but bundle its\n"
        "                             dependency closure (tree plugins)\n"
        "      --root REL             Top of the directory tree the FELF is part\n"
        "                             of, relative to the FELF's directory (see\n"
        "                             below; default: that directory)\n"
        "      --env NAME=VALUE       Set an application environment variable;\n"
        "                             VALUE may use $FELF_ROOT, $FELF_APP, $VAR\n"
        "      --lib-dir DIR          Also look for the libraries to embed in DIR\n"
        "                             (at pack time only; see below)\n"
        "      --host-lib PATTERN     Prefer the host copy of matching libraries\n"
        "                             (name or prefix*; graphics stack by default)\n"
        "      --no-default-host-libs Do not prefer the host graphics stack\n"
        "\n",
        FELF_VERSION_STRING, DEFAULT_COMPRESS_LEVEL);
    /* Split in two: C99 compilers need only support 4095-byte strings. */
    fputs(
        "A multicall binary (coreutils, busybox) dispatches on argv[0]: give\n"
        "its names with --alias, for example --program coreutils=PATH\n"
        "--alias coreutils=ls,cat,cp (or --alias busybox=$(busybox --list |\n"
        "paste -sd,)).  --program NAME=PATH for a file already embedded does\n"
        "the same, one name at a time.  Running a FELF with\n"
        "FELF_INSTALL_LINKS=DIR set makes DIR/NAME a symlink to it for every\n"
        "program and alias (relative when DIR is the FELF's directory), and\n"
        "runs nothing.  Existing files in DIR are not replaced.\n"
        "\n"
        "--root is for a FELF inside a directory tree, such as bin/app in a\n"
        "folder with lib/ and share/: pack it with -o tree/bin/app --root ..\n"
        "The path is stored relative and resolved from the FELF's location at\n"
        "each start, so the tree can be moved anywhere.  The root is where\n"
        "$FELF_ROOT points (for --env values such as $FELF_ROOT/share/app),\n"
        "and in strict mode the program may load files under it besides the\n"
        "bundled ones (plugins kept as files, --plugin).\n"
        "\n"
        "--lib-dir is for libraries the packer cannot find otherwise, such as\n"
        "ones next to a build with no RUNPATH to them.  A library is looked\n"
        "for in FELF's glibc (glibc's own), the requesting object's RUNPATH\n"
        "(or RPATH), the interpreter's directory and /lib, /usr/lib, ...,\n"
        "then each --lib-dir in order.  LD_LIBRARY_PATH is not used.  The\n"
        "libraries found are embedded: the FELF does not look in DIR when it\n"
        "runs.\n"
        "\n"
        "extract writes every object, decompressed, and the database\n"
        "(felf.sqlite) into DIR.  An object's file is its name ([A-Za-z0-9._+-], other characters become _), with\n"
        "\".<id>\" appended when that name is not unique.\n"
        "\n"
        "Every FELF carries a glibc locale archive for its bundled glibc: the\n"
        "bootstrap points LOCALE_ARCHIVE at it when the caller's locale is C\n"
        "or unset, or the host has no archive, and sets LANG=C.UTF-8 when the\n"
        "caller set no locale at all.  Programs therefore start with a UTF-8\n"
        "locale even in an empty container.\n"
        "\n"
        "The bootstrap, audit module and runtime shim are built into felf;\n"
        "FELF_BOOTSTRAP, FELF_AUDIT and FELF_SHIM name other files to use\n"
        "(for example to pack for another architecture).\n"
        "\n"
        "Every program is bundled with FELF's glibc (felf --version), whose\n"
        "loader maps libraries straight from the FELF file: they are stored\n"
        "uncompressed (unless --compress LEVEL is given), everything else\n"
        "is compressed and copied into memory at start.  A program that\n"
        "needs a newer glibc is rejected.\n"
        "\n"
        "Checksums detect corruption; they are not signatures.\n", out);
}

static int push_argv(char ***argv, size_t *count, size_t *cap, char *arg)
{
    if (*count + 2 > *cap) {
        size_t newcap = *cap ? *cap * 2 : 64;
        char **p = realloc(*argv, newcap * sizeof(*p));
        if (p == NULL) {
            fprintf(stderr, "felf: out of memory\n");
            return -1;
        }
        *argv = p;
        *cap = newcap;
    }
    (*argv)[(*count)++] = arg;
    (*argv)[*count] = NULL;
    return 0;
}

/* Replace every @FILE argument by FILE's lines.  The new argv is never
 * freed: it lives as long as the process. */
static int expand_response_files(int *argc, char ***argv)
{
    char **out = NULL;
    size_t count = 0, cap = 0;
    for (int i = 0; i < *argc; i++) {
        const char *arg = (*argv)[i];
        if (i == 0 || arg[0] != '@') {
            if (push_argv(&out, &count, &cap, (*argv)[i]) != 0)
                return -1;
            continue;
        }
        FILE *f = fopen(arg + 1, "r");
        if (f == NULL) {
            fprintf(stderr, "felf: cannot open %s: %s\n", arg + 1,
                    strerror(errno));
            return -1;
        }
        char *line = NULL;
        size_t line_cap = 0;
        int rc = 0;
        while (rc == 0 && getline(&line, &line_cap, f) >= 0) {
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] == '\0' || line[0] == '#')
                continue;
            char *copy = strdup(line);
            rc = copy != NULL ? push_argv(&out, &count, &cap, copy) : -1;
        }
        free(line);
        fclose(f);
        if (rc != 0)
            return -1;
    }
    *argc = (int)count;
    *argv = out;
    return 0;
}

/* The asset's path: the file named by its variable, else a copy of the
 * embedded bytes written into DIR. */
static int asset_path(const felf_asset_t *asset, const char *dir,
                      char *out, size_t outlen)
{
    const char *env = asset->env != NULL ? getenv(asset->env) : NULL;
    if (env != NULL && env[0] != '\0') {
        snprintf(out, outlen, "%s", env);
        return 0;
    }
    if (snprintf(out, outlen, "%s/%s", dir, asset->name) >= (int)outlen) {
        fprintf(stderr, "felf: TMPDIR is too long\n");
        return -1;
    }
    char err[512] = {0};
    if (felf_write_file(out, asset->data, asset->size, 0600,
                        err, sizeof(err)) != 0) {
        fprintf(stderr, "felf: %s\n", err);
        return -1;
    }
    return 0;
}

static int push_arg(const char ***array, size_t *count, size_t *cap,
                    const char *value)
{
    if (*count == *cap) {
        size_t newcap = *cap ? *cap * 2 : 8;
        const char **p = realloc(*array, newcap * sizeof(*p));
        if (p == NULL) {
            fprintf(stderr, "felf: out of memory\n");
            return -1;
        }
        *array = p;
        *cap = newcap;
    }
    (*array)[(*count)++] = value;
    return 0;
}

/* --compress LEVEL|none: a zstd level, or 0 for none. */
static int parse_compress(const char *arg, int *level)
{
    char *end;
    long value = strtol(arg, &end, 10);
    if (strcmp(arg, "none") == 0) {
        *level = 0;
    } else if (end != arg && *end == '\0' && value >= 1 && value <= 22) {
        *level = (int)value;
    } else {
        fprintf(stderr, "felf: --compress expects a zstd level "
                "(1-22) or none: %s\n", arg);
        return -1;
    }
    return 0;
}

static int cmd_pack(int argc, char **argv)
{
    const char *output = NULL;
    const char *root_opt = NULL;
    int strict = 1;
    int lazy = 0;
    const char **includes = NULL;
    size_t include_count = 0, include_cap = 0;
    const char **lib_dirs = NULL;
    size_t lib_dir_count = 0, lib_dir_cap = 0;
    const char **plugins = NULL;
    size_t plugin_count = 0, plugin_cap = 0;
    const char **envs = NULL;
    size_t env_count = 0, env_cap = 0;
    const char **host_libs = NULL;
    size_t host_lib_count = 0, host_lib_cap = 0;
    int no_default_host_libs = 0;
    int compress_level = DEFAULT_COMPRESS_LEVEL;
    int compress_given = 0;
    const char **programs = NULL;
    size_t program_count = 0, program_cap = 0;
    const char **aliases = NULL;
    size_t alias_count = 0, alias_cap = 0;

    static const struct option options[] = {
        {"output", required_argument, NULL, 'o'},
        {"compress-libraries", no_argument, NULL, 1000}, /* removed */
        {"permissive", no_argument, NULL, 1001},
        {"include", required_argument, NULL, 1006},
        {"lazy", no_argument, NULL, 1007},
        {"lib-dir", required_argument, NULL, 1008},
        {"root", required_argument, NULL, 1011},
        {"plugin", required_argument, NULL, 1012},
        {"env", required_argument, NULL, 1014},
        {"host-lib", required_argument, NULL, 1015},
        {"no-default-host-libs", no_argument, NULL, 1016},
        {"program", required_argument, NULL, 1018},
        {"alias", required_argument, NULL, 1020},
        {"compress", required_argument, NULL, 1019},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    optind = 2;
    int c;
    while ((c = getopt_long(argc, argv, "o:h", options, NULL)) != -1) {
        switch (c) {
        case 'o': output = optarg; break;
        case 1000:
            fprintf(stderr, "felf: --compress-libraries was removed; "
                    "--compress LEVEL now compresses the libraries\n");
            return 2;
        case 1001: strict = 0; break;
        case 1006:
            if (push_arg(&includes, &include_count, &include_cap,
                         optarg) != 0)
                return 1;
            break;
        case 1007: lazy = 1; break;
        case 1008:
            if (push_arg(&lib_dirs, &lib_dir_count, &lib_dir_cap,
                         optarg) != 0)
                return 1;
            break;
        case 1011: root_opt = optarg; break;
        case 1012:
            if (push_arg(&plugins, &plugin_count, &plugin_cap, optarg) != 0)
                return 1;
            break;
        case 1014:
            if (strchr(optarg, '=') == NULL || optarg[0] == '=') {
                fprintf(stderr, "felf: --env expects NAME=VALUE: %s\n",
                        optarg);
                return 2;
            }
            if (push_arg(&envs, &env_count, &env_cap, optarg) != 0)
                return 1;
            break;
        case 1015:
            if (push_arg(&host_libs, &host_lib_count, &host_lib_cap,
                         optarg) != 0)
                return 1;
            break;
        case 1016: no_default_host_libs = 1; break;
        case 1019:
            if (parse_compress(optarg, &compress_level) != 0)
                return 2;
            compress_given = 1;
            break;
        case 1020:
            if (push_arg(&aliases, &alias_count, &alias_cap, optarg) != 0)
                return 1;
            break;
        case 1018:
            if (push_arg(&programs, &program_count, &program_cap, optarg) != 0)
                return 1;
            break;
        case 'h': usage(stdout); return 0;
        default: usage(stderr); return 2;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "felf: pack requires an input file\n");
        return 2;
    }
    const char *input = argv[optind++];
    if (optind != argc) {
        fprintf(stderr, "felf: unexpected argument: %s\n", argv[optind]);
        return 2;
    }
    if (output == NULL) {
        fprintf(stderr, "felf: pack requires -o/--output\n");
        return 2;
    }

    /* The packer reads its assets as files, like everything else it
     * bundles: write the embedded ones to a private directory. */
    const char *tmpdir = getenv("TMPDIR");
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/felf-assets-XXXXXX",
             tmpdir != NULL && tmpdir[0] != '\0' ? tmpdir : "/tmp");
    if (mkdtemp(dir) == NULL) {
        fprintf(stderr, "felf: cannot create %s: %s\n", dir,
                strerror(errno));
        return 1;
    }
    const felf_asset_t *const assets[] = {
        &felf_asset_bootstrap, &felf_asset_audit, &felf_asset_shim,
        &felf_asset_locale_archive};
    enum { N_ASSETS = sizeof(assets) / sizeof(assets[0]) };
    char paths[N_ASSETS][PATH_MAX];
    char glibc[PATH_MAX];
    snprintf(glibc, sizeof(glibc), "%s/glibc", dir);
    int rc = 1;
    char err[512] = {0};
    for (size_t i = 0; i < N_ASSETS; i++)
        if (assets[i]->name == NULL)
            paths[i][0] = '\0'; /* not in this felf */
        else if (asset_path(assets[i], dir, paths[i], sizeof(paths[i])) != 0)
            goto out;
    /* FELF's glibc, in DIR/glibc. */
    if (mkdir(glibc, 0700) != 0) {
        fprintf(stderr, "felf: cannot create %s: %s\n", glibc,
                strerror(errno));
        goto out;
    }
    for (const felf_asset_t *a = felf_glibc_assets; a->name != NULL; a++) {
        char path[PATH_MAX];
        if (asset_path(a, glibc, path, sizeof(path)) != 0)
            goto out;
    }

    felf_pack_options_t opts = {
        .input = input,
        .output = output,
        .bootstrap = paths[0],
        .audit = paths[1],
        .shim = paths[2],
        .root = root_opt,
        .glibc_dir = glibc,
        .includes = includes,
        .include_count = include_count,
        .plugins = plugins,
        .plugin_count = plugin_count,
        .env = envs,
        .env_count = env_count,
        .lib_dirs = lib_dirs,
        .lib_dir_count = lib_dir_count,
        .host_libs = host_libs,
        .host_lib_count = host_lib_count,
        .no_default_host_libs = no_default_host_libs,
        .locale_archive = paths[3][0] != '\0' ? paths[3] : NULL,
        .compress_level = compress_level,
        .programs = programs,
        .program_count = program_count,
        .aliases = aliases,
        .alias_count = alias_count,
        /* An explicit level compresses the libraries too. */
        .compress_libraries = compress_given && compress_level > 0,
        .strict = strict,
        .lazy = lazy,
    };
    rc = felf_pack(&opts, err, sizeof(err));
    if (rc != 0)
        fprintf(stderr, "felf: pack: %s\n", err);
    else
        printf("wrote %s\n", output);

out:
    for (size_t i = 0; i < N_ASSETS; i++) {
        char path[PATH_MAX];
        if (assets[i]->name != NULL &&
            snprintf(path, sizeof(path), "%s/%s", dir,
                     assets[i]->name) < (int)sizeof(path))
            unlink(path);
    }
    for (const felf_asset_t *a = felf_glibc_assets; a->name != NULL; a++) {
        char path[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", glibc, a->name) <
            (int)sizeof(path))
            unlink(path);
    }
    rmdir(glibc);
    rmdir(dir);
    free(includes);
    free(lib_dirs);
    free(plugins);
    free(envs);
    free(host_libs);
    free(programs);
    free(aliases);
    return rc != 0;
}

static void print_error(const char *command, const char *err)
{
    fprintf(stderr, "felf: %s: %s\n", command, err);
}

/* inspect and list: FILE [--json]. */
static int cmd_show(int argc, char **argv,
                    int (*show)(const char *, int, char *, size_t))
{
    int json = 0;
    static const struct option options[] = {
        {"json", no_argument, NULL, 'j'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    optind = 2;
    int c;
    while ((c = getopt_long(argc, argv, "jh", options, NULL)) != -1) {
        if (c == 'j') json = 1;
        else if (c == 'h') { usage(stdout); return 0; }
        else return 2;
    }
    if (optind + 1 != argc) {
        fprintf(stderr, "felf: %s requires exactly one file\n", argv[1]);
        return 2;
    }
    char err[512] = {0};
    if (show(argv[optind], json, err, sizeof(err)) != 0) {
        print_error(argv[1], err);
        return 1;
    }
    return 0;
}

static int cmd_verify(int argc, char **argv)
{
    if (argc == 3 && (strcmp(argv[2], "-h") == 0 ||
                      strcmp(argv[2], "--help") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "felf: verify requires exactly one file\n");
        return 2;
    }
    char err[512] = {0};
    if (felf_cmd_verify(argv[2], err, sizeof(err)) != 0) {
        print_error("verify", err);
        return 1;
    }
    printf("FELF verification: OK (%s)\n", argv[2]);
    return 0;
}

static int cmd_extract(int argc, char **argv)
{
    const char *output = NULL;
    static const struct option options[] = {
        {"output", required_argument, NULL, 'o'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    optind = 2;
    int c;
    while ((c = getopt_long(argc, argv, "o:h", options, NULL)) != -1) {
        if (c == 'o') output = optarg;
        else if (c == 'h') { usage(stdout); return 0; }
        else return 2;
    }
    if (optind + 1 != argc) {
        fprintf(stderr, "felf: extract requires exactly one file\n");
        return 2;
    }
    if (output == NULL) {
        fprintf(stderr, "felf: extract requires --output DIR\n");
        return 2;
    }
    char err[512] = {0};
    if (felf_cmd_extract(argv[optind], output, err, sizeof(err)) != 0) {
        print_error("extract", err);
        return 1;
    }
    return 0;
}

static int cmd_sql(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "felf: sql requires FILE and QUERY\n");
        return 2;
    }
    size_t len = 0;
    for (int i = 3; i < argc; i++)
        len += strlen(argv[i]) + 1;
    char *query = malloc(len + 1);
    if (query == NULL) {
        fprintf(stderr, "felf: out of memory\n");
        return 1;
    }
    query[0] = '\0';
    for (int i = 3; i < argc; i++) {
        if (i != 3)
            strcat(query, " ");
        strcat(query, argv[i]);
    }
    char err[512] = {0};
    int rc = felf_cmd_sql(argv[2], query, err, sizeof(err));
    free(query);
    if (rc != 0) {
        print_error("sql", err);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (expand_response_files(&argc, &argv) != 0)
        return 1;
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(stdout);
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
        printf("felf %s\n", FELF_VERSION_STRING);
        printf("glibc %s (FELF's, maps libraries from the file)\n",
               felf_glibc_version);
        return 0;
    }
    if (strcmp(argv[1], "pack") == 0)
        return cmd_pack(argc, argv);
    if (strcmp(argv[1], "inspect") == 0)
        return cmd_show(argc, argv, felf_cmd_inspect);
    if (strcmp(argv[1], "list") == 0)
        return cmd_show(argc, argv, felf_cmd_list);
    if (strcmp(argv[1], "verify") == 0)
        return cmd_verify(argc, argv);
    if (strcmp(argv[1], "extract") == 0)
        return cmd_extract(argc, argv);
    if (strcmp(argv[1], "sql") == 0)
        return cmd_sql(argc, argv);
    fprintf(stderr, "felf: unknown command: %s\n\n", argv[1]);
    usage(stderr);
    return 2;
}
