set(CURRENT_TARGET chain_plugin)
list(APPEND CURRENT_TARGET_HEADERS
     include/graphene/plugins/chain/plugin.hpp
     )

list(APPEND CURRENT_TARGET_SOURCES
     plugin.cpp
     )

if(BUILD_SHARED_LIBRARIES)
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

set_property(TARGET graphene_${CURRENT_TARGET} PROPERTY EXPORT_NAME ${CURRENT_TARGET})

target_link_libraries(
        graphene_${CURRENT_TARGET}
        graphene_chain
        graphene_protocol
        fc
        appbase
        graphene::json_rpc
)
target_include_directories(graphene_${CURRENT_TARGET}
                           PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
                           "${CMAKE_CURRENT_SOURCE_DIR}/../../"
                           "${CMAKE_CURRENT_SOURCE_DIR}/../snapshot/include")

install(TARGETS
        graphene_${CURRENT_TARGET}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )
install(FILES ${HEADERS} DESTINATION "include/graphene/chain_plugin")
