// Guard for dlt_p2p_node::is_routable_endpoint().
//
// Peer exchange gossips whatever endpoints its sender has connected.  Without
// this filter a node running a local pair advertises 127.0.0.1:2001 to the
// whole network; every receiver adopts it, dials its OWN listener, keeps both
// directions of the self-dial as ACTIVE peers, and re-advertises the same
// endpoint — a self-sustaining loop that inflates peer counts, dilutes the
// wedge watchdog's corroboration set and lets a soft-ban land back on the node
// that issued it.  The predicate is the sole gate on both the share side and
// the adopt side, so pin its boundaries.

#include <boost/test/unit_test.hpp>

#include <graphene/network/dlt_p2p_node.hpp>

using graphene::network::dlt_p2p_node;

namespace {
bool routable(const char* ep) {
    return dlt_p2p_node::is_routable_endpoint(fc::ip::endpoint::from_string(ep));
}
} // namespace

BOOST_AUTO_TEST_SUITE(routable_endpoint)

BOOST_AUTO_TEST_CASE(public_endpoints_are_routable) {
    BOOST_CHECK(routable("175.110.112.214:2001"));
    BOOST_CHECK(routable("8.8.8.8:2001"));
    BOOST_CHECK(routable("172.32.0.1:2001"));   // just outside 172.16/12
    BOOST_CHECK(routable("126.255.255.255:2001"));
    BOOST_CHECK(routable("128.0.0.1:2001"));    // just outside 127/8
}

BOOST_AUTO_TEST_CASE(loopback_is_never_routable) {
    // The endpoint pair from the field report: the outbound self-dial and the
    // accepted side of the same connection.
    BOOST_CHECK(!routable("127.0.0.1:2001"));
    BOOST_CHECK(!routable("127.0.0.1:36702"));
    BOOST_CHECK(!routable("127.255.255.255:2001"));
}

BOOST_AUTO_TEST_CASE(private_and_unspecified_are_never_routable) {
    BOOST_CHECK(!routable("10.0.0.1:2001"));
    BOOST_CHECK(!routable("172.16.0.1:2001"));
    BOOST_CHECK(!routable("172.31.255.255:2001"));
    BOOST_CHECK(!routable("192.168.1.10:2001"));
    BOOST_CHECK(!routable("169.254.1.1:2001"));   // link-local
    BOOST_CHECK(!routable("224.0.0.1:2001"));     // multicast
    BOOST_CHECK(!routable("0.0.0.0:2001"));       // unspecified
}

BOOST_AUTO_TEST_CASE(port_zero_is_never_routable) {
    // A hostname seed that resolved without a port yields port 0 — not dialable.
    BOOST_CHECK(!routable("175.110.112.214:0"));
}

BOOST_AUTO_TEST_SUITE_END()
