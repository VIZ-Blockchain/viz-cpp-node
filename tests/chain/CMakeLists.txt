# Exit-from-emergency threshold: header-only arithmetic, so the binary needs no chain link.
add_executable(chain_exit_threshold_tests exit_threshold_test.cpp)
target_link_libraries(chain_exit_threshold_tests PRIVATE Boost::unit_test_framework)
target_include_directories(chain_exit_threshold_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/protocol/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/chain/include")
add_test(NAME chain_exit_threshold COMMAND chain_exit_threshold_tests)

# Header-only unpack premise: pins that a stored block can be unpacked as block_header and
# that the parser stops before the transactions (the validity argument of the header-only
# get_block_header path). Needs fc for serialization, not the chain library.
add_executable(chain_block_header_unpack_tests block_header_unpack_test.cpp)
target_link_libraries(chain_block_header_unpack_tests PRIVATE graphene_protocol fc Boost::unit_test_framework)
target_include_directories(chain_block_header_unpack_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/protocol/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/chain/include")
add_test(NAME chain_block_header_unpack COMMAND chain_block_header_unpack_tests)
