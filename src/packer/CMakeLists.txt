# The felf CLI.  It carries the runtime pieces it puts into every FELF
# (assets.c): the bootstrap, the audit module and runtime shim (built here,
# or prebuilt: Nix builds the bootstrap statically and the modules against
# glibc, apart from a static CLI), FELF's glibc (../glibc) and the locale
# archive.
if(FELF_BOOTSTRAP_FILE)
    set(felf_bootstrap_file "${FELF_BOOTSTRAP_FILE}")
elseif(FELF_BUILD_BOOTSTRAP)
    set(felf_bootstrap_file "${FELF_OUTPUT_DIR}/felf-bootstrap")
else()
    message(FATAL_ERROR
        "FELF_BUILD_CLI needs FELF_BUILD_BOOTSTRAP or FELF_BOOTSTRAP_FILE")
endif()
if(FELF_AUDIT_FILE AND FELF_SHIM_FILE)
    set(felf_audit_file "${FELF_AUDIT_FILE}")
    set(felf_shim_file "${FELF_SHIM_FILE}")
elseif(FELF_BUILD_AUDIT)
    set(felf_audit_file "${FELF_OUTPUT_DIR}/libfelf_audit.so")
    set(felf_shim_file "${FELF_OUTPUT_DIR}/libfelf_runtime.so")
else()
    message(FATAL_ERROR
        "FELF_BUILD_CLI needs FELF_BUILD_AUDIT or FELF_AUDIT_FILE and "
        "FELF_SHIM_FILE")
endif()

add_executable(felf
    main.c
    assets.c
    elf_parser.c
    resolver.c
    felf_file.c
    pack.c
    inspect.c
    verify.c
    sql.c
    extract.c)
target_link_libraries(felf PRIVATE
    felf_glibc_assets felf_store felf_read Threads::Threads)
target_compile_options(felf PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(felf PRIVATE
    FELF_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(felf PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FELF_OUTPUT_DIR}")
if(FELF_STATIC_CLI)
    target_link_options(felf PRIVATE -static)
endif()

# #embed is C23.  The files are named by path, not $<TARGET_FILE:...>, so
# OBJECT_DEPENDS can name them and assets.c is rebuilt when one changes.
set_source_files_properties(assets.c PROPERTIES
    COMPILE_OPTIONS "-std=gnu23"
    COMPILE_DEFINITIONS
        "FELF_BOOTSTRAP_FILE=\"${felf_bootstrap_file}\";FELF_AUDIT_FILE=\"${felf_audit_file}\";FELF_SHIM_FILE=\"${felf_shim_file}\";FELF_LOCALE_ARCHIVE_FILE=\"${felf_locale_archive}\""
    OBJECT_DEPENDS
        "${felf_bootstrap_file};${felf_audit_file};${felf_shim_file};${felf_locale_archive}")
if(FELF_BUILD_AUDIT AND NOT FELF_AUDIT_FILE)
    add_dependencies(felf felf_audit felf_runtime)
endif()
if(FELF_BUILD_BOOTSTRAP AND NOT FELF_BOOTSTRAP_FILE)
    add_dependencies(felf felf-bootstrap)
endif()
if(felf_glibc_built)
    # The generated locale archive.
    add_dependencies(felf felf_glibc_build)
endif()

install(TARGETS felf RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
