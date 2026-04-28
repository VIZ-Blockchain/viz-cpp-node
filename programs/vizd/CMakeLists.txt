set(CURRENT_TARGET vizd)
add_executable(${CURRENT_TARGET} main.cpp)

if(UNIX AND NOT APPLE)
    set(rt_library rt)
elseif(APPLE)
    list(APPEND PLATFORM_SPECIFIC_LIBS readline)
endif()

find_package(Gperftools QUIET)
if(GPERFTOOLS_FOUND)
    message(STATUS "Found gperftools; compiling vizd with TCMalloc")
    list(APPEND PLATFORM_SPECIFIC_LIBS tcmalloc)
endif()

target_link_libraries(
        ${CURRENT_TARGET} PRIVATE
        appbase
        graphene::webserver_plugin
        graphene::p2p
        graphene_utilities
        graphene::chain_plugin
        graphene::network_broadcast_api
        graphene::witness
        graphene::witness_api
        graphene::database_api
        graphene::test_api_plugin
        graphene::social_network
        graphene::tags
        graphene::operation_history
        graphene::account_by_key
        graphene::account_history
        graphene::private_message
        graphene::debug_node
        graphene::raw_block
        graphene::block_info
        graphene::json_rpc
        graphene::follow
        graphene::committee_api
        graphene::invite_api
        graphene::paid_subscription_api
        graphene::custom_protocol_api
        graphene::snapshot
        ${MONGO_LIB}
        graphene_protocol
        fc
        ${CMAKE_DL_LIBS}
        ${PLATFORM_SPECIFIC_LIBS}
)

install(TARGETS
        ${CURRENT_TARGET}

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )
