# cmake -DDIR=<glibc lib dir> -DVERSION=<version> -DOUT=<file.c> -P ...
#
# Writes the C source that embeds every file of DIR (felf_glibc_assets,
# packer/assets.h).  OUT is only rewritten when it changes.
file(GLOB files LIST_DIRECTORIES false "${DIR}/*")
list(SORT files)
if(NOT files)
    message(FATAL_ERROR "FELF's glibc: ${DIR} is empty")
endif()

set(source "/* Generated from FELF's glibc, ${DIR}. */\n")
string(APPEND source "#include \"assets.h\"\n\n")
set(table "")
set(n 0)
foreach(file IN LISTS files)
    get_filename_component(name "${file}" NAME)
    string(APPEND source
        "static const unsigned char glibc_${n}[] = {\n"
        "#embed \"${file}\"\n};\n")
    string(APPEND table
        "    {\"${name}\", NULL, glibc_${n}, sizeof(glibc_${n})},\n")
    math(EXPR n "${n} + 1")
endforeach()
string(APPEND source
    "\nconst felf_asset_t felf_glibc_assets[] = {\n${table}"
    "    {NULL, NULL, NULL, 0},\n};\n"
    "const char felf_glibc_version[] = \"${VERSION}\";\n")

file(WRITE "${OUT}.tmp" "${source}")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
