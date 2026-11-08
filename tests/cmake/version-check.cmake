include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# A program built against VER_2 must not be bundled with a library that only
# defines VER_1: glibc would fail later, at load time, with a worse message.

get_filename_component(ver1_dir "${V1}" DIRECTORY)
get_filename_component(ver2_dir "${V2}" DIRECTORY)
set(ver2_gone "${ver2_dir}.gone")
file(REMOVE_RECURSE "${ver2_gone}")
set(restore FALSE)
if(EXISTS "${ver2_dir}")
    file(RENAME "${ver2_dir}" "${ver2_gone}")
    set(restore TRUE)
endif()

felf_run(bad "${FELF}" pack "${MAIN}" --lib-dir "${ver1_dir}" -o
    "${WORK}/ver-bad.felf")

if(restore)
    file(RENAME "${ver2_gone}" "${ver2_dir}")
endif()

felf_assert_rc("a missing symbol version is rejected" 1 "${bad_RESULT}")
felf_assert_contains("the rejection names the version" "${bad_ERROR}"
    "needs version VER_2")
felf_assert_contains("the rejection names the bundled library" "${bad_ERROR}"
    "does not define")

# The matching library packs and runs.
felf_run(good "${FELF}" pack "${MAIN}" --lib-dir "${ver2_dir}" -o
    "${WORK}/ver-good.felf")
felf_assert_rc("a matching symbol version packs" 0 "${good_RESULT}")
felf_run(run "${WORK}/ver-good.felf")
felf_assert_rc("the versioned FELF runs" 0 "${run_RESULT}")
felf_run(verify "${FELF}" verify "${WORK}/ver-good.felf")
felf_assert_rc("the versioned FELF verifies" 0 "${verify_RESULT}")
