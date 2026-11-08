include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Object descriptors start at 1000.  The bootstrap raises its own soft
# limit, but the application must see the caller's limit again.

set(soft_script [=[
ulimit -Sn 1010
exec "$1"
]=])
execute_process(COMMAND "${BASH}" -c "${soft_script}" bash "${FILE}"
    RESULT_VARIABLE soft_result
    OUTPUT_VARIABLE soft_output
    ERROR_VARIABLE soft_error)
felf_assert_rc("run with a low soft limit" 0 "${soft_result}")
felf_assert_contains("application sees the caller's soft limit"
    "${soft_output}" "nofile=1010")

set(hard_script [=[
ulimit -n 1010
"$1"
]=])
execute_process(COMMAND "${BASH}" -c "${hard_script}" bash "${FILE}"
    RESULT_VARIABLE hard_result
    OUTPUT_VARIABLE hard_output
    ERROR_VARIABLE hard_error)
if("${hard_result}" STREQUAL "0")
    message(FATAL_ERROR "a hard limit of 1010 was accepted")
endif()
felf_assert_contains("too-low hard limit is a clear error" "${hard_error}"
    "open-file limit too low")
