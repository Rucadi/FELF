include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# The runtime shim makes /proc/self/exe and the application environment look
# right: FELF_ROOT is the tree root, --env values are expanded, loader
# variables are removed before main(), and child processes do not inherit
# FELF's descriptors or loader hooks.

file(MAKE_DIRECTORY "${WORK}/app/bin" "${WORK}/app/share")
file(REAL_PATH "${WORK}/app" app_root)
file(REAL_PATH "${WORK}/app/bin" app_bin)

foreach(mode lazy preload)
    set(extra)
    if(mode STREQUAL "lazy")
        set(extra --lazy)
    endif()
    set(app "${app_bin}/app-${mode}")
    felf_run("pack_${mode}" "${FELF}" pack "${EXE}" ${extra} --root ..
        --env "APP_DATA=$FELF_ROOT/share" --env "APP_HOME=$HOME"
        --env "APP_LITERAL=$$x" -o "${app}")
    felf_assert_rc("${mode}: pack the app" 0 "${pack_${mode}_RESULT}")
    file(REAL_PATH "${app}" app_real)

    felf_run("run_${mode}" "${CMAKE_COMMAND}" -E env
        APP_DATA=overridden LD_LIBRARY_PATH=/host/lib "${app}")
    felf_assert_rc("${mode}: run the app" 0 "${run_${mode}_RESULT}")
    set(out "${run_${mode}_OUTPUT}")

    felf_assert_contains("${mode}: readlink /proc/self/exe" "${out}"
        "readlink=${app_real}")
    felf_assert_contains("${mode}: realpath /proc/self/exe" "${out}"
        "realpath=${app_real}")
    felf_assert_contains("${mode}: realpath /proc/<pid>/exe" "${out}"
        "pid-realpath=${app_real}")
    felf_assert_contains("${mode}: getauxval(AT_EXECFN)" "${out}"
        "execfn=${app_real}")
    felf_assert_contains("${mode}: LD_PRELOAD is removed" "${out}"
        "LD_PRELOAD=(unset)")
    felf_assert_contains("${mode}: LD_AUDIT is removed" "${out}"
        "LD_AUDIT=(unset)")
    felf_assert_contains("${mode}: LD_LIBRARY_PATH is restored" "${out}"
        "LD_LIBRARY_PATH=/host/lib")
    felf_assert_contains("${mode}: FELF_APP_PATH is removed" "${out}"
        "FELF_APP_PATH=(unset)")
    felf_assert_contains("${mode}: FELF_ROOT" "${out}"
        "FELF_ROOT=${app_root}")
    felf_assert_contains("${mode}: env expands FELF_ROOT and overrides the caller"
        "${out}" "APP_DATA=${app_root}/share")
    felf_assert_contains("${mode}: env expands caller variables" "${out}"
        "APP_HOME=$ENV{HOME}")
    felf_assert_contains("${mode}: env supports a literal dollar" "${out}"
        "APP_LITERAL=$x")
    felf_assert_contains("${mode}: no inheritable FELF descriptors" "${out}"
        "inheritable-felf-fds=0")

    felf_run("child_${mode}" "${CMAKE_COMMAND}" -E env
        APP_DATA=overridden LD_LIBRARY_PATH=/host/lib "${app}" env)
    felf_assert_rc("${mode}: child runs" 0 "${child_${mode}_RESULT}")
    felf_assert_contains("${mode}: child gets the expanded environment"
        "${child_${mode}_OUTPUT}" "APP_DATA=${app_root}/share")
    felf_assert_not_contains("${mode}: child has no LD_AUDIT"
        "${child_${mode}_OUTPUT}" "LD_AUDIT=/")
    felf_assert_not_contains("${mode}: child has no LD_PRELOAD"
        "${child_${mode}_OUTPUT}" "LD_PRELOAD=/")
endforeach()

# A program that replaces getenv/setenv/unsetenv (as bash does for shell
# variables) still gets the caller's environment back and does not hang.
felf_run(pack_ownenv "${FELF}" pack "${OWNENV}" --lazy -o
    "${WORK}/ownenv.felf")
felf_assert_rc("ownenv: pack" 0 "${pack_ownenv_RESULT}")
felf_run(ownenv "${ENV}" -u LD_PRELOAD -u LD_AUDIT
    LD_LIBRARY_PATH=/host/lib "${WORK}/ownenv.felf")
felf_assert_rc("ownenv: run" 0 "${ownenv_RESULT}")
felf_assert_contains("ownenv: the program runs" "${ownenv_OUTPUT}" "done")
felf_assert_contains("ownenv: LD_LIBRARY_PATH is restored" "${ownenv_OUTPUT}"
    "LD_LIBRARY_PATH=/host/lib")
felf_assert_not_contains("ownenv: no FELF variable is left" "${ownenv_OUTPUT}"
    "FELF_ORIG_")
felf_assert_not_contains("ownenv: no LD_PRELOAD is left" "${ownenv_OUTPUT}"
    "LD_PRELOAD=")
