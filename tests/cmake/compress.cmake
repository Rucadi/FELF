include("${CMAKE_CURRENT_LIST_DIR}/TestUtils.cmake")

# zstd shrinks a FELF, and extract gives back the original object bytes.

file(SIZE "${WORK}/hello-zstd.felf" zstd_size)
file(SIZE "${WORK}/hello-plain.felf" plain_size)
if(NOT zstd_size LESS plain_size)
    message(FATAL_ERROR
        "zstd FELF is not smaller (${zstd_size} >= ${plain_size} bytes)")
endif()

set(extract "${WORK}/zstd-extract")
file(REMOVE_RECURSE "${extract}")
felf_run(extract "${FELF}" extract "${WORK}/hello-zstd.felf"
    --output "${extract}")
felf_assert_rc("extract a zstd FELF" 0 "${extract_RESULT}")
felf_scrub_store_hex("${HELLO}" expected_hex)
file(READ "${extract}/hello" actual_hex HEX)
felf_assert_equal("extract decompresses the program byte for byte"
    "${expected_hex}" "${actual_hex}")
