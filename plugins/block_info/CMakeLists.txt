set(CURRENT_TARGET block_info)

list(APPEND CURRENT_TARGET_HEADERS
    include/graphene/plugins/block_info/plugin.hpp
    include/graphene/plugins/block_info/block_info.hpp
)

list(APPEND CURRENT_TARGET_SOURCES
    plugin.cpp
)

add_library(graphene_${CURRENT_TARGET} ${VIZ_LIBRARY_TYPE}
        ${CURRENT_TARGET_HEADERS}
        ${CURRENT_TARGET_SOURCES}
    )

add_library(graphene::${CURRENT_TARGET} ALIAS graphene_${CURRENT_TARGET})

set_property(TARGET graphene_${CURRENT_TARGET} PROPERTY EXPORT_NAME ${CURRENT_TARGET})

target_link_libraries(
        graphene_${CURRENT_TARGET}
        graphene_chain
        graphene_protocol
        appbase
        graphene_chain_plugin
        graphene::json_rpc
        fc
)

target_include_directories(
        graphene_${CURRENT_TARGET}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../"
)

install(TARGETS
        graphene_${CURRENT_TARGET}

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)
