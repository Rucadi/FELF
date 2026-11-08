# FELF's CTest suite.
#
# Everything is registered with add_test: fixture programs are CMake targets,
# packed files and directory trees are CTest fixtures, and the tests that need
# several steps (extract, corruption, trees, ...) are small CMake
# scripts under cmake/.  Run it with:
#
#   ctest --test-dir build --output-on-failure
#   ctest --test-dir build -L direct
#
# A test that needs an artifact says FIXTURES_REQUIRED, so CTest builds the
# artifact once (setup/pack-*) and skips the tests honestly if it fails.

if(NOT FELF_BUILD_TESTS)
    return()
endif()

if(FELF_TEST_CLI)
    set(felf_cli "${FELF_TEST_CLI}")
elseif(TARGET felf)
    set(felf_cli "$<TARGET_FILE:felf>")
else()
    message(FATAL_ERROR
        "FELF_BUILD_TESTS needs a felf CLI: build FELF_BUILD_CLI or set "
        "FELF_TEST_CLI=/path/to/felf")
endif()

include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
    enable_language(CXX)
    set(felf_have_cxx TRUE)
else()
    message(STATUS "FELF tests: no C++ compiler, C++ tests disabled")
    set(felf_have_cxx FALSE)
endif()

find_program(FELF_TEST_BASH NAMES bash)
find_program(FELF_TEST_ENV NAMES env)
find_program(FELF_TEST_READELF NAMES readelf)
find_program(FELF_TEST_UNSHARE NAMES unshare)

# Tests whose result depends on how the CLI/bootstrap were linked.  Nix and
# the README's build both use a static bootstrap/CLI; a plain developer build
# does not, so those tests are only registered when the caller says so.
if(NOT DEFINED FELF_TEST_STATIC_BOOTSTRAP)
    if(FELF_STATIC_BOOTSTRAP OR FELF_BOOTSTRAP_FILE)
        set(FELF_TEST_STATIC_BOOTSTRAP ON)
    else()
        set(FELF_TEST_STATIC_BOOTSTRAP OFF)
    endif()
endif()
if(NOT DEFINED FELF_TEST_STATIC_CLI)
    set(FELF_TEST_STATIC_CLI "${FELF_STATIC_CLI}")
endif()
# Every felf carries FELF's glibc (the top-level build refuses to build one
# without it).  A prebuilt CLI is checked here, so the direct-mapping tests
# never run against one that does not.
if(FELF_TEST_CLI)
    execute_process(COMMAND "${FELF_TEST_CLI}" --version
        OUTPUT_VARIABLE felf_test_cli_version
        RESULT_VARIABLE felf_test_cli_rc)
    if(NOT felf_test_cli_rc EQUAL 0 OR
       NOT felf_test_cli_version MATCHES "glibc [^\n]* \\(FELF's")
        message(FATAL_ERROR
            "FELF_TEST_CLI=${FELF_TEST_CLI} does not carry FELF's glibc "
            "(`felf --version` names none)")
    endif()
endif()
set(FELF_TEST_BUSYBOX "$ENV{FELF_TEST_BUSYBOX}" CACHE FILEPATH
    "Static busybox for the /nix-masked test (default: the FELF_TEST_BUSYBOX environment variable, else skip it)")

set(FELF_TEST_WORK "${CMAKE_CURRENT_BINARY_DIR}/work")
set(FELF_TEST_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
file(MAKE_DIRECTORY
    "${FELF_TEST_WORK}"
    "${FELF_TEST_WORK}/multi/bin"
    "${FELF_TEST_WORK}/multicall/bin"
    "${FELF_TEST_WORK}/links"
    "${FELF_TEST_WORK}/links-abs"
    "${FELF_TEST_WORK}/links-busy"
    "${FELF_TEST_WORK}/tree/bin"
    "${FELF_TEST_WORK}/tree/lib/plugins"
    "${FELF_TEST_WORK}/outside"
    "${FELF_TEST_WORK}/loc/bin"
    "${FELF_TEST_WORK}/loc/libexec/tool"
    "${FELF_TEST_WORK}/hostlibs"
    "${FELF_TEST_WORK}/app/bin"
    "${FELF_TEST_WORK}/app/share")

# --------------------------------------------------------------------------
# Fixture programs and libraries

function(felf_fixture_exe name output)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES
        OUTPUT_NAME "${output}"
        POSITION_INDEPENDENT_CODE ON)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    target_link_options(${name} PRIVATE -pie)
endfunction()

function(felf_fixture_lib name output soname)
    add_library(${name} SHARED ${ARGN})
    set_target_properties(${name} PROPERTIES
        OUTPUT_NAME "${output}"
        POSITION_INDEPENDENT_CODE ON)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    target_link_options(${name} PRIVATE "-Wl,-soname,${soname}")
endfunction()

felf_fixture_exe(felf-test-hello hello ${CMAKE_CURRENT_SOURCE_DIR}/hello/hello.c)
felf_fixture_exe(felf-test-exit exit ${CMAKE_CURRENT_SOURCE_DIR}/basic/exit.c)
felf_fixture_exe(felf-test-nofile nofile ${CMAKE_CURRENT_SOURCE_DIR}/basic/nofile.c)
felf_fixture_exe(felf-test-locale locale ${CMAKE_CURRENT_SOURCE_DIR}/basic/locale.c)
felf_fixture_exe(felf-test-multicall multicall
    ${CMAKE_CURRENT_SOURCE_DIR}/multicall/main.c)
felf_fixture_exe(felf-test-ownenv ownenv
    ${CMAKE_CURRENT_SOURCE_DIR}/ownenv/main.c)
felf_fixture_exe(felf-test-exe exe_test ${CMAKE_CURRENT_SOURCE_DIR}/exe/main.c)
felf_fixture_exe(felf-test-mangle mangle
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mangle.c)
felf_fixture_exe(felf-test-tree tree_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tree/main.c)
target_link_libraries(felf-test-tree PRIVATE ${CMAKE_DL_LIBS})
felf_fixture_exe(felf-test-dlopen dlopen
    ${CMAKE_CURRENT_SOURCE_DIR}/dlopen/main.c)
target_link_libraries(felf-test-dlopen PRIVATE ${CMAKE_DL_LIBS})
felf_fixture_exe(felf-test-direct direct
    ${CMAKE_CURRENT_SOURCE_DIR}/direct/main.c)
target_link_libraries(felf-test-direct PRIVATE ${CMAKE_DL_LIBS})

felf_fixture_exe(felf-test-share-one share-one
    ${CMAKE_CURRENT_SOURCE_DIR}/direct/share.c)
