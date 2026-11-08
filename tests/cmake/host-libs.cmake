include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Host-preferred libraries: at runtime the host copy is tried first, the
# embedded copy is the fallback, and dependencies of a host object may come
# from the host even in strict mode.

set(host_build "${WORK}/host-build")
set(host_libs "${WORK}/host-libs")
felf_assert_file("the embedded host library" "${HOSTLIB}")
felf_assert_file("the host copy" "${HOSTLIB_COPY}")
felf_assert_file("the host-only vendor library" "${HOST_VENDOR}")

foreach(mode lazy preload)
    set(extra)
    if(mode STREQUAL "lazy")
        set(extra --lazy)
    endif()
    set(file "${WORK}/host-${mode}.felf")
    felf_run("pack_${mode}" "${FELF}" pack "${HOST}" ${extra}
        --lib-dir "${host_build}" --host-lib libfelfhost.so.1 -o "${file}")
    felf_assert_rc("${mode}: pack" 0 "${pack_${mode}_RESULT}")

    felf_run("host_${mode}" "${ENV}" "LD_LIBRARY_PATH=${host_libs}" "${file}")
    felf_assert_rc("${mode}: host run" 0 "${host_${mode}_RESULT}")
    felf_assert_contains("${mode}: the host copy is preferred"
        "${host_${mode}_OUTPUT}" "host value=42")

    felf_run("fallback_${mode}" "${ENV}" -u LD_LIBRARY_PATH "${file}")
    felf_assert_rc("${mode}: fallback run" 0 "${fallback_${mode}_RESULT}")
    felf_assert_contains("${mode}: the embedded copy is the fallback"
        "${fallback_${mode}_OUTPUT}" "host value=1")

    felf_run("verify_${mode}" "${FELF}" verify "${file}")
    felf_assert_rc("${mode}: verify" 0 "${verify_${mode}_RESULT}")
endforeach()

felf_run(meta "${FELF}" sql "${WORK}/host-lazy.felf"
    "SELECT value FROM meta WHERE key = 'host_libs'")
felf_assert_rc("host library metadata" 0 "${meta_RESULT}")
felf_assert_contains("the default graphics stack is recorded"
    "${meta_OUTPUT}" "libGL.so.1")
felf_assert_contains("the extra host library is recorded"
    "${meta_OUTPUT}" "libfelfhost.so.1")

# Without --host-lib the bundled copy always wins.
felf_run(pack_plain "${FELF}" pack "${HOST}" --lazy --lib-dir "${host_build}"
    -o "${WORK}/host-plain.felf")
felf_assert_rc("plain pack" 0 "${pack_plain_RESULT}")
felf_run(plain "${ENV}" "LD_LIBRARY_PATH=${host_libs}"
    "${WORK}/host-plain.felf")
felf_assert_rc("plain run" 0 "${plain_RESULT}")
felf_assert_contains("a non-host library stays bundled" "${plain_OUTPUT}"
    "host value=1")

# A host-preferred library that is missing at pack time is not an error:
# the host copy is used, and there is no embedded fallback.
felf_run(pack_missing "${FELF}" pack "${HOST}" --lazy
    --host-lib "libfelfhost*" -o "${WORK}/host-missing.felf")
felf_assert_rc("a missing host library does not fail the pack" 0
    "${pack_missing_RESULT}")
felf_run(missing "${ENV}" "LD_LIBRARY_PATH=${host_libs}"
    "${WORK}/host-missing.felf")
felf_assert_rc("missing host library run" 0 "${missing_RESULT}")
felf_assert_contains("the prefix pattern resolves on the host"
    "${missing_OUTPUT}" "host value=42")
felf_run(verify_missing "${FELF}" verify "${WORK}/host-missing.felf")
felf_assert_rc("a FELF without its host library verifies" 0
    "${verify_missing_RESULT}")

# --no-default-host-libs leaves the graphics stack alone.
felf_run(pack_nodefault "${FELF}" pack "${HOST}" --lazy --lib-dir
    "${host_build}" --no-default-host-libs -o "${WORK}/host-nodefault.felf")
felf_assert_rc("--no-default-host-libs pack" 0 "${pack_nodefault_RESULT}")
felf_run(no_default_meta "${FELF}" sql "${WORK}/host-nodefault.felf"
    "SELECT COALESCE((SELECT value FROM meta WHERE key = 'host_libs'), '')")
felf_assert_rc("host metadata query" 0 "${no_default_meta_RESULT}")
felf_assert_not_contains("the default stack is not recorded"
    "${no_default_meta_OUTPUT}" "libGL.so.1")
felf_run(no_default "${ENV}" "LD_LIBRARY_PATH=${host_libs}"
    "${WORK}/host-nodefault.felf")
felf_assert_rc("--no-default-host-libs run" 0 "${no_default_RESULT}")
felf_assert_contains("--no-default-host-libs keeps the bundled copy"
    "${no_default_OUTPUT}" "host value=1")
