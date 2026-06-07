add_library(private_message_plugin
    private_message_plugin.cpp
)

target_link_libraries(private_message_plugin
    graphene_chain
    graphene_protocol
    json_rpc_plugin
    appbase
    fc
)

target_include_directories(private_message_plugin
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

install(TARGETS
    private_message_plugin

    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
