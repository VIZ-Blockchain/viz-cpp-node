set(CURRENT_TARGET test_api_plugin)

list(APPEND CURRENT_TARGET_HEADERS
     include/graphene/plugins/test_api/test_api_plugin.hpp
     )

list(APPEND CURRENT_TARGET_SOURCES
     test_api_plugin.cpp
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
        graphene::json_rpc
        fc
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

install(FILES ${HEADERS} DESTINATION "include/graphene/test_api_plugin")


#target_link_libraries( test_api_plugin plugin appbase fc )