set(CURRENT_TARGET follow)

list(APPEND CURRENT_TARGET_HEADERS
     include/graphene/plugins/follow/follow_api_object.hpp
     include/graphene/plugins/follow/follow_evaluators.hpp
     include/graphene/plugins/follow/follow_objects.hpp
     include/graphene/plugins/follow/follow_operations.hpp
     include/graphene/plugins/follow/follow_forward.hpp
     include/graphene/plugins/follow/plugin.hpp
)

list(APPEND CURRENT_TARGET_SOURCES
     follow_evaluators.cpp
     follow_operations.cpp
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
        graphene::json_rpc
        graphene::chain_plugin
        graphene::protocol
        graphene::api
        appbase
        fc
)
target_include_directories(graphene_${CURRENT_TARGET} PUBLIC "include")

install(TARGETS
        graphene_${CURRENT_TARGET}

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )