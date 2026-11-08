include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Two programs of one FELF map the same libraries from the same file at the
# same offsets, so the page cache holds one copy.  PSS charges a page shared
# by N processes 1/N to each.  With --compress LEVEL each process has
# its own memfds and is charged all of it.

# mapping keys ("offset device inode") of executable mappings from PREFIX.
function(felf_map_keys file prefix out_var)
    file(STRINGS "${file}" lines)
    set(keys "")
    foreach(line IN LISTS lines)
        string(REGEX MATCH
            "^[0-9a-f]+-[0-9a-f]+ +([^ ]+) +([0-9a-f]+) +([0-9a-f]+:[0-9a-f]+) +([0-9]+) +(.*)$"
            match "${line}")
        if(match)
            set(perms "${CMAKE_MATCH_1}")
            set(offset "${CMAKE_MATCH_2}")
            set(dev "${CMAKE_MATCH_3}")
            set(inode "${CMAKE_MATCH_4}")
            set(path "${CMAKE_MATCH_5}")
            if(perms MATCHES "x")
                string(FIND "${path}" "${prefix}" pos)
                if(pos EQUAL 0)
                    list(APPEND keys "${offset} ${dev} ${inode}")
                endif()
            endif()
        endif()
    endforeach()
    list(SORT keys)
    set(${out_var} "${keys}" PARENT_SCOPE)
endfunction()

