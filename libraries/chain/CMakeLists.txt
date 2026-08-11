set(GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/graphene/chain")
set(hardfork_hpp_out "${GENERATED_INCLUDE_DIR}/hardfork.hpp")
file(MAKE_DIRECTORY "${GENERATED_INCLUDE_DIR}")

if(MSVC OR MINGW)
set(hardfork_d_dir "${CMAKE_CURRENT_SOURCE_DIR}/hardfork.d")
    add_custom_command(
      OUTPUT "${hardfork_hpp_out}"
      COMMAND ${CMAKE_COMMAND} -E echo "Generating ${hardfork_hpp_out} via cat-parts"
      COMMAND $<TARGET_FILE:cat-parts> "${hardfork_d_dir}" "${hardfork_hpp_out}"
      DEPENDS "${hardfork_d_dir}" cat-parts
      COMMENT "Generating ${hardfork_hpp_out} via cat-parts"
      VERBATIM
    )
else()
    find_package(Python COMPONENTS Interpreter REQUIRED)
    add_custom_command(
      OUTPUT "${hardfork_hpp_out}"
      COMMAND ${Python_EXECUTABLE} "${CMAKE_SOURCE_DIR}/programs/build_helpers/cat_parts.py"
              "${CMAKE_CURRENT_SOURCE_DIR}/hardfork.d" "${hardfork_hpp_out}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/hardfork.d" "${CMAKE_SOURCE_DIR}/programs/build_helpers/cat_parts.py"
      COMMENT "Generating ${hardfork_hpp_out} via Python"
      VERBATIM
    )
endif()

# Common target that depends on the generated file
add_custom_target(build_hardfork_hpp DEPENDS "${hardfork_hpp_out}")

# Mark the file as generated
set_source_files_properties("${hardfork_hpp_out}" PROPERTIES GENERATED TRUE)

# Include the binary include dir
include_directories("${CMAKE_CURRENT_BINARY_DIR}/include")

# Variable used below in add_library
set(hardfork_hpp_file "${hardfork_hpp_out}")

## SORT .cpp by most likely to change / break compile

add_library(graphene_chain ${VIZ_LIBRARY_TYPE}

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
            pm/lmsr_q96.cpp
            pm/parimutuel.cpp
            pm/leverage.cpp
            pm_evaluator.cpp

            include/graphene/chain/pm/lmsr_q96.hpp
            include/graphene/chain/pm/parimutuel.hpp
            include/graphene/chain/pm/leverage.hpp
            include/graphene/chain/pm_evaluator.hpp
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
            include/graphene/chain/validator_objects.hpp
            include/graphene/chain/committee_objects.hpp
            include/graphene/chain/invite_objects.hpp
            include/graphene/chain/paid_subscription_objects.hpp
            include/graphene/chain/pm_objects.hpp

            ${hardfork_hpp_file}
            )

add_dependencies(graphene_chain graphene_protocol graphene_utilities build_hardfork_hpp)
target_link_libraries(graphene_chain graphene_protocol graphene_utilities fc chainbase appbase ${PATCH_MERGE_LIB})
target_include_directories(graphene_chain PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../../")

if(ENABLE_PCH)
    target_precompile_headers(graphene_chain PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp")
endif()

if(WITH_COVERAGE)
    target_compile_options(graphene_chain PRIVATE --coverage)
    target_link_options(graphene_chain PUBLIC --coverage)
endif()

if(MSVC)
    set_source_files_properties(database.cpp PROPERTIES COMPILE_FLAGS "/bigobj")
    # HF14 LMSR is pure integer math, but forbid contraction/fast-math defensively (consensus determinism).
    set_source_files_properties(pm/lmsr_q96.cpp PROPERTIES COMPILE_FLAGS "/fp:precise")
else()
    set_source_files_properties(pm/lmsr_q96.cpp PROPERTIES COMPILE_FLAGS "-fno-fast-math -ffp-contract=off")
endif(MSVC)

install(TARGETS
        graphene_chain

        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )
install(FILES ${HEADERS} DESTINATION "include/graphene/chain")
