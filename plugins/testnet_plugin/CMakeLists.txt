set(CURRENT_TARGET testnet_plugin)

list(APPEND CURRENT_TARGET_HEADERS
     include/graphene/plugins/testnet_plugin/testnet_plugin.hpp
     )

list(APPEND CURRENT_TARGET_SOURCES
     testnet_plugin.cpp
     )

add_library(graphene_${CURRENT_TARGET} ${VIZ_LIBRARY_TYPE}
                ${CURRENT_TARGET_HEADERS}
                ${CURRENT_TARGET_SOURCES}
                )

add_library(graphene::${CURRENT_TARGET} ALIAS graphene_${CURRENT_TARGET})
set_property(TARGET graphene_${CURRENT_TARGET} PROPERTY EXPORT_NAME ${CURRENT_TARGET})

target_link_libraries(
        graphene_${CURRENT_TARGET}
        graphene::chain_plugin
        graphene::protocol
        graphene_utilities
        graphene_time
        appbase
)

target_include_directories(graphene_${CURRENT_TARGET}
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

install(TARGETS
        graphene_${CURRENT_TARGET}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )
