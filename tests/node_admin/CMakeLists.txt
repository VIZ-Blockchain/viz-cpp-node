# Pure helpers of the node_admin plugin (secret masking, admin-token and config.ini parsing).
# The live contract is covered by plugins/node_admin/tests/acceptance.sh; these checks pin the
# pure functions and the malformed shapes they must reject.

add_executable(node_admin_util_tests node_admin_util_test.cpp)

target_link_libraries(node_admin_util_tests
    PRIVATE fc
            Boost::unit_test_framework
)

target_include_directories(node_admin_util_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/node_admin/include"
)

# Static libboost_unit_test_framework — do NOT define BOOST_TEST_DYN_LINK.

add_test(NAME node_admin_util COMMAND node_admin_util_tests)

# The verdict branches of admin.objects.check: the judge invites an operator to wipe node
# state, so a false "corrupted" and a false "ok" both matter. Header-only and fc-free.
add_executable(node_admin_objects_tests node_admin_objects_test.cpp)

target_link_libraries(node_admin_objects_tests
    PRIVATE Boost::unit_test_framework
)

target_include_directories(node_admin_objects_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/node_admin/include"
)

add_test(NAME node_admin_objects COMMAND node_admin_objects_tests)
