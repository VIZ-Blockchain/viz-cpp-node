set(CURRENT_TARGET network)

list(APPEND ${CURRENT_TARGET}_HEADERS
        include/graphene/network/config.hpp
        include/graphene/network/core_messages.hpp
        include/graphene/network/exceptions.hpp
        include/graphene/network/message.hpp
        include/graphene/network/message_oriented_connection.hpp
        include/graphene/network/node.hpp
        include/graphene/network/peer_connection.hpp
        include/graphene/network/peer_database.hpp
        include/graphene/network/stcp_socket.hpp
        )

list(APPEND ${CURRENT_TARGET}_SOURCES
        core_messages.cpp
        message_oriented_connection.cpp
        node.cpp
        peer_connection.cpp
        peer_database.cpp
        stcp_socket.cpp
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

target_link_libraries(graphene_${CURRENT_TARGET} PUBLIC fc graphene_protocol)
target_include_directories(graphene_${CURRENT_TARGET}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../protocol/include"
        #PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../version/include"
        )

if(MSVC)
    set_source_files_properties(node.cpp PROPERTIES COMPILE_FLAGS "/bigobj")
endif(MSVC)

install(TARGETS
        graphene_${CURRENT_TARGET}

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )
install(FILES ${${CURRENT_TARGET}_HEADERS} DESTINATION "include/graphene/${CURRENT_TARGET}")

