# FELF's glibc: every FELF carries it, and its patched loader maps the
# libraries straight from the FELF file (patches/0001-felf-loader.patch).
#
# Either it is handed in, already built and patched (Nix: nix/pkgs/
# felf-glibc.nix passes FELF_GLIBC_DIR, and the development shell exports
# it), or this directory downloads glibc, applies patches/, builds it and
# generates the locale archive with that glibc's own localedef.
#
# Either way it defines the felf_glibc_assets object library (the files,
# embedded in the CLI) and tells the parent where the locale archive is.

set(FELF_GLIBC_DIR "" CACHE PATH
    "Prebuilt, patched FELF glibc (nix/pkgs/felf-glibc.nix) to embed; empty: build it here")
set(FELF_GLIBC_VERSION "" CACHE STRING
    "Version of FELF_GLIBC_DIR (default: FELF_GLIBC_BUILD_VERSION when built here)")
set(FELF_LOCALE_ARCHIVE_FILE "" CACHE FILEPATH
    "glibc locale-archive embedded in every FELF (default: generated with the glibc built here)")
# The development shell (nix/shell.nix) exports these, so a plain `cmake`
# there, or a build directory configured before entering it, picks them up.
foreach(var FELF_GLIBC_DIR FELF_GLIBC_VERSION FELF_LOCALE_ARCHIVE_FILE)
    if(NOT ${var} AND DEFINED ENV{${var}})
        set_property(CACHE ${var} PROPERTY VALUE "$ENV{${var}}")
    endif()
endforeach()

# What the glibc build here uses.  The default source is the release
# nixpkgs builds; the loader patch is written against it.
set(FELF_GLIBC_BUILD_VERSION "2.44" CACHE STRING "glibc version built when FELF_GLIBC_DIR is empty")
set(FELF_GLIBC_SOURCE
    "https://ftpmirror.gnu.org/glibc/glibc-${FELF_GLIBC_BUILD_VERSION}.tar.xz"
    CACHE STRING "glibc source to build: a URL, a tarball or an unpacked tree")
set(FELF_GLIBC_SOURCE_HASH "" CACHE STRING
    "ALGO=hash of FELF_GLIBC_SOURCE (default: the known hash of the default source)")
set(FELF_GLIBC_CC "" CACHE FILEPATH "C compiler for glibc (default: gcc)")
set(FELF_GLIBC_KERNEL_HEADERS "" CACHE PATH
    "Linux headers for glibc's --with-headers (default: the compiler's)")
set(FELF_GLIBC_LOCALES "C.UTF-8/UTF-8;en_US.UTF-8/UTF-8" CACHE STRING
    "Locales in the generated archive (NAME/CHARSET, as in glibc's SUPPORTED)")

set(felf_glibc_assets_c "${CMAKE_CURRENT_BINARY_DIR}/glibc_assets.c")
set(felf_glibc_assets_script "${CMAKE_CURRENT_SOURCE_DIR}/glibc_assets.cmake")

if(FELF_GLIBC_DIR)
    # ---- Handed in: check it, list its files at configure time.
    set(felf_glibc_hint
        "point it at a patched FELF glibc (`nix-build -A felf-glibc`, then "
        "<out>/lib), or leave it empty to build one here")
    if(NOT EXISTS "${FELF_GLIBC_DIR}/libc.so.6")
        message(FATAL_ERROR
            "FELF_GLIBC_DIR=${FELF_GLIBC_DIR} has no libc.so.6: "
            ${felf_glibc_hint})
    endif()
    file(GLOB felf_glibc_loader LIST_DIRECTORIES false
        "${FELF_GLIBC_DIR}/ld-linux*.so.*")
    if(NOT felf_glibc_loader)
        message(FATAL_ERROR
            "FELF_GLIBC_DIR=${FELF_GLIBC_DIR} has no ld-linux loader: "
            ${felf_glibc_hint})
    endif()
    if(NOT FELF_GLIBC_VERSION)
        message(FATAL_ERROR
            "FELF_GLIBC_DIR needs FELF_GLIBC_VERSION, the version of that glibc")
    endif()
    message(STATUS
        "Embedding FELF's glibc ${FELF_GLIBC_VERSION} from ${FELF_GLIBC_DIR}")
    set(felf_glibc_lib_dir "${FELF_GLIBC_DIR}")
    set(felf_glibc_version "${FELF_GLIBC_VERSION}")
    file(GLOB felf_glibc_deps LIST_DIRECTORIES false CONFIGURE_DEPENDS
        "${FELF_GLIBC_DIR}/*")
    set(felf_glibc_built OFF)
