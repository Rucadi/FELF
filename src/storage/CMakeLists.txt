# The reader, used by the bootstrap and the CLI.
add_library(felf_read STATIC
    store_read.c)
target_link_libraries(felf_read PUBLIC felf_format felf_sqlite felf_zstd)
target_compile_options(felf_read PRIVATE -Wall -Wextra -Wpedantic)

# The writer, used by the CLI only.  It stores what the packer's resolver
# found, so it and its users see the packer's headers (store_build.h
# includes resolver.h).
if(FELF_BUILD_CLI)
    # Embed schema.sql as a C string.  Keeping one source of truth avoids a
    # generated database that disagrees with the documented schema.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schema.sql" FELF_SCHEMA_SQL)
    configure_file(schema_sql.h.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/schema_sql.h" @ONLY)

    add_library(felf_store STATIC
        store_build.c)
    target_include_directories(felf_store
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${PROJECT_SOURCE_DIR}/src/packer"
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(felf_store PUBLIC felf_read)
    target_compile_options(felf_store PRIVATE -Wall -Wextra -Wpedantic)
endif()
