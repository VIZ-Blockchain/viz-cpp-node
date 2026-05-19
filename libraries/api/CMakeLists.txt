set(CURRENT_TARGET api)

list(APPEND CURRENT_TARGET_HEADERS
    include/graphene/api/account_api_object.hpp
    include/graphene/api/content_api_object.hpp
    include/graphene/api/chain_api_properties.hpp
    include/graphene/api/validator_api_object.hpp
    include/graphene/api/discussion.hpp
    include/graphene/api/vote_state.hpp
    include/graphene/api/account_vote.hpp
    include/graphene/api/discussion_helper.hpp
    include/graphene/api/committee_api_object.hpp
    include/graphene/api/invite_api_object.hpp
    include/graphene/api/paid_subscription_api_object.hpp
)

list(APPEND CURRENT_TARGET_SOURCES
    account_api_object.cpp
    discussion_helper.cpp
    content_api_object.cpp
    chain_api_properties.cpp
    validator_api_object.cpp
    committee_api_object.cpp
    invite_api_object.cpp
    paid_subscription_api_object.cpp
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
    graphene_utilities
    fc
)

target_include_directories(graphene_${CURRENT_TARGET}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

install(TARGETS
        graphene_${CURRENT_TARGET}

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )