include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Embedded objects carry no /nix/store references (Nix would treat them as
# dependencies of the FELF).  The program's PT_INTERP is the reference the
# test can locate reliably; if the build did not use a store interpreter,
# there is nothing to check.

felf_run(interp "${READELF}" -lW "${PROGRAM}")
felf_assert_rc("readelf" 0 "${interp_RESULT}")
if(NOT interp_OUTPUT MATCHES "interpreter: ([^]]+)")
    message(STATUS "direct/store-references: no PT_INTERP, skipped")
    return()
endif()
set(interp_path "${CMAKE_MATCH_1}")

if(NOT interp_path MATCHES "/nix/store/([0-9abcdfghijklmnpqrsvwxyz]{32})")
    message(STATUS
        "direct/store-references: interpreter is not in /nix/store, skipped")
    return()
endif()
set(hash "${CMAKE_MATCH_1}")
string(HEX "${hash}" hash_hex)

file(READ "${FELF}" felf_hex HEX)
string(FIND "${felf_hex}" "${hash_hex}" pos)
if(NOT pos EQUAL -1)
    message(FATAL_ERROR
        "the FELF still contains the store hash ${hash} of its interpreter")
endif()
