# HF14 LMSR Q96 vector-parity test — consensus merge gate (strict equality vs the
# normative PHP/GMP vectors). Built under BUILD_CONSENSUS_TESTS, same as consensus_sim.

add_executable(pm_lmsr_vectors_tests lmsr_vectors_test.cpp)

target_link_libraries(pm_lmsr_vectors_tests
    PRIVATE graphene_chain
            graphene_protocol
            fc
            Boost::unit_test_framework
)

# Absolute path to the frozen vector file (lives next to this CMakeLists).
target_compile_definitions(pm_lmsr_vectors_tests
    PRIVATE LMSR_VECTORS_JSON=\"${CMAKE_CURRENT_SOURCE_DIR}/lmsr_fixed_vectors.json\"
)

# Static libboost_unit_test_framework — do NOT define BOOST_TEST_DYN_LINK (see consensus_sim note).

add_test(NAME pm_lmsr_vectors COMMAND pm_lmsr_vectors_tests)

# HF14 parimutuel settlement test — zero-sum conservation + payout-by-weight + edge cases.
add_executable(pm_parimutuel_tests parimutuel_test.cpp)

target_link_libraries(pm_parimutuel_tests
    PRIVATE graphene_chain
            graphene_protocol
            fc
            Boost::unit_test_framework
)

add_test(NAME pm_parimutuel COMMAND pm_parimutuel_tests)

# HF14 leverage CPMM math test — frozen vs prototype (cancel_value, threshold, max leverage).
add_executable(pm_leverage_tests leverage_test.cpp)

target_link_libraries(pm_leverage_tests
    PRIVATE graphene_chain
            graphene_protocol
            fc
            Boost::unit_test_framework
)

add_test(NAME pm_leverage COMMAND pm_leverage_tests)

# HF14 prediction-market metadata parsing — pure helpers of the prediction_market_api plugin
# (JSON metadata extraction + CSV jurisdiction membership).
add_executable(pm_meta_parse_tests meta_parse_test.cpp)

target_link_libraries(pm_meta_parse_tests
    PRIVATE fc
            Boost::unit_test_framework
)

target_include_directories(pm_meta_parse_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/prediction_market_api/include"
)

add_test(NAME pm_meta_parse COMMAND pm_meta_parse_tests)
