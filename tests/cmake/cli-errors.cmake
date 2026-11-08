include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Usage and argument errors.  The CLI reports these on stderr with exit 2;
# a missing input file is an ordinary failure (exit 1).

felf_run(no_args "${FELF}")
felf_assert_rc("no arguments" 2 "${no_args_RESULT}")
felf_assert_contains("no arguments prints usage" "${no_args_ERROR}" "Usage:")

felf_run(unknown "${FELF}" frobnicate)
felf_assert_rc("unknown command" 2 "${unknown_RESULT}")
felf_assert_contains("unknown command is named" "${unknown_ERROR}"
    "unknown command: frobnicate")

felf_run(pack_no_input "${FELF}" pack -o "${CMAKE_CURRENT_BINARY_DIR}/x")
felf_assert_rc("pack without input" 2 "${pack_no_input_RESULT}")
felf_assert_contains("pack without input message" "${pack_no_input_ERROR}"
    "requires an input file")

felf_run(pack_no_output "${FELF}" pack "${FELF}")
felf_assert_rc("pack without output" 2 "${pack_no_output_RESULT}")
felf_assert_contains("pack without output message" "${pack_no_output_ERROR}"
    "requires -o/--output")

felf_run(pack_extra "${FELF}" pack "${FELF}" -o "${CMAKE_CURRENT_BINARY_DIR}/x"
    extra)
felf_assert_rc("pack with an extra argument" 2 "${pack_extra_RESULT}")
felf_assert_contains("pack extra argument message" "${pack_extra_ERROR}"
    "unexpected argument: extra")

felf_run(pack_compress "${FELF}" pack "${FELF}" -o
    "${CMAKE_CURRENT_BINARY_DIR}/x" --compress gzip)
felf_assert_rc("pack with a bad --compress" 2 "${pack_compress_RESULT}")
felf_assert_contains("bad --compress message" "${pack_compress_ERROR}"
    "expects a zstd level")

felf_run(pack_complibs "${FELF}" pack "${FELF}" -o
    "${CMAKE_CURRENT_BINARY_DIR}/x" --compress-libraries)
felf_assert_rc("pack with --compress-libraries" 2 "${pack_complibs_RESULT}")
felf_assert_contains("--compress-libraries message" "${pack_complibs_ERROR}"
    "--compress LEVEL now compresses the libraries")

felf_run(pack_env "${FELF}" pack "${FELF}" -o
    "${CMAKE_CURRENT_BINARY_DIR}/x" --env NOVALUE)
felf_assert_rc("pack with a bad --env" 2 "${pack_env_RESULT}")
felf_assert_contains("bad --env message" "${pack_env_ERROR}"
    "expects NAME=VALUE")

foreach(level 0 23)
    felf_run("pack_level_${level}" "${FELF}" pack "${FELF}" -o
        "${CMAKE_CURRENT_BINARY_DIR}/x" --compress "${level}")
    felf_assert_rc("--compress ${level}" 2 "${pack_level_${level}_RESULT}")
    felf_assert_contains("--compress ${level} message"
        "${pack_level_${level}_ERROR}" "expects a zstd level")
endforeach()

felf_run(pack_alias "${FELF}" pack "${HELLO}" -o
    "${CMAKE_CURRENT_BINARY_DIR}/x" --alias NOEQUALS)
felf_assert_rc("pack with a bad --alias" 1 "${pack_alias_RESULT}")
felf_assert_contains("bad --alias message" "${pack_alias_ERROR}"
    "expects PROGRAM=NAME")

felf_run(inspect_missing "${FELF}" inspect "${CMAKE_CURRENT_BINARY_DIR}/nope")
felf_assert_rc("inspect a missing file" 1 "${inspect_missing_RESULT}")
felf_assert_contains("inspect missing message" "${inspect_missing_ERROR}"
    "cannot open")

felf_run(verify_args "${FELF}" verify)
felf_assert_rc("verify without a file" 2 "${verify_args_RESULT}")
felf_assert_contains("verify usage message" "${verify_args_ERROR}"
    "requires exactly one file")

felf_run(extract_no_output "${FELF}" extract "${FELF}")
felf_assert_rc("extract without --output" 2 "${extract_no_output_RESULT}")
felf_assert_contains("extract usage message" "${extract_no_output_ERROR}"
    "requires --output DIR")

felf_run(sql_no_file "${FELF}" sql)
felf_assert_rc("sql without a file" 2 "${sql_no_file_RESULT}")
felf_assert_contains("sql usage message" "${sql_no_file_ERROR}"
    "requires FILE and QUERY")

# @FILE expands to one argument per line; comments and empty lines are
# skipped.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/response" "# a comment\n\n--help\n")
felf_run(response "${FELF}" "@${CMAKE_CURRENT_BINARY_DIR}/response")
felf_assert_rc("@FILE expansion" 0 "${response_RESULT}")
felf_assert_contains("@FILE help" "${response_OUTPUT}" "felf pack")

felf_run(response_missing "${FELF}" pack
    "@${CMAKE_CURRENT_BINARY_DIR}/not-there" -o
    "${CMAKE_CURRENT_BINARY_DIR}/x")
felf_assert_rc("missing @FILE" 1 "${response_missing_RESULT}")
felf_assert_contains("missing @FILE message" "${response_missing_ERROR}"
    "cannot open")

# Sub-command help goes to stdout and exits 0.
foreach(command pack inspect verify extract)
    felf_run("help_${command}" "${FELF}" "${command}" --help)
    felf_assert_rc("${command} --help" 0 "${help_${command}_RESULT}")
    felf_assert_contains("${command} --help output" "${help_${command}_OUTPUT}"
        "Usage:")
endforeach()
