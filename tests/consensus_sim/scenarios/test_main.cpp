#define BOOST_TEST_MODULE consensus_sim
#include <boost/test/unit_test.hpp>

// Only the harness and scenarios are built with ASan; chain and fc are not. libc++ annotates
// std::vector for container-overflow, so a vector instantiation shared by both halves (the
// operation vector inside block_log::read_block, for one) reports a write into its own fresh
// buffer during growth. A false positive, not a heap bug -- ASan documents it for partially
// instrumented programs; libstdc++ does not annotate, so Linux CI never sees it. ASAN_OPTIONS
// still overrides this.
extern "C" const char *__asan_default_options() { return "detect_container_overflow=0"; }

// Sanity test - must always pass
BOOST_AUTO_TEST_CASE(harness_compiles_and_links) {
    BOOST_CHECK_EQUAL(1 + 1, 2);
}
