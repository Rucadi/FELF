include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# A FELF runs from a read-only directory without writing anything.

set(dir "${WORK}/read-only")
file(REMOVE_RECURSE "${dir}")
file(MAKE_DIRECTORY "${dir}")
felf_run(copy "${CMAKE_COMMAND}" -E copy "${FILE}" "${dir}/program")
felf_assert_rc("copy the program" 0 "${copy_RESULT}")
file(CHMOD "${dir}/program" PERMISSIONS
    OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(CHMOD "${dir}" PERMISSIONS
    OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

felf_run(run "${dir}/program" readonly)
felf_assert_rc("run from a read-only directory" 0 "${run_RESULT}")
felf_assert_contains("read-only output" "${run_OUTPUT}" "Hello from FELF!")

# Restore write permission so the build directory can be cleaned up.
file(CHMOD "${dir}" PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ
    WORLD_EXECUTE)
