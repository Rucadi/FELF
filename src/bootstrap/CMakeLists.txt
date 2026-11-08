add_executable(felf-bootstrap
    main.c
    handoff.c
    audit_index.c)
target_link_libraries(felf-bootstrap PRIVATE felf_read Threads::Threads)
target_compile_options(felf-bootstrap PRIVATE
    -Wall -Wextra -Wpedantic -fno-stack-protector)
target_link_options(felf-bootstrap PRIVATE -Wl,--gc-sections)
if(FELF_STATIC_BOOTSTRAP)
    target_link_options(felf-bootstrap PRIVATE -static)
endif()
set_target_properties(felf-bootstrap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FELF_OUTPUT_DIR}")
