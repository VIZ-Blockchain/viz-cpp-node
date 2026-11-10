# Futility tracking of validator_guard key restoration (restore_backoff.hpp).
#
# Header-only and fc-free, so the binary needs no chain link — but it is NOT decoration: both
# verdicts move real transactions (a false "restore" broadcasts forever, a false "stop" needs a
# human to re-key). Driven here rather than only on a live stand.
add_executable(validator_guard_backoff_tests restore_backoff_test.cpp)

target_link_libraries(validator_guard_backoff_tests PRIVATE Boost::unit_test_framework)

target_include_directories(validator_guard_backoff_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/validator_guard/include")

# Static libboost_unit_test_framework — do NOT define BOOST_TEST_DYN_LINK.

add_test(NAME validator_guard_backoff COMMAND validator_guard_backoff_tests)
