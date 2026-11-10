include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# The number of objects is not bounded: a FELF with thousands of libraries
# packs, verifies, and finds each of them by name (the loader index is
# sorted and searched, not scanned).

set(count 4200)
set(dir "${WORK}/many")
file(REMOVE_RECURSE "${dir}")
file(MAKE_DIRECTORY "${dir}")
set(args "")
foreach(i RANGE 1 ${count})
    file(COPY_FILE "${LIB}" "${dir}/libmany${i}.so")
    string(APPEND args "--include\n${dir}/libmany${i}.so\n")
endforeach()
file(WRITE "${dir}/args" "${args}")

felf_run_ok("pack ${count} libraries" "${FELF}" pack "${PROGRAM}"
    -o "${dir}/many.felf" "@${dir}/args")
felf_run_ok("verify" "${FELF}" verify "${dir}/many.felf")
felf_run_ok("list" "${FELF}" sql "${dir}/many.felf"
    "SELECT COUNT(*) FROM objects WHERE name LIKE 'libmany%'")
felf_last_line("${FELF_LAST_OUTPUT}" libraries)
felf_assert_equal("every library is embedded" "${count}" "${libraries}")

felf_run_ok("load the first, a middle and the last library"
    "${dir}/many.felf" libmany1.so libmany2100.so "libmany${count}.so")
felf_assert_contains("first" "${FELF_LAST_OUTPUT}" "libmany1.so: value=7")
felf_assert_contains("middle" "${FELF_LAST_OUTPUT}" "libmany2100.so: value=7")
felf_assert_contains("last" "${FELF_LAST_OUTPUT}"
    "libmany${count}.so: value=7")

felf_run_fail("a name that is not bundled" "${dir}/many.felf" libmany0.so)
file(REMOVE_RECURSE "${dir}")
