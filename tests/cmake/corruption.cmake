include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# The checksums detect damage to an object, to the footer and to the file
# length.  Each case starts from an intact copy.

felf_run(offset "${FELF}" sql "${FILE}"
    "SELECT offset FROM objects WHERE kind = 'program'")
felf_assert_rc("query the program offset" 0 "${offset_RESULT}")
felf_last_line("${offset_OUTPUT}" program_offset)
if(NOT program_offset MATCHES "^[0-9]+$")
    message(FATAL_ERROR "no numeric program offset in:\n${offset_OUTPUT}")
endif()

set(object "${WORK}/corrupt-object.felf")
felf_run(copy_object "${CMAKE_COMMAND}" -E copy "${FILE}" "${object}")
felf_assert_rc("copy for object corruption" 0 "${copy_object_RESULT}")
math(EXPR object_byte "${program_offset} + 100")
felf_run(flip_object "${MANGLE}" flip "${object}" "${object_byte}" 255)
felf_assert_rc("damage an object" 0 "${flip_object_RESULT}")
felf_run(verify_object "${FELF}" verify "${object}")
felf_assert_rc("object corruption is detected" 1 "${verify_object_RESULT}")

set(footer "${WORK}/corrupt-footer.felf")
felf_run(copy_footer "${CMAKE_COMMAND}" -E copy "${FILE}" "${footer}")
felf_assert_rc("copy for footer corruption" 0 "${copy_footer_RESULT}")
file(SIZE "${footer}" footer_size)
math(EXPR footer_byte "${footer_size} - 256")
felf_run(flip_footer "${MANGLE}" flip "${footer}" "${footer_byte}" 255)
felf_assert_rc("damage the footer" 0 "${flip_footer_RESULT}")
felf_run(verify_footer "${FELF}" verify "${footer}")
felf_assert_rc("footer corruption is detected" 1 "${verify_footer_RESULT}")

set(truncated "${WORK}/corrupt-truncated.felf")
felf_run(copy_truncated "${CMAKE_COMMAND}" -E copy "${FILE}" "${truncated}")
felf_assert_rc("copy for truncation" 0 "${copy_truncated_RESULT}")
file(SIZE "${truncated}" truncated_size)
math(EXPR truncated_size "${truncated_size} - 1")
felf_run(truncate "${MANGLE}" truncate "${truncated}" "${truncated_size}")
felf_assert_rc("truncate a FELF" 0 "${truncate_RESULT}")
felf_run(verify_truncated "${FELF}" verify "${truncated}")
felf_assert_rc("truncation is detected" 1 "${verify_truncated_RESULT}")

# The intact file still verifies.
felf_run(verify "${FELF}" verify "${FILE}")
felf_assert_rc("intact FELF verifies" 0 "${verify_RESULT}")
