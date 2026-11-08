include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# extract writes every object, decompressed, and the database.

set(extract "${WORK}/extract")
file(REMOVE_RECURSE "${extract}")

felf_run(extract "${FELF}" extract "${WORK}/hello-mapped.felf"
    --output "${extract}")
felf_assert_rc("extract" 0 "${extract_RESULT}")
felf_assert_file("extracted program" "${extract}/hello")
felf_assert_file("extracted database" "${extract}/felf.sqlite")
felf_assert_no_file("extract writes no manifest" "${extract}/manifest.txt")

# The program is stored after /nix/store hashes are rewritten to 'e's.
felf_scrub_store_hex("${HELLO}" expected_hex)
file(READ "${extract}/hello" actual_hex HEX)
felf_assert_equal("extracted program bytes" "${expected_hex}" "${actual_hex}")
