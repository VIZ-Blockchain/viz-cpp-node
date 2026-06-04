#define BOOST_TEST_MODULE consensus_sim
#include <boost/test/unit_test.hpp>

// Sanity test - must always pass
BOOST_AUTO_TEST_CASE(harness_compiles_and_links) {
    BOOST_CHECK_EQUAL(1 + 1, 2);
}
