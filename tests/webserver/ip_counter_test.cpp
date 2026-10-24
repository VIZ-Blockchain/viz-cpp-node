#define BOOST_TEST_MODULE webserver_ip_counter
#include <boost/test/unit_test.hpp>
#include <graphene/plugins/webserver/ip_counter_cache.hpp>

using graphene::plugins::webserver::detail::ip_counter_cache;

BOOST_AUTO_TEST_CASE(new_clients_enter_a_full_200k_table) {
    ip_counter_cache cache;
    for (unsigned i = 0; i < 200000; ++i) {
        auto &c = cache.touch("2001:db8::" + std::to_string(i));
        c.last_seen = 100; // All requests can arrive in the same second.
        c.minute = 1;
    }
    auto &active = cache.touch("2001:db8::0");
    active.minute = 17; active.hour = 123; active.day = 456;
    cache.touch("new-legitimate-client").minute = 1;
    BOOST_CHECK_EQUAL(cache.size(), 200000u);
    BOOST_CHECK(cache.find("new-legitimate-client") != cache.end());
    BOOST_CHECK(cache.find("2001:db8::1") == cache.end());
    BOOST_REQUIRE(cache.find("2001:db8::0") != cache.end());
    BOOST_CHECK_EQUAL(cache.find("2001:db8::0")->second.minute, 17u);
    BOOST_CHECK_EQUAL(cache.find("2001:db8::0")->second.hour, 123u);
    BOOST_CHECK_EQUAL(cache.find("2001:db8::0")->second.day, 456u);
}

BOOST_AUTO_TEST_CASE(churn_preserves_hot_windows_and_reset_clears_lru) {
    ip_counter_cache cache(3);
    cache.touch("active").minute = 9;
    for (unsigned i = 0; i < 1000; ++i) {
        cache.touch("scanner-" + std::to_string(i));
        BOOST_CHECK_EQUAL(cache.touch("active").minute, 9u);
        BOOST_CHECK_LE(cache.size(), 3u);
    }
    cache.clear();
    BOOST_CHECK_EQUAL(cache.size(), 0u);
    cache.touch("after-reset").minute = 1;
    BOOST_CHECK_EQUAL(cache.size(), 1u);
    BOOST_CHECK_EQUAL(cache.touch("after-reset").minute, 1u);
}
