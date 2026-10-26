# Bounded file appender (optional log with a line cap). The live behaviour is wired in
# programs/vizd/main.cpp; these checks pin the tail-trim contract and the compaction
# that happens while logging.

add_executable(capped_file_appender_tests capped_file_appender_test.cpp)

target_link_libraries(capped_file_appender_tests
    PRIVATE graphene_utilities
            fc
            Boost::unit_test_framework
)

# Static libboost_unit_test_framework — do NOT define BOOST_TEST_DYN_LINK.

add_test(NAME capped_file_appender COMMAND capped_file_appender_tests)

# [log.file_appender.<name>] section: the four rotation keys used to be hard-coded, so an
# operator's edit was accepted and silently ignored (goal #7270).
add_executable(file_appender_options_tests file_appender_options_test.cpp)

target_link_libraries(file_appender_options_tests
    PRIVATE graphene_utilities
            fc
            Boost::unit_test_framework
)

add_test(NAME file_appender_options COMMAND file_appender_options_tests)

# Rotation itself: the hourly file must keep its lines across a restart, the boundary must
# start the next file and re-point node.log, and a stale EMPTY file must be removed. Plus
# the fc::ofstream mode translation (default/out truncate, only `append` appends) — the
# numbers of fc::ofstream::mode collide with std::ios_base::app, so a raw cast silently
# turned every default open into an append (wallet/json/aes/mmap).
add_executable(file_appender_rotation_tests file_appender_rotation_test.cpp)

target_link_libraries(file_appender_rotation_tests
    PRIVATE fc
            Boost::unit_test_framework
)

add_test(NAME file_appender_rotation COMMAND file_appender_rotation_tests)
