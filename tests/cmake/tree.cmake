include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# Tree bundles: plugins stay as ordinary files, but their dependencies are
# embedded.  The plugin's RUNPATH points at a build directory that is made
# unavailable, so the dependency must come from the FELF.

felf_assert_file("the tree plugin dependency" "${DEP}")
set(tree "${WORK}/tree")
set(plugin_dir "${tree}/lib/plugins")
set(gone "${DEP_DIR}.gone")
file(REMOVE_RECURSE "${tree}" "${gone}" "${WORK}/tree-link")
file(MAKE_DIRECTORY "${tree}/bin" "${plugin_dir}" "${WORK}/outside")
felf_run(copy_plugin "${CMAKE_COMMAND}" -E copy "${PLUGIN}"
    "${plugin_dir}/libtreeplugin.so")
felf_assert_rc("copy the tree plugin" 0 "${copy_plugin_RESULT}")
felf_run(copy_outside "${CMAKE_COMMAND}" -E copy "${PLUGIN}"
    "${WORK}/outside/libtreeplugin.so")
felf_assert_rc("copy the outside plugin" 0 "${copy_outside_RESULT}")
file(CREATE_LINK "tree" "${WORK}/tree-link" SYMBOLIC)

foreach(mode lazy preload)
    set(extra)
    if(mode STREQUAL "lazy")
        set(extra --lazy)
    endif()
    felf_run("pack_${mode}" "${FELF}" pack "${TREE}" ${extra}
        --root .. --lib-dir "${DEP_DIR}" --plugin
        "${plugin_dir}/libtreeplugin.so"
        -o "${tree}/bin/tree-${mode}")
    felf_assert_rc("${mode}: pack" 0 "${pack_${mode}_RESULT}")

    felf_run("objects_${mode}" "${FELF}" sql "${tree}/bin/tree-${mode}"
        "SELECT name FROM objects ORDER BY name")
    felf_assert_rc("${mode}: object list" 0 "${objects_${mode}_RESULT}")
    felf_assert_contains("${mode}: the plugin dependency is embedded"
        "${objects_${mode}_OUTPUT}" "libdep.so")
    felf_assert_not_contains("${mode}: the plugin itself is not embedded"
        "${objects_${mode}_OUTPUT}" "libtreeplugin.so")
endforeach()

# Make the plugin's RUNPATH unavailable.  The permissive pack still has the
# dependency in the renamed directory; it is embedded as a fallback.
set(restore_dep FALSE)
if(EXISTS "${DEP_DIR}")
    file(RENAME "${DEP_DIR}" "${gone}")
    set(restore_dep TRUE)
endif()

felf_run(pack_permissive "${FELF}" pack "${TREE}" --permissive --lazy
    --root .. --lib-dir "${gone}" --plugin
    "${plugin_dir}/libtreeplugin.so"
    -o "${tree}/bin/tree-permissive")
felf_run(run_permissive "${CMAKE_COMMAND}" -E env FELF_DEBUG=1
    "${tree}/bin/tree-permissive" "${WORK}/outside/libtreeplugin.so")
felf_run(pack_broken "${FELF}" pack "${TREE}" --plugin
    "${WORK}/outside/libtreeplugin.so" -o "${WORK}/broken.felf")

foreach(mode lazy preload)
    felf_run("run_${mode}" "${tree}/bin/tree-${mode}"
        "${plugin_dir}/libtreeplugin.so")
    felf_run("outside_${mode}" "${tree}/bin/tree-${mode}"
        "${WORK}/outside/libtreeplugin.so")
    felf_run("link_${mode}" "${WORK}/tree-link/bin/tree-${mode}"
        "${WORK}/tree-link/bin/../lib/plugins/libtreeplugin.so")
endforeach()

if(restore_dep)
    file(RENAME "${gone}" "${DEP_DIR}")
endif()

foreach(mode lazy preload)
    felf_assert_rc("${mode}: the tree plugin runs" 0 "${run_${mode}_RESULT}")
    felf_assert_contains("${mode}: the bundled dependency is used"
        "${run_${mode}_OUTPUT}" "tree value=77")
    felf_assert_contains("${mode}: a plugin outside the root is denied"
        "${outside_${mode}_OUTPUT}" "tree plugin failed")
    felf_assert_contains("${mode}: a symlinked plugin path is inside the root"
        "${link_${mode}_OUTPUT}" "tree value=77")
endforeach()

felf_assert_rc("permissive: pack" 0 "${pack_permissive_RESULT}")
felf_assert_contains("permissive: an outside plugin loads"
    "${run_permissive_OUTPUT}" "tree value=77")
felf_assert_contains("permissive: it uses the bundled dependency"
    "${run_permissive_ERROR}" "felf[audit]: bundled libdep.so")

felf_assert_rc("an unresolvable plugin does not fail the pack" 0
    "${pack_broken_RESULT}")
felf_assert_contains("an unresolvable plugin is a warning" "${pack_broken_ERROR}"
    "warning: plugin")
