set(CURRENT_TARGET witness_api)

list(APPEND CURRENT_TARGET_HEADERS
    include/graphene/plugins/witness_api/plugin.hpp
    include/graphene/plugins/witness_api/api_objects/feed_history_api_object.hpp
)

list(APPEND CURRENT_TARGET_SOURCES
    plugin.cpp
)

if (BUILD_SHARED_LIBRARIES)
    add_library(graphene_${CURRENT_TARGET} SHARED
        ${CURRENT_TARGET_HEADERS}
        ${CURRENT_TARGET_SOURCES}
    )
else()
    add_library(graphene_${CURRENT_TARGET} STATIC
        ${CURRENT_TARGET_HEADERS}
        ${CURRENT_TARGET_SOURCES}
    )
endif()

add_library(graphene::${CURRENT_TARGET} ALIAS graphene_${CURRENT_TARGET})

target_link_libraries (
    graphene_${CURRENT_TARGET}
    graphene_chain
    graphene_chain_plugin
    graphene_protocol
    graphene_api
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