# "Rss Pss" in kB for executable mappings from PREFIX.
function(felf_map_memory file prefix out_var)
    file(STRINGS "${file}" lines)
    set(selected FALSE)
    set(rss 0)
    set(pss 0)
    foreach(line IN LISTS lines)
        if(line MATCHES
                "^[0-9a-f]+-[0-9a-f]+ +([^ ]+) +[0-9a-f]+ +[0-9a-f]+:[0-9a-f]+ +[0-9]+ +(.*)$")
            set(perms "${CMAKE_MATCH_1}")
            set(path "${CMAKE_MATCH_2}")
            set(selected FALSE)
            if(perms MATCHES "x")
                string(FIND "${path}" "${prefix}" pos)
                if(pos EQUAL 0)
                    set(selected TRUE)
                endif()
            endif()
        elseif(selected AND line MATCHES "^Rss: +([0-9]+)")
            math(EXPR rss "${rss} + ${CMAKE_MATCH_1}")
        elseif(selected AND line MATCHES "^Pss: +([0-9]+)")
            math(EXPR pss "${pss} + ${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${out_var} "${rss} ${pss}" PARENT_SCOPE)
endfunction()

# Start both programs, wait until both are ready, and snapshot their maps
# while they are alive.
set(collect [=[
set -e
one=$1
two=$2
dir=$3
"$one" >"$dir/out-one" 2>&1 & p1=$!
"$two" >"$dir/out-two" 2>&1 & p2=$!
trap 'kill $p1 $p2 2>/dev/null || true' EXIT
i=0
while [ "$i" -lt 100 ]; do
    if grep -q "one ready" "$dir/out-one" 2>/dev/null &&
       grep -q "two ready" "$dir/out-two" 2>/dev/null; then
        break
    fi
    sleep 0.1
    i=$((i + 1))
done
if [ "$i" -ge 100 ]; then
    cat "$dir/out-one" "$dir/out-two" >&2
    exit 1
fi
cat "/proc/$p1/maps" >"$dir/maps-one"
cat "/proc/$p2/maps" >"$dir/maps-two"
cat "/proc/$p1/smaps" >"$dir/smaps-one"
cat "/proc/$p2/smaps" >"$dir/smaps-two"
kill "$p1" "$p2" 2>/dev/null || true
wait "$p1" 2>/dev/null || true
wait "$p2" 2>/dev/null || true
echo collected
]=])

function(felf_pack_share mode out_dir)
    file(REMOVE_RECURSE "${out_dir}")
    file(MAKE_DIRECTORY "${out_dir}")
    set(extra)
    if(mode STREQUAL "lazy")
        set(extra --lazy)
    elseif(mode STREQUAL "compressed")
        set(extra --compress 19)
    endif()
    felf_run("pack_${mode}" "${FELF}" pack "${SHARE_ONE}"
        --program "${SHARE_TWO}" ${extra} -o "${out_dir}/share.felf")
    felf_assert_rc("${mode}: pack" 0 "${pack_${mode}_RESULT}")
    file(CREATE_LINK "share.felf" "${out_dir}/share-one" SYMBOLIC)
    file(CREATE_LINK "share.felf" "${out_dir}/share-two" SYMBOLIC)
endfunction()

foreach(mode preload lazy)
    set(dir "${WORK}/share-${mode}")
    felf_pack_share("${mode}" "${dir}")
    file(REAL_PATH "${dir}/share.felf" share_file)

    execute_process(COMMAND "${BASH}" -c "${collect}" bash
            "${dir}/share-one" "${dir}/share-two" "${dir}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    felf_assert_rc("${mode}: start both programs" 0 "${result}")

    felf_map_keys("${dir}/maps-one" "${share_file}" keys_one)
    felf_map_keys("${dir}/maps-two" "${share_file}" keys_two)
    if(keys_one STREQUAL "")
        message(FATAL_ERROR "${mode}: no library is mapped from the FELF file")
    endif()
    felf_assert_equal("${mode}: both programs map the same file parts"
        "${keys_one}" "${keys_two}")

    felf_map_memory("${dir}/smaps-one" "${share_file}" mem_one)
    felf_map_memory("${dir}/smaps-two" "${share_file}" mem_two)
    separate_arguments(mem1 UNIX_COMMAND "${mem_one}")
    list(GET mem1 0 rss1)
    list(GET mem1 1 pss1)
    separate_arguments(mem2 UNIX_COMMAND "${mem_two}")
    list(GET mem2 0 rss2)
    list(GET mem2 1 pss2)
    if(rss1 LESS 1 OR rss2 LESS 1)
        message(FATAL_ERROR "${mode}: no memory charged to the mappings")
    endif()
    math(EXPR pss1_x10 "${pss1} * 10")
    math(EXPR rss1_x6 "${rss1} * 6")
    if(pss1 GREATER 0 AND pss1_x10 GREATER rss1_x6)
        message(FATAL_ERROR
            "${mode}: library pages are not shared (Pss ${pss1} of Rss ${rss1} kB)")
    endif()
    math(EXPR pss2_x10 "${pss2} * 10")
    math(EXPR rss2_x6 "${rss2} * 6")
    if(pss2 GREATER 0 AND pss2_x10 GREATER rss2_x6)
        message(FATAL_ERROR
            "${mode}: library pages are not shared (Pss ${pss2} of Rss ${rss2} kB)")
    endif()
endforeach()

# Control: compressed libraries are memfds, one copy per process.
set(dir "${WORK}/share-compressed")
felf_pack_share(compressed "${dir}")
file(REAL_PATH "${dir}/share.felf" share_file)
execute_process(COMMAND "${BASH}" -c "${collect}" bash
        "${dir}/share-one" "${dir}/share-two" "${dir}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
felf_assert_rc("compressed: start both programs" 0 "${result}")

felf_map_keys("${dir}/maps-one" "${share_file}" file_keys)
if(NOT file_keys STREQUAL "")
    message(FATAL_ERROR
        "compressed: libraries are unexpectedly mapped from the FELF file")
endif()

felf_map_memory("${dir}/smaps-one" "/memfd:felf:libc.so.6" mem_one)
separate_arguments(mem UNIX_COMMAND "${mem_one}")
list(GET mem 0 rss)
list(GET mem 1 pss)
if(rss LESS 1)
    message(FATAL_ERROR "compressed: libc is not a memfd (Rss ${rss} kB)")
endif()
felf_assert_equal("compressed: a memfd libc is charged in full" "${rss}"
    "${pss}")
