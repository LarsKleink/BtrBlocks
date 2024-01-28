# ---------------------------------------------------------------------------
# BtrBlocks Examples
# ---------------------------------------------------------------------------

set(BTR_EXAMPLES_DIR ${CMAKE_CURRENT_LIST_DIR})

add_executable(my_compression ${BTR_EXAMPLES_DIR}/compression.cpp)
target_link_libraries(my_compression btrblocks)
