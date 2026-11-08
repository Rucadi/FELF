include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Inputs that are not FELFs are rejected by every reader, not just verify.

set(text "${WORK}/not-a-felf.txt")
set(empty "${WORK}/empty.felf")
file(WRITE "${text}" "this is not an ELF file\n")
file(WRITE "${empty}" "")

foreach(command inspect list verify)
    felf_run("${command}_text" "${FELF}" "${command}" "${text}")
    felf_assert_rc("${command} text" 1 "${${command}_text_RESULT}")

    felf_run("${command}_empty" "${FELF}" "${command}" "${empty}")
    felf_assert_rc("${command} empty" 1 "${${command}_empty_RESULT}")
endforeach()

felf_run(sql_text "${FELF}" sql "${text}" "SELECT 1")
felf_assert_rc("sql text" 1 "${sql_text_RESULT}")

felf_run(extract_text "${FELF}" extract "${text}" --output
    "${WORK}/not-a-felf-out")
felf_assert_rc("extract text" 1 "${extract_text_RESULT}")
