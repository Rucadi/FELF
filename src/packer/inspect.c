#include "commands.h"
#include "felf_file.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_string(const char *s)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\b': fputs("\\b", stdout); break;
        case '\f': fputs("\\f", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (*p < 0x20)
                printf("\\u%04x", *p);
            else
                putchar(*p);
        }
    }
    putchar('"');
}

int felf_cmd_inspect(const char *path, int json, char *err, size_t errlen)
{
    felf_file_t file;
    if (felf_file_open(path, 0, &file, err, errlen) != 0)
        return -1;
    /* The default program: meta "default_program", else the first one. */
    const felf_db_object_t *program = NULL;
    size_t programs = 0;
    for (size_t i = 0; i < file.db.object_count; i++) {
        const felf_db_object_t *o = &file.db.objects[i];
        if (strcmp(o->kind, "program") != 0)
            continue;
        programs++;
        if (program == NULL ||
            strcmp(o->name, file.db.default_program) == 0)
            program = o;
    }
    if (json) {
        printf("{\"format_version\":%u,\"architecture\":", FELF_FORMAT_VERSION);
        json_string(file.architecture);
        printf(",\"runtime\":");
        json_string(file.abi);
        printf(",\"strict\":%s,\"lazy\":%s,\"direct_map\":%s",
               (file.footer.flags & FELF_FLAG_STRICT) ? "true" : "false",
               (file.footer.flags & FELF_FLAG_LAZY) ? "true" : "false",
               file.db.direct_map ? "true" : "false");
        printf(",\"package\":{\"name\":");
        json_string(file.package_name);
        printf(",\"version\":");
        json_string(file.package_version);
        printf("},\"program\":");
        json_string(program ? program->name : "");
        printf(",\"programs\":[");
        for (size_t i = 0, n = 0; i < file.db.object_count; i++) {
            if (strcmp(file.db.objects[i].kind, "program") != 0)
                continue;
            if (n++)
                putchar(',');
            json_string(file.db.objects[i].name);
        }
        putchar(']');
        printf(",\"objects\":[");
        for (size_t i = 0; i < file.db.object_count; i++) {
            if (i)
                putchar(',');
            const felf_db_object_t *o = &file.db.objects[i];
            printf("{\"id\":%lld,\"name\":", (long long)o->id);
            json_string(o->name);
            printf(",\"soname\":");
            if (o->soname)
                json_string(o->soname);
            else
                fputs("null", stdout);
            printf(",\"kind\":");
            json_string(o->kind);
            printf(",\"size\":%llu,\"offset\":%llu,\"alignment\":%llu,"
                   "\"compression\":\"%s\",\"stored_size\":%llu}",
                   (unsigned long long)o->size,
                   (unsigned long long)o->offset,
                   (unsigned long long)o->alignment,
                   o->compressed ? "zstd" : "none",
                   (unsigned long long)o->stored_size);
        }
        printf("]}\n");
    } else {
        printf("FELF Version: %u\n", FELF_FORMAT_VERSION);
        printf("Architecture: %s\n", file.architecture);
        printf("Runtime: %s\n", file.abi);
        printf("Strict Mode: %s\n",
               (file.footer.flags & FELF_FLAG_STRICT) ? "Enabled" : "Disabled");
        printf("Lazy Loading: %s\n",
               (file.footer.flags & FELF_FLAG_LAZY) ? "Enabled" : "Disabled");
        printf("Direct Mapping: %s\n", file.db.direct_map
                   ? "Enabled (uncompressed libraries map from the file)"
                   : "Disabled");
        printf("Package: %s %s\n", file.package_name, file.package_version);
        printf("\nOriginal Program%s:\n", programs > 1 ? "s" : "");
        if (program == NULL)
            printf("  (missing)\n");
        for (size_t i = 0; i < file.db.object_count; i++) {
            const felf_db_object_t *o = &file.db.objects[i];
            if (strcmp(o->kind, "program") == 0)
                printf("  %s%s\n", o->name,
                       programs > 1 && o == program ? "  (default)" : "");
        }
        printf("\nEmbedded Objects (%zu):\n", file.db.object_count);
        uint64_t total = 0, stored = 0;
        for (size_t i = 0; i < file.db.object_count; i++) {
            const felf_db_object_t *o = &file.db.objects[i];
            total += o->size;
            stored += o->stored_size;
            if (o->compressed)
                printf("  %-32s %-8s %llu bytes (zstd %llu)\n", o->name,
                       o->kind, (unsigned long long)o->size,
                       (unsigned long long)o->stored_size);
            else
                printf("  %-32s %-8s %llu bytes\n", o->name, o->kind,
                       (unsigned long long)o->size);
        }
        printf("\nObject bytes: %llu (stored: %llu)\n",
               (unsigned long long)total, (unsigned long long)stored);
        printf("\nMetadata:\n");
        printf("  SQLite database: present (%llu bytes)\n",
               (unsigned long long)file.footer.db_size);
        printf("  Checksums: present (run `felf verify` to validate)\n");
    }
    felf_file_close(&file);
    return 0;
}

int felf_cmd_list(const char *path, int json, char *err, size_t errlen)
{
    felf_file_t file;
    if (felf_file_open(path, 0, &file, err, errlen) != 0)
        return -1;
    if (json) {
        printf("[");
        for (size_t i = 0; i < file.db.object_count; i++) {
            if (i)
                putchar(',');
            const felf_db_object_t *o = &file.db.objects[i];
            printf("{\"id\":%lld,\"name\":", (long long)o->id);
            json_string(o->name);
            printf(",\"soname\":");
            if (o->soname)
                json_string(o->soname);
            else
                fputs("null", stdout);
            printf(",\"kind\":");
            json_string(o->kind);
            printf(",\"size\":%llu,\"offset\":%llu}",
                   (unsigned long long)o->size,
                   (unsigned long long)o->offset);
        }
        printf("]\n");
    } else {
        printf("%-4s %-32s %-24s %-8s %-7s %12s %12s\n",
               "ID", "NAME", "SONAME", "KIND", "PACKED", "SIZE", "OFFSET");
        for (size_t i = 0; i < file.db.object_count; i++) {
            const felf_db_object_t *o = &file.db.objects[i];
            printf("%-4lld %-32s %-24s %-8s %-7s %12llu %12llu\n",
                   (long long)o->id, o->name,
                   o->soname ? o->soname : "-", o->kind,
                   o->compressed ? "zstd" : "none",
                   (unsigned long long)o->size,
                   (unsigned long long)o->offset);
        }
    }
    felf_file_close(&file);
    return 0;
}
