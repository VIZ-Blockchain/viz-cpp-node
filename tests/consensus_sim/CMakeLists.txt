# Consensus simulation harness - opt-in test target

set(HARNESS_SOURCES
    harness/virtual_clock.cpp
    harness/genesis_factory.cpp
    harness/simulated_node.cpp
    harness/message_bus.cpp
    harness/fault_injector.cpp
    harness/scenario_driver.cpp
    harness/invariants.cpp
    harness/failure_log.cpp
)

add_library(consensus_sim_harness STATIC ${HARNESS_SOURCES})

target_include_directories(consensus_sim_harness
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/harness
)

target_link_libraries(consensus_sim_harness
    PUBLIC graphene_chain graphene_protocol graphene_utilities fc
)

# Harness uses std::optional and structured bindings; chain code itself
# stays C++14, so this is scoped to the harness + scenarios targets.
set_target_properties(consensus_sim_harness PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Sanitizers on by default for the harness
target_compile_options(consensus_sim_harness PUBLIC
    -fsanitize=address
    -fsanitize=undefined
    -fno-omit-frame-pointer
    -O1
    -g
)
target_link_options(consensus_sim_harness PUBLIC
    -fsanitize=address
    -fsanitize=undefined
)

if(WITH_COVERAGE)
    target_compile_options(consensus_sim_harness PUBLIC --coverage)
    target_link_options(consensus_sim_harness PUBLIC --coverage)
endif()

set(SCENARIO_SOURCES
    scenarios/test_main.cpp
    scenarios/test_virtual_clock.cpp
    scenarios/test_message_bus.cpp
    scenarios/test_genesis_factory.cpp
    scenarios/test_simulated_node_smoke.cpp
    scenarios/test_smoke_no_faults.cpp
    scenarios/test_determinism_replay.cpp
    scenarios/test_equivocation.cpp
)

add_executable(consensus_sim_tests ${SCENARIO_SOURCES})

target_link_libraries(consensus_sim_tests
    PRIVATE consensus_sim_harness
            Boost::unit_test_framework
)

set_target_properties(consensus_sim_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# We link against the static libboost_unit_test_framework.a in the viz-dev
# image, so do NOT define BOOST_TEST_DYN_LINK — that would expand to the
# old-signature unit_test_main() which is absent from the static archive.

add_test(NAME consensus_sim COMMAND consensus_sim_tests)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/failures)

if(WITH_COVERAGE)
    find_program(GCOVR_PATH gcovr)
    if(GCOVR_PATH)
        add_custom_target(consensus_sim_coverage
            COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
            COMMAND ${GCOVR_PATH}
                --root ${CMAKE_SOURCE_DIR}
                --filter ${CMAKE_SOURCE_DIR}/libraries/chain
                --filter ${CMAKE_SOURCE_DIR}/libraries/protocol
                --filter ${CMAKE_SOURCE_DIR}/tests/consensus_sim
                --html-details
                --output ${CMAKE_BINARY_DIR}/coverage/index.html
                ${CMAKE_BINARY_DIR}
            DEPENDS consensus_sim_tests
            COMMENT "Generating consensus_sim coverage report"
        )
    else()
        message(WARNING "gcovr not found; coverage report target unavailable")
    endif()
endif()
