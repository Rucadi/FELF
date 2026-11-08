include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# felf sql runs read-only queries only; it must not modify the database.

felf_run(select "${FELF}" sql "${FILE}" "SELECT 'read-ok'")
felf_assert_rc("SELECT" 0 "${select_RESULT}")
felf_assert_contains("SELECT output" "${select_OUTPUT}" "read-ok")

foreach(statement
        "DELETE FROM objects"
        "UPDATE objects SET name = 'x'"
        "INSERT INTO meta (key, value) VALUES ('x', 'y')"
        "CREATE TABLE bad (x INTEGER)"
        "DROP TABLE objects")
    string(MAKE_C_IDENTIFIER "${statement}" id)
    felf_run("write_${id}" "${FELF}" sql "${FILE}" "${statement}")
    felf_assert_rc("${statement}" 1 "${write_${id}_RESULT}")
    felf_assert_contains("${statement} is rejected"
        "${write_${id}_ERROR}" "not read-only")
endforeach()

# ATTACH counts as read-only to SQLite but creates the file it names.
get_filename_component(dir "${FILE}" DIRECTORY)
set(attached "${dir}/sql-attach-target.db")
file(REMOVE "${attached}")
felf_run(attach "${FELF}" sql "${FILE}" "ATTACH '${attached}' AS other")
felf_assert_rc("ATTACH" 1 "${attach_RESULT}")
felf_assert_contains("ATTACH is rejected" "${attach_ERROR}"
    "ATTACH and DETACH are not allowed")
felf_assert_no_file("ATTACH creates no file" "${attached}")

# The database is intact after the attempts.
felf_run(after "${FELF}" verify "${FILE}")
felf_assert_rc("verify after rejected writes" 0 "${after_RESULT}")
