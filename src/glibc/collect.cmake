# cmake -DSTAGE=<install_root> -DOUT=<dir> -DLOCALES=<a/b,c/d> -P ...
#
# Turns the installed glibc in STAGE into FELF's glibc, like
# nix/pkgs/felf-glibc.nix does for Nix's:
#
#   OUT/lib/                    the shared libraries (lib*.so.*) and the loader
#   OUT/locale/locale-archive   LOCALES, built with this glibc's localedef
#   OUT/stamp                   touched last; the build depends on it
set(lib "${STAGE}/usr/lib")
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/lib" "${OUT}/locale" "${OUT}/root/usr/lib/locale")

file(GLOB loader LIST_DIRECTORIES false "${lib}/ld-linux*.so.*")
file(GLOB libs LIST_DIRECTORIES false "${lib}/lib*.so.*")
if(NOT loader OR NOT EXISTS "${lib}/libc.so.6")
    message(FATAL_ERROR "FELF's glibc: no loader or libc.so.6 in ${lib}")
endif()
foreach(file IN LISTS loader libs)
    get_filename_component(name "${file}" NAME)
    # Symlinks are resolved: the FELF holds plain files.
    file(REAL_PATH "${file}" real)
    file(COPY_FILE "${real}" "${OUT}/lib/${name}")
endforeach()

# The loader must run on its own (a stray DT_RUNPATH, say from a compiler
# wrapper, makes it abort before it does anything).
list(GET loader 0 ld)
execute_process(COMMAND "${ld}" --version
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "FELF's glibc: ${ld} does not run (${rc}): ${err}")
endif()

# The locale archive, compiled by this glibc's localedef (run with this
# glibc's loader, from the stage) from this glibc's locale sources.
string(REPLACE "," ";" locales "${LOCALES}")
foreach(entry IN LISTS locales)
    string(REPLACE "/" ";" parts "${entry}")
    list(GET parts 0 locale)
    list(GET parts 1 charset)
    string(REGEX REPLACE "[.@].*" "" source "${locale}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            "I18NPATH=${STAGE}/usr/share/i18n"
            "${ld}" --library-path "${lib}"
            "${STAGE}/usr/bin/localedef"
            --prefix=${OUT}/root
            -i "${source}" -f "${charset}" "${locale}"
        RESULT_VARIABLE rc)
    # localedef exits 1 for warnings; anything more is an error.
    if(rc GREATER 1 OR NOT rc MATCHES "^[0-9]+$")
        message(FATAL_ERROR "FELF's glibc: localedef ${entry} failed (${rc})")
    endif()
endforeach()
file(RENAME "${OUT}/root/usr/lib/locale/locale-archive"
    "${OUT}/locale/locale-archive")
file(REMOVE_RECURSE "${OUT}/root")

file(TOUCH "${OUT}/stamp")
