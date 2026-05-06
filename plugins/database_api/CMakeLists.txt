set(CURRENT_TARGET database_api)

list(APPEND ${CURRENT_TARGET}_HEADERS
     include/graphene/plugins/database_api/state.hpp
     include/graphene/plugins/database_api/plugin.hpp

     include/graphene/plugins/database_api/api_objects/account_recovery_request_api_object.hpp
     include/graphene/plugins/database_api/forward.hpp
     include/graphene/plugins/database_api/api_objects/master_authority_history_api_object.hpp
     include/graphene/plugins/database_api/api_objects/proposal_api_object.hpp


     )

list(APPEND ${CURRENT_TARGET}_SOURCES
     api.cpp
     proposal_api_object.cpp
)

if(BUILD_SHARED_LIBRARIES)
    add_library(graphene_${CURRENT_TARGET} SHARED
                ${${CURRENT_TARGET}_HEADERS}
                ${${CURRENT_TARGET}_SOURCES}
                )
else()
    add_library(graphene_${CURRENT_TARGET} STATIC
                ${${CURRENT_TARGET}_HEADERS}
                ${${CURRENT_TARGET}_SOURCES}
                )
endif()

add_library(graphene::${CURRENT_TARGET} ALIAS graphene_${CURRENT_TARGET})
set_property(TARGET graphene_${CURRENT_TARGET} PROPERTY EXPORT_NAME ${CURRENT_TARGET})

target_link_libraries(
        graphene_${CURRENT_TARGET}
        graphene_chain
        graphene::chain_plugin
        graphene_protocol
        graphene::json_rpc
        graphene_utilities
        appbase
        fc
        graphene::api
)
target_include_directories(graphene_${CURRENT_TARGET} PUBLIC "include")

install(TARGETS
        graphene_${CURRENT_TARGET}

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )
