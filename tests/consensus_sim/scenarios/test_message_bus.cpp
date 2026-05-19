#include <boost/test/unit_test.hpp>
#include "message_bus.hpp"

#include <memory>

using namespace consensus_sim;

namespace {
inline fc::time_point_sec epoch() {
    return fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");
}
}

BOOST_AUTO_TEST_SUITE(message_bus_suite)

BOOST_AUTO_TEST_CASE(enqueue_and_pump_delivers_in_time_order) {
    message_bus bus;
    auto t0 = epoch();
    auto p1 = std::make_shared<int>(1);
    auto p2 = std::make_shared<int>(2);

    bus.enqueue("a", "b", std::static_pointer_cast<void>(p2), t0 + 10);
    bus.enqueue("a", "b", std::static_pointer_cast<void>(p1), t0 + 5);

    auto deliveries = bus.pump_until(t0 + 7);
    BOOST_REQUIRE_EQUAL(deliveries.size(), 1u);
    BOOST_CHECK_EQUAL(*static_cast<int*>(deliveries[0].payload.get()), 1);

    deliveries = bus.pump_until(t0 + 15);
    BOOST_REQUIRE_EQUAL(deliveries.size(), 1u);
    BOOST_CHECK_EQUAL(*static_cast<int*>(deliveries[0].payload.get()), 2);
}

BOOST_AUTO_TEST_CASE(partition_drops_messages_across_split) {
    message_bus bus;
    auto t0 = epoch();
    bus.partition({"a"}, {"b"});

    auto p = std::make_shared<int>(42);
    bus.enqueue("a", "b", std::static_pointer_cast<void>(p), t0 + 1);

    auto deliveries = bus.pump_until(t0 + 10);
    BOOST_CHECK_EQUAL(deliveries.size(), 0u);
}

BOOST_AUTO_TEST_CASE(heal_restores_delivery) {
    message_bus bus;
    auto t0 = epoch();
    bus.partition({"a"}, {"b"});
    bus.heal();

    auto p = std::make_shared<int>(42);
    bus.enqueue("a", "b", std::static_pointer_cast<void>(p), t0 + 1);

    auto deliveries = bus.pump_until(t0 + 10);
    BOOST_CHECK_EQUAL(deliveries.size(), 1u);
}

BOOST_AUTO_TEST_CASE(drop_next_skips_one_message) {
    message_bus bus;
    auto t0 = epoch();
    bus.drop_next("a", "b");

    auto p1 = std::make_shared<int>(1);
    auto p2 = std::make_shared<int>(2);
    bus.enqueue("a", "b", std::static_pointer_cast<void>(p1), t0 + 1);
    bus.enqueue("a", "b", std::static_pointer_cast<void>(p2), t0 + 2);

    auto deliveries = bus.pump_until(t0 + 10);
    BOOST_REQUIRE_EQUAL(deliveries.size(), 1u);
    BOOST_CHECK_EQUAL(*static_cast<int*>(deliveries[0].payload.get()), 2);
}

BOOST_AUTO_TEST_SUITE_END()
