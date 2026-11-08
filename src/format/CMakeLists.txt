add_library(felf_format STATIC
    format.c
    sha256.c)
target_link_libraries(felf_format PUBLIC felf_headers)
target_compile_options(felf_format PRIVATE -Wall -Wextra -Wpedantic)
