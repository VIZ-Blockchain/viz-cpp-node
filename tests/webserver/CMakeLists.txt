add_executable(webserver_ip_counter_tests ip_counter_test.cpp)
target_link_libraries(webserver_ip_counter_tests PRIVATE Boost::unit_test_framework)
target_include_directories(webserver_ip_counter_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/webserver/include")
add_test(NAME webserver_ip_counter COMMAND webserver_ip_counter_tests)
