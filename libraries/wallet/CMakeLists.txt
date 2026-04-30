set(CURRENT_TARGET wallet)

# Wallet API docs are generated at runtime from FC reflection via the standin.
# A previous Perl+Doxygen pipeline produced richer docs but was disabled in CI
# (the generator was never installed) and has been removed.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/api_documentation.cpp
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/api_documentation_standin.cpp ${CMAKE_CURRENT_BINARY_DIR}/api_documentation.cpp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/api_documentation_standin.cpp)

list(APPEND ${CURRENT_TARGET}_HEADERS
        include/graphene/wallet/api_documentation.hpp
        include/graphene/wallet/reflect_util.hpp
        include/graphene/wallet/remote_node_api.hpp
        include/graphene/wallet/wallet.hpp
)

list(APPEND ${CURRENT_TARGET}_SOURCES
        api_documentation_standin.cpp
        wallet.cpp
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
# I don't know why graphene_app is required twice in the following line, I just know the linker breaks if it isn't.
target_link_libraries(
        graphene_${CURRENT_TARGET}
        PRIVATE
        graphene_network
        graphene_chain
        graphene::network_broadcast_api
        graphene::database_api
        graphene::account_history
        graphene::operation_history
        graphene::social_network
        graphene::tags
        graphene::private_message
        graphene::follow
        graphene::account_by_key
        graphene::witness_api
        graphene_protocol
        graphene_utilities
        fc
        ${CMAKE_DL_LIBS}
        ${PLATFORM_SPECIFIC_LIBS}
)
target_include_directories( graphene_${CURRENT_TARGET} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    set_source_files_properties(wallet.cpp PROPERTIES COMPILE_FLAGS "/bigobj")
endif(MSVC)

install(TARGETS
        graphene_${CURRENT_TARGET}

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )
install(FILES ${HEADERS} DESTINATION "include/graphene/wallet")
