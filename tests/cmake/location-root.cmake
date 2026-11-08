include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# A tree whose program symlinks were replaced by copies of the FELF
# (cp -L, tar -h, ...): the copy at libexec/tool/sub must still find the
# tree root from its own location.

set(loc "${WORK}/loc")
file(REMOVE_RECURSE "${loc}")
file(MAKE_DIRECTORY "${loc}/bin" "${loc}/libexec/tool")
felf_run(copy_program "${CMAKE_COMMAND}" -E copy "${EXE}"
    "${loc}/libexec/tool/sub")
felf_assert_rc("copy the second program" 0 "${copy_program_RESULT}")

felf_run(pack "${FELF}" pack "${HELLO}" --lazy --root .. --program
    "${loc}/libexec/tool/sub" -o "${loc}/bin/main")
felf_assert_rc("pack the tree" 0 "${pack_RESULT}")

felf_run(copy_felf "${CMAKE_COMMAND}" -E copy "${loc}/bin/main"
    "${loc}/libexec/tool/sub")
felf_assert_rc("copy the FELF over the second program" 0
    "${copy_felf_RESULT}")

file(REAL_PATH "${loc}" loc_root)

felf_run(copied "${loc}/libexec/tool/sub")
felf_assert_rc("the copied program runs" 0 "${copied_RESULT}")
felf_assert_contains("the copied program finds the tree root" "${copied_OUTPUT}"
    "FELF_ROOT=${loc_root}")

felf_run(main "${loc}/bin/main")
felf_assert_rc("the main FELF runs" 0 "${main_RESULT}")
felf_assert_contains("the main FELF still runs the default program"
    "${main_OUTPUT}" "Hello from FELF!")

execute_process(COMMAND "${CMAKE_COMMAND}" -E chdir "${loc}"
        ./libexec/tool/sub
    RESULT_VARIABLE relative_result
    OUTPUT_VARIABLE relative_output
    ERROR_VARIABLE relative_error)
felf_assert_rc("the copied program runs through a relative path" 0
    "${relative_result}")
felf_assert_contains("the relative copy finds the tree root"
    "${relative_output}" "FELF_ROOT=${loc_root}")
