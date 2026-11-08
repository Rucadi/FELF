include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# cmake/FELF.cmake in a user's project: felf_pack builds a FELF with a
# linked and a dlopen'ed library, and the installed copy runs without the
# build tree.

set(build "${WORK}/cmake-module/build")
set(prefix "${WORK}/cmake-module/prefix")
file(REMOVE_RECURSE "${WORK}/cmake-module")

felf_run_ok("configure" "${CMAKE_COMMAND}" -S "${PROJECT}" -B "${build}"
    -G "${GENERATOR}"
    -DFELF_MODULE=${MODULE}
    -DFELF_EXECUTABLE=${FELF}
    -DFELF_DOWNLOAD=OFF
    -DCMAKE_INSTALL_PREFIX=${prefix})
felf_assert_contains("configure finds felf" "${FELF_LAST_OUTPUT}" "FELF: felf ")

felf_run_ok("build" "${CMAKE_COMMAND}" --build "${build}")
felf_assert_file("app.felf" "${build}/app.felf")

felf_run_ok("verify" "${FELF}" verify "${build}/app.felf")
felf_assert_contains("verify" "${FELF_LAST_OUTPUT}" "verification: OK")

felf_run_ok("install" "${CMAKE_COMMAND}" --install "${build}")
file(REMOVE_RECURSE "${build}")
felf_run_ok("run installed" "${prefix}/bin/app.felf")
felf_assert_contains("run installed" "${FELF_LAST_OUTPUT}"
    "greet extra env=packed")

file(CREATE_LINK app.felf "${prefix}/bin/hello" SYMBOLIC)
felf_run_ok("run as hello" "${prefix}/bin/hello")
felf_assert_contains("run as hello" "${FELF_LAST_OUTPUT}" "argv0=")
felf_assert_contains("run as hello" "${FELF_LAST_OUTPUT}" "hello greet")
