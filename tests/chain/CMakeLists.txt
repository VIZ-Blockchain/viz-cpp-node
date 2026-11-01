# Exit-from-emergency threshold: header-only arithmetic, so the binary needs no chain link.
add_executable(chain_exit_threshold_tests exit_threshold_test.cpp)
target_link_libraries(chain_exit_threshold_tests PRIVATE Boost::unit_test_framework)
target_include_directories(chain_exit_threshold_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/protocol/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/chain/include")
add_test(NAME chain_exit_threshold COMMAND chain_exit_threshold_tests)
