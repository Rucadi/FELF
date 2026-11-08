include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Program names are unique.  Naming a file that is already embedded is
# allowed (it becomes an alias for the same object) only through --program
# with the same path; a second program name for a different file is not.

felf_run(duplicate "${FELF}" pack "${HELLO}" -o "${WORK}/multi-dup.felf"
    --program "hello=${NOFILE}")
felf_assert_rc("a duplicate program name" 1 "${duplicate_RESULT}")
felf_assert_contains("duplicate program message" "${duplicate_ERROR}"
    "already exists")

felf_run(bad_name "${FELF}" pack "${HELLO}" -o "${WORK}/multi-bad-name.felf"
    --program "=${NOFILE}")
felf_assert_rc("an invalid program name" 1 "${bad_name_RESULT}")
felf_assert_contains("invalid program name message" "${bad_name_ERROR}"
    "invalid program name")
