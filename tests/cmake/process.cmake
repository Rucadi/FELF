include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Exit statuses, signals and stdin are forwarded to the application.

felf_run(code "${EXIT}" code)
felf_assert_rc("exit code 42" 42 "${code_RESULT}")

# CTest treats a signal as a test crash; let the shell turn it into 128+15
# so the test can assert the exact value the caller sees.
execute_process(COMMAND "${BASH}" -c
        "\"$1\" signal; echo status=$?"
        bash "${EXIT}"
    RESULT_VARIABLE signal_result
    OUTPUT_VARIABLE signal_output
    ERROR_VARIABLE signal_error)
felf_assert_rc("signal wrapper" 0 "${signal_result}")
felf_assert_contains("SIGTERM is forwarded" "${signal_output}" "status=143")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/stdin" "piped input\n")
execute_process(COMMAND "${EXIT}"
    INPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/stdin"
    RESULT_VARIABLE stdin_result
    OUTPUT_VARIABLE stdin_output
    ERROR_VARIABLE stdin_error)
felf_assert_rc("stdin run" 0 "${stdin_result}")
felf_assert_contains("stdin is forwarded" "${stdin_output}" "stdin=piped input")
