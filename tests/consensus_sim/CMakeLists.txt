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
    harness/tx_factory.cpp
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

# Sanitizers on by default for the harness (GCC/Clang only — MSVC rejects these flags).
if(NOT MSVC)
    target_compile_options(consensus_sim_harness PUBLIC
        -fsanitize=address
        -fsanitize=undefined
        # fc's static_variant / boost flat_set storage is intentionally under-aligned (benign on
        # x86); the alignment check floods the log with non-fatal noise, so disable just that one.
        -fno-sanitize=alignment
        -fno-omit-frame-pointer
        -O1
        -g
    )
    target_link_options(consensus_sim_harness PUBLIC
        -fsanitize=address
        -fsanitize=undefined
        -fno-sanitize=alignment
    )
endif()

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
    scenarios/test_pm_lifecycle.cpp
    scenarios/test_wedge_predicate.cpp
    scenarios/test_routable_endpoint.cpp
)

add_executable(consensus_sim_tests ${SCENARIO_SOURCES})

target_link_libraries(consensus_sim_tests
    PRIVATE consensus_sim_harness
            graphene::network         # test_wedge_predicate.cpp: dlt_p2p_node::is_wedged
                                      # test_routable_endpoint.cpp: is_routable_endpoint
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

# Settlement cost benchmark (#432) — see bench/settle_bench.cpp. Deliberately NOT linked against
# consensus_sim_harness: that library carries ASAN/UBSAN as PUBLIC options, and sanitized timings
# are useless for choosing pm_settle_rows_per_block. The same harness sources are compiled here at
# the project's normal optimisation level. Not registered with add_test — it measures, it does not
# assert.
add_executable(pm_settle_bench EXCLUDE_FROM_ALL ${HARNESS_SOURCES} bench/settle_bench.cpp)

target_include_directories(pm_settle_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/harness)

target_link_libraries(pm_settle_bench
    PRIVATE graphene_chain graphene_protocol graphene_utilities fc
)

set_target_properties(pm_settle_bench PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
