include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Names, offsets, compression and the database are deterministic: two packs
# of the same input are byte-for-byte identical.  Exercise the default,
# an explicit level, uncompressed objects and lazy mode.

foreach(case default level19 plain lazy)
    set(first "${WORK}/repro-${case}-1.felf")
    set(second "${WORK}/repro-${case}-2.felf")
    set(extra)
    if(case STREQUAL "level19")
        set(extra --compress 19)
    elseif(case STREQUAL "plain")
        set(extra --compress none)
    elseif(case STREQUAL "lazy")
        set(extra --lazy)
    endif()

    felf_run("${case}_first" "${FELF}" pack "${HELLO}" ${extra} -o "${first}")
    felf_assert_rc("${case}: first pack" 0 "${${case}_first_RESULT}")
    felf_run("${case}_second" "${FELF}" pack "${HELLO}" ${extra} -o "${second}")
    felf_assert_rc("${case}: second pack" 0 "${${case}_second_RESULT}")
    felf_assert_same_file("${case}: identical packs" "${first}" "${second}")
endforeach()
