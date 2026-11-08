# Assertions shared by the CMake-script tests (cmake -P).
#
# These are not a test runner: each script is one CTest test, and a failed
# check fails that test with a useful message.

# felf_run(PREFIX COMMAND...)
# Sets PREFIX_OUTPUT, PREFIX_ERROR and PREFIX_RESULT.
function(felf_run prefix)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 120)
    set(${prefix}_OUTPUT "${output}" PARENT_SCOPE)
    set(${prefix}_ERROR "${error}" PARENT_SCOPE)
    set(${prefix}_RESULT "${result}" PARENT_SCOPE)
endfunction()

# felf_run_ok(NAME COMMAND...): like felf_run, but fails on a non-zero exit.
function(felf_run_ok name)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 120)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR
            "${name}: exit ${result}\nstdout:\n${output}\nstderr:\n${error}")
    endif()
    set(FELF_LAST_OUTPUT "${output}" PARENT_SCOPE)
    set(FELF_LAST_ERROR "${error}" PARENT_SCOPE)
    set(FELF_LAST_RESULT "${result}" PARENT_SCOPE)
endfunction()

# felf_run_fail(NAME COMMAND...): like felf_run, but fails on a zero exit.
function(felf_run_fail name)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 120)
    if("${result}" STREQUAL "0")
        message(FATAL_ERROR
            "${name}: expected a failure, but it exited 0\n"
            "stdout:\n${output}\nstderr:\n${error}")
    endif()
    set(FELF_LAST_OUTPUT "${output}" PARENT_SCOPE)
    set(FELF_LAST_ERROR "${error}" PARENT_SCOPE)
    set(FELF_LAST_RESULT "${result}" PARENT_SCOPE)
endfunction()

function(felf_assert_contains name haystack needle)
    string(FIND "${haystack}" "${needle}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR
            "${name}: expected to find '${needle}' in:\n${haystack}")
    endif()
endfunction()

function(felf_assert_not_contains name haystack needle)
    string(FIND "${haystack}" "${needle}" pos)
    if(NOT pos EQUAL -1)
        message(FATAL_ERROR
            "${name}: did not expect '${needle}' in:\n${haystack}")
    endif()
endfunction()

function(felf_assert_equal name expected actual)
    if(NOT "${expected}" STREQUAL "${actual}")
        message(FATAL_ERROR
            "${name}: expected '${expected}', got '${actual}'")
    endif()
endfunction()

function(felf_assert_rc name expected actual)
    if(NOT "${expected}" STREQUAL "${actual}")
        message(FATAL_ERROR
            "${name}: expected exit ${expected}, got ${actual}")
    endif()
endfunction()

function(felf_assert_file name path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${name}: expected '${path}' to exist")
    endif()
endfunction()

function(felf_assert_no_file name path)
    if(EXISTS "${path}")
        message(FATAL_ERROR "${name}: did not expect '${path}'")
    endif()
endfunction()

function(felf_assert_symlink name path expected)
    if(NOT IS_SYMLINK "${path}")
        message(FATAL_ERROR "${name}: '${path}' is not a symbolic link")
    endif()
    file(READ_SYMLINK "${path}" target)
    if(NOT "${target}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "${name}: '${path}' points to '${target}', expected '${expected}'")
    endif()
endfunction()

function(felf_assert_same_file name a b)
    file(SHA256 "${a}" a_hash)
    file(SHA256 "${b}" b_hash)
    if(NOT "${a_hash}" STREQUAL "${b_hash}")
        message(FATAL_ERROR "${name}: '${a}' and '${b}' differ")
    endif()
endfunction()

# The packer rewrites each /nix/store/<hash> to 'e's.  Produce the same
# bytes as a hex string, so a script can compare them with file(READ HEX)
# without needing a binary-safe CMake string.
function(felf_scrub_store_hex in out_var)
    file(READ "${in}" hex HEX)
    set(prefix "2f6e69782f73746f72652f")  # "/nix/store/"
    # A 32-byte hash as 64 hex digits.  It could be restricted to Nix's
    # base-32 alphabet, but a character class keeps CMake's regex small
    # (nested groups for every byte exceed its parenthesis limit).
    set(bytes "")
    foreach(i RANGE 1 64)
        string(APPEND bytes "[0-9a-f]")
    endforeach()
    set(replacement "${prefix}")
    foreach(i RANGE 1 32)
        string(APPEND replacement "65")  # 'e'
    endforeach()
    string(REGEX REPLACE "${prefix}${bytes}" "${replacement}" hex "${hex}")
    set(${out_var} "${hex}" PARENT_SCOPE)
endfunction()

# Read a command's output and return the last line (e.g. a SQL value).
function(felf_last_line text out_var)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(STRIP "${text}" text)
    string(REGEX MATCH "[^\n]*$" line "${text}")
    set(${out_var} "${line}" PARENT_SCOPE)
endfunction()
