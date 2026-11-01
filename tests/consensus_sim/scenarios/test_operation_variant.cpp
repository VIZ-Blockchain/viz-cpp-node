// Contract of the `operation` static_variant: every alternative round-trips through the raw
// and JSON serializers under its own tag, tags are what the type list says they are, and an
// out-of-range tag is rejected instead of dispatching into garbage. The variant index is the
// consensus op-id, so this pins the dispatch machinery itself, not any one operation.

#include <boost/test/unit_test.hpp>

#include <graphene/protocol/operations.hpp>
#include <fc/io/raw.hpp>

using namespace graphene::protocol;

BOOST_AUTO_TEST_SUITE(operation_variant)

BOOST_AUTO_TEST_CASE(every_alternative_round_trips_under_its_tag) {
    for (int i = 0; i < operation::count(); ++i) {
        operation op;
        op.set_which(i);
        BOOST_REQUIRE_EQUAL(op.which(), i);

        auto bytes = fc::raw::pack(op);
        BOOST_CHECK_EQUAL(fc::raw::unpack<operation>(bytes).which(), i);

        fc::variant v;
        fc::to_variant(op, v);
        operation from_json;
        fc::from_variant(v, from_json);
        BOOST_CHECK_EQUAL(from_json.which(), i);

        operation copied(op);
        operation moved(std::move(copied));
        BOOST_CHECK_EQUAL(moved.which(), i);
        operation assigned;
        assigned = op;
        BOOST_CHECK_EQUAL(assigned.which(), i);
    }
}

BOOST_AUTO_TEST_CASE(tags_follow_the_type_list) {
    BOOST_CHECK_EQUAL(int(operation::tag<vote_operation>::value), 0);
    BOOST_CHECK_EQUAL(int(operation::tag<transfer_operation>::value), 2);
    BOOST_CHECK_EQUAL(int(operation::tag<validator_equivocation_operation>::value), operation::count() - 1);

    operation op = transfer_operation();
    BOOST_CHECK_EQUAL(op.which(), int(operation::tag<transfer_operation>::value));
    op.get<transfer_operation>().memo = "x";
    BOOST_CHECK_EQUAL(op.get<transfer_operation>().memo, "x");
    BOOST_CHECK_THROW(op.get<vote_operation>(), fc::assert_exception);
}

BOOST_AUTO_TEST_CASE(out_of_range_tag_is_rejected) {
    operation op;
    BOOST_CHECK_THROW(op.set_which(operation::count()), fc::exception);

    auto bytes = fc::raw::pack(fc::unsigned_int(operation::count() + 7));
    BOOST_CHECK_THROW(fc::raw::unpack<operation>(bytes), fc::exception);
}

BOOST_AUTO_TEST_SUITE_END()