else()
    # ---- Built here.
    include(ExternalProject)
    include(ProcessorCount)

    if(FELF_GLIBC_VERSION AND
       NOT FELF_GLIBC_VERSION STREQUAL FELF_GLIBC_BUILD_VERSION)
        message(FATAL_ERROR
            "FELF_GLIBC_VERSION=${FELF_GLIBC_VERSION} without FELF_GLIBC_DIR: "
            "set FELF_GLIBC_BUILD_VERSION to choose the glibc built here")
    endif()
    set(felf_glibc_version "${FELF_GLIBC_BUILD_VERSION}")

    set(felf_glibc_known_hash_2.44
        "SHA256=37f600f2bef3c5e8300147059568b2a2e40a7ad6ccc65ce942556d49429cc667")
    set(felf_glibc_hash "${FELF_GLIBC_SOURCE_HASH}")
    if(NOT felf_glibc_hash AND FELF_GLIBC_SOURCE STREQUAL
       "https://ftpmirror.gnu.org/glibc/glibc-${FELF_GLIBC_BUILD_VERSION}.tar.xz")
        set(felf_glibc_hash "${felf_glibc_known_hash_${FELF_GLIBC_BUILD_VERSION}}")
    endif()
    if(IS_DIRECTORY "${FELF_GLIBC_SOURCE}")
        # An unpacked tree is copied, so the patches never touch it.
        set(felf_glibc_download
            DOWNLOAD_COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${FELF_GLIBC_SOURCE}" <SOURCE_DIR>)
    elseif(felf_glibc_hash)
        set(felf_glibc_download
            URL "${FELF_GLIBC_SOURCE}" URL_HASH "${felf_glibc_hash}")
    else()
        set(felf_glibc_download URL "${FELF_GLIBC_SOURCE}")
    endif()

    if(NOT FELF_GLIBC_CC)
        find_program(felf_glibc_gcc NAMES gcc REQUIRED)
        set(FELF_GLIBC_CC "${felf_glibc_gcc}")
    endif()
    find_program(FELF_PATCH NAMES patch REQUIRED)
    find_program(FELF_MAKE NAMES gmake make REQUIRED)
    ProcessorCount(felf_jobs)
    if(felf_jobs EQUAL 0)
        set(felf_jobs 1)
    endif()

    # The FELF loader patch, then patches/vanilla/: what Nix's glibc gets
    # from nixpkgs' own patches and FELF relies on (LOCALE_ARCHIVE, no host
    # ld.so.preload or ld.so.cache).  Nix applies only the first.
    file(GLOB felf_glibc_vanilla_patches
        "${CMAKE_CURRENT_SOURCE_DIR}/patches/vanilla/*.patch")
    list(SORT felf_glibc_vanilla_patches)
    set(felf_glibc_patches
        "${CMAKE_CURRENT_SOURCE_DIR}/patches/0001-felf-loader.patch"
        ${felf_glibc_vanilla_patches})
    set(felf_glibc_patch_command "")
    foreach(patch IN LISTS felf_glibc_patches)
        list(APPEND felf_glibc_patch_command
            COMMAND "${FELF_PATCH}" -p1 --forward -i "${patch}")
    endforeach()
    list(REMOVE_AT felf_glibc_patch_command 0)

    # Everything lives in /usr/lib (no /lib64 split), which is where the
    # stage is collected from.  The loader reads no host configuration
    # (patches/vanilla), so the prefix only names glibc's own data.
    set(felf_glibc_configure
        "<SOURCE_DIR>/configure"
        --prefix=/usr
        --sysconfdir=/etc
        --libdir=/usr/lib
        --libexecdir=/usr/lib
        --disable-werror
        --disable-profile
        --disable-nscd
        --disable-timezone-tools
        --enable-kernel=3.10.0
        --enable-stack-protector=strong
        --enable-bind-now
        libc_cv_slibdir=/usr/lib
        libc_cv_rtlddir=/usr/lib)
    if(FELF_GLIBC_KERNEL_HEADERS)
        list(APPEND felf_glibc_configure
            "--with-headers=${FELF_GLIBC_KERNEL_HEADERS}")
    endif()

    # glibc builds with its own flags: none of this project's, and nothing
    # from a Nix compiler wrapper (harmless elsewhere): its hardening adds
    # _FORTIFY_SOURCE, which glibc refuses outside --enable-fortify-source,
    # and its rpaths would give ld-linux a DT_RUNPATH, which the loader
    # asserts it does not have.
    set(felf_glibc_env "${CMAKE_COMMAND}" -E env
        "CC=${FELF_GLIBC_CC}" "CFLAGS=-O2" "CPPFLAGS=" "LDFLAGS="
        "NIX_HARDENING_ENABLE=" "NIX_DONT_SET_RPATH=1" "NIX_LDFLAGS="
        "NIX_LDFLAGS_BEFORE=" "NIX_CFLAGS_LINK=" "NIX_CFLAGS_COMPILE=")

    set(felf_glibc_root "${CMAKE_CURRENT_BINARY_DIR}/felf-glibc")
    set(felf_glibc_lib_dir "${felf_glibc_root}/lib")
    set(felf_glibc_stamp "${felf_glibc_root}/stamp")
    set(felf_glibc_locale "${felf_glibc_root}/locale/locale-archive")
    string(REPLACE ";" "," felf_glibc_locales "${FELF_GLIBC_LOCALES}")

    ExternalProject_Add(felf_glibc_build
        ${felf_glibc_download}
        PREFIX "${CMAKE_CURRENT_BINARY_DIR}/build"
        PATCH_COMMAND ${felf_glibc_patch_command}
        CONFIGURE_COMMAND ${felf_glibc_env} ${felf_glibc_configure}
        BUILD_COMMAND ${felf_glibc_env} "${FELF_MAKE}" -j${felf_jobs}
        INSTALL_COMMAND ${felf_glibc_env} "${FELF_MAKE}" -j${felf_jobs}
                install install_root=<INSTALL_DIR>
            COMMAND "${CMAKE_COMMAND}"
                "-DSTAGE=<INSTALL_DIR>"
                "-DOUT=${felf_glibc_root}"
                "-DLOCALES=${felf_glibc_locales}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/collect.cmake"
        INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/stage"
        BUILD_BYPRODUCTS "${felf_glibc_stamp}" "${felf_glibc_locale}"
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
        LOG_OUTPUT_ON_FAILURE ON)
    # A changed patch starts over from a fresh source tree.
    ExternalProject_Add_StepDependencies(felf_glibc_build download
        ${felf_glibc_patches})

    message(STATUS
        "FELF's glibc: building glibc ${felf_glibc_version} with "
        "src/glibc/patches (set FELF_GLIBC_DIR to use a prebuilt one)")
    set(felf_glibc_deps "${felf_glibc_stamp}")
    set(felf_glibc_built ON)
endif()

# The list of files is known only once the glibc exists, so the source that
# embeds them is generated at build time.
add_custom_command(OUTPUT "${felf_glibc_assets_c}"
    COMMAND "${CMAKE_COMMAND}"
        "-DDIR=${felf_glibc_lib_dir}"
        "-DVERSION=${felf_glibc_version}"
        "-DOUT=${felf_glibc_assets_c}"
        -P "${felf_glibc_assets_script}"
    DEPENDS "${felf_glibc_assets_script}" ${felf_glibc_deps}
    COMMENT "Listing FELF's glibc ${felf_glibc_version}"
    VERBATIM)

add_library(felf_glibc_assets OBJECT "${felf_glibc_assets_c}")
target_include_directories(felf_glibc_assets PRIVATE
    "${PROJECT_SOURCE_DIR}/src/packer")
# #embed is C23.
set_source_files_properties("${felf_glibc_assets_c}" PROPERTIES
    COMPILE_OPTIONS "-std=gnu23"
    OBJECT_DEPENDS "${felf_glibc_deps}")
if(felf_glibc_built)
    add_dependencies(felf_glibc_assets felf_glibc_build)
endif()

# Every FELF carries a locale archive: glibc in the FELF has no host locale
# location of its own, and programs such as btop refuse to start without
# one.  A host archive is only a fallback for a handed-in glibc.
if(FELF_LOCALE_ARCHIVE_FILE)
    set(felf_locale_archive "${FELF_LOCALE_ARCHIVE_FILE}")
elseif(felf_glibc_built)
    set(felf_locale_archive "${felf_glibc_locale}")
else()
    find_file(felf_host_locale_archive NAMES locale-archive
        PATHS
            /run/current-system/sw/lib/locale
            /usr/lib/locale
            /usr/lib64/locale
        NO_DEFAULT_PATH)
    if(NOT felf_host_locale_archive)
        message(FATAL_ERROR
            "FELF_BUILD_CLI needs FELF_LOCALE_ARCHIVE_FILE: every FELF "
            "embeds a glibc locale archive (pass "
            "-DFELF_LOCALE_ARCHIVE_FILE=/path/to/locale-archive)")
    endif()
    set(felf_locale_archive "${felf_host_locale_archive}")
endif()
message(STATUS "Embedding locale archive ${felf_locale_archive}")
set(felf_locale_archive "${felf_locale_archive}" PARENT_SCOPE)
set(felf_glibc_built "${felf_glibc_built}" PARENT_SCOPE)
