include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Many processes can run the same FELF at once: each decompresses its own
# objects into its own memfds, and arguments do not leak between them.

set(dir "${WORK}/concurrent")
file(REMOVE_RECURSE "${dir}")
file(MAKE_DIRECTORY "${dir}")

set(script [=[
set -e
dir=$1
file=$2
pids=""
i=1
while [ "$i" -le 12 ]; do
    "$file" "run$i" >"$dir/out$i" 2>"$dir/err$i" &
    pids="$pids $!"
    i=$((i + 1))
done
for pid in $pids; do
    wait "$pid"
done
i=1
while [ "$i" -le 12 ]; do
    grep -Fq "arg1=run$i" "$dir/out$i"
    i=$((i + 1))
done
echo concurrent-ok
]=])

execute_process(COMMAND "${BASH}" -c "${script}" bash "${dir}" "${FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
felf_assert_rc("twelve concurrent runs" 0 "${result}")
felf_assert_contains("all runs completed" "${output}" "concurrent-ok")
