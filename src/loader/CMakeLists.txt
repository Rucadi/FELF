# Runtime shim: /proc/self/exe and child-process environment.
add_library(felf_runtime MODULE felf_runtime.c)
target_link_libraries(felf_runtime PRIVATE felf_headers ${CMAKE_DL_LIBS})
target_compile_options(felf_runtime PRIVATE -Wall -Wextra)
set_target_properties(felf_runtime PROPERTIES
    PREFIX ""
    OUTPUT_NAME "libfelf_runtime"
    LIBRARY_OUTPUT_DIRECTORY "${FELF_OUTPUT_DIR}")

add_library(felf_audit MODULE felf_audit.c)
target_link_libraries(felf_audit PRIVATE felf_headers)
# Freestanding: GCC must not turn the hand-written string loops back into
# strlen/memcmp calls or add stack-protector calls (Nix hardening), and
# -z defs makes any such call a link error instead of an audit module that
# glibc silently ignores.
target_compile_options(felf_audit PRIVATE -Wall -Wextra -Wpedantic
    -ffreestanding -fno-tree-loop-distribute-patterns
    -fno-stack-protector)
target_link_options(felf_audit PRIVATE -nostdlib -shared -Wl,-z,defs)
set_target_properties(felf_audit PROPERTIES
    PREFIX ""
    OUTPUT_NAME "libfelf_audit"
    LIBRARY_OUTPUT_DIRECTORY "${FELF_OUTPUT_DIR}")