target_compile_definitions(felf-test-share-one PRIVATE WHO="one")
felf_fixture_exe(felf-test-share-two share-two
    ${CMAKE_CURRENT_SOURCE_DIR}/direct/share.c)
target_compile_definitions(felf-test-share-two PRIVATE WHO="two")

if(felf_have_cxx)
    felf_fixture_exe(felf-test-cpp cpp ${CMAKE_CURRENT_SOURCE_DIR}/cpp/cpp.cpp)
    target_compile_features(felf-test-cpp PRIVATE cxx_std_17)
    target_link_libraries(felf-test-cpp PRIVATE Threads::Threads)
endif()

felf_fixture_lib(felf-test-plugin plugin libplugin.so
    ${CMAKE_CURRENT_SOURCE_DIR}/dlopen/plugin.c)
set_target_properties(felf-test-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# The same SONAME with a different value: a bundled object must win over it.
felf_fixture_lib(felf-test-plugin-alt plugin libplugin.so
    ${CMAKE_CURRENT_SOURCE_DIR}/dlopen/alt.c)
set_target_properties(felf-test-plugin-alt PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/hostlibs")

# Loaded into the dynamic bootstrap (if the bootstrap is dynamic).
felf_fixture_lib(felf-test-preload evil libevil.so
    ${CMAKE_CURRENT_SOURCE_DIR}/dlopen/preload.c)
set_target_properties(felf-test-preload PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/hostlibs")

# Tree bundle: the plugin stays a file, libdep.so is embedded.
felf_fixture_lib(felf-test-tree-dep dep libdep.so
    ${CMAKE_CURRENT_SOURCE_DIR}/tree/dep.c)
set_target_properties(felf-test-tree-dep PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/tree-build")
felf_fixture_lib(felf-test-tree-plugin treeplugin libtreeplugin.so
    ${CMAKE_CURRENT_SOURCE_DIR}/tree/plugin.c)
target_link_libraries(felf-test-tree-plugin PRIVATE felf-test-tree-dep)
set_target_properties(felf-test-tree-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/tree-build")

# Host-preferred library: the embedded copy returns 1, the host copy 2+40.
felf_fixture_lib(felf-test-hostlib felfhost libfelfhost.so.1
    ${CMAKE_CURRENT_SOURCE_DIR}/host/hostlib.c)
set_target_properties(felf-test-hostlib PROPERTIES
    SUFFIX ".so.1"
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/host-build")
felf_fixture_lib(felf-test-host-vendor felfvendor libfelfvendor.so.0
    ${CMAKE_CURRENT_SOURCE_DIR}/host/vendor.c)
set_target_properties(felf-test-host-vendor PROPERTIES
    SUFFIX ".so.0"
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/host-libs")
felf_fixture_lib(felf-test-hostlib-copy felfhost libfelfhost.so.1
    ${CMAKE_CURRENT_SOURCE_DIR}/host/hostlib.c)
target_compile_definitions(felf-test-hostlib-copy PRIVATE FELF_HOST_COPY)
target_link_libraries(felf-test-hostlib-copy PRIVATE felf-test-host-vendor)
set_target_properties(felf-test-hostlib-copy PROPERTIES
    SUFFIX ".so.1"
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/host-libs")
felf_fixture_exe(felf-test-host host_test ${CMAKE_CURRENT_SOURCE_DIR}/host/main.c)
target_link_libraries(felf-test-host PRIVATE felf-test-hostlib)
set_target_properties(felf-test-host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/host-build")

# Versioned symbols: main needs VER_2, the only bundleable library has VER_1.
felf_fixture_lib(felf-test-v1 v libv.so ${CMAKE_CURRENT_SOURCE_DIR}/version/v.c)
target_link_options(felf-test-v1 PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version/v1.map")
set_target_properties(felf-test-v1 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/ver1")
felf_fixture_lib(felf-test-v2 v libv.so ${CMAKE_CURRENT_SOURCE_DIR}/version/v.c)
target_link_options(felf-test-v2 PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/version/v2.map")
set_target_properties(felf-test-v2 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/ver2")
felf_fixture_exe(felf-test-version-main main
    ${CMAKE_CURRENT_SOURCE_DIR}/version/main.c)
target_link_libraries(felf-test-version-main PRIVATE felf-test-v2)
set_target_properties(felf-test-version-main PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/ver-build")

# A library with a dependency the packer cannot find.
felf_fixture_lib(felf-test-ghost ghost libghost.so
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ghost.c)
set_target_properties(felf-test-ghost PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/include-ghost")
felf_fixture_lib(felf-test-needy needy libneedy.so
    ${CMAKE_CURRENT_SOURCE_DIR}/include/needy.c)
target_link_libraries(felf-test-needy PRIVATE felf-test-ghost)
# Toolchains that default to --as-needed (Ubuntu's) must keep DT_NEEDED.
target_link_options(felf-test-needy PRIVATE "-Wl,--no-as-needed")
set_target_properties(felf-test-needy PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${FELF_TEST_WORK}/include-needy")

# Paths to the fixture targets (generator expressions are evaluated by CTest).
set(FELF_HELLO "$<TARGET_FILE:felf-test-hello>")
set(FELF_EXIT "$<TARGET_FILE:felf-test-exit>")
set(FELF_NOFILE "$<TARGET_FILE:felf-test-nofile>")
set(FELF_LOCALE "$<TARGET_FILE:felf-test-locale>")
set(FELF_MULTICALL "$<TARGET_FILE:felf-test-multicall>")
set(FELF_OWNENV "$<TARGET_FILE:felf-test-ownenv>")
set(FELF_EXE "$<TARGET_FILE:felf-test-exe>")
set(FELF_TREE "$<TARGET_FILE:felf-test-tree>")
set(FELF_DLOPEN "$<TARGET_FILE:felf-test-dlopen>")
set(FELF_DIRECT "$<TARGET_FILE:felf-test-direct>")
set(FELF_PLUGIN "$<TARGET_FILE:felf-test-plugin>")
set(FELF_PLUGIN_ALT "$<TARGET_FILE:felf-test-plugin-alt>")
set(FELF_PRELOAD "$<TARGET_FILE:felf-test-preload>")
set(FELF_TREE_DEP "$<TARGET_FILE:felf-test-tree-dep>")
set(FELF_TREE_PLUGIN "$<TARGET_FILE:felf-test-tree-plugin>")
set(FELF_HOST "$<TARGET_FILE:felf-test-host>")
set(FELF_HOSTLIB "$<TARGET_FILE:felf-test-hostlib>")
set(FELF_HOSTLIB_COPY "$<TARGET_FILE:felf-test-hostlib-copy>")
set(FELF_HOST_VENDOR "$<TARGET_FILE:felf-test-host-vendor>")
set(FELF_VERSION_MAIN "$<TARGET_FILE:felf-test-version-main>")
set(FELF_VERSION_V1 "$<TARGET_FILE:felf-test-v1>")
set(FELF_VERSION_V2 "$<TARGET_FILE:felf-test-v2>")
set(FELF_NEEDY "$<TARGET_FILE:felf-test-needy>")

# --------------------------------------------------------------------------
# Test registration helpers

function(felf_add_test name)
    add_test(NAME ${name} COMMAND ${ARGN})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

function(felf_add_fixture_test name fixture)
    felf_add_test(${name} ${ARGN})
    set_tests_properties(${name} PROPERTIES FIXTURES_REQUIRED "${fixture}")
endfunction()

function(felf_add_output_test name fixture regex)
    felf_add_fixture_test(${name} "${fixture}" ${ARGN})
    set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
endfunction()

function(felf_add_pack_fixture name input output)
    felf_add_test(setup/pack-${name} ${felf_cli} pack ${input}
        -o "${FELF_TEST_WORK}/${output}" ${ARGN})
    set_tests_properties(setup/pack-${name} PROPERTIES
        FIXTURES_SETUP "${name}"
        LABELS "setup;pack"
        TIMEOUT 300)
endfunction()

function(felf_add_script_test name script)
    felf_add_test(${name} ${CMAKE_COMMAND} ${ARGN}
        -P "${FELF_TEST_CMAKE}/${script}")
endfunction()

set(felf_env "${FELF_TEST_ENV}")

# --------------------------------------------------------------------------
# CLI

felf_add_test(cli/version ${felf_cli} --version)
set_tests_properties(cli/version PROPERTIES
    LABELS "cli"
    PASS_REGULAR_EXPRESSION "^felf [0-9]+\\.[0-9]+")

felf_add_test(cli/help ${felf_cli} --help)
set_tests_properties(cli/help PROPERTIES
    LABELS "cli"
    PASS_REGULAR_EXPRESSION "felf pack")

felf_add_script_test(cli/errors cli-errors.cmake
    -DFELF=${felf_cli}
    -DHELLO=${FELF_HELLO})
set_tests_properties(cli/errors PROPERTIES LABELS "cli")

if(FELF_TEST_STATIC_CLI AND FELF_TEST_READELF)
    felf_add_test(cli/static ${FELF_TEST_READELF} -lW ${felf_cli})
    set_tests_properties(cli/static PROPERTIES
        LABELS "cli"
        FAIL_REGULAR_EXPRESSION "program interpreter")
else()
    message(STATUS "FELF tests: static-CLI test disabled")
endif()

# --------------------------------------------------------------------------
# Core packs

felf_add_pack_fixture(hello-mapped ${FELF_HELLO} hello-mapped.felf)
felf_add_pack_fixture(hello-compressed ${FELF_HELLO} hello-compressed.felf
    --compress 19)
felf_add_pack_fixture(hello-lazy ${FELF_HELLO} hello-lazy.felf --lazy)
felf_add_pack_fixture(hello-zstd ${FELF_HELLO} hello-zstd.felf --lazy
    --compress 19)
felf_add_pack_fixture(hello-plain ${FELF_HELLO} hello-plain.felf --lazy
    --compress none)

felf_add_pack_fixture(exit ${FELF_EXIT} exit.felf)
felf_add_pack_fixture(locale ${FELF_LOCALE} locale.felf --lazy)
felf_add_pack_fixture(nofile ${FELF_NOFILE} nofile.felf --lazy)
if(felf_have_cxx)
    felf_add_pack_fixture(cpp "$<TARGET_FILE:felf-test-cpp>" cpp.felf)
    felf_add_pack_fixture(cpp-lazy "$<TARGET_FILE:felf-test-cpp>"
        cpp-lazy.felf --lazy)
endif()

# --------------------------------------------------------------------------
# Basic execution, libraries mapped from the file or compressed

foreach(mode mapped compressed)
    felf_add_output_test(run/hello-${mode}/output hello-${mode}
        "Hello from FELF!" ${CMAKE_COMMAND} -E env
        "${FELF_TEST_WORK}/hello-${mode}.felf" one "two words")
    set_tests_properties(run/hello-${mode}/output PROPERTIES LABELS "run")
    felf_add_output_test(run/hello-${mode}/arguments hello-${mode}
        "arg2=two words" ${CMAKE_COMMAND} -E env
        "${FELF_TEST_WORK}/hello-${mode}.felf" one "two words")
    set_tests_properties(run/hello-${mode}/arguments PROPERTIES LABELS "run")
    felf_add_fixture_test(verify/hello-${mode} hello-${mode}
        ${felf_cli} verify "${FELF_TEST_WORK}/hello-${mode}.felf")
    set_tests_properties(verify/hello-${mode} PROPERTIES
        LABELS "verify"
        PASS_REGULAR_EXPRESSION "verification: OK")
endforeach()

# Per-test environment without ENVIRONMENT_MODIFICATION (CMake 3.22).
add_test(NAME run/hello-mapped/environment
    COMMAND ${felf_env} FELF_TEST_VALUE=mapped
        "${FELF_TEST_WORK}/hello-mapped.felf")
set_tests_properties(run/hello-mapped/environment PROPERTIES
    FIXTURES_REQUIRED hello-mapped
    LABELS "run"
    PASS_REGULAR_EXPRESSION "env=mapped")

add_test(NAME run/hello-compressed/environment
    COMMAND ${felf_env} FELF_TEST_VALUE=compressed
        "${FELF_TEST_WORK}/hello-compressed.felf")
set_tests_properties(run/hello-compressed/environment PROPERTIES
    FIXTURES_REQUIRED hello-compressed
    LABELS "run"
    PASS_REGULAR_EXPRESSION "env=compressed")

felf_add_output_test(run/hello-lazy hello-lazy "env=lazy" ${felf_env}
    FELF_TEST_VALUE=lazy "${FELF_TEST_WORK}/hello-lazy.felf" one)
set_tests_properties(run/hello-lazy PROPERTIES LABELS "run")

felf_add_fixture_test(verify/hello-lazy hello-lazy
    ${felf_cli} verify "${FELF_TEST_WORK}/hello-lazy.felf")
set_tests_properties(verify/hello-lazy PROPERTIES
    LABELS "verify" PASS_REGULAR_EXPRESSION "verification: OK")

felf_add_fixture_test(inspect/hello-lazy hello-lazy
    ${felf_cli} inspect "${FELF_TEST_WORK}/hello-lazy.felf")
set_tests_properties(inspect/hello-lazy PROPERTIES
    LABELS "inspect" PASS_REGULAR_EXPRESSION "Lazy Loading: Enabled")

# --------------------------------------------------------------------------
# Exit codes, signals, stdin

felf_add_script_test(process/exit-codes process.cmake
    -DFELF=${felf_cli}
    -DEXIT=${FELF_TEST_WORK}/exit.felf
    -DBASH=${FELF_TEST_BASH})
set_tests_properties(process/exit-codes PROPERTIES
    FIXTURES_REQUIRED exit
    LABELS "run")

# --------------------------------------------------------------------------
# C++, pthreads, TLS-adjacent runtime initialization

if(felf_have_cxx)
    felf_add_output_test(run/cpp cpp "Hello from C\\+\\+ FELF"
        ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/cpp.felf" a b c)
    set_tests_properties(run/cpp PROPERTIES LABELS "run")
    felf_add_output_test(run/cpp/thread cpp "value=42"
        ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/cpp.felf" a b c)
    set_tests_properties(run/cpp/thread PROPERTIES LABELS "run")
    felf_add_output_test(run/cpp-lazy cpp-lazy "Hello from C\\+\\+ FELF"
        ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/cpp-lazy.felf" a b c)
    set_tests_properties(run/cpp-lazy PROPERTIES LABELS "run")
    felf_add_output_test(run/cpp-lazy/thread cpp-lazy "value=42"
        ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/cpp-lazy.felf" a b c)
    set_tests_properties(run/cpp-lazy/thread PROPERTIES LABELS "run")
endif()

# --------------------------------------------------------------------------
# dlopen of a bundled plugin and strict denial of a host library

felf_add_pack_fixture(dlopen ${FELF_DLOPEN} dlopen.felf
    --include "${FELF_PLUGIN}")
felf_add_pack_fixture(dlopen-lazy ${FELF_DLOPEN} dlopen-lazy.felf --lazy
    --include "${FELF_PLUGIN}")

foreach(mode dlopen dlopen-lazy)
    felf_add_output_test(${mode}/bundled-plugin ${mode} "plugin value=1234"
        ${felf_env} "LD_LIBRARY_PATH=${FELF_TEST_WORK}/hostlibs"
        "LD_PRELOAD=${FELF_PRELOAD}"
        "${FELF_TEST_WORK}/${mode}.felf")
    set_tests_properties(${mode}/bundled-plugin PROPERTIES LABELS "dlopen")
    felf_add_output_test(${mode}/host-libm-denied ${mode} "host libm=DENIED"
        ${felf_env} "LD_LIBRARY_PATH=${FELF_TEST_WORK}/hostlibs"
        "LD_PRELOAD=${FELF_PRELOAD}"
        "${FELF_TEST_WORK}/${mode}.felf")
    set_tests_properties(${mode}/host-libm-denied PROPERTIES LABELS "dlopen")
    if(FELF_TEST_STATIC_BOOTSTRAP)
        felf_add_fixture_test(${mode}/host-preload-ignored ${mode}
            ${felf_env} "LD_LIBRARY_PATH=${FELF_TEST_WORK}/hostlibs"
            "LD_PRELOAD=${FELF_PRELOAD}"
            "${FELF_TEST_WORK}/${mode}.felf")
        set_tests_properties(${mode}/host-preload-ignored PROPERTIES
            LABELS "dlopen"
            FAIL_REGULAR_EXPRESSION "HOST PRELOAD RUN")
    endif()
endforeach()

felf_add_output_test(dlopen/absolute-path dlopen-lazy
    "absolute plugin value=1234" ${CMAKE_COMMAND} -E env
    "${FELF_TEST_WORK}/dlopen-lazy.felf"
    "${FELF_TEST_WORK}/hostlibs/libplugin.so")
set_tests_properties(dlopen/absolute-path PROPERTIES LABELS "dlopen")

felf_add_script_test(dlopen/include-warnings include-warnings.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DDLOPEN=${FELF_DLOPEN}
    -DPLUGIN=${FELF_PLUGIN}
    -DPLUGIN_ALT=${FELF_PLUGIN_ALT}
    -DNEEDY=${FELF_NEEDY})
set_tests_properties(dlopen/include-warnings PROPERTIES LABELS "dlopen;pack")

# --------------------------------------------------------------------------
# Direct mapping: FELF carries FELF's glibc, whose patched loader maps the
# libraries straight from the FELF file.

felf_add_test(cli/version-direct ${felf_cli} --version)
set_tests_properties(cli/version-direct PROPERTIES
    LABELS "direct;cli"
    PASS_REGULAR_EXPRESSION
        "glibc .* \\(FELF's, maps libraries from the file\\)")

felf_add_pack_fixture(direct ${FELF_DIRECT} direct.felf --lazy --include
    "${FELF_PLUGIN}")
felf_add_pack_fixture(direct-compressed ${FELF_DIRECT}
    direct-compressed.felf --compress 19 --lazy --include
    "${FELF_PLUGIN}")
felf_add_pack_fixture(dlopen-direct ${FELF_DLOPEN} dlopen-direct.felf
    --lazy --include "${FELF_PLUGIN}")

felf_add_output_test(direct/libc-mapped direct
    "libc=felf name=libc.so.6" ${felf_env} FELF_DEBUG=1
    "${FELF_TEST_WORK}/direct.felf")
set_tests_properties(direct/libc-mapped PROPERTIES LABELS "direct")
felf_add_output_test(direct/plugin-mapped direct
    "plugin=felf value=1234" ${felf_env} FELF_DEBUG=1
    "${FELF_TEST_WORK}/direct.felf")
set_tests_properties(direct/plugin-mapped PROPERTIES LABELS "direct")
felf_add_output_test(direct/audit-offsets direct "-> felf-map-1:"
    ${felf_env} FELF_DEBUG=1 "${FELF_TEST_WORK}/direct.felf")
set_tests_properties(direct/audit-offsets PROPERTIES LABELS "direct")

felf_add_fixture_test(direct/verify direct ${felf_cli} verify
    "${FELF_TEST_WORK}/direct.felf")
set_tests_properties(direct/verify PROPERTIES
    LABELS "direct;verify" PASS_REGULAR_EXPRESSION "verification: OK")
felf_add_fixture_test(direct/inspect direct ${felf_cli} inspect
    "${FELF_TEST_WORK}/direct.felf")
set_tests_properties(direct/inspect PROPERTIES
    LABELS "direct;inspect"
    PASS_REGULAR_EXPRESSION "Direct Mapping: Enabled")
felf_add_fixture_test(direct/program-compressed direct ${felf_cli} sql
    "${FELF_TEST_WORK}/direct.felf"
    "SELECT kind || '=' || compression FROM objects WHERE kind = 'program'")
set_tests_properties(direct/program-compressed PROPERTIES
    LABELS "direct;sql" PASS_REGULAR_EXPRESSION "program=zstd")
felf_add_fixture_test(direct/interp-compressed direct ${felf_cli} sql
    "${FELF_TEST_WORK}/direct.felf"
    "SELECT kind || '=' || compression FROM objects WHERE kind = 'interp'")
set_tests_properties(direct/interp-compressed PROPERTIES
    LABELS "direct;sql" PASS_REGULAR_EXPRESSION "interp=zstd")
felf_add_fixture_test(direct/library-uncompressed direct ${felf_cli} sql
    "${FELF_TEST_WORK}/direct.felf"
    "SELECT kind || '=' || compression FROM objects WHERE kind = 'shared'")
set_tests_properties(direct/library-uncompressed PROPERTIES
    LABELS "direct;sql" FAIL_REGULAR_EXPRESSION "shared=zstd")
# --compress LEVEL compresses the libraries too.
felf_add_fixture_test(direct/library-compressed direct-compressed ${felf_cli} sql
    "${FELF_TEST_WORK}/direct-compressed.felf"
    "SELECT kind || '=' || compression FROM objects WHERE kind = 'shared'")
set_tests_properties(direct/library-compressed PROPERTIES
    LABELS "direct;sql" PASS_REGULAR_EXPRESSION "shared=zstd")

felf_add_output_test(direct/compressed-memfd direct-compressed
    "plugin=memfd value=1234" ${CMAKE_COMMAND} -E env
    "${FELF_TEST_WORK}/direct-compressed.felf")
set_tests_properties(direct/compressed-memfd PROPERTIES LABELS "direct")

felf_add_output_test(direct/absolute-dlopen dlopen-direct
    "absolute plugin value=1234" ${CMAKE_COMMAND} -E env
    "${FELF_TEST_WORK}/dlopen-direct.felf"
    "${FELF_TEST_WORK}/hostlibs/libplugin.so")
set_tests_properties(direct/absolute-dlopen PROPERTIES LABELS "direct")
felf_add_output_test(direct/strict-host-libm dlopen-direct
    "host libm=DENIED" ${CMAKE_COMMAND} -E env
    "${FELF_TEST_WORK}/dlopen-direct.felf")
set_tests_properties(direct/strict-host-libm PROPERTIES LABELS "direct")

if(FELF_TEST_READELF)
    felf_add_script_test(direct/store-references direct-store.cmake
        -DREADELF=${FELF_TEST_READELF}
        -DPROGRAM=${FELF_DIRECT}
        -DFELF=${FELF_TEST_WORK}/direct.felf)
    set_tests_properties(direct/store-references PROPERTIES
        FIXTURES_REQUIRED direct
        LABELS "direct")
endif()

felf_add_script_test(direct/memory-sharing memory-sharing.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DSHARE_ONE=$<TARGET_FILE:felf-test-share-one>
    -DSHARE_TWO=$<TARGET_FILE:felf-test-share-two>
    -DBASH=${FELF_TEST_BASH})
set_tests_properties(direct/memory-sharing PROPERTIES
    FIXTURES_REQUIRED "direct;direct-compressed"
    LABELS "direct;slow")

# --------------------------------------------------------------------------
# Versioned symbols (a program built against a newer glibc than the bundled
# library is rejected at pack time).

felf_add_script_test(pack/version-check version-check.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DMAIN=${FELF_VERSION_MAIN}
    -DV1=${FELF_VERSION_V1}
    -DV2=${FELF_VERSION_V2})
set_tests_properties(pack/version-check PROPERTIES LABELS "pack")

# --------------------------------------------------------------------------
# Inspection, SQL, extraction

felf_add_fixture_test(inspect/hello hello-mapped ${felf_cli} inspect
    "${FELF_TEST_WORK}/hello-mapped.felf")
set_tests_properties(inspect/hello PROPERTIES
    LABELS "inspect" PASS_REGULAR_EXPRESSION "Strict Mode: Enabled")

felf_add_fixture_test(inspect/hello/program hello-mapped ${felf_cli} inspect
    "${FELF_TEST_WORK}/hello-mapped.felf")
set_tests_properties(inspect/hello/program PROPERTIES
    LABELS "inspect" PASS_REGULAR_EXPRESSION "hello +program")

felf_add_fixture_test(inspect/hello/json hello-mapped ${felf_cli} inspect
    "${FELF_TEST_WORK}/hello-mapped.felf" --json)
set_tests_properties(inspect/hello/json PROPERTIES
    LABELS "inspect" PASS_REGULAR_EXPRESSION "\"format_version\":1")

felf_add_fixture_test(list/hello hello-mapped ${felf_cli} list
    "${FELF_TEST_WORK}/hello-mapped.felf")
set_tests_properties(list/hello PROPERTIES
    LABELS "inspect" PASS_REGULAR_EXPRESSION "hello")

felf_add_fixture_test(list/hello/json hello-mapped ${felf_cli} list
    "${FELF_TEST_WORK}/hello-mapped.felf" --json)
set_tests_properties(list/hello/json PROPERTIES
    LABELS "inspect" PASS_REGULAR_EXPRESSION "\"name\":\"hello\"")

felf_add_fixture_test(sql/hello hello-mapped ${felf_cli} sql
    "${FELF_TEST_WORK}/hello-mapped.felf"
    "SELECT name FROM objects WHERE kind = 'program'")
set_tests_properties(sql/hello PROPERTIES
    LABELS "sql" PASS_REGULAR_EXPRESSION "hello")

felf_add_script_test(sql/read-only sql-read-only.cmake
    -DFELF=${felf_cli}
    -DFILE=${FELF_TEST_WORK}/hello-mapped.felf)
set_tests_properties(sql/read-only PROPERTIES
    FIXTURES_REQUIRED hello-mapped
    LABELS "sql;security")

felf_add_script_test(extract/files extract.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DHELLO=${FELF_HELLO})
set_tests_properties(extract/files PROPERTIES
    FIXTURES_REQUIRED "hello-mapped"
    LABELS "extract")

# --------------------------------------------------------------------------
# Reproducibility

felf_add_script_test(pack/reproducibility reproducibility.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DHELLO=${FELF_HELLO})
set_tests_properties(pack/reproducibility PROPERTIES LABELS "pack")

# cmake/FELF.cmake, used by a project of its own.
felf_add_script_test(cmake/module cmake-module.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/cmake-module
    -DMODULE=${PROJECT_SOURCE_DIR}/cmake/FELF.cmake
    -DGENERATOR=${CMAKE_GENERATOR})
set_tests_properties(cmake/module PROPERTIES LABELS "pack;cmake" TIMEOUT 300)

# --------------------------------------------------------------------------
# Compression

felf_add_fixture_test(compress/inspect-zstd hello-zstd ${felf_cli} inspect
    "${FELF_TEST_WORK}/hello-zstd.felf")
set_tests_properties(compress/inspect-zstd PROPERTIES
    LABELS "compress" PASS_REGULAR_EXPRESSION "\\(zstd ")

felf_add_fixture_test(compress/inspect-plain hello-plain ${felf_cli} inspect
    "${FELF_TEST_WORK}/hello-plain.felf")
set_tests_properties(compress/inspect-plain PROPERTIES
    LABELS "compress" FAIL_REGULAR_EXPRESSION "\\(zstd ")

felf_add_output_test(compress/plain-runs hello-plain "Hello from FELF!"
    ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/hello-plain.felf")
set_tests_properties(compress/plain-runs PROPERTIES LABELS "compress;run")

felf_add_script_test(compress/size-and-extract compress.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DHELLO=${FELF_HELLO})
set_tests_properties(compress/size-and-extract PROPERTIES
    FIXTURES_REQUIRED "hello-zstd;hello-plain"
    LABELS "compress")

# --------------------------------------------------------------------------
# Corruption detection

felf_add_script_test(verify/corruption corruption.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DMANGLE=$<TARGET_FILE:felf-test-mangle>
    -DFILE=${FELF_TEST_WORK}/hello-mapped.felf)
set_tests_properties(verify/corruption PROPERTIES
    FIXTURES_REQUIRED hello-mapped
    LABELS "verify;security")

felf_add_script_test(verify/not-a-felf not-a-felf.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK})
set_tests_properties(verify/not-a-felf PROPERTIES LABELS "verify;security")

# --------------------------------------------------------------------------
# Read-only execution and concurrent invocation

felf_add_script_test(run/read-only read-only.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DFILE=${FELF_TEST_WORK}/hello-compressed.felf)
set_tests_properties(run/read-only PROPERTIES
    FIXTURES_REQUIRED hello-compressed
    LABELS "run")

felf_add_script_test(run/concurrent concurrent.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DFILE=${FELF_TEST_WORK}/hello-compressed.felf
    -DBASH=${FELF_TEST_BASH})
set_tests_properties(run/concurrent PROPERTIES
    FIXTURES_REQUIRED hello-compressed
    LABELS "run;slow")

# --------------------------------------------------------------------------
# Open-file limit: object descriptors start at 1000, and containers often
# have a soft limit of 1024.  The bootstrap raises the soft limit for itself
# only when a descriptor it needs is above it, and the application sees the
# caller's limit again.

felf_add_script_test(run/open-file-limit file-limit.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DFILE=${FELF_TEST_WORK}/nofile.felf
    -DBASH=${FELF_TEST_BASH})
set_tests_properties(run/open-file-limit PROPERTIES
    FIXTURES_REQUIRED nofile
    LABELS "run")

# --------------------------------------------------------------------------
# Locale data: every FELF embeds a glibc locale-archive (this is not an
# option), points LOCALE_ARCHIVE at it, and names C.UTF-8 when the caller
# set no locale.

felf_add_output_test(locale/default-utf8 locale
    "ctype=C\\.UTF-8" ${felf_env}
    -u LANG -u LANGUAGE -u LC_ALL -u LC_CTYPE -u LC_NUMERIC -u LC_TIME
    -u LC_COLLATE -u LC_MONETARY -u LC_MESSAGES -u LC_PAPER -u LC_NAME
    -u LC_ADDRESS -u LC_TELEPHONE -u LC_MEASUREMENT -u LC_IDENTIFICATION
    -u LOCALE_ARCHIVE -u LOCALE_ARCHIVE_2_27
    FELF_DEBUG=1 "${FELF_TEST_WORK}/locale.felf" default)
set_tests_properties(locale/default-utf8 PROPERTIES LABELS "locale")

felf_add_output_test(locale/default-lang locale "lang=C\\.UTF-8" ${felf_env}
    -u LANG -u LANGUAGE -u LC_ALL -u LC_CTYPE -u LC_NUMERIC -u LC_TIME
    -u LC_COLLATE -u LC_MONETARY -u LC_MESSAGES -u LC_PAPER -u LC_NAME
    -u LC_ADDRESS -u LC_TELEPHONE -u LC_MEASUREMENT -u LC_IDENTIFICATION
    -u LOCALE_ARCHIVE -u LOCALE_ARCHIVE_2_27
    "${FELF_TEST_WORK}/locale.felf" default)
set_tests_properties(locale/default-lang PROPERTIES LABELS "locale")

felf_add_output_test(locale/archive-descriptor locale
    "LOCALE_ARCHIVE=/proc/self/fd/[0-9]+ \\(embedded: the caller set no locale\\)"
    ${felf_env}
    -u LANG -u LANGUAGE -u LC_ALL -u LC_CTYPE -u LC_NUMERIC -u LC_TIME
    -u LC_COLLATE -u LC_MONETARY -u LC_MESSAGES -u LC_PAPER -u LC_NAME
    -u LC_ADDRESS -u LC_TELEPHONE -u LC_MEASUREMENT -u LC_IDENTIFICATION
    -u LOCALE_ARCHIVE -u LOCALE_ARCHIVE_2_27
    FELF_DEBUG=1 "${FELF_TEST_WORK}/locale.felf" default)
set_tests_properties(locale/archive-descriptor PROPERTIES LABELS "locale")

felf_add_output_test(locale/archive-unset locale "archive=\\(unset\\)"
    ${felf_env} -u LANG -u LANGUAGE -u LC_ALL -u LC_CTYPE -u LC_NUMERIC
    -u LC_TIME -u LC_COLLATE -u LC_MONETARY -u LC_MESSAGES -u LC_PAPER
    -u LC_NAME -u LC_ADDRESS -u LC_TELEPHONE -u LC_MEASUREMENT
    -u LC_IDENTIFICATION -u LOCALE_ARCHIVE -u LOCALE_ARCHIVE_2_27
    "${FELF_TEST_WORK}/locale.felf")
set_tests_properties(locale/archive-unset PROPERTIES LABELS "locale")

felf_add_output_test(locale/caller-2-27 locale "archive2=/caller/archive"
    ${felf_env} -u LANG -u LANGUAGE -u LC_ALL -u LC_CTYPE -u LC_NUMERIC
    -u LC_TIME -u LC_COLLATE -u LC_MONETARY -u LC_MESSAGES -u LC_PAPER
    -u LC_NAME -u LC_ADDRESS -u LC_TELEPHONE -u LC_MEASUREMENT
    -u LC_IDENTIFICATION -u LOCALE_ARCHIVE LOCALE_ARCHIVE_2_27=/caller/archive
    "${FELF_TEST_WORK}/locale.felf")
set_tests_properties(locale/caller-2-27 PROPERTIES LABELS "locale")

felf_add_output_test(locale/caller-archive locale "archive=/caller/archive"
    ${felf_env} -u LANG -u LANGUAGE -u LC_ALL -u LC_CTYPE -u LC_NUMERIC
    -u LC_TIME -u LC_COLLATE -u LC_MONETARY -u LC_MESSAGES -u LC_PAPER
    -u LC_NAME -u LC_ADDRESS -u LC_TELEPHONE -u LC_MEASUREMENT
    -u LC_IDENTIFICATION -u LOCALE_ARCHIVE_2_27 LOCALE_ARCHIVE=/caller/archive
    "${FELF_TEST_WORK}/locale.felf")
set_tests_properties(locale/caller-archive PROPERTIES LABELS "locale")

# The caller's archive is kept: the embedded one is not decompressed into
# a memfd, and FELF_DEBUG names the one in use.
felf_add_output_test(locale/embedded-skipped locale
    "LOCALE_ARCHIVE=/caller/archive \\(caller's, kept\\)"
    ${felf_env}
    -u LOCALE_ARCHIVE_2_27 LOCALE_ARCHIVE=/caller/archive
    FELF_DEBUG=1 "${FELF_TEST_WORK}/locale.felf")
set_tests_properties(locale/embedded-skipped PROPERTIES LABELS "locale")

felf_add_output_test(locale/explicit-c locale "lang=\\(unset\\)"
    ${felf_env} -u LANG -u LANGUAGE -u LC_CTYPE -u LC_NUMERIC -u LC_TIME
    -u LC_COLLATE -u LC_MONETARY -u LC_MESSAGES -u LC_PAPER -u LC_NAME
    -u LC_ADDRESS -u LC_TELEPHONE -u LC_MEASUREMENT -u LC_IDENTIFICATION
    -u LOCALE_ARCHIVE -u LOCALE_ARCHIVE_2_27 LC_ALL=C
    "${FELF_TEST_WORK}/locale.felf" default)
set_tests_properties(locale/explicit-c PROPERTIES LABELS "locale")

felf_add_output_test(locale/explicit-c-debug locale
    "LOCALE_ARCHIVE=/proc/self/fd/[0-9]+ \\(embedded: the caller's locale is C\\)"
    ${felf_env} -u LANG -u LANGUAGE -u LC_CTYPE -u LOCALE_ARCHIVE
    -u LOCALE_ARCHIVE_2_27 LC_ALL=C FELF_DEBUG=1
    "${FELF_TEST_WORK}/locale.felf" default)
set_tests_properties(locale/explicit-c-debug PROPERTIES LABELS "locale")

felf_add_output_test(locale/en-us locale "ctype=en_US\\.UTF-8"
    ${felf_env} -u LC_ALL -u LC_CTYPE -u LC_NUMERIC -u LC_TIME -u LC_COLLATE
    -u LC_MONETARY -u LC_MESSAGES -u LC_PAPER -u LC_NAME -u LC_ADDRESS
    -u LC_TELEPHONE -u LC_MEASUREMENT -u LC_IDENTIFICATION -u LOCALE_ARCHIVE
    -u LOCALE_ARCHIVE_2_27 LANG=en_US.UTF-8
    "${FELF_TEST_WORK}/locale.felf" default)
set_tests_properties(locale/en-us PROPERTIES LABELS "locale")

felf_add_fixture_test(verify/locale locale ${felf_cli} verify
    "${FELF_TEST_WORK}/locale.felf")
set_tests_properties(verify/locale PROPERTIES
    LABELS "verify" PASS_REGULAR_EXPRESSION "verification: OK")

# --------------------------------------------------------------------------
# Several programs in one FELF, sharing its libraries

felf_add_pack_fixture(multi-pack ${FELF_HELLO} multi/bin/multi --lazy
    --program "${FELF_NOFILE}" --program "loc=${FELF_LOCALE}")
felf_add_script_test(setup/multi-links multi-links.cmake -DWORK=${FELF_TEST_WORK})
set_tests_properties(setup/multi-links PROPERTIES
    FIXTURES_SETUP multi-links
    FIXTURES_REQUIRED multi-pack
    LABELS "setup")

add_test(NAME multi/default
    COMMAND ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/multi/bin/multi")
set_tests_properties(multi/default PROPERTIES
    FIXTURES_REQUIRED multi-links LABELS "multi"
    PASS_REGULAR_EXPRESSION "Hello from FELF!")

add_test(NAME multi/symlink-program
    COMMAND ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/multi/bin/nofile")
set_tests_properties(multi/symlink-program PROPERTIES
    FIXTURES_REQUIRED multi-links LABELS "multi"
    PASS_REGULAR_EXPRESSION "nofile=")

add_test(NAME multi/named-program
    COMMAND ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/multi/bin/loc")
set_tests_properties(multi/named-program PROPERTIES
    FIXTURES_REQUIRED multi-links LABELS "multi"
    PASS_REGULAR_EXPRESSION "setlocale=")

add_test(NAME multi/unknown-symlink
    COMMAND ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/multi/bin/unknown")
set_tests_properties(multi/unknown-symlink PROPERTIES
    FIXTURES_REQUIRED multi-links LABELS "multi"
    PASS_REGULAR_EXPRESSION "Hello from FELF!")

add_test(NAME multi/symlink-outside-tree
    COMMAND ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/elsewhere/nofile")
set_tests_properties(multi/symlink-outside-tree PROPERTIES
    FIXTURES_REQUIRED multi-links LABELS "multi"
    PASS_REGULAR_EXPRESSION "nofile=")

add_test(NAME multi/relative-symlink
    COMMAND "${CMAKE_COMMAND}" -E chdir "${FELF_TEST_WORK}/multi/bin"
        ./nofile)
set_tests_properties(multi/relative-symlink PROPERTIES
    FIXTURES_REQUIRED multi-links LABELS "multi"
    PASS_REGULAR_EXPRESSION "nofile=")

felf_add_fixture_test(multi/inspect-list multi-links ${felf_cli} inspect
    "${FELF_TEST_WORK}/multi/bin/multi")
set_tests_properties(multi/inspect-list PROPERTIES
    LABELS "multi;inspect" PASS_REGULAR_EXPRESSION "loc")

felf_add_fixture_test(multi/inspect-default multi-links ${felf_cli} inspect
    "${FELF_TEST_WORK}/multi/bin/multi")
set_tests_properties(multi/inspect-default PROPERTIES
    LABELS "multi;inspect" PASS_REGULAR_EXPRESSION "hello  \\(default\\)")

felf_add_fixture_test(multi/inspect-json multi-links ${felf_cli} inspect
    "${FELF_TEST_WORK}/multi/bin/multi" --json)
set_tests_properties(multi/inspect-json PROPERTIES
    LABELS "multi;inspect"
    PASS_REGULAR_EXPRESSION "\"programs\":\\[\"hello\",\"loc\",\"nofile\"\\]")

felf_add_fixture_test(multi/shared-libc multi-links ${felf_cli} sql
    "${FELF_TEST_WORK}/multi/bin/multi"
    "SELECT CASE WHEN count(*) = 1 THEN 'one-libc' ELSE 'many-libc' END FROM objects WHERE soname = 'libc.so.6'")
set_tests_properties(multi/shared-libc PROPERTIES
    LABELS "multi;sql" PASS_REGULAR_EXPRESSION "one-libc")

felf_add_fixture_test(multi/verify multi-links ${felf_cli} verify
    "${FELF_TEST_WORK}/multi/bin/multi")
set_tests_properties(multi/verify PROPERTIES
    LABELS "multi;verify" PASS_REGULAR_EXPRESSION "verification: OK")

felf_add_script_test(multi/duplicate-program multi.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DHELLO=${FELF_HELLO}
    -DNOFILE=${FELF_NOFILE})
set_tests_properties(multi/duplicate-program PROPERTIES LABELS "multi;pack")

# --------------------------------------------------------------------------
# Multicall binaries and FELF_INSTALL_LINKS

felf_add_pack_fixture(multicall-pack ${FELF_MULTICALL} multicall/bin/box
    --lazy --program "one=${FELF_MULTICALL}" --program "two=${FELF_MULTICALL}")
felf_add_script_test(setup/multicall-links multicall-links.cmake
    -DWORK=${FELF_TEST_WORK})
set_tests_properties(setup/multicall-links PROPERTIES
    FIXTURES_SETUP multicall-links
    FIXTURES_REQUIRED multicall-pack
    LABELS "setup")

add_test(NAME multicall/alias-runs-as-argv0
    COMMAND ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/multicall/bin/two")
set_tests_properties(multicall/alias-runs-as-argv0 PROPERTIES
    FIXTURES_REQUIRED multicall-links LABELS "multi"
    PASS_REGULAR_EXPRESSION "applet=two")

add_test(NAME multicall/default-runs-as-argv0
    COMMAND ${CMAKE_COMMAND} -E env "${FELF_TEST_WORK}/multicall/bin/box")
set_tests_properties(multicall/default-runs-as-argv0 PROPERTIES
    FIXTURES_REQUIRED multicall-links LABELS "multi"
    PASS_REGULAR_EXPRESSION "applet=box")

felf_add_fixture_test(multicall/embedded-once multicall-links ${felf_cli} sql
    "${FELF_TEST_WORK}/multicall/bin/box"
    "SELECT CASE WHEN count(*) = 1 THEN 'one-program' ELSE 'many-programs' END FROM objects WHERE kind = 'program'")
set_tests_properties(multicall/embedded-once PROPERTIES
    LABELS "multi;sql" PASS_REGULAR_EXPRESSION "one-program")

felf_add_pack_fixture(links-pack ${FELF_MULTICALL} links/box --lazy
    --alias "multicall=three,four" --program "${FELF_NOFILE}")
felf_add_script_test(multicall/install-links links.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DFILE=${FELF_TEST_WORK}/links/box
    -DMULTICALL=${FELF_MULTICALL}
    -DNOFILE=${FELF_NOFILE})
set_tests_properties(multicall/install-links PROPERTIES
    FIXTURES_REQUIRED links-pack
    LABELS "multi")
felf_add_script_test(multicall/location-root location-root.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DHELLO=${FELF_HELLO}
    -DEXE=${FELF_EXE})
set_tests_properties(multicall/location-root PROPERTIES LABELS "multi")

# --------------------------------------------------------------------------
# Tree bundles: plugins stay as files, their dependencies are embedded.

felf_add_script_test(tree/bundle tree.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DTREE=${FELF_TREE}
    -DPLUGIN=${FELF_TREE_PLUGIN}
    -DDEP=${FELF_TREE_DEP}
    -DDEP_DIR=${FELF_TEST_WORK}/tree-build)
set_tests_properties(tree/bundle PROPERTIES LABELS "tree")

# --------------------------------------------------------------------------
# Host-preferred libraries (the graphics stack)

felf_add_script_test(host/libraries host-libs.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DENV=${FELF_TEST_ENV}
    -DHOST=${FELF_HOST}
    -DHOSTLIB=${FELF_HOSTLIB}
    -DHOSTLIB_COPY=${FELF_HOSTLIB_COPY}
    -DHOST_VENDOR=${FELF_HOST_VENDOR})
set_tests_properties(host/libraries PROPERTIES LABELS "host")

# --------------------------------------------------------------------------
# Runtime shim: /proc/self/exe, application environment, child processes

felf_add_script_test(shim/runtime shim.cmake
    -DFELF=${felf_cli}
    -DWORK=${FELF_TEST_WORK}
    -DENV=${FELF_TEST_ENV}
    -DEXE=${FELF_EXE}
    -DOWNENV=${FELF_OWNENV})
set_tests_properties(shim/runtime PROPERTIES LABELS "shim")

# --------------------------------------------------------------------------
# No-Nix runtime test: mask /nix in a user+mount namespace and execute a
# packed file from outside the store.  The test itself skips (exit 77) when
# the kernel or container does not allow the namespace.

if(FELF_TEST_BUSYBOX AND EXISTS "${FELF_TEST_BUSYBOX}"
        AND FELF_TEST_UNSHARE AND FELF_TEST_BASH)
    add_test(NAME runtime/no-nix
        COMMAND "${FELF_TEST_BASH}" -c
            "\"$1\" -Urm \"$2\" sh -c \"'$2' mount -t tmpfs tmpfs /nix && FELF_TEST_VALUE=no-nix '$3' clean\" || exit 77"
            bash "${FELF_TEST_UNSHARE}" "${FELF_TEST_BUSYBOX}"
            "${FELF_TEST_WORK}/hello-mapped.felf")
    set_tests_properties(runtime/no-nix PROPERTIES
        FIXTURES_REQUIRED hello-mapped
        LABELS "runtime"
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "env=no-nix")
else()
    message(STATUS
        "FELF tests: /nix-masked test disabled (FELF_TEST_BUSYBOX/unshare)")
endif()
