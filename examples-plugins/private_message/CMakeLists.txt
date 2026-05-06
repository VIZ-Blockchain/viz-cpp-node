set(CURRENT_TARGET private_message)

list(APPEND CURRENT_TARGET_HEADERS
        include/graphene/plugins/private_message/private_message_plugin.hpp
        include/graphene/plugins/private_message/private_message_objects.hpp
        include/graphene/plugins/private_message/private_message_evaluators.hpp
        )

list(APPEND CURRENT_TARGET_SOURCES
        private_message_plugin.cpp
        private_message_objects.cpp
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
        graphene::chain_plugin
        graphene::p2p
        graphene::protocol
        graphene::network
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

