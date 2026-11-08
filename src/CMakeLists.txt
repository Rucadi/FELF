# FELF's components, one directory each:
#
#   include/    headers shared by every component (felf_headers)
#   format/     the on-disk format and SHA-256 (felf_format)
#   storage/    the embedded SQLite store: reader and writer
#   loader/     the audit module and runtime shim every FELF carries
#   bootstrap/  the static program at the front of every FELF
#   glibc/      FELF's patched glibc and its locale archive
#   packer/     the felf CLI, which embeds all of the above

add_library(felf_headers INTERFACE)
target_include_directories(felf_headers INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_subdirectory(format)
add_subdirectory(storage)
if(FELF_BUILD_AUDIT)
    add_subdirectory(loader)
endif()
if(FELF_BUILD_BOOTSTRAP)
    add_subdirectory(bootstrap)
endif()
if(FELF_BUILD_CLI)
    add_subdirectory(glibc)
    add_subdirectory(packer)
endif()
