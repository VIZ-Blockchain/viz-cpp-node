if(MSVC)
    set(hardfork_hpp_file "${CMAKE_CURRENT_SOURCE_DIR}/include/graphene/chain/hardfork.hpp")
    add_custom_target(build_hardfork_hpp COMMAND cat-parts "${CMAKE_CURRENT_SOURCE_DIR}/hardfork.d" ${hardfork_hpp_file})
    add_dependencies(build_hardfork_hpp cat-parts)
else(MSVC)
    set(hardfork_hpp_file "${CMAKE_CURRENT_BINARY_DIR}/include/graphene/chain/hardfork.hpp")
    add_custom_target(build_hardfork_hpp
            COMMAND "${CMAKE_SOURCE_DIR}/programs/build_helpers/cat_parts.py" "${CMAKE_CURRENT_SOURCE_DIR}/hardfork.d" ${hardfork_hpp_file})
endif(MSVC)


set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/include/graphene/chain/hardfork.hpp" PROPERTIES GENERATED TRUE)

## SORT .cpp by most likely to change / break compile

if(BUILD_SHARED_LIBRARIES)
    add_library(graphene_chain SHARED

            # As database takes the longest to compile, start it first
            database.cpp
            fork_database.cpp

            chain_evaluator.cpp
            invite_evaluator.cpp

            chain_objects.cpp
            shared_authority.cpp
            #        transaction_object.cpp
            block_log.cpp
            dlt_block_log.cpp
            proposal_object.cpp
            proposal_evaluator.cpp
            database_proposal_object.cpp
            chain_properties_evaluators.cpp
            committee_evaluator.cpp
            invite_evaluator.cpp
            paid_subscription_evaluator.cpp

            include/graphene/chain/account_object.hpp
            include/graphene/chain/block_log.hpp
            include/graphene/chain/dlt_block_log.hpp
            include/graphene/chain/block_summary_object.hpp
            include/graphene/chain/content_object.hpp
            include/graphene/chain/proposal_object.hpp
            include/graphene/chain/compound.hpp
            include/graphene/chain/custom_operation_interpreter.hpp
            include/graphene/chain/database.hpp
            include/graphene/chain/database_exceptions.hpp
            include/graphene/chain/db_with.hpp
            include/graphene/chain/evaluator.hpp
            include/graphene/chain/evaluator_registry.hpp
            include/graphene/chain/fork_database.hpp
            include/graphene/chain/generic_custom_operation_interpreter.hpp
            include/graphene/chain/global_property_object.hpp
            include/graphene/chain/immutable_chain_parameters.hpp
            include/graphene/chain/index.hpp
            include/graphene/chain/node_property_object.hpp
            include/graphene/chain/operation_notification.hpp
            include/graphene/chain/shared_authority.hpp
            include/graphene/chain/shared_db_merkle.hpp
            include/graphene/chain/chain_evaluator.hpp
            include/graphene/chain/chain_object_types.hpp
            include/graphene/chain/chain_objects.hpp
            include/graphene/chain/transaction_object.hpp
            include/graphene/chain/witness_objects.hpp
            include/graphene/chain/committee_objects.hpp
            include/graphene/chain/paid_subscription_objects.hpp

            ${hardfork_hpp_file}
            "${CMAKE_CURRENT_BINARY_DIR}/include/graphene/chain/hardfork.hpp"
            )
else()
    add_library(graphene_chain STATIC

            # As database takes the longest to compile, start it first
            database.cpp
            fork_database.cpp

            chain_evaluator.cpp

            chain_objects.cpp
            shared_authority.cpp
            #        transaction_object.cpp
            block_log.cpp
            dlt_block_log.cpp
            proposal_object.cpp
            proposal_evaluator.cpp
            database_proposal_object.cpp
            chain_properties_evaluators.cpp
            committee_evaluator.cpp
            invite_evaluator.cpp
            paid_subscription_evaluator.cpp

            include/graphene/chain/account_object.hpp
            include/graphene/chain/block_log.hpp
            include/graphene/chain/dlt_block_log.hpp
            include/graphene/chain/block_summary_object.hpp
            include/graphene/chain/content_object.hpp
            include/graphene/chain/proposal_object.hpp
            include/graphene/chain/compound.hpp
            include/graphene/chain/custom_operation_interpreter.hpp
            include/graphene/chain/database.hpp
            include/graphene/chain/database_exceptions.hpp
            include/graphene/chain/db_with.hpp
            include/graphene/chain/evaluator.hpp
            include/graphene/chain/evaluator_registry.hpp
            include/graphene/chain/fork_database.hpp
            include/graphene/chain/generic_custom_operation_interpreter.hpp
            include/graphene/chain/global_property_object.hpp
            include/graphene/chain/immutable_chain_parameters.hpp
            include/graphene/chain/index.hpp
            include/graphene/chain/node_property_object.hpp
            include/graphene/chain/operation_notification.hpp
            include/graphene/chain/shared_authority.hpp
            include/graphene/chain/shared_db_merkle.hpp
            include/graphene/chain/chain_evaluator.hpp
            include/graphene/chain/chain_object_types.hpp
            include/graphene/chain/chain_objects.hpp
            include/graphene/chain/transaction_object.hpp
            include/graphene/chain/witness_objects.hpp
            include/graphene/chain/committee_objects.hpp
            include/graphene/chain/invite_objects.hpp
            include/graphene/chain/paid_subscription_objects.hpp

            ${hardfork_hpp_file}
            "${CMAKE_CURRENT_BINARY_DIR}/include/graphene/chain/hardfork.hpp"
            )
endif()

add_dependencies(graphene_chain graphene_protocol graphene_utilities build_hardfork_hpp)
target_link_libraries(graphene_chain graphene_protocol graphene_utilities fc chainbase appbase ${PATCH_MERGE_LIB})
target_include_directories(graphene_chain PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../../")

if(MSVC)
    set_source_files_properties(database.cpp PROPERTIES COMPILE_FLAGS "/bigobj")
endif(MSVC)

install(TARGETS
        graphene_chain

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )
install(FILES ${HEADERS} DESTINATION "include/graphene/chain")
