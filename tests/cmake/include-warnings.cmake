include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# --include embeds extra shared objects.  The first object with a SONAME
# wins, a file that is not an ELF and a library whose dependency cannot be
# resolved are warnings (the pack still succeeds).  The arguments can come
# from an @FILE.

set(notes "${WORK}/include-notes.so")
file(WRITE "${notes}" "not an ELF\n")

# The needy library links against libghost.so.  Move the ghost directory
# away while packing: its absolute RUNPATH then points nowhere, so the
# dependency really is unresolvable.  Restore it before asserting so a
# failed check does not affect later runs.
set(ghost_dir "${WORK}/include-ghost")
set(ghost_gone "${WORK}/include-ghost.gone")
set(restore_ghost FALSE)
file(REMOVE_RECURSE "${ghost_gone}")
if(EXISTS "${ghost_dir}")
    file(RENAME "${ghost_dir}" "${ghost_gone}")
    set(restore_ghost TRUE)
endif()

file(WRITE "${WORK}/includes"
    "# extra objects, one per line\n"
    "--include=${PLUGIN}\n"
    "--include=${PLUGIN_ALT}\n"
    "\n"
    "--include=${notes}\n"
    "--include=${NEEDY}\n")

felf_run(pack "${FELF}" pack "${DLOPEN}" --lazy "@${WORK}/includes"
    -o "${WORK}/include.felf")

if(restore_ghost)
    file(RENAME "${ghost_gone}" "${ghost_dir}")
endif()

felf_assert_rc("pack with include warnings" 0 "${pack_RESULT}")
felf_assert_contains("a non-library is a warning" "${pack_ERROR}"
    "not bundling ${notes}")
felf_assert_contains("an unresolvable library is a warning" "${pack_ERROR}"
    "not bundling ${NEEDY}")

# Only the first libplugin.so is embedded; the alt copy with the same
# SONAME does not add an object.
felf_run(objects "${FELF}" sql "${WORK}/include.felf"
    "SELECT name FROM objects WHERE name IN ('libplugin.so','notes.so','libneedy.so','libghost.so') ORDER BY name")
felf_assert_rc("count included objects" 0 "${objects_RESULT}")
felf_assert_contains("one libplugin.so is embedded" "${objects_OUTPUT}"
    "libplugin.so")
felf_assert_not_contains("the alt copy is not embedded" "${objects_OUTPUT}"
    "libghost.so")

# The bundled copy is the one that runs, even though the host directory has
# a different libplugin.so with the same SONAME.
felf_run(run "${FELF}" pack "${DLOPEN}" --lazy --include "${PLUGIN}"
    --include "${PLUGIN_ALT}" -o "${WORK}/include-first.felf")
felf_assert_rc("pack include-first" 0 "${run_RESULT}")
felf_run(run_first "${WORK}/include-first.felf")
felf_assert_rc("run include-first" 0 "${run_first_RESULT}")
felf_assert_contains("the first libplugin.so wins" "${run_first_OUTPUT}"
    "plugin value=1234")
