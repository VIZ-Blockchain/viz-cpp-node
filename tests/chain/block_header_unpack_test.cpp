// Premise of the header-only block read used by database_api.get_block_header():
// a stored block can be unpacked as `block_header` and the parser stops right after the
// header, because FC_REFLECT_DERIVED puts the base members first on the wire.
//
// Why this test exists. Reading a whole signed_block to return its header costs ~129 KB
// of heap per unique block and is not returned to the OS (measured 19.11.2026: 245-249 MiB
// per 2000 fresh blocks, identical on thread pools of 8 and 256 and with the PM plugin off).
// A client enumerating blocks turned that into gigabytes of RSS on api.viz.world.
// The optimization is only valid while the wire order holds, and a change of the block
// layout would keep the code compiling while making get_block_header() return garbage for
// every field after the first divergence. Hence this pin.
#define BOOST_TEST_MODULE chain_block_header_unpack
#include <boost/test/unit_test.hpp>

#include <graphene/protocol/block.hpp>

#include <fc/io/raw.hpp>

using namespace graphene::protocol;

namespace {

/// A block whose three empty transactions form a tail the header-only read must NOT consume.
/// The height is irrelevant here (nothing looks at block_num), so `previous` stays zeroed.
signed_block make_block(uint32_t stamp, const std::string& validator) {
    signed_block b;
    b.previous = block_id_type();
    b.timestamp = fc::time_point_sec(1700000000 + stamp);
    b.validator = validator;
    b.transactions.resize(3);   // three empty transactions = a tail the header read must skip
    return b;
}

} // namespace

BOOST_AUTO_TEST_CASE(header_only_unpack_reproduces_every_header_field) {
    const signed_block b = make_block(1, "hub.viz.world");

    const std::vector<char> wire = fc::raw::pack(b);
    fc::datastream<const char*> ds(wire.data(), wire.size());
    block_header header;
    fc::raw::unpack(ds, header);

    BOOST_CHECK(header.previous == b.previous);
    BOOST_CHECK(header.timestamp == b.timestamp);
    BOOST_CHECK_EQUAL(header.validator, b.validator);
    BOOST_CHECK(header.transaction_merkle_root == b.transaction_merkle_root);
    BOOST_CHECK_EQUAL(header.extensions.size(), b.extensions.size());
}

BOOST_AUTO_TEST_CASE(header_only_unpack_stops_before_the_transactions) {
    // The point of the change: the parser must not walk the tail. If this ever stops
    // holding, the "optimization" silently reads the whole block again and the 129 KB
    // per block comes back — with the same green tests as before.
    const signed_block empty = make_block(2, "a");
    signed_block with_tail = make_block(3, "a");
    with_tail.transactions.resize(50);

    const std::vector<char> wire_empty = fc::raw::pack(empty);
    const std::vector<char> wire_tail = fc::raw::pack(with_tail);
    BOOST_REQUIRE(wire_tail.size() > wire_empty.size());

    fc::datastream<const char*> ds_empty(wire_empty.data(), wire_empty.size());
    fc::datastream<const char*> ds_tail(wire_tail.data(), wire_tail.size());
    block_header h_empty, h_tail;
    fc::raw::unpack(ds_empty, h_empty);
    fc::raw::unpack(ds_tail, h_tail);

    // Same header, different tails: the number of bytes consumed must be identical and
    // must be strictly smaller than the whole block.
    BOOST_CHECK_EQUAL(ds_empty.tellp(), ds_tail.tellp());
    BOOST_CHECK(ds_tail.tellp() < (long)wire_tail.size());
}
