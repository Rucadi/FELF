include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# FELF_INSTALL_LINKS=DIR creates DIR/<program> and DIR/<alias> symlinks to
# the FELF instead of running a program.  Links in the FELF's own directory
# are relative; links elsewhere are absolute.  Existing files are kept.

file(REAL_PATH "${FILE}" file_real)

# Start from a clean set of links, but keep the FELF itself.
file(GLOB links "${WORK}/links/*")
list(REMOVE_ITEM links "${FILE}")
file(REMOVE ${links})
file(REMOVE_RECURSE "${WORK}/links-abs" "${WORK}/links-busy")
file(MAKE_DIRECTORY "${WORK}/links-abs" "${WORK}/links-busy")

execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "FELF_INSTALL_LINKS=${WORK}/links" "${FILE}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
felf_assert_rc("install links" 0 "${install_result}")
felf_assert_contains("install links reports them" "${install_error}"
    "4 links created")
felf_assert_not_contains("install links runs no program" "${install_output}"
    "applet=")

foreach(name multicall three four nofile)
    felf_assert_symlink("${name} links to the FELF" "${WORK}/links/${name}"
        "box")
endforeach()

felf_run(alias "${WORK}/links/three")
felf_assert_rc("run an installed alias" 0 "${alias_RESULT}")
felf_assert_contains("alias runs with its argv[0]" "${alias_OUTPUT}"
    "applet=three")

felf_run(program "${WORK}/links/nofile")
felf_assert_rc("run an installed program link" 0 "${program_RESULT}")
felf_assert_contains("installed link selects the other program"
    "${program_OUTPUT}" "nofile=")

# Running it again counts the existing links and creates nothing.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "FELF_INSTALL_LINKS=${WORK}/links" "${FILE}"
    RESULT_VARIABLE again_result
    OUTPUT_VARIABLE again_output
    ERROR_VARIABLE again_error)
felf_assert_rc("install links again" 0 "${again_result}")
felf_assert_contains("existing links are counted" "${again_error}"
    "0 links created")
felf_assert_contains("all links already exist" "${again_error}"
    "4 already there")

# A link elsewhere points at the FELF's absolute path.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "FELF_INSTALL_LINKS=${WORK}/links-abs" "${FILE}"
    RESULT_VARIABLE abs_result
    OUTPUT_VARIABLE abs_output
    ERROR_VARIABLE abs_error)
felf_assert_rc("install links elsewhere" 0 "${abs_result}")
felf_assert_symlink("an outside link is absolute" "${WORK}/links-abs/three"
    "${file_real}")

# An existing unrelated file is not replaced; the other links are made.
file(WRITE "${WORK}/links-busy/three" "mine\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "FELF_INSTALL_LINKS=${WORK}/links-busy" "${FILE}"
    RESULT_VARIABLE busy_result
    OUTPUT_VARIABLE busy_output
    ERROR_VARIABLE busy_error)
if("${busy_result}" STREQUAL "0")
    message(FATAL_ERROR "install links with an existing file succeeded")
endif()
felf_assert_contains("an existing file is reported" "${busy_error}"
    "not replaced")
felf_run(busy_file "${CMAKE_COMMAND}" -E cat "${WORK}/links-busy/three")
felf_assert_equal("the existing file was not changed" "mine\n"
    "${busy_file_OUTPUT}")
felf_assert_symlink("the other links are still made"
    "${WORK}/links-busy/four" "${file_real}")

# A missing destination is an error, not a silent no-op.
felf_run(missing_dir "${CMAKE_COMMAND}" -E env
    "FELF_INSTALL_LINKS=${WORK}/no-such-directory" "${FILE}")
felf_assert_rc("install links into a missing directory" 1
    "${missing_dir_RESULT}")
felf_assert_contains("missing link directory message" "${missing_dir_ERROR}"
    "is not a directory")

# --alias must name a program in the FELF and may not collide with another
# program name.
felf_run(bad_alias "${FELF}" pack "${MULTICALL}" -o "${WORK}/alias-bad.felf"
    --alias nosuch=x)
felf_assert_rc("--alias of an unknown program" 1 "${bad_alias_RESULT}")
felf_assert_contains("unknown alias message" "${bad_alias_ERROR}"
    "no program named nosuch")

felf_run(dup_alias "${FELF}" pack "${MULTICALL}" -o "${WORK}/alias-dup.felf"
    --program "${NOFILE}" --alias "multicall=nofile")
felf_assert_rc("--alias that names another program" 1 "${dup_alias_RESULT}")
felf_assert_contains("duplicate alias message" "${dup_alias_ERROR}"
    "already exists")
