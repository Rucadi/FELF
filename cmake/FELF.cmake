# FELF.cmake: pack a project's executables into FELFs from CMake.
#
#   include(FELF.cmake)
#   add_executable(app main.c)
#   felf_pack(app-felf TARGET app)          # builds app.felf
#
# The module uses a felf from FELF_EXECUTABLE or PATH.  If there is none, it
# downloads the static felf of release FELF_VERSION for the host from GitHub
# at configure time and checks its SHA-256.  Nothing is built from source.
#
# felf_pack(<name>
#     TARGET <executable> | PROGRAM <path>
#     [NAME <name>]                       name of the main program
#     [OUTPUT <path>]                     default: <binary dir>/<output name>.felf
#     [PROGRAMS <name>=<target|path>...]  more programs (--program)
#     [ALIASES <program>=<n1>[,<n2>...]...]
#     [INCLUDES <target|path>...]         dlopen'ed libraries to embed
#     [PLUGINS <target|path>...]          libraries kept as files (--plugin)
#     [LIB_DIRS <dir>...]  [HOST_LIBS <pattern>...]  [ENV <NAME=VALUE>...]
#     [ROOT <rel>]  [COMPRESS <level|none>]
#     [LAZY] [PERMISSIVE] [NO_DEFAULT_HOST_LIBS]
#     [EXTRA_ARGS <arg>...]  [DEPENDS <file|target>...]
#     [EXCLUDE_FROM_ALL]
#     [INSTALL_DESTINATION <dir> [INSTALL_COMPONENT <component>]])
#
# creates the custom target <name>, which writes the FELF.  The options are
# those of `felf pack` (`felf --help`, docs/packing.md).  Any argument that
# names a CMake target stands for its file.  ENV values are passed as they
# are, so $FELF_ROOT, $FELF_APP and $VAR reach felf unexpanded.  The
# property FELF_OUTPUT of <name> holds the output path.

include_guard(GLOBAL)

if(CMAKE_VERSION VERSION_LESS 3.19)
    message(FATAL_ERROR "FELF.cmake needs CMake 3.19 or newer")
endif()

# The release this file belongs to.  The release workflow fills in the
# checksums of its binaries; a copy taken from the source tree has none and
# reads SHA256SUMS from the release instead.
set(_FELF_MODULE_VERSION "0.1.0")
set(_FELF_SHA256_x86_64 "")
set(_FELF_SHA256_aarch64 "")

set(FELF_VERSION "${_FELF_MODULE_VERSION}" CACHE STRING
    "felf release to download when no felf is found")
set(FELF_RELEASE_URL "https://github.com/Rucadi/FELF/releases/download"
    CACHE STRING "Base URL of the felf releases")
set(FELF_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_felf" CACHE PATH
    "Where a downloaded felf is kept")
option(FELF_DOWNLOAD "Download felf when FELF_EXECUTABLE and PATH have none" ON)

function(_felf_host_arch out)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" cpu)
    if(cpu MATCHES "^(x86_64|amd64)$")
        set(${out} x86_64 PARENT_SCOPE)
    elseif(cpu MATCHES "^(aarch64|arm64)$")
        set(${out} aarch64 PARENT_SCOPE)
    else()
        set(${out} "" PARENT_SCOPE)
    endif()
endfunction()

