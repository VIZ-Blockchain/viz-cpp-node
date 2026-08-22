set(CURRENT_TARGET account_history)

list(APPEND CURRENT_TARGET_HEADERS
    include/graphene/plugins/account_history/plugin.hpp
    include/graphene/plugins/account_history/history_object.hpp
)

list(APPEND CURRENT_TARGET_SOURCES
    plugin.cpp
)

add_library(graphene_${CURRENT_TARGET} ${VIZ_LIBRARY_TYPE}
        ${CURRENT_TARGET_HEADERS}
        ${CURRENT_TARGET_SOURCES}
    )

add_library(graphene::${CURRENT_TARGET} ALIAS graphene_${CURRENT_TARGET})

target_link_libraries (
    graphene_${CURRENT_TARGET}
    graphene_chain
    graphene_chain_plugin
    graphene_protocol
    graphene::operation_history
    appbase
    graphene_json_rpc
    graphene_time
    chainbase
    fc
)

target_include_directories(graphene_${CURRENT_TARGET}
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../"
)

install(TARGETS
    graphene_${CURRENT_TARGET}

    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)