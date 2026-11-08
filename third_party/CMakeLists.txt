# Vendored dependencies (README.md), built from source so the static
# bootstrap and the CLI never depend on the system's copies.  Each function
# and object gets its own section: the bootstrap links with --gc-sections
# and keeps only the parts it uses (SQLite's reader, zstd's decompressor).
add_library(felf_sqlite STATIC sqlite/sqlite3.c)
target_include_directories(felf_sqlite SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/sqlite")
# The FELF tools read a deserialized in-memory database from one thread and
# never load extensions.
target_compile_definitions(felf_sqlite PRIVATE
    SQLITE_THREADSAFE=0
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_MEMSTATUS=0)
target_compile_options(felf_sqlite PRIVATE
    -w -ffunction-sections -fdata-sections)

# zstd: the packer compresses objects, the bootstrap and the CLI decompress
# them.  The single-file build enables zstd's (unused) multithreading, so it
# needs pthreads.
add_library(felf_zstd STATIC zstd/zstd.c)
target_include_directories(felf_zstd SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/zstd")
target_compile_options(felf_zstd PRIVATE
    -w -ffunction-sections -fdata-sections)
target_link_libraries(felf_zstd PUBLIC Threads::Threads)