# Downloads felf-<version>-<arch>-linux into FELF_DOWNLOAD_DIR/<version>/felf
# unless it is already there, and sets OUT to it.
function(_felf_download out)
    _felf_host_arch(arch)
    if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" OR arch STREQUAL "")
        message(FATAL_ERROR
            "FELF: no felf release for ${CMAKE_HOST_SYSTEM_NAME}/"
            "${CMAKE_HOST_SYSTEM_PROCESSOR}; set FELF_EXECUTABLE")
    endif()
    set(dir "${FELF_DOWNLOAD_DIR}/${FELF_VERSION}")
    set(felf "${dir}/felf")
    if(EXISTS "${felf}")
        set(${out} "${felf}" PARENT_SCOPE)
        return()
    endif()

    set(asset "felf-${FELF_VERSION}-${arch}-linux")
    set(base "${FELF_RELEASE_URL}/v${FELF_VERSION}")
    if(FELF_VERSION STREQUAL _FELF_MODULE_VERSION AND _FELF_SHA256_${arch})
        set(sha256 "${_FELF_SHA256_${arch}}")
    else()
        file(DOWNLOAD "${base}/SHA256SUMS" "${dir}/SHA256SUMS"
            STATUS status TLS_VERIFY ON)
        list(GET status 0 rc)
        if(NOT rc EQUAL 0)
            list(GET status 1 msg)
            message(FATAL_ERROR "FELF: cannot download ${base}/SHA256SUMS: ${msg}")
        endif()
        file(STRINGS "${dir}/SHA256SUMS" line REGEX "  ${asset}$")
        string(REGEX MATCH "^[0-9a-f]+" sha256 "${line}")
        if(NOT sha256 MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR "FELF: ${asset} is not in ${base}/SHA256SUMS")
        endif()
    endif()

    message(STATUS "FELF: downloading ${base}/${asset}")
    file(DOWNLOAD "${base}/${asset}" "${dir}/${asset}.part"
        STATUS status TLS_VERIFY ON)
    list(GET status 0 rc)
    if(NOT rc EQUAL 0)
        list(GET status 1 msg)
        file(REMOVE "${dir}/${asset}.part")
        message(FATAL_ERROR "FELF: cannot download ${base}/${asset}: ${msg}")
    endif()
    file(SHA256 "${dir}/${asset}.part" actual)
    if(NOT actual STREQUAL sha256)
        file(REMOVE "${dir}/${asset}.part")
        message(FATAL_ERROR
            "FELF: ${asset} has SHA-256 ${actual}, expected ${sha256}")
    endif()
    file(CHMOD "${dir}/${asset}.part" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    file(RENAME "${dir}/${asset}.part" "${felf}")
    set(${out} "${felf}" PARENT_SCOPE)
endfunction()

find_program(FELF_EXECUTABLE felf DOC "The felf CLI")
if(FELF_EXECUTABLE)
    set(_felf "${FELF_EXECUTABLE}")
elseif(FELF_DOWNLOAD)
    _felf_download(_felf)
else()
    message(FATAL_ERROR
        "FELF: no felf found; set FELF_EXECUTABLE or turn FELF_DOWNLOAD on")
endif()

execute_process(COMMAND "${_felf}" --version
    OUTPUT_VARIABLE _felf_version_out RESULT_VARIABLE _felf_rc)
if(NOT _felf_rc EQUAL 0 OR NOT _felf_version_out MATCHES "^felf ([^\n]+)")
    message(FATAL_ERROR "FELF: ${_felf} --version failed")
endif()
set(FELF_FOUND_VERSION "${CMAKE_MATCH_1}")
message(STATUS "FELF: felf ${FELF_FOUND_VERSION} (${_felf})")

if(NOT TARGET felf::felf)
    add_executable(felf::felf IMPORTED GLOBAL)
    set_target_properties(felf::felf PROPERTIES IMPORTED_LOCATION "${_felf}")
endif()
unset(_felf)
unset(_felf_rc)
unset(_felf_version_out)

# Sets OUT to the file VALUE stands for ($<TARGET_FILE:...> for a target)
# and appends targets to the list DEPS.
macro(_felf_file out value deps)
    if(TARGET "${value}")
        set(${out} "$<TARGET_FILE:${value}>")
        list(APPEND ${deps} "${value}")
    else()
        get_filename_component(${out} "${value}" ABSOLUTE)
        list(APPEND ${deps} "${${out}}")
    endif()
endmacro()

function(felf_pack name)
    cmake_parse_arguments(PARSE_ARGV 1 arg
        "LAZY;PERMISSIVE;NO_DEFAULT_HOST_LIBS;EXCLUDE_FROM_ALL"
        "TARGET;PROGRAM;NAME;OUTPUT;ROOT;COMPRESS;INSTALL_DESTINATION;INSTALL_COMPONENT"
        "PROGRAMS;ALIASES;INCLUDES;PLUGINS;LIB_DIRS;HOST_LIBS;ENV;EXTRA_ARGS;DEPENDS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "felf_pack: unknown arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(arg_TARGET AND arg_PROGRAM OR NOT arg_TARGET AND NOT arg_PROGRAM)
        message(FATAL_ERROR "felf_pack(${name}): give TARGET or PROGRAM")
    endif()
    if(CMAKE_CROSSCOMPILING)
        message(WARNING
            "felf_pack(${name}): felf packs for the architecture it was "
            "built for; cross-compiled programs need FELF_BOOTSTRAP, "
            "FELF_AUDIT and FELF_SHIM in the environment (docs/packing.md)")
    endif()

    set(deps ${arg_DEPENDS})
    if(arg_TARGET)
        _felf_file(input "${arg_TARGET}" deps)
        get_target_property(stem "${arg_TARGET}" OUTPUT_NAME)
        if(NOT stem)
            set(stem "${arg_TARGET}")
        endif()
    else()
        _felf_file(input "${arg_PROGRAM}" deps)
        get_filename_component(stem "${arg_PROGRAM}" NAME)
    endif()
    if(arg_NAME)
        set(input "${arg_NAME}=${input}")
    endif()
    if(arg_OUTPUT)
        get_filename_component(output "${arg_OUTPUT}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    else()
        set(output "${CMAKE_CURRENT_BINARY_DIR}/${stem}.felf")
    endif()

    set(args pack "${input}" -o "${output}")
    foreach(program IN LISTS arg_PROGRAMS)
        if(program MATCHES "^([^=]+)=(.+)$")
            set(pname "${CMAKE_MATCH_1}")
            _felf_file(path "${CMAKE_MATCH_2}" deps)
            list(APPEND args --program "${pname}=${path}")
        else()
            _felf_file(path "${program}" deps)
            list(APPEND args --program "${path}")
        endif()
    endforeach()
    foreach(alias IN LISTS arg_ALIASES)
        list(APPEND args --alias "${alias}")
    endforeach()
    foreach(lib IN LISTS arg_INCLUDES)
        _felf_file(path "${lib}" deps)
        list(APPEND args --include "${path}")
    endforeach()
    foreach(lib IN LISTS arg_PLUGINS)
        _felf_file(path "${lib}" deps)
        list(APPEND args --plugin "${path}")
    endforeach()
    foreach(dir IN LISTS arg_LIB_DIRS)
        get_filename_component(dir "${dir}" ABSOLUTE)
        list(APPEND args --lib-dir "${dir}")
    endforeach()
    foreach(pattern IN LISTS arg_HOST_LIBS)
        list(APPEND args --host-lib "${pattern}")
    endforeach()
    foreach(var IN LISTS arg_ENV)
        list(APPEND args --env "${var}")
    endforeach()
    if(arg_ROOT)
        list(APPEND args --root "${arg_ROOT}")
    endif()
    if(arg_COMPRESS)
        list(APPEND args --compress "${arg_COMPRESS}")
    endif()
    foreach(flag LAZY PERMISSIVE NO_DEFAULT_HOST_LIBS)
        if(arg_${flag})
            string(TOLOWER "--${flag}" option)
            string(REPLACE "_" "-" option "${option}")
            list(APPEND args "${option}")
        endif()
    endforeach()
    list(APPEND args ${arg_EXTRA_ARGS})

    get_filename_component(output_dir "${output}" DIRECTORY)
    get_filename_component(output_name "${output}" NAME)
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
        COMMAND felf::felf ${args}
        DEPENDS ${deps}
        COMMENT "Packing FELF ${output_name}"
        VERBATIM)
    if(arg_EXCLUDE_FROM_ALL)
        add_custom_target(${name} DEPENDS "${output}")
    else()
        add_custom_target(${name} ALL DEPENDS "${output}")
    endif()
    set_target_properties(${name} PROPERTIES FELF_OUTPUT "${output}")

    if(arg_INSTALL_DESTINATION)
        set(component)
        if(arg_INSTALL_COMPONENT)
            set(component COMPONENT "${arg_INSTALL_COMPONENT}")
        endif()
        install(PROGRAMS "${output}"
            DESTINATION "${arg_INSTALL_DESTINATION}" ${component})
    endif()
endfunction()
