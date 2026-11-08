cmake_minimum_required(VERSION 3.21)
project(felf VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# A plain `cmake -S . -B build && cmake --build build` builds everything,
# FELF's patched glibc included (src/glibc).  The options below select parts
# or hand in prebuilt pieces (Nix builds each piece apart, nix/pkgs/).
option(FELF_BUILD_CLI "Build the felf CLI" ON)
option(FELF_BUILD_BOOTSTRAP "Build the static FELF bootstrap" ON)
option(FELF_BUILD_AUDIT "Build the strict-mode audit module" ON)
option(FELF_BUILD_TESTS "Build and register tests" ON)
option(FELF_STATIC_BOOTSTRAP "Link the bootstrap statically" OFF)
option(FELF_STATIC_CLI "Link the CLI statically" OFF)
set(FELF_TEST_CLI "" CACHE FILEPATH
    "CLI under test when the felf target is not built (default: the felf target)")
set(FELF_AUDIT_FILE "" CACHE FILEPATH
    "Prebuilt audit module to embed in the CLI (default: build it)")
set(FELF_SHIM_FILE "" CACHE FILEPATH
    "Prebuilt runtime shim to embed in the CLI (default: build it)")
set(FELF_BOOTSTRAP_FILE "" CACHE FILEPATH
    "Prebuilt static bootstrap to embed in the CLI (default: build it)")

include(GNUInstallDirs)

find_package(Threads REQUIRED)

# The bootstrap, the audit module, the shim and the CLI are written to the
# top of the build directory, where nix/pkgs/ and the tests expect them.
set(FELF_OUTPUT_DIR "${CMAKE_BINARY_DIR}")

add_subdirectory(third_party)
add_subdirectory(src)

if(FELF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
