set(CURRENT_TARGET protocol)

list(APPEND ${CURRENT_TARGET}_HEADERS
        include/graphene/protocol/asset.hpp
        include/graphene/protocol/authority.hpp
        include/graphene/protocol/base.hpp
        include/graphene/protocol/block.hpp
        include/graphene/protocol/block_header.hpp
        include/graphene/protocol/config.hpp
        include/graphene/protocol/exceptions.hpp
        include/graphene/protocol/get_config.hpp
        include/graphene/protocol/operation_util.hpp
        include/graphene/protocol/operation_util_impl.hpp
        include/graphene/protocol/operations.hpp
        include/graphene/protocol/proposal_operations.hpp
        include/graphene/protocol/protocol.hpp
        include/graphene/protocol/sign_state.hpp
        include/graphene/protocol/chain_operations.hpp
        include/graphene/protocol/chain_virtual_operations.hpp
        include/graphene/protocol/transaction.hpp
        include/graphene/protocol/types.hpp
        include/graphene/protocol/version.hpp
        )

list(APPEND ${CURRENT_TARGET}_SOURCES
        asset.cpp
        authority.cpp
        block.cpp
        get_config.cpp
        operation_util_impl.cpp
        operations.cpp
        proposal_operations.cpp
        sign_state.cpp
        chain_operations.cpp
        transaction.cpp
        types.cpp
        version.cpp
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

target_link_libraries(graphene_${CURRENT_TARGET} fc)
target_include_directories(graphene_${CURRENT_TARGET}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include")

install(TARGETS
        graphene_${CURRENT_TARGET}

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )

install(FILES ${${CURRENT_TARGET}_HEADERS} DESTINATION "include/graphene/${CURRENT_TARGET}")
