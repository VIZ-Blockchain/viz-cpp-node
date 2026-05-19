#include <boost/test/unit_test.hpp>
#include "virtual_clock.hpp"
#include <fc/time.hpp>

using namespace consensus_sim;

BOOST_AUTO_TEST_SUITE(virtual_clock_suite)

namespace {
constexpr const char* kEpoch = "2026-01-01T00:00:00";
}

BOOST_AUTO_TEST_CASE(starts_at_explicit_epoch) {
    auto t0 = fc::time_point_sec::from_iso_string(kEpoch);
    virtual_clock c(t0);
    BOOST_CHECK(c.now() == t0);
}

BOOST_AUTO_TEST_CASE(advance_to_moves_forward) {
    auto t0 = fc::time_point_sec::from_iso_string(kEpoch);
    virtual_clock c(t0);
    c.advance_to(t0 + 60);
    BOOST_CHECK(c.now() == t0 + fc::seconds(60));
}

BOOST_AUTO_TEST_CASE(advance_to_rejects_backward) {
    auto t0 = fc::time_point_sec::from_iso_string(kEpoch);
    virtual_clock c(t0);
    c.advance_to(t0 + 60);
    BOOST_CHECK_THROW(c.advance_to(t0 + 30), std::logic_error);
}

BOOST_AUTO_TEST_CASE(advance_to_same_time_is_noop) {
    auto t0 = fc::time_point_sec::from_iso_string(kEpoch);
    virtual_clock c(t0);
    c.advance_to(t0 + 60);
    BOOST_CHECK_NO_THROW(c.advance_to(t0 + 60));
    BOOST_CHECK(c.now() == t0 + fc::seconds(60));
}

BOOST_AUTO_TEST_SUITE_END()
