set(CURRENT_TARGET network_broadcast_api)

list(APPEND CURRENT_TARGET_HEADERS
     include/graphene/plugins/network_broadcast_api/network_broadcast_api_plugin.hpp
     )

list(APPEND CURRENT_TARGET_SOURCES
     network_broadcast_api.cpp
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
        graphene::chain_plugin
        graphene::json_rpc
        graphene::p2p
        appbase
)

target_include_directories(graphene_${CURRENT_TARGET}
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../../")

install(TARGETS
        graphene_${CURRENT_TARGET}

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )



