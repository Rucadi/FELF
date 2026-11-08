include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Object descriptors start at 1000, and 998 and 999 are fixed.  The
# bootstrap raises its own soft limit only when a descriptor it needs is
# above it, and the application sees the caller's limit again.

# felf_run_limit(PREFIX ULIMIT-ARGS): run FILE under `ulimit ULIMIT-ARGS`
# with FELF_DEBUG=1.
function(felf_run_limit prefix limit)
    set(script "ulimit ${limit} && FELF_DEBUG=1 exec \"$1\"")
    execute_process(COMMAND "${BASH}" -c "${script}" bash "${FILE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    set(${prefix}_RESULT "${result}" PARENT_SCOPE)
    set(${prefix}_OUTPUT "${output}" PARENT_SCOPE)
    set(${prefix}_ERROR "${error}" PARENT_SCOPE)
endfunction()

# 1024, the common default, leaves room for every memfd: nothing is raised.
felf_run_limit(default "-Sn 1024")
felf_assert_rc("run with a soft limit of 1024" 0 "${default_RESULT}")
felf_assert_contains("application sees 1024" "${default_OUTPUT}"
    "nofile=1024")
felf_assert_not_contains("1024 is not raised" "${default_ERROR}"
    "raised open-file soft limit")

# 1001 holds descriptor 1000 only: the bootstrap raises it for the next
# memfd, and restores it before exec.
felf_run_limit(soft "-Sn 1001")
felf_assert_rc("run with a soft limit of 1001" 0 "${soft_RESULT}")
felf_assert_contains("1001 is raised" "${soft_ERROR}"
    "raised open-file soft limit 1001 ->")
felf_assert_contains("application sees the caller's soft limit"
    "${soft_OUTPUT}" "nofile=1001")

# A hard limit of 1000 leaves no room for descriptor 1000.
felf_run_limit(hard "-n 1000")
if("${hard_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "a hard limit of 1000 was accepted")
endif()
felf_assert_contains("too-low hard limit is a clear error" "${hard_ERROR}"
    "open-file limit too low")
