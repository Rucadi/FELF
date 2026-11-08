# A project that packs its program with cmake/FELF.cmake, as a user's would
# (tests/cmake/cmake-module.cmake builds it).  app links libgreet and
# dlopens libextra, so both must end up in the FELF.
cmake_minimum_required(VERSION 3.21)
project(felf_cmake_module LANGUAGES C)

include("${FELF_MODULE}")

add_library(greet SHARED greet.c)
add_library(extra SHARED extra.c)
add_executable(app main.c)
target_link_libraries(app PRIVATE greet ${CMAKE_DL_LIBS})

felf_pack(app-felf
    TARGET app
    PROGRAMS hello=app
    INCLUDES extra
    ENV FELF_CMAKE_TEST=packed
    LAZY
    INSTALL_DESTINATION bin)
