#ifndef FELF_COMMANDS_H
#define FELF_COMMANDS_H

#include <stddef.h>

int felf_cmd_inspect(const char *path, int json, char *err, size_t errlen);
int felf_cmd_list(const char *path, int json, char *err, size_t errlen);
int felf_cmd_verify(const char *path, char *err, size_t errlen);
int felf_cmd_sql(const char *path, const char *query,
                 char *err, size_t errlen);
int felf_cmd_extract(const char *path, const char *output,
                     char *err, size_t errlen);

#endif
