// issue #204 — database::validate_invariants() is real again.
//
// The flag --validate-database-invariants used to be accepted and never read, and the
// method it should have called had no definition.  These cases pin both halves of the
// restored behaviour on a live chain: a healthy state reconciles (the check does not
// fire falsely), and a ONE-RAW divergence of either pool is caught (the check is not
// decorative).  The anchor parameter is exercised too — passing exactly the observed
// delta must silence the failure, otherwise the parameter is dead and #204's "legacy
// residual" escape hatch would repeat the original defect.
#include <boost/test/unit_test.hpp>

#include "scenario_driver.hpp"

#include <graphene/chain/global_property_object.hpp>

#include <fc/exception/exception.hpp>

using namespace consensus_sim;

BOOST_AUTO_TEST_SUITE(db_invariants_suite)

namespace {

    void set_supply(graphene::chain::database& db, const graphene::protocol::asset& supply) {
        db.modify(db.get_dynamic_global_properties(),
                  [&](graphene::chain::dynamic_global_property_object& p) { p.current_supply = supply; });
    }

    void set_total_vesting_shares(graphene::chain::database& db,
                                  const graphene::protocol::asset& shares) {
        db.modify(db.get_dynamic_global_properties(),
                  [&](graphene::chain::dynamic_global_property_object& p) { p.total_vesting_shares = shares; });
    }

} // namespace

BOOST_AUTO_TEST_CASE(healthy_chain_reconciles_and_divergence_is_detected) {
    scenario_config cfg;
    cfg.seed = 0x204C0DE;
    cfg.num_witnesses = 7;
    cfg.max_slots = 7;
    cfg.start_time = fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");

    scenario_driver d(cfg);
    auto result = d.run();
    BOOST_REQUIRE(!result);
    auto& db = d.nodes()[0]->db();
    BOOST_REQUIRE_GT(db.head_block_num(), 0u);

    const auto supply0 = db.get_dynamic_global_properties().current_supply;
    const auto shares0 = db.get_dynamic_global_properties().total_vesting_shares;

    // (a) Healthy state: block production above must not have left a divergence.
    BOOST_CHECK_NO_THROW(db.validate_invariants());

    // (b) TOKEN: one raw of unbacked supply is a divergence, whatever the anchor says.
    set_supply(db, supply0 + graphene::protocol::asset(1, TOKEN_SYMBOL));
    BOOST_CHECK_THROW(db.validate_invariants(), fc::assert_exception);
    BOOST_CHECK_THROW(db.validate_invariants(2), fc::assert_exception);
    // ...and the anchor is READ, not decorative: it must absorb exactly the drift it names.
    BOOST_CHECK_NO_THROW(db.validate_invariants(-1));
    set_supply(db, supply0);
    BOOST_CHECK_NO_THROW(db.validate_invariants());

    // (c) SHARES: the expected side drifting by one satoshi is caught as well.
    set_total_vesting_shares(db, shares0 + graphene::protocol::asset(1, shares0.symbol));
    BOOST_CHECK_THROW(db.validate_invariants(), fc::assert_exception);
    // The anchor governs TOKEN only — it must not paper over a SHARES failure.
    BOOST_CHECK_THROW(db.validate_invariants(-1), fc::assert_exception);
    set_total_vesting_shares(db, shares0);
    BOOST_CHECK_NO_THROW(db.validate_invariants());

    // (d) The added blocks did not move the chain off its genesis accounting in a way the
    // next call would hide: the same call is still clean after the round-trip.
    BOOST_CHECK_EQUAL(db.get_dynamic_global_properties().current_supply.amount.value,
                      supply0.amount.value);
}

BOOST_AUTO_TEST_SUITE_END()
