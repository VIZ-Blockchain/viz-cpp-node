set(CURRENT_TARGET custom_protocol_api)

list(APPEND CURRENT_TARGET_HEADERS
        include/graphene/plugins/custom_protocol_api/custom_protocol_api.hpp
        include/graphene/plugins/custom_protocol_api/custom_protocol_api_visitor.hpp
        include/graphene/plugins/custom_protocol_api/custom_protocol_api_object.hpp
)

list(APPEND CURRENT_TARGET_SOURCES
        custom_protocol_api.cpp
        custom_protocol_api_visitor.cpp
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
        graphene::network
        graphene::api
        appbase
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
