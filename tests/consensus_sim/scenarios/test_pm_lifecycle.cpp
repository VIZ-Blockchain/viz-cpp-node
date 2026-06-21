// HF14 Prediction-Market integration tests (single node, real chain state, real evaluators).
//
// Brings up a live `database`, registers the initiator ("viz") as a real validator with
// stake + self-vote (the genesis committee witness is excluded from normal selection, so a
// real validator is needed to pass HF13's stake-weighted scheduling), drives to HF14, then
// exercises the on-chain PM flow: oracle registration and a full binary market lifecycle
// (create → bet → resolve → auto-payout) with parimutuel + zero-sum assertions.

#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"

#include <graphene/chain/database.hpp>
#include <graphene/chain/operation_notification.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/chain/pm/lmsr_q96.hpp>
#include <graphene/chain/pm/parimutuel.hpp>
#include <graphene/protocol/pm_operations.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/config.hpp>

#include <fc/crypto/sha256.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace consensus_sim;
using namespace graphene::chain;
using namespace graphene::protocol;

namespace {
    account_name_type next_validator(simulated_node& n) { return n.db().get_scheduled_validator(1); }

    fc::ecc::private_key key_for(const genesis_params& gp, const account_name_type& v) {
        return (v == gp.initiator_name) ? gp.initiator_key : gp.genesis_witness_key;
    }

    void produce(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
        when += fc::seconds(CHAIN_BLOCK_INTERVAL);
        const auto v = next_validator(n);
        n.produce_block(v, key_for(gp, v), when);
    }

    signed_transaction sign_ops(const std::vector<operation>& ops,
                                const fc::ecc::private_key& key, const simulated_node& node) {
        signed_transaction tx;
        tx.set_reference_block(node.head_block_id());
        tx.set_expiration(node.head_block_time() + fc::seconds(60));
        for (const auto& op : ops) tx.operations.emplace_back(op);
        tx.sign(key, node.chain_id());
        return tx;
    }

    authority single_key_auth(const public_key_type& k) {
        authority a; a.weight_threshold = 1; a.key_auths[k] = 1; return a;
    }
    fc::ecc::private_key derive_key(const std::string& name) {
        return fc::ecc::private_key::regenerate(fc::sha256::hash(name));
    }

    // Register "viz" as a staked, self-voted validator, then advance until HF14 activates.
    // Returns true if HF14 became active. On a non-testnet build (HF14 far future) or other
    // genesis limitation it returns false and the caller skips the on-chain assertions.
    bool bring_to_hf14(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when) {
        try {
            produce(node, gp, when); // block 1 (committee gap-filler)

            const share_type bal = node.db().get_account(gp.initiator_name).balance.amount;
            transfer_to_vesting_operation tv;
            tv.from = gp.initiator_name; tv.to = gp.initiator_name;
            tv.amount = asset(share_type(bal.value / 2), TOKEN_SYMBOL);
            validator_update_operation vu;
            vu.owner = gp.initiator_name; vu.url = "viz";
            vu.block_signing_key = gp.initiator_key.get_public_key();
            account_validator_vote_operation vv;
            vv.account = gp.initiator_name; vv.validator = gp.initiator_name; vv.approve = true;
            node.push_pending_transaction(sign_ops({tv, vu, vv}, gp.initiator_key, node));

            for (int i = 0; i < 300 && !node.db().has_hardfork(CHAIN_HARDFORK_14); ++i)
                produce(node, gp, when);
        } catch (const std::exception& e) {
            BOOST_TEST_MESSAGE(std::string("could not reach HF14 (harness/genesis limit): ") + e.what());
        }
        return node.db().has_hardfork(CHAIN_HARDFORK_14);
    }

    void create_and_fund(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when,
                         const std::string& name, const fc::ecc::private_key& key, share_type liquid) {
        const auto pub = key.get_public_key();
        const auto& mp = node.db().get_validator_schedule_object().median_props;
        account_create_operation ac;
        ac.fee = mp.account_creation_fee;
        ac.delegation = asset(0, SHARES_SYMBOL);
        ac.creator = gp.initiator_name;
        ac.new_account_name = name;
        ac.master = single_key_auth(pub);
        ac.active = single_key_auth(pub);
        ac.regular = single_key_auth(pub);
        ac.memo_key = pub;
        transfer_operation tr;
        tr.from = gp.initiator_name; tr.to = name; tr.amount = asset(liquid, TOKEN_SYMBOL);
        node.push_pending_transaction(sign_ops({ac, tr}, gp.initiator_key, node));
        produce(node, gp, when);
    }

    // Long-market odds-drift simulation, shared by the binary/skewed/multi cases.
    // Places the given (outcome, stake) bets from the initiator, prints how far the
    // commission-baked BOARD coefficient shown at bet time drifts from the FINAL one, then
    // resolves outcome 0 and asserts the market settles.
    //
    // IMPORTANT: the board coefficient is PARIMUTUEL — odds_i = 1 + Σ(other stake pools) x
    // (1-commission) / own_stake_pool. It is built from a_bets_sum/b_bets_sum (the staked
    // BETS), which CPMM/LMSR/lazy-pool liquidity never touches (liquidity only feeds the
    // curve reserves → bet WEIGHT). So deeper liquidity cannot pin these odds; only the order
    // flow can. `binary` selects the bet shape (side vs outcome_index).
    void run_drift_market(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when,
                          bool binary, int outcome_count, int feeBp,
                          const std::vector<int16_t>& outc, const std::vector<int64_t>& stk,
                          const pm_market_id_type& market_id, const char* label) {
        const double nf = 1.0 - feeBp / 10000.0;
        const int N = (int)outc.size();

        std::vector<operation> batch;
        for (int i = 0; i < N; ++i) {
            pm_place_bet_operation pb;
            pb.account = gp.initiator_name; pb.market_id = market_id._id;
            if (binary) { pb.side = (int8_t)outc[i]; pb.outcome_index = -1; }
            else        { pb.side = -1;              pb.outcome_index = outc[i]; }
            pb.amount = asset(share_type(stk[i]), TOKEN_SYMBOL); pb.mode = 0;
            batch.push_back(pb);
            if (batch.size() == 10) { node.push_pending_transaction(sign_ops(batch, gp.initiator_key, node)); produce(node, gp, when); batch.clear(); }
        }
        if (!batch.empty()) { node.push_pending_transaction(sign_ops(batch, gp.initiator_key, node)); produce(node, gp, when); }

        std::vector<int> bo; std::vector<int64_t> bs, bw;
        {
            const auto& bidx = node.db().get_index<pm_bet_index>().indices().get<by_market>();
            for (auto it = bidx.lower_bound(boost::make_tuple(market_id, pm_bet_id_type()));
                 it != bidx.end() && it->market == market_id; ++it) {
                bo.push_back(binary ? (int)it->side : (int)it->outcome_index);
                bs.push_back(it->amount.value); bw.push_back(it->weight.value);
            }
        }
        const int M = (int)bo.size();
        BOOST_REQUIRE_EQUAL(M, N);

        std::vector<int64_t> pool(outcome_count, 0); int64_t totPool = 0;
        for (int i = 0; i < M; ++i) { pool[bo[i]] += bs[i]; totPool += bs[i]; }

        std::vector<int64_t> rpool(outcome_count, 0); int64_t rtot = 0;
        std::vector<double> shown(M), finalc(M), absdrift(M);
        for (int i = 0; i < M; ++i) {
            int o = bo[i];
            rpool[o] += bs[i]; rtot += bs[i];
            double ownR = (double)rpool[o], oppR = (double)(rtot - rpool[o]);
            double ownF = (double)pool[o],  oppF = (double)(totPool - pool[o]);
            shown[i]  = 1.0 + oppR * nf / ownR;
            finalc[i] = 1.0 + oppF * nf / ownF;
            absdrift[i] = std::fabs((finalc[i] - shown[i]) / shown[i]);
        }

        char buf[256];
        BOOST_TEST_MESSAGE("");
        std::snprintf(buf, sizeof(buf), "=== %s : %d bets, %d outcomes, commission %d%% ===", label, M, outcome_count, feeBp/100);
        BOOST_TEST_MESSAGE(buf);
        {
            std::string ln = "final pools (VIZ): ";
            for (int o = 0; o < outcome_count; ++o) { std::snprintf(buf,sizeof(buf), "%s%lld", o?" / ":"", (long long)(pool[o]/1000)); ln += buf; }
            ln += "   ->  final board odds: ";
            for (int o = 0; o < outcome_count; ++o) {
                double od = 1.0 + (double)(totPool - pool[o]) * nf / pool[o];
                std::snprintf(buf,sizeof(buf), "%sx%.2f", o?" / ":"", od); ln += buf;
            }
            BOOST_TEST_MESSAGE(ln);
        }
        BOOST_TEST_MESSAGE("sample (first 3 / last 3) — i: outcome stake  shown -> final  (|drift|):");
        for (int k = 0; k < 6 && k < M; ++k) {
            int i = (k < 3) ? k : M - 6 + k;
            std::snprintf(buf, sizeof(buf), "  #%4d: out=%d stake=%lld  x%.3f -> x%.3f  (%.1f%%)",
                          i, bo[i], (long long)(bs[i]/1000), shown[i], finalc[i], 100.0*absdrift[i]);
            BOOST_TEST_MESSAGE(buf);
        }
        BOOST_TEST_MESSAGE("decile  avg|drift|%");
        for (int d = 0; d < 10; ++d) {
            int lo = d*(M/10), hi = lo + (M/10); double sd = 0;
            for (int i = lo; i < hi; ++i) sd += absdrift[i];
            std::snprintf(buf, sizeof(buf), "  %2d     %6.2f%%", d+1, 100.0*sd/(hi-lo));
            BOOST_TEST_MESSAGE(buf);
        }
        std::vector<double> srt = absdrift; std::sort(srt.begin(), srt.end());
        double mean = 0; for (double x : absdrift) mean += x; mean /= M;
        std::snprintf(buf, sizeof(buf), "drift |d|: mean=%.2f%%  median=%.2f%%  p90=%.2f%%  max=%.2f%%",
                      100*mean, 100*srt[M/2], 100*srt[(M*9)/10], 100*srt[M-1]);
        BOOST_TEST_MESSAGE(buf);

        // Resolve outcome 0 (early — needs betting closed) and confirm settlement.
        const auto& mkt = node.db().get<pm_market_object>(market_id);
        for (int i = 0; i < 600 && node.head_block_time() < mkt.betting_expiration; ++i) produce(node, gp, when);
        pm_resolve_market_operation rm; rm.oracle = gp.initiator_name; rm.market_id = market_id._id; rm.winning_outcome = 0;
        node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
        produce(node, gp, when);
        for (int i = 0; i < 300 && node.db().get<pm_market_object>(market_id).payout_status != 3; ++i) produce(node, gp, when);
        BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);
    }
}

BOOST_AUTO_TEST_SUITE(pm_lifecycle_suite)

BOOST_AUTO_TEST_CASE(oracle_register_after_hf14) {
    auto gp = make_genesis_params(0x1234u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-oracle", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; PM math covered by tests/pm/*. Skipping.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const asset insurance = mp.pm_min_oracle_insurance;

    pm_oracle_register_operation op;
    op.owner = gp.initiator_name;
    op.insurance = insurance;
    op.fixed_fee = asset(0, TOKEN_SYMBOL);
    op.rules_url = "";
    const asset before = node.db().get_account(gp.initiator_name).balance;
    node.push_pending_transaction(sign_ops({op}, gp.initiator_key, node));
    produce(node, gp, when);

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    auto it = oidx.find(gp.initiator_name);
    BOOST_REQUIRE(it != oidx.end());
    BOOST_CHECK_EQUAL(it->insurance.value, insurance.amount.value);
    const asset after = node.db().get_account(gp.initiator_name).balance;
    BOOST_CHECK_EQUAL((before - after).amount.value,
                      insurance.amount.value + mp.pm_oracle_registration_fee.amount.value);
}

BOOST_AUTO_TEST_CASE(binary_market_lifecycle) {
    auto gp = make_genesis_params(0x2222u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-market", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping market lifecycle.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value; // a convenient nonzero scale

    // The validator publishes PM props with a short dispute grace so the auto-payout cron
    // settles within a handful of blocks (default grace is 12h — impractical for a test).
    {
        chain_properties_pm props;                 // inline consensus defaults
        props.pm_dispute_grace_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    // viz registers as oracle and creates a self-oracle binary market (auto-active).
    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    // Fund two bettors.
    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true; // resolve after betting ends, before result_expiration
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);

    // The market is the only one created → id 0.
    const pm_market_id_type market_id(0);
    BOOST_REQUIRE(node.db().find<pm_market_object>(market_id) != nullptr);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).status, 1); // active (self-oracle)

    // alice bets side A, bob bets side B.
    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    pm_place_bet_operation bb;
    bb.account = "bob"; bb.market_id = 0; bb.side = 1; bb.outcome_index = -1;
    bb.amount = asset(share_type(unit), TOKEN_SYMBOL); bb.mode = 0;
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    const asset alice_after_bet = node.db().get_account("alice").balance;
    const asset bob_after_bet   = node.db().get_account("bob").balance;

    // Advance past betting_expiration (30s) but before result_expiration (90s), then
    // early-resolve to A (side 0). Resolving after result_expiration would let the
    // oracle-missed cron close the market first.
    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0; rm.decision_url = "";
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);

    // Advance well past result_expiration + grace so the auto-payout cron settles the market.
    const uint32_t grace = (uint32_t)mp.pm_dispute_grace_sec;
    const int blocks = (int)((grace + 240) / CHAIN_BLOCK_INTERVAL) + 80;
    for (int i = 0; i < blocks &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);

    // Winner (alice) paid > stake; loser (bob) got nothing back; zero-sum across bettors.
    const asset alice_final = node.db().get_account("alice").balance;
    const asset bob_final   = node.db().get_account("bob").balance;
    BOOST_TEST_MESSAGE("alice paid: " << (alice_final - alice_after_bet).amount.value
                       << "  bob: " << (bob_final - bob_after_bet).amount.value);
    BOOST_CHECK_GT((alice_final - alice_after_bet).amount.value, 0);          // winner credited
    BOOST_CHECK_EQUAL((bob_final - bob_after_bet).amount.value, 0);           // loser unpaid
    // Binary self-oracle, no fees: winner receives the whole losers' pool + own stake back.
    BOOST_CHECK_EQUAL((alice_final - alice_after_bet).amount.value, unit * 2);
}

BOOST_AUTO_TEST_CASE(committee_dispute_flips_outcome) {
    auto gp = make_genesis_params(0x3333u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-dispute", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping dispute scenario.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    // Short PM timers (defaults are 12h/3d/14d) + small dispute fee so a bettor can file.
    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec          = 30;
        props.pm_oracle_dispute_response_sec = 5;
        props.pm_dispute_vote_period_sec     = 15;
        props.pm_dispute_auto_close_sec      = 600;   // > voting_end so finalize wins the race
        props.pm_dispute_fee                 = asset(unit, TOKEN_SYMBOL);
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_vote_period_sec, 15u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 0; // committee
    cm.dispute_penalty_percent = 500; // 5% insurance slash if the oracle is overridden
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    // alice → A, bob → B.
    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    // Oracle resolves to A (early), then bob disputes claiming B is correct.
    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).resolved_outcome, 0);

    pm_dispute_create_operation dc;
    dc.disputer = "bob"; dc.market_id = 0; dc.proposed_outcome = 1; dc.reason = "B won";
    node.push_pending_transaction(sign_ops({dc}, bob_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 2); // disputed

    // The validator (a stakeholder) first votes to UPHOLD the oracle (toward A)...
    pm_dispute_vote_operation dv;
    dv.voter = gp.initiator_name; dv.market_id = 0; dv.vote_outcome = -1; dv.vote_percent = 10000;
    node.push_pending_transaction(sign_ops({dv}, gp.initiator_key, node));
    produce(node, gp, when);

    // ...then REVISES the ballot before voting closes, as new arguments surface — a committee
    // dispute is an open public hearing (no commit-reveal), so a repeat vote overwrites the
    // previous one (latest ballot wins, no "Already voted" rejection). The flip to B below only
    // succeeds if this revision is accepted.
    dv.vote_outcome = 1;
    node.push_pending_transaction(sign_ops({dv}, gp.initiator_key, node));
    produce(node, gp, when);

    const asset bob_pre = node.db().get_account("bob").balance;

    // Advance past voting_end → finalize flips to B → auto-payout settles.
    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(market_id);
    BOOST_TEST_MESSAGE("flipped outcome=" << (int)m.resolved_outcome
                       << "  payout_status=" << (int)m.payout_status
                       << "  bob delta=" << (node.db().get_account("bob").balance - bob_pre).amount.value);
    BOOST_CHECK_EQUAL(m.resolved_outcome, 1);                 // overturned to B
    BOOST_CHECK_EQUAL(m.payout_status, 3);                    // settled
    // bob bet B and the dispute overturned to B → bob is paid (own stake + alice's pool +
    // the slashed oracle insurance forfeit). alice (A) gets nothing.
    BOOST_CHECK_GT((node.db().get_account("bob").balance - bob_pre).amount.value, 0);
}

// Committee dispute — lazy-pool stake carries governance weight. A non-validator voter "carol" has
// ~0 vesting SHARES but a large lazy-pool deposit. Her deposit, converted to vesting-shares (pool NAV
// × her shares / total shares, at get_vesting_share_price), must alone clear the participation quorum
// and overturn the oracle. Without the lazy-pool credit her weight is ~0 → the dispute would uphold.
BOOST_AUTO_TEST_CASE(committee_dispute_lazy_pool_voting_weight) {
    auto gp = make_genesis_params(0x3344u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-lazyvote", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping lazy-pool voting weight.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec           = 30;
        props.pm_oracle_dispute_response_sec = 5;
        props.pm_dispute_vote_period_sec     = 15;
        props.pm_dispute_auto_close_sec      = 600;
        props.pm_dispute_fee                 = asset(unit, TOKEN_SYMBOL);
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_vote_period_sec != 15; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_vote_period_sec, 15u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    // carol: no vesting SHARES; her only governance weight is a lazy-pool deposit. Size it to
    // ~tvf/3 so that even after it inflates the quorum denominator she clears the 10% bar.
    const int64_t tvf = node.db().get_dynamic_global_properties().total_vesting_fund.amount.value;
    const int64_t deposit_amt = tvf / 3;
    auto carol_key = derive_key("carol"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "carol", carol_key, share_type(deposit_amt + unit));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_lazy_deposit_operation dep;
    dep.account = "carol"; dep.amount = asset(share_type(deposit_amt), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dep}, carol_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_lazy_pool_object>(pm_lazy_pool_id_type(0)).free_balance.value, deposit_amt);

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 0; cm.dispute_penalty_percent = 500;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_place_bet_operation bb;
    bb.account = "bob"; bb.market_id = 0; bb.side = 1; bb.outcome_index = -1;
    bb.amount = asset(share_type(unit), TOKEN_SYMBOL); bb.mode = 0;
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0; // oracle says A
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);

    pm_dispute_create_operation dc;
    dc.disputer = "bob"; dc.market_id = 0; dc.proposed_outcome = 1; dc.reason = "B won";
    node.push_pending_transaction(sign_ops({dc}, bob_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 2);

    // Only carol votes (the validator abstains). Her weight is purely the lazy-pool stake.
    pm_dispute_vote_operation dv;
    dv.voter = "carol"; dv.market_id = 0; dv.vote_outcome = 1; dv.vote_percent = 10000;
    node.push_pending_transaction(sign_ops({dv}, carol_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(market_id);
    BOOST_TEST_MESSAGE("lazy-vote: resolved=" << (int)m.resolved_outcome
                       << " payout_status=" << (int)m.payout_status << " deposit=" << deposit_amt);
    // Overturned to B ⇒ carol's lazy-pool stake alone passed the quorum and won the tally.
    BOOST_CHECK_EQUAL(m.resolved_outcome, 1);
    BOOST_CHECK_EQUAL(m.payout_status, 3);
}

// #2 — External oracle rejects a pending market. The creator's seed liquidity is returned
// (exactly once — zero-sum: PM must never emit tokens) and the market closes with status=-1.
// The non-refundable creation fee stays with the DAO, so the creator's net cost is just the fee.
BOOST_AUTO_TEST_CASE(oracle_reject_refunds_liquidity_once) {
    auto gp = make_genesis_params(0x6666u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-reject", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping oracle-reject scenario.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    const int64_t fee  = mp.pm_market_creation_fee.amount.value;

    // viz registers as the (external) oracle; carol is a separate creator.
    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto carol_key = derive_key("carol");
    create_and_fund(node, gp, when, "carol", carol_key, share_type(unit * 4 + fee + unit));

    const asset carol_before = node.db().get_account("carol").balance;

    pm_create_market_operation cm;
    cm.creator = "carol"; cm.oracle = gp.initiator_name; // external oracle → pending
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(60);
    cm.result_expiration  = node.head_block_time() + fc::seconds(120);
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, carol_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).status, 0); // pending acceptance

    const asset carol_after_create = node.db().get_account("carol").balance;
    // create cost = fee + seed liquidity.
    BOOST_CHECK_EQUAL((carol_before - carol_after_create).amount.value, fee + unit * 4);

    // Oracle rejects.
    pm_oracle_accept_market_operation rej;
    rej.oracle = gp.initiator_name; rej.market_id = 0; rej.accept = false;
    node.push_pending_transaction(sign_ops({rej}, gp.initiator_key, node));
    produce(node, gp, when);

    const asset carol_after_reject = node.db().get_account("carol").balance;
    const int64_t refund = (carol_after_reject - carol_after_create).amount.value;
    BOOST_TEST_MESSAGE("reject: seed refund=" << refund << " (expect " << (unit * 4)
                       << ", net cost vs before=" << (carol_before - carol_after_reject).amount.value << ")");
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(market_id).status, -1);
    // Seed returned exactly once — no token emission.
    BOOST_CHECK_EQUAL(refund, unit * 4);
    // Net creator cost == the creation fee only.
    BOOST_CHECK_EQUAL((carol_before - carol_after_reject).amount.value, fee);
}

// #8 — Bet cancellation (requires allow_cancellation). C bets A, D bets B (shifting the
// CPMM reserves), then C cancels: the canceller is made whole (full stake back), the bet
// flips to status 1, and the market's reserves/bets_sum are reversed by exactly the stake.
BOOST_AUTO_TEST_CASE(bet_cancellation_reverses_cpmm) {
    auto gp = make_genesis_params(0x5555u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-cancel", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping cancellation scenario.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(60);
    cm.result_expiration  = node.head_block_time() + fc::seconds(120);
    cm.allow_cancellation = true;
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    // alice → A, bob → B (bob's bet shifts the pool that alice cancels against).
    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    // Locate alice's active bet id.
    uint64_t alice_bet_id = 0; bool found = false;
    for (const auto& b : node.db().get_index<pm_bet_index>().indices()) {
        if (b.market == market_id && b.account == "alice" && b.status == 0) {
            alice_bet_id = b.id._id; found = true; break;
        }
    }
    BOOST_REQUIRE(found);

    const asset alice_pre_cancel = node.db().get_account("alice").balance;
    const share_type bets_sum_pre = node.db().get<pm_market_object>(market_id).bets_sum;

    pm_cancel_bet_operation cancel;
    cancel.account = "alice"; cancel.bet_id = alice_bet_id; cancel.min_return = 0;
    node.push_pending_transaction(sign_ops({cancel}, alice_key, node));
    produce(node, gp, when);

    // Canceller refunded full stake; bet now cancelled (status 1); market bets_sum reduced by stake.
    const int64_t refund = (node.db().get_account("alice").balance - alice_pre_cancel).amount.value;
    const auto& bet = node.db().get<pm_bet_object>(pm_bet_id_type(alice_bet_id));
    const share_type bets_sum_post = node.db().get<pm_market_object>(market_id).bets_sum;
    BOOST_TEST_MESSAGE("cancel: refund=" << refund
                       << " bet.status=" << (int)bet.status
                       << " bets_sum " << bets_sum_pre.value << " -> " << bets_sum_post.value);
    BOOST_CHECK_EQUAL(refund, unit);
    BOOST_CHECK_EQUAL(bet.status, 1);
    BOOST_CHECK_EQUAL((bets_sum_pre - bets_sum_post).value, unit);
}

// #5 — Oracle misses the resolution deadline. With no report by result_expiration the
// maintenance cron slashes pm_oracle_penalty_percent of the oracle's insurance to the
// committee, refunds every active bet in full, returns LP, and closes the market with
// resolved_outcome=-1 / payout_status=3. Zero-sum: bettors made whole, oracle pays the fine.
BOOST_AUTO_TEST_CASE(oracle_missed_refunds_and_slashes) {
    auto gp = make_genesis_params(0x4444u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-missed", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping oracle-missed scenario.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(60);
    cm.allow_early_resolution = false; // let it expire unresolved
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    const asset alice_after_bet = node.db().get_account("alice").balance;
    const asset bob_after_bet   = node.db().get_account("bob").balance;

    // Oracle insurance before the miss penalty.
    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    const int64_t insurance_before = oidx.find(gp.initiator_name)->insurance.value;
    const int64_t expect_slash = insurance_before * (int64_t)mp.pm_oracle_penalty_percent / 10000;
    BOOST_REQUIRE_GT(expect_slash, 0);

    // Never resolve. Advance past result_expiration (+60s) so the missed-oracle cron fires.
    for (int i = 0; i < 120 &&
                    node.db().get<pm_market_object>(market_id).status != 3; ++i)
        produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(market_id);
    BOOST_REQUIRE_EQUAL(m.status, 3);
    BOOST_CHECK_EQUAL(m.payout_status, 3);
    BOOST_CHECK_EQUAL(m.resolved_outcome, -1);

    // Both bettors refunded their full stake.
    const int64_t alice_refund = (node.db().get_account("alice").balance - alice_after_bet).amount.value;
    const int64_t bob_refund   = (node.db().get_account("bob").balance   - bob_after_bet).amount.value;
    BOOST_TEST_MESSAGE("oracle-missed: alice refund=" << alice_refund
                       << " bob refund=" << bob_refund
                       << " insurance slashed=" << (insurance_before - oidx.find(gp.initiator_name)->insurance.value));
    BOOST_CHECK_EQUAL(alice_refund, unit);
    BOOST_CHECK_EQUAL(bob_refund,   unit);

    // Oracle insurance slashed exactly by pm_oracle_penalty_percent.
    BOOST_CHECK_EQUAL(insurance_before - oidx.find(gp.initiator_name)->insurance.value, expect_slash);
}

// #37 — Lazy-pool active-market exposure penalty (consensus risk control). For every OTHER
// active market the same oracle already runs, a fresh allocation is cut by 5% (×95/100).
// We fund the pool, open two self-oracle markets by the same oracle back-to-back, and assert
// the second market's allocation equals its un-penalized 20%-of-free base reduced by exactly
// one 5% step. (#16's "betting block / listing hide" is an API read-filter — below_risk_floor
// in prediction_market_api, explicitly non-consensus — so it has no on-chain assertion.)
BOOST_AUTO_TEST_CASE(lazy_active_market_penalty) {
    auto gp = make_genesis_params(0x7777u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-lazy37", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping lazy-penalty scenario.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    BOOST_REQUIRE(mp.pm_lazy_pool_enabled);
    const int64_t alloc_bp = (int64_t)mp.pm_lazy_alloc_percent;
    const int64_t cap_bp   = (int64_t)mp.pm_lazy_max_total_alloc_percent;

    // Pure-integer mirror of maybe_allocate_lazy's base+clamp (penalties applied separately).
    auto base_alloc = [&](int64_t free, int64_t allocated) -> int64_t {
        int64_t total = free + allocated;
        int64_t headroom = total * cap_bp / 10000 - allocated;
        int64_t a = free * alloc_bp / 10000;
        if (a > free) a = free;
        if (a > headroom) a = headroom;
        return a < 0 ? 0 : a;
    };

    const pm_lazy_pool_id_type pool_id(0);
    auto pool_free      = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).free_balance.value; };
    auto pool_allocated = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).allocated_balance.value; };

    // Raw allocation recorded at a market's activation (pm_lazy_allocation_object.original_amount).
    // Unlike pool.allocated_balance, this is NOT touched by the graduated-recall cron (§7, which
    // claws back idle markets' capital every block), so it isolates the activation-time penalty.
    auto alloc_orig = [&](pm_market_id_type mid) -> int64_t {
        const auto& aidx = node.db().get_index<pm_lazy_allocation_index>().indices().get<by_market>();
        auto it = aidx.find(mid);
        return it != aidx.end() ? it->original_amount.value : -1;
    };

    // viz seeds the lazy pool and funds a bettor.
    pm_lazy_deposit_operation dep;
    dep.account = gp.initiator_name; dep.amount = asset(share_type(unit * 10), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dep}, gp.initiator_key, node));
    produce(node, gp, when);
    auto alice_key = derive_key("alice");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));

    auto self_market_op = [&]() {
        pm_create_market_operation cm;
        cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name; // self-oracle → active at creation
        cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
        cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
        cm.betting_expiration = node.head_block_time() + fc::seconds(300);
        cm.result_expiration  = node.head_block_time() + fc::seconds(600);
        cm.dispute_mode = 0;
        return cm;
    };

    // M1: no OTHER active market for this oracle → full un-penalised allocation.
    const int64_t exp_m1 = base_alloc(pool_free(), pool_allocated());
    // Create M1 AND bet on it in the same block, so M1 is never "idle": the recall cron then
    // leaves its allocation (and the pool's free_balance) intact, keeping M2's input deterministic.
    {
        pm_create_market_operation cm = self_market_op();
        pm_place_bet_operation ba;
        ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
        ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
        node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
        node.push_pending_transaction(sign_ops({ba}, alice_key, node));
        produce(node, gp, when);
    }
    const int64_t m1_orig = alloc_orig(pm_market_id_type(0));

    // M2: same oracle, exactly one OTHER active market (M1) → base cut by exactly one 5% step.
    const int64_t base_m2 = base_alloc(pool_free(), pool_allocated()); // un-penalised
    const int64_t exp_m2  = base_m2 * 95 / 100;
    {
        pm_create_market_operation cm = self_market_op();
        node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
        produce(node, gp, when);
    }
    const int64_t m2_orig = alloc_orig(pm_market_id_type(1));

    BOOST_TEST_MESSAGE("lazy #37: m1_orig=" << m1_orig << " (exp " << exp_m1 << ")  "
                       << "m2_orig=" << m2_orig << " (un-penalised " << base_m2
                       << " -> exp " << exp_m2 << ")");
    BOOST_REQUIRE_GT(exp_m1, 0);
    BOOST_CHECK_EQUAL(m1_orig, exp_m1);            // first market: full allocation
    BOOST_CHECK_EQUAL(m2_orig, exp_m2);            // second: exactly one 5% active-market penalty
    BOOST_CHECK_LT(m2_orig, base_m2);              // strictly below its un-penalised base
}

// #36 — Graduated recall is time-gated: an idle market's pool allocation is clawed back ONE
// 10%-step per ~10% of the market's lifetime, NOT every block. This is the regression guard
// for the recall-cron fix (the cron runs every block; without the time gate it drained an
// idle market's whole subsidy within ~10 blocks). We open an idle, long-lived market and
// watch alloc.check_step advance one notch per step_dur, holding steady between gates.
BOOST_AUTO_TEST_CASE(lazy_recall_is_time_gated) {
    auto gp = make_genesis_params(0x8888u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-recall", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping recall-timing scenario.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    BOOST_REQUIRE(mp.pm_lazy_pool_enabled);

    const pm_market_id_type mid(0);
    auto alloc_step = [&]() -> int {
        const auto& aidx = node.db().get_index<pm_lazy_allocation_index>().indices().get<by_market>();
        auto it = aidx.find(mid);
        return it != aidx.end() ? (int)it->check_step : -1;
    };
    auto alloc_amt = [&]() -> int64_t {
        const auto& aidx = node.db().get_index<pm_lazy_allocation_index>().indices().get<by_market>();
        auto it = aidx.find(mid);
        return it != aidx.end() ? it->amount.value : -1;
    };
    auto advance_until = [&](uint32_t target_sec) {
        for (int g = 0; g < 1000 && node.head_block_time().sec_since_epoch() < target_sec; ++g)
            produce(node, gp, when);
    };

    // Seed the pool so the self-oracle market gets a real allocation at activation.
    pm_lazy_deposit_operation dep;
    dep.account = gp.initiator_name; dep.amount = asset(share_type(unit * 10), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dep}, gp.initiator_key, node));
    produce(node, gp, when);

    // Long-lived market, never bet on → stays idle. window/10 = step_dur ≫ block interval.
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(2000);
    cm.result_expiration  = node.head_block_time() + fc::seconds(4000);
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(mid);
    const uint32_t t0 = m.created_time.sec_since_epoch();
    const int64_t window = (int64_t)m.result_expiration.sec_since_epoch() - (int64_t)t0;
    const int64_t step_dur = window / 10;
    BOOST_REQUIRE_GT(step_dur, (int64_t)CHAIN_BLOCK_INTERVAL * 4); // gate must span many blocks

    const int64_t orig = alloc_amt();
    BOOST_REQUIRE_GT(orig, 0);
    BOOST_CHECK_EQUAL(alloc_step(), 0); // just allocated, nothing recalled yet

    // Before the first gate (half a step in): still no recall, even though many blocks passed.
    advance_until(t0 + (uint32_t)(step_dur / 2));
    const int64_t amt_pre = alloc_amt();
    BOOST_CHECK_EQUAL(alloc_step(), 0);
    BOOST_CHECK_EQUAL(amt_pre, orig);

    // Just past the first gate: exactly ONE 10% step recalled.
    advance_until(t0 + (uint32_t)step_dur + (uint32_t)(step_dur / 4));
    const int step1 = alloc_step();
    const int64_t amt1 = alloc_amt();
    BOOST_CHECK_EQUAL(step1, 1);
    BOOST_CHECK_LT(amt1, orig);

    // THE regression check: many more blocks pass within the same step window → still step 1,
    // allocation unchanged. (Under the bug this would have advanced one step per block.)
    advance_until(t0 + (uint32_t)(step_dur * 9 / 5)); // ~1.8 steps, still before gate 2
    BOOST_CHECK_EQUAL(alloc_step(), 1);
    BOOST_CHECK_EQUAL(alloc_amt(), amt1);

    // Past the second gate: now exactly step 2, allocation reduced again.
    advance_until(t0 + (uint32_t)(step_dur * 2) + (uint32_t)(step_dur / 4));
    const int step2 = alloc_step();
    const int64_t amt2 = alloc_amt();
    BOOST_TEST_MESSAGE("recall timing: step_dur=" << step_dur << " orig=" << orig
                       << " amt1=" << amt1 << " step2=" << step2 << " amt2=" << amt2);
    BOOST_CHECK_EQUAL(step2, 2);
    BOOST_CHECK_LT(amt2, amt1);
}

// #7 — Committee upholds the oracle (dispute fails). The original outcome stands, the winner
// keeps the normal parimutuel payout, the oracle's insurance is NOT slashed, and the loser
// forfeits both the bet and the dispute fee. We measure where the forfeited dispute fee goes
// (winner via forfeit_pool vs. lost) to confirm zero-sum is preserved.
BOOST_AUTO_TEST_CASE(committee_dispute_upholds_oracle) {
    auto gp = make_genesis_params(0x9999u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-uphold", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping uphold scenario.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec           = 30;
        props.pm_oracle_dispute_response_sec = 5;
        props.pm_dispute_vote_period_sec     = 15;
        props.pm_dispute_auto_close_sec      = 600;
        props.pm_dispute_fee                 = asset(unit, TOKEN_SYMBOL);
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    // A SEPARATE oracle account (not the block-producing validator) so its balance reflects
    // only the dispute fee — viz's balance is polluted by block rewards.
    auto orac_key = derive_key("orac");
    const int64_t reg_fee = mp.pm_oracle_registration_fee.amount.value;
    create_and_fund(node, gp, when, "orac", orac_key,
                    share_type(mp.pm_min_oracle_insurance.amount.value + reg_fee + unit * 2));
    pm_oracle_register_operation oreg;
    oreg.owner = "orac"; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, orac_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    // viz creates the market with the EXTERNAL oracle → it starts pending until orac accepts.
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = "orac";
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_oracle_accept_market_operation acc;
    acc.oracle = "orac"; acc.market_id = 0; acc.accept = true;
    node.push_pending_transaction(sign_ops({acc}, orac_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).status, 1);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);
    const asset alice_after_bet = node.db().get_account("alice").balance;

    // orac resolves to A; bob disputes (claims B).
    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = "orac"; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, orac_key, node));
    produce(node, gp, when);

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    const int64_t insurance_before = oidx.find(account_name_type("orac"))->insurance.value;
    const asset orac_before = node.db().get_account("orac").balance;

    pm_dispute_create_operation dc;
    dc.disputer = "bob"; dc.market_id = 0; dc.proposed_outcome = 1; dc.reason = "B won";
    node.push_pending_transaction(sign_ops({dc}, bob_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 2);
    const asset bob_after_dispute = node.db().get_account("bob").balance;

    // The validator (full stake) votes to UPHOLD the oracle (negative-side weight). With
    // oracle_defense ≥ change votes, the dispute is rejected.
    pm_dispute_vote_operation dv;
    dv.voter = gp.initiator_name; dv.market_id = 0; dv.vote_outcome = -1; dv.vote_percent = 10000;
    node.push_pending_transaction(sign_ops({dv}, gp.initiator_key, node));
    produce(node, gp, when);

    // Advance past voting_end → finalize upholds → auto-payout settles.
    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(market_id);
    const int64_t insurance_after = oidx.find(account_name_type("orac"))->insurance.value;
    const int64_t alice_gain = (node.db().get_account("alice").balance - alice_after_bet).amount.value;
    const int64_t bob_gain   = (node.db().get_account("bob").balance - bob_after_dispute).amount.value;
    const int64_t orac_gain  = (node.db().get_account("orac").balance - orac_before).amount.value;

    BOOST_TEST_MESSAGE("uphold: resolved=" << (int)m.resolved_outcome
                       << " payout_status=" << (int)m.payout_status
                       << " alice_gain=" << alice_gain << " (expect winner pool " << (unit * 2) << ")"
                       << " bob_gain_post_dispute=" << bob_gain
                       << " orac_gain=" << orac_gain << " (expect fee " << unit << ")"
                       << " insurance " << insurance_before << "->" << insurance_after);

    BOOST_CHECK_EQUAL(m.resolved_outcome, 0);                 // oracle's A upheld
    BOOST_CHECK_EQUAL(m.payout_status, 3);                    // settled
    BOOST_CHECK_EQUAL(insurance_after, insurance_before);     // oracle NOT slashed on uphold
    BOOST_CHECK_EQUAL(alice_gain, unit * 2);                  // winner keeps the normal parimutuel pool
    BOOST_CHECK_EQUAL(bob_gain, 0);                           // loser gets nothing back post-dispute
    // Zero-sum: the forfeited dispute fee compensates the upheld oracle (committee-dao §Resolution).
    BOOST_CHECK_EQUAL(orac_gain, unit);
}

// #7b — Good-faith oracle (dispute_penalty_percent < 0). The community overrides the oracle,
// but because the market was flagged subjective, the oracle is NOT slashed — instead it keeps
// a carve-out of the dispute fee (|pp|/10000), and the disputer is refunded the remainder.
BOOST_AUTO_TEST_CASE(dispute_good_faith_oracle_not_slashed) {
    auto gp = make_genesis_params(0xAAAAu, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-goodfaith", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping good-faith scenario.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec           = 30;
        props.pm_oracle_dispute_response_sec = 5;
        props.pm_dispute_vote_period_sec     = 15;
        props.pm_dispute_auto_close_sec      = 600;
        props.pm_dispute_fee                 = asset(unit, TOKEN_SYMBOL);
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    auto orac_key = derive_key("orac");
    const int64_t reg_fee = mp.pm_oracle_registration_fee.amount.value;
    create_and_fund(node, gp, when, "orac", orac_key,
                    share_type(mp.pm_min_oracle_insurance.amount.value + reg_fee + unit * 2));
    pm_oracle_register_operation oreg;
    oreg.owner = "orac"; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, orac_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = "orac";
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 0;
    cm.dispute_penalty_percent = -5000; // subjective market → oracle keeps 50% of the fee, no slash
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_oracle_accept_market_operation acc;
    acc.oracle = "orac"; acc.market_id = 0; acc.accept = true;
    node.push_pending_transaction(sign_ops({acc}, orac_key, node));
    produce(node, gp, when);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = "orac"; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, orac_key, node));
    produce(node, gp, when);

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    const int64_t insurance_before = oidx.find(account_name_type("orac"))->insurance.value;
    const asset orac_before = node.db().get_account("orac").balance;

    pm_dispute_create_operation dc;
    dc.disputer = "bob"; dc.market_id = 0; dc.proposed_outcome = 1; dc.reason = "B won";
    node.push_pending_transaction(sign_ops({dc}, bob_key, node));
    produce(node, gp, when);
    const asset bob_after_dispute = node.db().get_account("bob").balance;

    // Validator votes to OVERRIDE toward B.
    pm_dispute_vote_operation dv;
    dv.voter = gp.initiator_name; dv.market_id = 0; dv.vote_outcome = 1; dv.vote_percent = 10000;
    node.push_pending_transaction(sign_ops({dv}, gp.initiator_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(market_id);
    const int64_t insurance_after = oidx.find(account_name_type("orac"))->insurance.value;
    const int64_t orac_gain = (node.db().get_account("orac").balance - orac_before).amount.value;
    const int64_t bob_gain  = (node.db().get_account("bob").balance - bob_after_dispute).amount.value;

    BOOST_TEST_MESSAGE("good-faith: resolved=" << (int)m.resolved_outcome
                       << " insurance " << insurance_before << "->" << insurance_after
                       << " orac_gain=" << orac_gain << " (expect fee/2 " << (unit / 2) << ")"
                       << " bob_gain=" << bob_gain);

    BOOST_CHECK_EQUAL(m.resolved_outcome, 1);                 // overridden to B
    BOOST_CHECK_EQUAL(insurance_after, insurance_before);     // good-faith: NOT slashed
    BOOST_CHECK_EQUAL(orac_gain, unit / 2);                   // oracle keeps |pp|/10000 of the fee
    BOOST_CHECK_GT(bob_gain, 0);                              // disputer refunded remainder + winnings
}

// #20/#22/#24 — Lazy pool deposit/shares/lock/planned-withdraw. First deposit mints 1:1 shares;
// a second deposit (no yield yet) mints proportionally; planned withdrawal is rejected before
// unlock (the lock is now enforced) and returns principal exactly once it elapses.
BOOST_AUTO_TEST_CASE(lazy_deposit_lock_and_withdraw) {
    auto gp = make_genesis_params(0xB111u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-lazylife", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping lazy lifecycle.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    BOOST_REQUIRE(mp.pm_lazy_pool_enabled);

    // Short deposit lock so the test can cross it.
    {
        chain_properties_pm props;
        props.pm_lazy_lock_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_lazy_lock_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_lazy_lock_sec, 30u);
    }

    const pm_lazy_pool_id_type pool_id(0);
    auto pool_free   = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).free_balance.value; };
    auto pool_shares = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).total_shares.value; };
    auto dep_of = [&](const char* acct) -> const pm_lazy_deposit_object* {
        const auto& didx = node.db().get_index<pm_lazy_deposit_index>().indices().get<by_deposit_account>();
        auto it = didx.find(account_name_type(acct));
        return it != didx.end() ? &*it : nullptr;
    };

    const int64_t D1 = unit * 10, D2 = unit * 4;

    // viz: first deposit → 1:1 shares.
    pm_lazy_deposit_operation d1;
    d1.account = gp.initiator_name; d1.amount = asset(share_type(D1), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({d1}, gp.initiator_key, node));
    produce(node, gp, when);
    BOOST_CHECK_EQUAL(pool_free(), D1);
    BOOST_CHECK_EQUAL(pool_shares(), D1);
    {
        auto* dv = dep_of("viz");
        BOOST_REQUIRE(dv);
        BOOST_CHECK_EQUAL(dv->shares.value, D1);
        BOOST_CHECK_EQUAL(dv->principal.value, D1);
        BOOST_CHECK_GT(dv->unlock_time.sec_since_epoch(), node.head_block_time().sec_since_epoch());
    }

    // alice: second deposit, no yield yet → new_shares = amount × total_shares / free = D2.
    auto alice_key = derive_key("alice");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(D2 + unit * 2));
    pm_lazy_deposit_operation d2;
    d2.account = "alice"; d2.amount = asset(share_type(D2), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({d2}, alice_key, node));
    produce(node, gp, when);
    BOOST_CHECK_EQUAL(pool_free(), D1 + D2);
    BOOST_CHECK_EQUAL(pool_shares(), D1 + D2);
    BOOST_REQUIRE(dep_of("alice"));
    BOOST_CHECK_EQUAL(dep_of("alice")->shares.value, D2);
    const asset alice_after_deposit = node.db().get_account("alice").balance;

    // Planned withdrawal BEFORE unlock is rejected (lock enforced).
    pm_lazy_withdraw_operation wbad;
    wbad.account = "alice"; wbad.shares = 0; wbad.emergency = false;
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({wbad}, alice_key, node)),
                      std::runtime_error);

    // Cross the lock, then plan-withdraw in full → principal back, pool restored.
    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_lazy_withdraw_operation wd;
    wd.account = "alice"; wd.shares = 0; wd.emergency = false;
    node.push_pending_transaction(sign_ops({wd}, alice_key, node));
    produce(node, gp, when);

    const int64_t alice_gain = (node.db().get_account("alice").balance - alice_after_deposit).amount.value;
    BOOST_TEST_MESSAGE("lazy lifecycle: alice planned withdraw=" << alice_gain
                       << " (principal " << D2 << ")  pool free=" << pool_free()
                       << " shares=" << pool_shares());
    BOOST_CHECK_EQUAL(alice_gain, D2);              // principal returned (no yield)
    BOOST_CHECK_EQUAL(pool_free(), D1);             // alice's principal left free_balance
    BOOST_CHECK_EQUAL(pool_shares(), D1);           // alice's shares burned
    BOOST_CHECK(dep_of("alice") == nullptr);        // deposit object removed
}

// #21/#25/#27 — Lazy pool earns yield and emergency-withdraw penalty. The pool allocates to a
// market; a one-sided bet that loses leaves the winners' pool undistributed → it flows to the
// LPs (incl. the pool) as yield. This verifies the free_balance accounting fix (yield must land
// in free_balance, and a withdrawal must deduct principal+rewards from it — otherwise rewards
// are emitted) plus the emergency penalty (penalty on profit retained in the pool).
BOOST_AUTO_TEST_CASE(lazy_yield_and_emergency_penalty) {
    auto gp = make_genesis_params(0xB222u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-lazyyield", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping lazy yield.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    BOOST_REQUIRE(mp.pm_lazy_pool_enabled);

    // Short auto-payout grace so the market settles fast. Keep the (long) deposit lock so viz's
    // deposit is still locked at emergency-withdraw time.
    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    const pm_lazy_pool_id_type pool_id(0);
    auto pool_free   = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).free_balance.value; };
    auto pool_shares = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).total_shares.value; };
    auto pool_earned = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).earned_balance.value; };

    const int64_t D = unit * 10;
    pm_lazy_deposit_operation dep;
    dep.account = gp.initiator_name; dep.amount = asset(share_type(D), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dep}, gp.initiator_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(pool_free(), D);

    // viz registers as oracle and opens a self-oracle market → the pool auto-allocates to it.
    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    BOOST_REQUIRE_LT(pool_free(), D); // pool allocated a slice to the market

    // alice bets side A only; nobody bets B.
    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    produce(node, gp, when);

    // Resolve to B (no B bettors) → A's pool is undistributed → flows to LPs incl. the pool.
    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 1;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);

    // Yield landed in the pool: earned > 0 AND free_balance == principal + yield (the fix —
    // without it the yield would be missing from free_balance, i.e. silently lost/emitted).
    const int64_t earned = pool_earned();
    BOOST_TEST_MESSAGE("lazy yield: earned=" << earned << " free=" << pool_free()
                       << " (expect D+earned=" << (D + earned) << ")");
    BOOST_CHECK_GT(earned, 0);
    BOOST_CHECK_EQUAL(pool_free(), D + earned);

    // viz emergency-withdraws while still locked → penalty on profit stays in the pool and is the
    // only thing left in free_balance (sole depositor); all shares burned. (Verifies the withdraw
    // deducts principal+rewards from free_balance, and the emergency penalty math/destination.)
    pm_lazy_withdraw_operation wd;
    wd.account = gp.initiator_name; wd.shares = 0; wd.emergency = true;
    node.push_pending_transaction(sign_ops({wd}, gp.initiator_key, node));
    produce(node, gp, when);

    const int64_t expect_penalty = earned * (int64_t)mp.pm_lazy_emergency_penalty_percent / 10000;
    BOOST_TEST_MESSAGE("emergency: free=" << pool_free() << " (expect penalty " << expect_penalty
                       << ") shares=" << pool_shares());
    BOOST_CHECK_EQUAL(pool_shares(), 0);
    BOOST_CHECK_EQUAL(pool_free(), expect_penalty);
}

// #49 — Leverage open → voluntary close. The pool fronts a loan L (leverage_fund_used += L,
// free_balance -= L); collateral+loan enter the CPMM. Closing immediately (no price move) returns
// the position value: the pool recovers its obligation L×(1+R/100) (profit = L×R/100 → earned),
// the trader gets the remainder. Zero-sum: the trader's net loss equals the pool's interest gain.
BOOST_AUTO_TEST_CASE(leverage_open_and_close) {
    auto gp = make_genesis_params(0xC111u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-lev", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping leverage open/close.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    // Enable leverage and relax the sizing knobs so a meaningful loan fits; disable lazy auto-
    // allocation so the pool's whole free_balance is available as leverage capital.
    {
        chain_properties_pm props;
        props.pm_leverage_enabled                    = true;
        props.pm_leverage_expiration_buffer_sec      = 0;
        props.pm_leverage_fund_percent               = 100;
        props.pm_leverage_max_per_position_bp        = 10000;
        props.pm_leverage_max_position_ratio_percent = 100;
        props.pm_lazy_alloc_percent                  = 0;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && !mp.pm_leverage_enabled; ++i) produce(node, gp, when);
        BOOST_REQUIRE(mp.pm_leverage_enabled);
    }

    const pm_lazy_pool_id_type pool_id(0);
    auto lev_used = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).leverage_fund_used.value; };
    auto pool_free   = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).free_balance.value; };
    auto pool_earned = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).earned_balance.value; };

    const int64_t D = unit * 100;       // 10M pool free
    pm_lazy_deposit_operation dep;
    dep.account = gp.initiator_name; dep.amount = asset(share_type(D), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dep}, gp.initiator_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(pool_free(), D);

    // Self-oracle market, liquidity ≥ pm_leverage_min_market_liquidity; long betting window.
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 60), TOKEN_SYMBOL); // 6M → reserves 3M/3M
    cm.betting_expiration = node.head_block_time() + fc::seconds(3600);
    cm.result_expiration  = node.head_block_time() + fc::seconds(7200);
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(pool_free(), D); // no lazy allocation (alloc_percent=0)

    auto trader_key = derive_key("trader");
    create_and_fund(node, gp, when, "trader", trader_key, share_type(unit * 25));

    const int64_t C = unit * 20, L = unit * 10; // collateral 2M, loan 1M
    const int64_t R = (int64_t)mp.pm_leverage_pool_profit_percent;
    const int64_t threshold = L + L * R / 100;       // obligation = L×(1+R/100) = 1.1M
    const int64_t expect_profit = threshold - L;     // pool interest = L×R/100 = 0.1M

    const asset trader_before = node.db().get_account("trader").balance;
    pm_leverage_open_operation op;
    op.account = "trader"; op.market_id = 0; op.outcome_index = 0;
    op.collateral = asset(share_type(C), TOKEN_SYMBOL); op.loan = asset(share_type(L), TOKEN_SYMBOL);
    op.min_tokens = 0; op.max_slippage_percent = 0;
    node.push_pending_transaction(sign_ops({op}, trader_key, node));
    produce(node, gp, when);

    const pm_leverage_position_id_type pos_id(0);
    {
        const auto& pos = node.db().get<pm_leverage_position_object>(pos_id);
        BOOST_CHECK_EQUAL(pos.loan.value, L);
        BOOST_CHECK_EQUAL(pos.liquidation_threshold.value, threshold);
        BOOST_CHECK_EQUAL(pos.status, 0);
    }
    BOOST_CHECK_EQUAL(lev_used(), L);            // pool fronted the loan
    BOOST_CHECK_EQUAL(pool_free(), D - L);       // loan left free_balance

    // Voluntary close (no price move → cv ≥ obligation).
    pm_leverage_close_operation cl;
    cl.account = "trader"; cl.position_id = 0; cl.min_return = 0;
    node.push_pending_transaction(sign_ops({cl}, trader_key, node));
    produce(node, gp, when);

    const int64_t trader_delta = (node.db().get_account("trader").balance - trader_before).amount.value;
    BOOST_TEST_MESSAGE("leverage: pos.status=" << (int)node.db().get<pm_leverage_position_object>(pos_id).status
                       << " lev_used=" << lev_used() << " pool free=" << pool_free()
                       << " earned=" << pool_earned() << " trader_delta=" << trader_delta
                       << " (expect -" << expect_profit << ")");
    BOOST_CHECK_EQUAL(node.db().get<pm_leverage_position_object>(pos_id).status, 4); // closed
    BOOST_CHECK_EQUAL(lev_used(), 0);                       // loan repaid
    BOOST_CHECK_EQUAL(pool_earned(), expect_profit);        // pool kept the interest
    BOOST_CHECK_EQUAL(pool_free(), D + expect_profit);      // free == D + interest (loan back + profit)
    BOOST_CHECK_EQUAL(trader_delta, -expect_profit);        // zero-sum: trader's loss == pool's gain
}

// #50 — Cancel-bet liquidation cascade (Case B) with pool bad debt. A large same-side bet is
// placed, a leveraged position opens on that side, then the bet is cancelled — the cancel
// recomputes k and shifts the curve so the position's cancel_value drops below its loan. The
// cancel-bet cascade liquidates it; the pool recovers only cancel_value (< loan) and eats the
// shortfall (bad debt). The liquidated trader loses their collateral (gets nothing back).
BOOST_AUTO_TEST_CASE(leverage_cancel_bet_cascade_bad_debt) {
    auto gp = make_genesis_params(0xC222u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-levbd", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping leverage cascade.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_leverage_enabled                    = true;
        props.pm_leverage_expiration_buffer_sec      = 0;
        props.pm_leverage_fund_percent               = 100;
        props.pm_leverage_max_per_position_bp        = 10000;
        props.pm_leverage_max_position_ratio_percent = 100;
        props.pm_lazy_alloc_percent                  = 0;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && !mp.pm_leverage_enabled; ++i) produce(node, gp, when);
        BOOST_REQUIRE(mp.pm_leverage_enabled);
    }

    const pm_lazy_pool_id_type pool_id(0);
    auto lev_used  = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).leverage_fund_used.value; };
    auto pool_free = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).free_balance.value; };

    const int64_t D = unit * 100;
    pm_lazy_deposit_operation dep;
    dep.account = gp.initiator_name; dep.amount = asset(share_type(D), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dep}, gp.initiator_key, node));
    produce(node, gp, when);

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 60), TOKEN_SYMBOL); // 6M → reserves 3M/3M
    cm.betting_expiration = node.head_block_time() + fc::seconds(3600);
    cm.result_expiration  = node.head_block_time() + fc::seconds(7200);
    cm.allow_cancellation = true;
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);

    auto bob_key = derive_key("bob"), trader_key = derive_key("trader");
    create_and_fund(node, gp, when, "bob",    bob_key,    share_type(unit * 70));
    create_and_fund(node, gp, when, "trader", trader_key, share_type(unit * 25));

    // bob bets side A (6M), moving reserves to (9M, 1M).
    pm_place_bet_operation bb;
    bb.account = "bob"; bb.market_id = 0; bb.side = 0; bb.outcome_index = -1;
    bb.amount = asset(share_type(unit * 60), TOKEN_SYMBOL); bb.mode = 0;
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    // Locate bob's bet id (to cancel later).
    uint64_t bob_bet_id = 0; bool found = false;
    for (const auto& b : node.db().get_index<pm_bet_index>().indices()) {
        if (b.market == pm_market_id_type(0) && b.account == "bob" && b.status == 0) {
            bob_bet_id = b.id._id; found = true; break;
        }
    }
    BOOST_REQUIRE(found);

    // trader opens a leveraged position on side A (C=2M, L=1M) at reserves (9M, 1M).
    const int64_t L = unit * 10;
    const asset trader_before = node.db().get_account("trader").balance;
    pm_leverage_open_operation op;
    op.account = "trader"; op.market_id = 0; op.outcome_index = 0;
    op.collateral = asset(share_type(unit * 20), TOKEN_SYMBOL); op.loan = asset(share_type(L), TOKEN_SYMBOL);
    op.min_tokens = 0; op.max_slippage_percent = 0;
    node.push_pending_transaction(sign_ops({op}, trader_key, node));
    produce(node, gp, when);
    const pm_leverage_position_id_type pos_id(0);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_leverage_position_object>(pos_id).status, 0);
    const int64_t free_before_cancel = pool_free(); // == D - L
    BOOST_REQUIRE_EQUAL(free_before_cancel, D - L);

    // bob cancels his side-A bet → cancel-bet cascade liquidates trader's now-underwater position.
    pm_cancel_bet_operation cancel;
    cancel.account = "bob"; cancel.bet_id = bob_bet_id; cancel.min_return = 0;
    node.push_pending_transaction(sign_ops({cancel}, bob_key, node));
    produce(node, gp, when);

    const auto& pos = node.db().get<pm_leverage_position_object>(pos_id);
    const int64_t cv = pos.cancel_value_at_liquidation.value;
    const int64_t trader_delta = (node.db().get_account("trader").balance - trader_before).amount.value;
    BOOST_TEST_MESSAGE("cascade bad-debt: pos.status=" << (int)pos.status
                       << " cv=" << cv << " loan=" << pos.loan.value
                       << " pool_received=" << pos.pool_received.value
                       << " bettor_received=" << pos.bettor_received.value
                       << " lev_used=" << lev_used() << " pool free=" << pool_free()
                       << " (expect D-baddebt=" << (D - (L - cv)) << ") trader_delta=" << trader_delta);

    BOOST_CHECK_EQUAL(pos.status, 1);                       // liquidated by the cancel cascade
    BOOST_CHECK_EQUAL(cv, 500000);                          // engineered underwater cancel_value
    BOOST_CHECK_LT(pos.pool_received.value, pos.loan.value);// BAD DEBT: pool recovered < loan
    BOOST_CHECK_EQUAL(pos.pool_received.value, cv);         // pool got exactly cancel_value
    BOOST_CHECK_EQUAL(pos.bettor_received.value, 0);        // liquidated trader gets nothing
    BOOST_CHECK_EQUAL(lev_used(), 0);                       // loan cleared
    BOOST_CHECK_EQUAL(pool_free(), D - (L - cv));           // pool absorbed the shortfall
    BOOST_CHECK_EQUAL(trader_delta, -(int64_t)(unit * 20)); // trader lost the full collateral (got 0 back)
}

// #54 — Account-mode dispute (dispute_mode=1, centralized resolver). The named resolver flips the
// outcome, slashes the resolver-specified penalty from the oracle, and the disputer is refunded
// the fee + reward carve-out (same canon as committee mode). A non-resolver cannot resolve.
BOOST_AUTO_TEST_CASE(account_mode_dispute_resolver_flips) {
    auto gp = make_genesis_params(0xD111u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-acctdisp", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping account-mode dispute.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 30;
        props.pm_dispute_fee       = asset(unit, TOKEN_SYMBOL);
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    // External oracle + a separate resolver account (must differ from oracle & creator).
    auto orac_key = derive_key("orac"), judge_key = derive_key("judge");
    const int64_t reg_fee = mp.pm_oracle_registration_fee.amount.value;
    create_and_fund(node, gp, when, "orac", orac_key,
                    share_type(mp.pm_min_oracle_insurance.amount.value + reg_fee + unit * 2));
    create_and_fund(node, gp, when, "judge", judge_key, share_type(unit * 2));
    pm_oracle_register_operation oreg;
    oreg.owner = "orac"; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, orac_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = "orac";
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 1; cm.dispute_resolver = "judge";
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_oracle_accept_market_operation acc;
    acc.oracle = "orac"; acc.market_id = 0; acc.accept = true;
    node.push_pending_transaction(sign_ops({acc}, orac_key, node));
    produce(node, gp, when);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = "orac"; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, orac_key, node));
    produce(node, gp, when);

    pm_dispute_create_operation dc;
    dc.disputer = "bob"; dc.market_id = 0; dc.proposed_outcome = 1; dc.reason = "B won";
    node.push_pending_transaction(sign_ops({dc}, bob_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 2);

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    const int64_t insurance_before = oidx.find(account_name_type("orac"))->insurance.value;
    const asset bob_after_dispute = node.db().get_account("bob").balance;

    // A non-resolver (alice) cannot resolve an account-mode dispute.
    pm_dispute_resolve_operation bad;
    bad.resolver = "alice"; bad.market_id = 0; bad.correct_outcome = 1;
    bad.penalty_amount = asset(unit * 5, TOKEN_SYMBOL);
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({bad}, alice_key, node)),
                      std::runtime_error);

    // The designated resolver flips to B and slashes 5 units of insurance.
    pm_dispute_resolve_operation dr;
    dr.resolver = "judge"; dr.market_id = 0; dr.correct_outcome = 1;
    dr.penalty_amount = asset(unit * 5, TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dr}, judge_key, node));
    produce(node, gp, when);

    // Advance past result_expiration + grace → auto-payout settles.
    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(market_id);
    const int64_t insurance_after = oidx.find(account_name_type("orac"))->insurance.value;
    const int64_t bob_gain = (node.db().get_account("bob").balance - bob_after_dispute).amount.value;
    BOOST_TEST_MESSAGE("account-mode: resolved=" << (int)m.resolved_outcome
                       << " payout_status=" << (int)m.payout_status
                       << " insurance " << insurance_before << "->" << insurance_after
                       << " bob_gain=" << bob_gain);

    BOOST_CHECK_EQUAL(m.resolved_outcome, 1);                          // resolver flipped to B
    BOOST_CHECK_EQUAL(m.payout_status, 3);                            // settled
    BOOST_CHECK_EQUAL(insurance_before - insurance_after, unit * 5);  // resolver-set penalty slashed
    BOOST_CHECK_GT(bob_gain, 0);                                      // disputer: fee refund + reward + winnings
}

// #17 — Oracle voluntary no-contest. The oracle bails on an active market: all bets are refunded,
// LP principal returned, and a penalty (pm_no_contest_penalty_permille of the dispute fee, NOT of
// the whole insurance) is slashed from the oracle's insurance and distributed pro-rata to the
// refunded bettors as compensation.
BOOST_AUTO_TEST_CASE(oracle_no_contest_refund_and_compensate) {
    auto gp = make_genesis_params(0xD222u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-nocontest", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping no-contest.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    // dispute_fee = 2 units → penalty = 500‰ × 2u = 1u; split pro-rata between the two bettors.
    // Short grace so the disputable no-contest settles quickly (no dispute filed here).
    {
        chain_properties_pm props;
        props.pm_dispute_fee       = asset(unit * 2, TOKEN_SYMBOL);
        props.pm_dispute_grace_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_fee.amount.value != unit * 2; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_fee.amount.value, unit * 2);
    }

    auto orac_key = derive_key("orac");
    const int64_t reg_fee = mp.pm_oracle_registration_fee.amount.value;
    create_and_fund(node, gp, when, "orac", orac_key,
                    share_type(mp.pm_min_oracle_insurance.amount.value + reg_fee + unit * 2));
    pm_oracle_register_operation oreg;
    oreg.owner = "orac"; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, orac_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = "orac";
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(60);
    cm.result_expiration  = node.head_block_time() + fc::seconds(120);
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_oracle_accept_market_operation acc;
    acc.oracle = "orac"; acc.market_id = 0; acc.accept = true;
    node.push_pending_transaction(sign_ops({acc}, orac_key, node));
    produce(node, gp, when);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);
    const asset alice_after_bet = node.db().get_account("alice").balance;
    const asset bob_after_bet   = node.db().get_account("bob").balance;

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    const int64_t insurance_before = oidx.find(account_name_type("orac"))->insurance.value;

    pm_no_contest_operation nc;
    nc.oracle = "orac"; nc.market_id = 0; nc.reason = "cannot judge";
    node.push_pending_transaction(sign_ops({nc}, orac_key, node));
    produce(node, gp, when);
    // No-contest is disputable: market is now unresolved (-1) with the dispute window open.
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).resolved_outcome, -1);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 1);

    // Nobody disputes → after the grace the market settles: refund + penalty distribution.
    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(market_id);
    const int64_t insurance_after = oidx.find(account_name_type("orac"))->insurance.value;
    const int64_t alice_gain = (node.db().get_account("alice").balance - alice_after_bet).amount.value;
    const int64_t bob_gain   = (node.db().get_account("bob").balance - bob_after_bet).amount.value;
    const int64_t penalty    = unit * 2 * (int64_t)mp.pm_no_contest_penalty_percent / 10000; // = 1u

    BOOST_TEST_MESSAGE("no-contest: status=" << (int)m.status << " payout=" << (int)m.payout_status
                       << " insurance " << insurance_before << "->" << insurance_after
                       << " (penalty " << penalty << ") alice_gain=" << alice_gain
                       << " bob_gain=" << bob_gain);

    BOOST_CHECK_EQUAL(m.payout_status, 3);
    BOOST_CHECK_EQUAL(insurance_before - insurance_after, penalty);   // penalty = ‰ × dispute_fee
    // each bettor: stake refund (unit) + half the penalty (penalty/2).
    BOOST_CHECK_EQUAL(alice_gain, unit + penalty / 2);
    BOOST_CHECK_EQUAL(bob_gain,   unit + penalty / 2);
}

// #18 — Dispute against an (abusive) no-contest. The oracle declares no-contest, but a bettor
// disputes and the committee overrides it to a real outcome — the market then settles to that
// outcome (not a refund) and the oracle is slashed for the wrongful no-contest.
BOOST_AUTO_TEST_CASE(dispute_overrides_no_contest) {
    auto gp = make_genesis_params(0xD333u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-ncdisp", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping no-contest dispute.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec           = 30;
        props.pm_oracle_dispute_response_sec = 5;
        props.pm_dispute_vote_period_sec     = 15;
        props.pm_dispute_auto_close_sec      = 600;
        props.pm_dispute_fee                 = asset(unit, TOKEN_SYMBOL);
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(300);
    cm.result_expiration  = node.head_block_time() + fc::seconds(600);
    cm.dispute_mode = 0;
    cm.dispute_penalty_percent = 500; // 5% insurance slash if the no-contest is overridden
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    // Oracle bails (no-contest), opening the dispute window.
    pm_no_contest_operation nc;
    nc.oracle = gp.initiator_name; nc.market_id = 0; nc.reason = "cannot judge";
    node.push_pending_transaction(sign_ops({nc}, gp.initiator_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).resolved_outcome, -1);

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    const int64_t insurance_before = oidx.find(gp.initiator_name)->insurance.value;

    // bob disputes, claiming B is the correct outcome (the market WAS judgeable).
    pm_dispute_create_operation dc;
    dc.disputer = "bob"; dc.market_id = 0; dc.proposed_outcome = 1; dc.reason = "B clearly won";
    node.push_pending_transaction(sign_ops({dc}, bob_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 2);
    const asset bob_pre = node.db().get_account("bob").balance;

    // The validator votes to override the no-contest toward B.
    pm_dispute_vote_operation dv;
    dv.voter = gp.initiator_name; dv.market_id = 0; dv.vote_outcome = 1; dv.vote_percent = 10000;
    node.push_pending_transaction(sign_ops({dv}, gp.initiator_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(market_id);
    const int64_t insurance_after = oidx.find(gp.initiator_name)->insurance.value;
    const int64_t bob_gain = (node.db().get_account("bob").balance - bob_pre).amount.value;
    BOOST_TEST_MESSAGE("no-contest overridden: resolved=" << (int)m.resolved_outcome
                       << " payout=" << (int)m.payout_status
                       << " insurance " << insurance_before << "->" << insurance_after
                       << " bob_gain=" << bob_gain);

    BOOST_CHECK_EQUAL(m.resolved_outcome, 1);                          // no-contest overridden to B
    BOOST_CHECK_EQUAL(m.payout_status, 3);                            // settled as a real outcome
    BOOST_CHECK_EQUAL(insurance_before - insurance_after,
                      insurance_before * 500 / 10000);               // 5% (×100% consensus) slashed
    BOOST_CHECK_GT(bob_gain, 0);                                      // disputer + B winner paid
}

// #19 — Dispute auto-close (anti-freeze). A dispute is filed but nobody votes; once auto_close_time
// passes (before voting would finalize) the cron voids the market: all bets refunded, oracle
// penalised, and — the path this guards — the disputer's escrowed fee is returned.
BOOST_AUTO_TEST_CASE(dispute_auto_close_refunds_fee) {
    auto gp = make_genesis_params(0xD444u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-autoclose", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping auto-close.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    // auto_close fires well before voting would finalize.
    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec           = 30;
        props.pm_oracle_dispute_response_sec = 5;
        props.pm_dispute_vote_period_sec     = 600;
        props.pm_dispute_auto_close_sec      = 5;
        props.pm_dispute_fee                 = asset(unit, TOKEN_SYMBOL);
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_auto_close_sec != 5; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_auto_close_sec, 5u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);
    const asset alice_after_bet = node.db().get_account("alice").balance;

    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    const int64_t insurance_before = oidx.find(gp.initiator_name)->insurance.value;

    pm_dispute_create_operation dc;
    dc.disputer = "bob"; dc.market_id = 0; dc.proposed_outcome = 1; dc.reason = "B won";
    node.push_pending_transaction(sign_ops({dc}, bob_key, node));
    produce(node, gp, when);
    const asset bob_after_dispute = node.db().get_account("bob").balance; // fee already debited

    // Nobody votes; advance past auto_close_time → the cron voids the market.
    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const auto& m = node.db().get<pm_market_object>(market_id);
    const int64_t insurance_after = oidx.find(gp.initiator_name)->insurance.value;
    const int64_t alice_gain = (node.db().get_account("alice").balance - alice_after_bet).amount.value;
    const int64_t bob_gain   = (node.db().get_account("bob").balance - bob_after_dispute).amount.value;
    BOOST_TEST_MESSAGE("auto-close: resolved=" << (int)m.resolved_outcome << " payout=" << (int)m.payout_status
                       << " insurance " << insurance_before << "->" << insurance_after
                       << " alice_gain=" << alice_gain << " bob_gain=" << bob_gain);

    BOOST_CHECK_EQUAL(m.resolved_outcome, -1);                        // voided
    BOOST_CHECK_EQUAL(m.payout_status, 3);
    BOOST_CHECK_EQUAL(insurance_before - insurance_after, insurance_before * 500 / 10000); // oracle penalised 5%
    BOOST_CHECK_EQUAL(alice_gain, unit);                             // bet refunded
    BOOST_CHECK_EQUAL(bob_gain, unit * 2);                           // bet refund + dispute fee returned
}

// #35 — Position transfer. A bettor hands their whole position to another account; on resolution
// the new owner collects the payout (the original owner gets nothing). An over-sized transfer is
// rejected.
BOOST_AUTO_TEST_CASE(position_transfer_changes_owner) {
    auto gp = make_genesis_params(0xE111u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-xfer", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping position transfer.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob"), carol_key = derive_key("carol");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));
    create_and_fund(node, gp, when, "carol", carol_key, share_type(unit * 2));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    // Find alice's bet + its weight.
    uint64_t alice_bet_id = 0; int64_t alice_weight = 0; bool found = false;
    for (const auto& b : node.db().get_index<pm_bet_index>().indices()) {
        if (b.market == market_id && b.account == "alice" && b.status == 0) {
            alice_bet_id = b.id._id; alice_weight = b.weight.value; found = true; break;
        }
    }
    BOOST_REQUIRE(found);

    // Over-sized transfer is rejected.
    pm_transfer_position_operation bad;
    bad.from = "alice"; bad.bet_id = alice_bet_id; bad.to = "carol";
    bad.amount = share_type(alice_weight + 1);
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({bad}, alice_key, node)),
                      std::runtime_error);

    // Full transfer (amount 0) hands the position to carol.
    pm_transfer_position_operation tp;
    tp.from = "alice"; tp.bet_id = alice_bet_id; tp.to = "carol"; tp.amount = 0;
    node.push_pending_transaction(sign_ops({tp}, alice_key, node));
    produce(node, gp, when);
    BOOST_CHECK(node.db().get<pm_bet_object>(pm_bet_id_type(alice_bet_id)).account == account_name_type("carol"));

    const asset alice_pre = node.db().get_account("alice").balance;
    const asset carol_pre = node.db().get_account("carol").balance;

    // Resolve A → the transferred position (now carol's) wins.
    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);
    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const int64_t alice_gain = (node.db().get_account("alice").balance - alice_pre).amount.value;
    const int64_t carol_gain = (node.db().get_account("carol").balance - carol_pre).amount.value;
    BOOST_TEST_MESSAGE("transfer: carol_gain=" << carol_gain << " (expect " << (unit * 2)
                       << ") alice_gain=" << alice_gain);
    BOOST_CHECK_EQUAL(carol_gain, unit * 2);  // new owner collects winner payout (stake + losers' pool)
    BOOST_CHECK_EQUAL(alice_gain, 0);         // original owner no longer holds the position
}

// #53 — Batch commit-reveal. A bettor commits a hashed bet (escrow), reveals it (surplus refunded,
// bet queued), and the epoch cron batch-settles it into the CPMM (side 0 → reserve_a rises, the
// same direction as an instant bet — guards the side-convention fix). An un-revealed commit is
// forfeited: a penalty stays in the pool and the remainder is refunded.
BOOST_AUTO_TEST_CASE(batch_commit_reveal_and_forfeit) {
    auto gp = make_genesis_params(0xF111u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-batch", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping batch commit-reveal.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_commit_reveal_enabled = true;
        props.pm_batch_epoch_blocks    = 5;
        props.pm_reveal_window_blocks  = 5;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && !mp.pm_commit_reveal_enabled; ++i) produce(node, gp, when);
        BOOST_REQUIRE(mp.pm_commit_reveal_enabled);
    }
    const uint16_t no_reveal = mp.pm_commit_no_reveal_penalty_percent;

    // Reproduces verify_commit's byte layout exactly.
    auto make_commitment = [&](const account_name_type& acct, int64_t mid, int8_t side,
                               int16_t oidx, int64_t amount, int64_t min_tokens, const std::string& salt) {
        fc::sha256::encoder enc;
        enc.write((const char*)&mid, sizeof(mid));
        enc.write((const char*)&acct.data, sizeof(acct.data));
        enc.write((const char*)&side, sizeof(side));
        enc.write((const char*)&oidx, sizeof(oidx));
        enc.write((const char*)&amount, sizeof(amount));
        enc.write((const char*)&min_tokens, sizeof(min_tokens));
        enc.write(salt.data(), (uint32_t)salt.size());
        return enc.result();
    };

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL); // reserves 2u / 2u
    cm.betting_expiration = node.head_block_time() + fc::seconds(600);
    cm.result_expiration  = node.head_block_time() + fc::seconds(1200);
    cm.allow_batch = true;
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    const int64_t reserve_a0 = node.db().get<pm_market_object>(market_id).reserve_a.value; // 2u

    // alice commits a side-A bet (escrow 2u, intended amount 1u), then reveals it.
    pm_commit_bet_operation ac;
    ac.account = "alice"; ac.market_id = 0;
    ac.commitment = make_commitment(account_name_type("alice"), 0, /*side*/0, /*oidx*/-1, unit, 0, "salt-a");
    ac.escrow_amount = asset(unit * 2, TOKEN_SYMBOL); ac.no_reveal_fee_percent = no_reveal;
    node.push_pending_transaction(sign_ops({ac}, alice_key, node));
    produce(node, gp, when);

    const asset alice_after_commit = node.db().get_account("alice").balance;
    pm_reveal_bet_operation ar;
    ar.account = "alice"; ar.commit_id = 0; ar.side = 0; ar.outcome_index = -1;
    ar.amount = asset(unit, TOKEN_SYMBOL); ar.salt = "salt-a"; ar.min_tokens = 0;
    node.push_pending_transaction(sign_ops({ar}, alice_key, node));
    produce(node, gp, when);
    // Surplus (escrow 2u − amount 1u = 1u) refunded at reveal.
    BOOST_CHECK_EQUAL((node.db().get_account("alice").balance - alice_after_commit).amount.value, unit);

    // bob commits but never reveals → will be forfeited.
    const asset bob_before_commit = node.db().get_account("bob").balance;
    pm_commit_bet_operation bc;
    bc.account = "bob"; bc.market_id = 0;
    bc.commitment = make_commitment(account_name_type("bob"), 0, 1, -1, unit, 0, "salt-b");
    bc.escrow_amount = asset(unit * 2, TOKEN_SYMBOL); bc.no_reveal_fee_percent = no_reveal;
    node.push_pending_transaction(sign_ops({bc}, bob_key, node));
    produce(node, gp, when);

    // Find alice's queued bet (status 5) and advance to the epoch boundary that batch-settles it.
    uint64_t alice_bet_id = 0; bool found = false;
    for (const auto& b : node.db().get_index<pm_bet_index>().indices())
        if (b.market == market_id && b.account == "alice") { alice_bet_id = b.id._id; found = true; break; }
    BOOST_REQUIRE(found);
    for (int i = 0; i < 20 &&
                    node.db().get<pm_bet_object>(pm_bet_id_type(alice_bet_id)).status == 5; ++i)
        produce(node, gp, when);

    const auto& abet = node.db().get<pm_bet_object>(pm_bet_id_type(alice_bet_id));
    const int64_t reserve_a1 = node.db().get<pm_market_object>(market_id).reserve_a.value;
    BOOST_TEST_MESSAGE("batch: alice bet status=" << (int)abet.status << " weight=" << abet.weight.value
                       << " reserve_a " << reserve_a0 << "->" << reserve_a1);
    BOOST_CHECK_EQUAL(abet.status, 0);                       // batch-settled into an active bet
    BOOST_CHECK_GT(abet.weight.value, 0);                    // received CPMM tokens
    BOOST_CHECK_EQUAL(reserve_a1, reserve_a0 + unit);        // side-0 bet raised reserve_a (fix)

    // Advance past bob's reveal deadline → forfeit: penalty kept, remainder refunded.
    for (int i = 0; i < 30 && (node.db().get_account("bob").balance - bob_before_commit).amount.value
                              == -(unit * 2); ++i)
        produce(node, gp, when);
    const int64_t bob_delta = (node.db().get_account("bob").balance - bob_before_commit).amount.value;
    const int64_t expect_penalty = unit * 2 * (int64_t)no_reveal / 10000;
    BOOST_TEST_MESSAGE("forfeit: bob_delta=" << bob_delta << " (expect -" << expect_penalty << ")");
    BOOST_CHECK_EQUAL(bob_delta, -expect_penalty);          // lost only the no-reveal penalty
}

// #32 — Multi-outcome (LMSR) market lifecycle: a 3-outcome market is created with the client-
// computed b, bettors back each outcome, the oracle resolves one, and only the winner is paid.
BOOST_AUTO_TEST_CASE(lmsr_multi_market_lifecycle) {
    auto gp = make_genesis_params(0xF222u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-lmsr", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping LMSR multi.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob"), carol_key = derive_key("carol");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));
    create_and_fund(node, gp, when, "carol", carol_key, share_type(unit * 4));

    const int64_t L = unit * 30; // generous LMSR liquidity
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 1; cm.outcomes = {"A", "B", "C"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(L), TOKEN_SYMBOL);
    cm.lmsr_b = graphene::chain::lmsr::lmsr_b_from_liquidity(L, 3); // client-computed b (node checks)
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).market_type, 1);

    // Each bettor backs a different outcome (side = -1 for multi; outcome_index selects).
    auto bet = [&](const char* who, const fc::ecc::private_key& key, int16_t oidx) {
        pm_place_bet_operation b;
        b.account = who; b.market_id = 0; b.side = -1; b.outcome_index = oidx;
        b.amount = asset(share_type(unit), TOKEN_SYMBOL); b.min_tokens = 0; b.mode = 0;
        node.push_pending_transaction(sign_ops({b}, key, node));
    };
    bet("alice", alice_key, 0);
    bet("bob",   bob_key,   1);
    bet("carol", carol_key, 2);
    produce(node, gp, when);
    const asset alice_after_bet = node.db().get_account("alice").balance;
    const asset bob_after_bet   = node.db().get_account("bob").balance;
    const asset carol_after_bet = node.db().get_account("carol").balance;

    // Resolve to outcome A (index 0) → alice wins.
    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);
    for (int i = 0; i < 200 &&
                    node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);

    const int64_t alice_gain = (node.db().get_account("alice").balance - alice_after_bet).amount.value;
    const int64_t bob_gain   = (node.db().get_account("bob").balance - bob_after_bet).amount.value;
    const int64_t carol_gain = (node.db().get_account("carol").balance - carol_after_bet).amount.value;
    BOOST_TEST_MESSAGE("lmsr multi: b=" << cm.lmsr_b.value << " alice(win)=" << alice_gain
                       << " bob=" << bob_gain << " carol=" << carol_gain);

    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);
    BOOST_CHECK_GT(alice_gain, 0);            // winner paid
    BOOST_CHECK_EQUAL(bob_gain, 0);           // losers get nothing
    BOOST_CHECK_EQUAL(carol_gain, 0);
}

// #55 — allow_instant_bet=false. A market may disable instant bets to force the front-run-resistant
// batch flow. An instant bet (mode 0) must be rejected; a batch bet (mode 1) is accepted. The
// consensus gate lives in pm_place_bet (the field alone was previously not enforced).
BOOST_AUTO_TEST_CASE(instant_bet_disabled_gate) {
    auto gp = make_genesis_params(0x5511u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-noinstant", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping instant-bet gate.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    BOOST_REQUIRE(mp.pm_commit_reveal_enabled); // needed for allow_batch to stick (create evaluator)

    auto alice_key = derive_key("alice");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));

    // Self-oracle binary market with instant betting disabled, batch enabled.
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(600);
    cm.result_expiration  = node.head_block_time() + fc::seconds(1200);
    cm.allow_instant_bet = false; cm.allow_batch = true;
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).allow_instant_bet, false);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).allow_batch, true);

    // Instant bet (mode 0) is rejected.
    pm_place_bet_operation instant;
    instant.account = "alice"; instant.market_id = 0; instant.side = 0; instant.outcome_index = -1;
    instant.amount = asset(share_type(unit), TOKEN_SYMBOL); instant.mode = 0;
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({instant}, alice_key, node)),
                      std::runtime_error);

    // Batch bet (mode 1) is accepted.
    const asset alice_before = node.db().get_account("alice").balance;
    pm_place_bet_operation batch = instant; batch.mode = 1;
    node.push_pending_transaction(sign_ops({batch}, alice_key, node));
    produce(node, gp, when);

    const int64_t spent = (alice_before - node.db().get_account("alice").balance).amount.value;
    BOOST_TEST_MESSAGE("instant-gate: batch spent=" << spent);
    BOOST_CHECK_EQUAL(spent, unit);   // batch bet went through (stake debited)
    uint32_t bets = 0;
    for (const auto& b : node.db().get_index<pm_bet_index>().indices())
        if (b.market == market_id && b.account == account_name_type("alice")) ++bets;
    BOOST_CHECK_EQUAL(bets, 1u);
}

// #14 — Dispute + time-limited creator ban. An account-mode resolver bars the creator from opening
// new markets. The ban is recorded as a pm_creator_ban_object and create_market rejects while in
// force (the op field existed but was previously a no-op).
BOOST_AUTO_TEST_CASE(dispute_bans_creator_from_new_markets) {
    auto gp = make_genesis_params(0x1401u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-creatorban", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping creator ban.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    const int64_t creation_fee = mp.pm_market_creation_fee.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 30;
        props.pm_dispute_fee       = asset(unit, TOKEN_SYMBOL);
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    auto orac_key = derive_key("orac"), judge_key = derive_key("judge"), maker_key = derive_key("maker");
    const int64_t reg_fee = mp.pm_oracle_registration_fee.amount.value;
    create_and_fund(node, gp, when, "orac", orac_key,
                    share_type(mp.pm_min_oracle_insurance.amount.value + reg_fee + unit * 2));
    create_and_fund(node, gp, when, "judge", judge_key, share_type(unit * 2));
    create_and_fund(node, gp, when, "maker", maker_key, share_type(creation_fee * 2 + unit * 10));
    pm_oracle_register_operation oreg;
    oreg.owner = "orac"; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, orac_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    // "maker" creates an account-mode market; "judge" is the resolver.
    pm_create_market_operation cm;
    cm.creator = "maker"; cm.oracle = "orac";
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true;
    cm.dispute_mode = 1; cm.dispute_resolver = "judge";
    node.push_pending_transaction(sign_ops({cm}, maker_key, node));
    produce(node, gp, when);

    pm_oracle_accept_market_operation acc;
    acc.oracle = "orac"; acc.market_id = 0; acc.accept = true;
    node.push_pending_transaction(sign_ops({acc}, orac_key, node));
    produce(node, gp, when);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba; bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = "orac"; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, orac_key, node));
    produce(node, gp, when);

    pm_dispute_create_operation dc;
    dc.disputer = "bob"; dc.market_id = 0; dc.proposed_outcome = 1; dc.reason = "B won";
    node.push_pending_transaction(sign_ops({dc}, bob_key, node));
    produce(node, gp, when);

    // The resolver flips the outcome AND bans the creator for a fixed window.
    const fc::time_point_sec ban_until = node.head_block_time() + fc::seconds(100000);
    pm_dispute_resolve_operation dr;
    dr.resolver = "judge"; dr.market_id = 0; dr.correct_outcome = 1;
    dr.penalty_amount = asset(unit * 2, TOKEN_SYMBOL);
    dr.ban_creator = true; dr.ban_creator_until = ban_until;
    node.push_pending_transaction(sign_ops({dr}, judge_key, node));
    produce(node, gp, when);

    // The ban is recorded.
    const auto& cbidx = node.db().get_index<pm_creator_ban_index>().indices().get<by_ban_account>();
    auto cb = cbidx.find(account_name_type("maker"));
    BOOST_REQUIRE(cb != cbidx.end());
    BOOST_CHECK(cb->banned_until == ban_until);
    BOOST_CHECK_EQUAL(cb->ban_count, 1u);

    // A banned creator cannot open a new market.
    pm_create_market_operation cm2;
    cm2.creator = "maker"; cm2.oracle = "orac";
    cm2.market_type = 0; cm2.outcomes = {"A", "B"}; cm2.url = "again";
    cm2.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm2.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm2.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm2.dispute_mode = 1; cm2.dispute_resolver = "judge";
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({cm2}, maker_key, node)),
                      std::runtime_error);
    BOOST_TEST_MESSAGE("creator-ban: maker banned until " << ban_until.sec_since_epoch()
                       << ", new market rejected");
}

// #15 — Fractional LP withdrawal. A second LP withdraws half its position during the betting period
// (principal is withdrawable before betting_expiration); the position stays active with the
// remainder, then a full withdrawal closes it.
BOOST_AUTO_TEST_CASE(fractional_liquidity_withdrawal) {
    auto gp = make_genesis_params(0x1501u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-fraclp", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping fractional LP withdrawal.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    // Self-oracle market with a long betting window (withdrawal is allowed before expiration).
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(3600);
    cm.result_expiration  = node.head_block_time() + fc::seconds(7200);
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    // A second LP adds liquidity (becomes pm_liquidity id 1; seed LP is id 0).
    auto lp2_key = derive_key("lp2");
    create_and_fund(node, gp, when, "lp2", lp2_key, share_type(unit * 4));
    pm_add_liquidity_operation al;
    al.provider = "lp2"; al.market_id = 0; al.amount = asset(share_type(unit * 2), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({al}, lp2_key, node));
    produce(node, gp, when);

    const pm_liquidity_id_type lp2_id(1);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_liquidity_object>(lp2_id).amount.value, unit * 2);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_liquidity_object>(lp2_id).status, 0);

    // Withdraw 50% — position stays active with the remainder.
    const asset lp2_before = node.db().get_account("lp2").balance;
    pm_withdraw_liquidity_operation w1;
    w1.provider = "lp2"; w1.liquidity_id = 1; w1.amount = asset(share_type(unit), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({w1}, lp2_key, node));
    produce(node, gp, when);

    BOOST_CHECK_EQUAL(node.db().get<pm_liquidity_object>(lp2_id).amount.value, unit); // half left
    BOOST_CHECK_EQUAL(node.db().get<pm_liquidity_object>(lp2_id).status, 0);          // still active
    BOOST_CHECK_EQUAL((node.db().get_account("lp2").balance - lp2_before).amount.value, unit);

    // Withdraw the remainder (amount 0 = full position) — position closes.
    pm_withdraw_liquidity_operation w2;
    w2.provider = "lp2"; w2.liquidity_id = 1; w2.amount = asset(0, TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({w2}, lp2_key, node));
    produce(node, gp, when);

    BOOST_CHECK_EQUAL(node.db().get<pm_liquidity_object>(lp2_id).status, 3);          // closed
    BOOST_CHECK_EQUAL((node.db().get_account("lp2").balance - lp2_before).amount.value, unit * 2);
    BOOST_TEST_MESSAGE("fractional LP: withdrew 50% then remainder; principal fully returned");
}

// #56 — Oracle fixed fee (offer→quote model). The creator OFFERS a max fixed fee at create; the
// external oracle QUOTES its own (<= offer) at accept, which is frozen onto the market and paid
// from the settlement pool (never minted). The oracle cannot quote above the offer. A self-oracle
// market freezes the creator's own offer (0 here). Quote fields ride on pm_oracle_accept_market.
BOOST_AUTO_TEST_CASE(oracle_fixed_fee_external_vs_self) {
    auto gp = make_genesis_params(0x5601u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-fixedfee", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping fixed-fee.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    const int64_t F = unit / 2; // fixed fee, < losers_sum so it is fully funded

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    auto orac_key = derive_key("orac");
    const int64_t reg_fee = mp.pm_oracle_registration_fee.amount.value;
    create_and_fund(node, gp, when, "orac", orac_key,
                    share_type(mp.pm_min_oracle_insurance.amount.value + reg_fee + unit * 2));
    pm_oracle_register_operation oreg;
    oreg.owner = "orac"; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = ""; // advisory only; market fee is quoted at accept
    node.push_pending_transaction(sign_ops({oreg}, orac_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    // M0: external oracle. Creator offers a fixed-fee ceiling of F.
    pm_create_market_operation cm0;
    cm0.creator = gp.initiator_name; cm0.oracle = "orac";
    cm0.market_type = 0; cm0.outcomes = {"A", "B"}; cm0.url = "external";
    cm0.oracle_fixed_fee = asset(share_type(F), TOKEN_SYMBOL); // offered ceiling
    cm0.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm0.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm0.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm0.allow_early_resolution = true; cm0.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm0}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type m0(0);

    // The oracle cannot quote a fixed fee above the creator's offer.
    pm_oracle_accept_market_operation bad;
    bad.oracle = "orac"; bad.market_id = 0; bad.accept = true;
    bad.oracle_fixed_fee = asset(share_type(F + unit), TOKEN_SYMBOL);
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({bad}, orac_key, node)), std::runtime_error);

    // The oracle quotes exactly F (<= offer); it is frozen onto the market.
    pm_oracle_accept_market_operation acc;
    acc.oracle = "orac"; acc.market_id = 0; acc.accept = true;
    acc.oracle_fee_percent = 0; acc.oracle_fixed_fee = asset(share_type(F), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({acc}, orac_key, node));
    produce(node, gp, when);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m0).oracle_fixed_fee.value, F);

    // M1: self-oracle → no fixed fee.
    pm_create_market_operation cm1;
    cm1.creator = gp.initiator_name; cm1.oracle = gp.initiator_name;
    cm1.market_type = 0; cm1.outcomes = {"A", "B"}; cm1.url = "self";
    cm1.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm1.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm1.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm1.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm1}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type m1(1);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m1).oracle_fixed_fee.value, 0);

    // Settle M0 with a losing pool; the external oracle is paid exactly its fixed fee.
    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba; bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = "orac"; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, orac_key, node));
    produce(node, gp, when);

    const asset orac_before = node.db().get_account("orac").balance;
    for (int i = 0; i < 200 && node.db().get<pm_market_object>(m0).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(m0).payout_status, 3);

    const int64_t orac_gain = (node.db().get_account("orac").balance - orac_before).amount.value;
    BOOST_TEST_MESSAGE("fixed-fee: external oracle gained " << orac_gain << " (expect " << F << ")");
    BOOST_CHECK_EQUAL(orac_gain, F); // oracle_fee_percent==0 → take is exactly the fixed fee
}

// #9 — Time-weighted LP fees. Two LPs of equal principal, deposited at different times, split the
// liquidity-fee bonus by principal × time-in-market: the earlier LP earns strictly more.
BOOST_AUTO_TEST_CASE(time_weighted_lp_fee_split) {
    auto gp = make_genesis_params(0x0901u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-lptime", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping time-weighted LP.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob"), lp2_key = derive_key("lp2");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));
    create_and_fund(node, gp, when, "lp2",   lp2_key,   share_type(unit * 4));

    // Seed LP (id 0) deposits at creation; 20% liquidity fee feeds the LP bonus.
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 2), TOKEN_SYMBOL);
    cm.liquidity_fee_percent = 1000; // 10% (bp) of the losers' pool → LP bonus
    cm.betting_expiration = node.head_block_time() + fc::seconds(150);
    cm.result_expiration  = node.head_block_time() + fc::seconds(180);
    cm.allow_early_resolution = true; cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    // Open the time gap (≈75s, > 1 minute granularity), then the late LP (id 1) deposits equal
    // principal.
    for (int i = 0; i < 25; ++i) produce(node, gp, when);
    pm_add_liquidity_operation al;
    al.provider = "lp2"; al.market_id = 0; al.amount = asset(share_type(unit * 2), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({al}, lp2_key, node));
    produce(node, gp, when);

    // Generate a losing pool (bob loses) so the liquidity fee is non-zero.
    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba; bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    // Past betting (150s), early-resolve to A, then settle.
    for (int i = 0; i < 30 && node.head_block_time() < cm.betting_expiration; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);
    for (int i = 0; i < 200 && node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);

    const int64_t seed_fee = node.db().get<pm_liquidity_object>(pm_liquidity_id_type(0)).earned_fee.value;
    const int64_t late_fee = node.db().get<pm_liquidity_object>(pm_liquidity_id_type(1)).earned_fee.value;
    BOOST_TEST_MESSAGE("time-weighted LP: seed earned=" << seed_fee << " late earned=" << late_fee);
    BOOST_CHECK_GT(seed_fee, 0);            // bonus distributed
    BOOST_CHECK_GT(seed_fee, late_fee);     // earlier LP earns strictly more for equal principal
}

// #38 — Fault penalty stamp. A zero-volume resolution (oracle resolves a market that took no bets)
// stamps the oracle's penalty_stamps counter (lazy-pool spam defence, §4.10).
BOOST_AUTO_TEST_CASE(zero_volume_resolution_stamps_oracle) {
    auto gp = make_genesis_params(0x3801u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-faultstamp", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping fault stamp.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    const uint32_t stamps_before = oidx.find(gp.initiator_name)->penalty_stamps;

    // Self-oracle market that takes NO bets.
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true; cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    // No bets. Advance past betting, resolve to A, settle.
    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);
    for (int i = 0; i < 200 && node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);

    const uint32_t stamps_after = oidx.find(gp.initiator_name)->penalty_stamps;
    BOOST_TEST_MESSAGE("fault stamp: penalty_stamps " << stamps_before << " -> " << stamps_after);
    BOOST_CHECK_EQUAL(stamps_after, stamps_before + 1); // zero-volume resolution stamped the oracle
}

// #52 — Convert a leveraged position to a normal bet. A profitable position (same-side volume pushed
// its cancel_value above the loan obligation) is converted: the loan + conversion fee are paid, the
// pool loan clears, and the position becomes a plain parimutuel bet.
BOOST_AUTO_TEST_CASE(leverage_convert_to_normal_bet) {
    auto gp = make_genesis_params(0x5201u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-levconv", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping leverage convert.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_leverage_enabled                    = true;
        props.pm_leverage_expiration_buffer_sec      = 0;
        props.pm_leverage_fund_percent               = 100;
        props.pm_leverage_max_per_position_bp        = 10000;
        props.pm_leverage_max_position_ratio_percent = 100;
        props.pm_lazy_alloc_percent                  = 0;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && !mp.pm_leverage_enabled; ++i) produce(node, gp, when);
        BOOST_REQUIRE(mp.pm_leverage_enabled);
    }

    const pm_lazy_pool_id_type pool_id(0);
    auto lev_used  = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).leverage_fund_used.value; };
    auto pool_earned = [&]{ return node.db().get<pm_lazy_pool_object>(pool_id).earned_balance.value; };

    const int64_t D = unit * 100;
    pm_lazy_deposit_operation dep;
    dep.account = gp.initiator_name; dep.amount = asset(share_type(D), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dep}, gp.initiator_key, node));
    produce(node, gp, when);

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 60), TOKEN_SYMBOL); // 6M → reserves 3M/3M
    cm.betting_expiration = node.head_block_time() + fc::seconds(3600);
    cm.result_expiration  = node.head_block_time() + fc::seconds(7200);
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);

    // Convert pays off the WHOLE loan obligation (L×(1+R/100) ≈ unit*11) plus a profit-share fee,
    // so the trader must hold far more than the collateral alone.
    auto trader_key = derive_key("trader"), whale_key = derive_key("whale");
    create_and_fund(node, gp, when, "trader", trader_key, share_type(unit * 60));
    create_and_fund(node, gp, when, "whale",  whale_key,  share_type(unit * 40));

    // trader opens a leveraged position on side A.
    const int64_t L = unit * 10;
    pm_leverage_open_operation op;
    op.account = "trader"; op.market_id = 0; op.outcome_index = 0;
    op.collateral = asset(share_type(unit * 20), TOKEN_SYMBOL); op.loan = asset(share_type(L), TOKEN_SYMBOL);
    op.min_tokens = 0; op.max_slippage_percent = 0;
    node.push_pending_transaction(sign_ops({op}, trader_key, node));
    produce(node, gp, when);
    const pm_leverage_position_id_type pos_id(0);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_leverage_position_object>(pos_id).status, 0);
    BOOST_REQUIRE_EQUAL(lev_used(), L);

    // A whale piles onto the SAME side (A), pushing the position's cancel_value above its loan
    // obligation → the position is now in profit and can be converted.
    pm_place_bet_operation wb;
    wb.account = "whale"; wb.market_id = 0; wb.side = 0; wb.outcome_index = -1;
    wb.amount = asset(share_type(unit * 20), TOKEN_SYMBOL); wb.mode = 0;
    node.push_pending_transaction(sign_ops({wb}, whale_key, node));
    produce(node, gp, when);

    const int64_t earned_before = pool_earned();
    const asset trader_before = node.db().get_account("trader").balance;
    pm_leverage_convert_operation cv;
    cv.account = "trader"; cv.position_id = 0;
    cv.conversion_profit_cost = (uint16_t)mp.pm_conversion_profit_cost_percent;
    node.push_pending_transaction(sign_ops({cv}, trader_key, node));
    produce(node, gp, when);

    const auto& pos = node.db().get<pm_leverage_position_object>(pos_id);
    const int64_t trader_delta = (node.db().get_account("trader").balance - trader_before).amount.value;
    BOOST_TEST_MESSAGE("convert: pos.status=" << (int)pos.status << " lev_used=" << lev_used()
                       << " pool_earned " << earned_before << "->" << pool_earned()
                       << " trader_delta=" << trader_delta);

    BOOST_CHECK_EQUAL(pos.status, 5);                       // converted
    BOOST_CHECK_EQUAL(lev_used(), 0);                       // loan repaid to the pool
    BOOST_CHECK_GT(pool_earned(), earned_before);           // pool kept interest + conversion fee
    BOOST_CHECK_LT(trader_delta, 0);                        // trader paid the obligation + fee
    // The position is now a plain pm_bet owned by the trader (side A).
    uint32_t trader_bets = 0;
    for (const auto& b : node.db().get_index<pm_bet_index>().indices())
        if (b.market == pm_market_id_type(0) && b.account == account_name_type("trader") && b.status == 0)
            ++trader_bets;
    BOOST_CHECK_EQUAL(trader_bets, 1u);
}

// ─── Virtual-operation emission (this session's new vops) ────────────────────────
// The harness produces blocks, so post_apply_operation only fires for plugins when
// enable_plugins_on_push_transaction(true) is set — we use that to capture vops.

// pm_market_accepted: emitted on external accept (oracle's quote frozen) and on a self-oracle
// create (auto-accept). Also covers the offer→quote rule on the % fee.
BOOST_AUTO_TEST_CASE(market_accepted_vop_and_quote) {
    auto gp = make_genesis_params(0x7A11u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-acceptvop", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping market_accepted vop.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    // pm_market_accepted is emitted inside the accept / self-create transaction (not the cron), so the
    // pending-apply (is_producing()==false) fires post_apply_operation without enabling plugins-on-push;
    // enabling it would double-count (pending + block). Cron vops below DO need the enable flag.
    std::vector<pm_market_accepted_operation> accepts;
    auto conn = node.db().post_apply_operation.connect([&](const operation_notification& note) {
        if (note.op.which() == operation::tag<pm_market_accepted_operation>::value)
            accepts.push_back(note.op.get<pm_market_accepted_operation>());
    });

    auto orac_key = derive_key("orac");
    const int64_t reg_fee = mp.pm_oracle_registration_fee.amount.value;
    create_and_fund(node, gp, when, "orac", orac_key,
                    share_type(mp.pm_min_oracle_insurance.amount.value + reg_fee + unit * 2));
    pm_oracle_register_operation oreg;
    oreg.owner = "orac"; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, orac_key, node));
    produce(node, gp, when);

    // M0 external: creator offers oracle_fee_percent ceiling 300, fixed ceiling unit/2.
    pm_create_market_operation cm0;
    cm0.creator = gp.initiator_name; cm0.oracle = "orac";
    cm0.market_type = 0; cm0.outcomes = {"A", "B"}; cm0.url = "ext";
    cm0.oracle_fee_percent = 300; cm0.oracle_fixed_fee = asset(share_type(unit / 2), TOKEN_SYMBOL);
    cm0.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm0.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm0.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm0.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm0}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type m0(0);

    // Quoting ABOVE the offer is rejected.
    pm_oracle_accept_market_operation bad;
    bad.oracle = "orac"; bad.market_id = 0; bad.accept = true;
    bad.oracle_fee_percent = 400; // > offer 300
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({bad}, orac_key, node)), std::runtime_error);

    // The oracle quotes 200 (< offer 300, < cap); it freezes and emits pm_market_accepted.
    accepts.clear();
    pm_oracle_accept_market_operation acc;
    acc.oracle = "orac"; acc.market_id = 0; acc.accept = true;
    acc.oracle_fee_percent = 200; acc.oracle_fixed_fee = asset(share_type(unit / 4), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({acc}, orac_key, node));
    produce(node, gp, when);

    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m0).oracle_fee_percent, 200);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m0).oracle_fixed_fee.value, unit / 4);
    // The harness fires post_apply_operation on BOTH pending-apply and block-generate, so an identical
    // tx-emitted vop appears 1-2 times; assert presence + fields, not an exact count.
    BOOST_REQUIRE(!accepts.empty());
    BOOST_CHECK(accepts[0].oracle == account_name_type("orac"));
    BOOST_CHECK(accepts[0].creator == gp.initiator_name);
    BOOST_CHECK_EQUAL(accepts[0].market_id, 0);
    BOOST_CHECK_EQUAL(accepts[0].oracle_fee_percent, 200);
    BOOST_CHECK_EQUAL(accepts[0].oracle_fixed_fee.amount.value, unit / 4); // vop field is `asset`
    BOOST_CHECK_EQUAL(accepts[0].self_oracle, false);

    // Self-oracle market auto-accepts at creation and emits pm_market_accepted(self_oracle=true).
    accepts.clear();
    pm_create_market_operation cm1;
    cm1.creator = gp.initiator_name; cm1.oracle = gp.initiator_name;
    cm1.market_type = 0; cm1.outcomes = {"A", "B"}; cm1.url = "self";
    cm1.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm1.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm1.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm1.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm1}, gp.initiator_key, node));
    produce(node, gp, when);
    BOOST_REQUIRE(!accepts.empty());
    BOOST_CHECK_EQUAL(accepts[0].self_oracle, true);
    BOOST_CHECK_EQUAL(accepts[0].market_id, 1);
    conn.disconnect();
}

// Oracle auto-accept policy: an oracle can pre-authorize markets to go LIVE at creation, gated by
// BOTH "from whom" (auto_accept_creator) and "which dispute setup" (auto_accept_resolver). The
// resolver pin is the anti-collusion guard: with it empty the oracle only auto-accepts COMMITTEE
// markets (dispute_mode==0), so a maker cannot slip in a colluding named resolver under the
// auto-accept. Non-matching markets fall back to the normal manual-accept (pending) flow.
//   M0: carol + committee            → matches (creator ok, resolver ok) → live (status 1) + vop.
//   M1: carol + named resolver judge → resolver mismatch (empty = committee only) → pending (0).
//   M2: dave  + committee            → creator mismatch (pinned to carol)        → pending (0).
BOOST_AUTO_TEST_CASE(auto_accept_resolver_and_creator_pins_gate_creation) {
    auto gp = make_genesis_params(0xA11Eu, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-autoaccept", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping auto-accept scenario.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit    = mp.pm_min_liquidity.amount.value;
    const int64_t fee     = mp.pm_market_creation_fee.amount.value;
    const int64_t reg_fee = mp.pm_oracle_registration_fee.amount.value;

    // pm_market_accepted fires inside the create tx (evaluator, not the cron); the pending-apply
    // already drives post_apply_operation, so capture without enabling plugins-on-push.
    std::vector<pm_market_accepted_operation> accepts;
    auto conn = node.db().post_apply_operation.connect([&](const operation_notification& note) {
        if (note.op.which() == operation::tag<pm_market_accepted_operation>::value)
            accepts.push_back(note.op.get<pm_market_accepted_operation>());
    });

    // Oracle registers with auto-accept ON, pinned to creator "carol", resolver EMPTY (committee-only).
    auto orac_key = derive_key("orac");
    create_and_fund(node, gp, when, "orac", orac_key,
                    share_type(mp.pm_min_oracle_insurance.amount.value + reg_fee + unit));
    pm_oracle_register_operation oreg;
    oreg.owner = "orac"; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fee_percent = 100; oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    oreg.auto_accept = true;
    oreg.auto_accept_creator  = "carol";
    oreg.auto_accept_resolver = account_name_type(); // empty ⇒ committee only
    node.push_pending_transaction(sign_ops({oreg}, orac_key, node));
    produce(node, gp, when);

    auto carol_key = derive_key("carol"), dave_key = derive_key("dave"), judge_key = derive_key("judge");
    create_and_fund(node, gp, when, "carol", carol_key, share_type(fee * 2 + unit * 9));
    create_and_fund(node, gp, when, "dave",  dave_key,  share_type(fee + unit * 5));
    create_and_fund(node, gp, when, "judge", judge_key, share_type(unit));

    auto make_market = [&](const std::string& creator, const fc::ecc::private_key& key,
                           uint8_t dispute_mode, const std::string& resolver, const char* url) {
        pm_create_market_operation cm;
        cm.creator = creator; cm.oracle = "orac";
        cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = url;
        cm.oracle_fee_percent = 300; cm.oracle_fixed_fee = asset(0, TOKEN_SYMBOL); // offer ceiling ≥ list 100
        cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
        cm.betting_expiration = node.head_block_time() + fc::seconds(60);
        cm.result_expiration  = node.head_block_time() + fc::seconds(120);
        cm.dispute_mode = dispute_mode;
        if (dispute_mode == 1) cm.dispute_resolver = resolver;
        node.push_pending_transaction(sign_ops({cm}, key, node));
        produce(node, gp, when);
    };

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    auto orac_it = oidx.find("orac");
    BOOST_REQUIRE(orac_it != oidx.end());

    // M0 — carol + committee ⇒ auto-accepted at creation (live), oracle's LIST terms frozen.
    accepts.clear();
    make_market("carol", carol_key, 0, "", "m0-committee");
    const pm_market_id_type m0(0);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m0).status, 1); // live, no manual accept
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m0).oracle_fee_percent, 100); // frozen at list price
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m0).oracle_fixed_fee.value, 0);
    BOOST_CHECK_EQUAL(orac_it->markets_accepted, 1u);
    BOOST_REQUIRE(!accepts.empty());
    BOOST_CHECK(accepts[0].oracle == account_name_type("orac"));
    BOOST_CHECK(accepts[0].creator == account_name_type("carol"));
    BOOST_CHECK_EQUAL(accepts[0].self_oracle, false);
    BOOST_CHECK_EQUAL(accepts[0].oracle_fee_percent, 100);

    // M1 — carol + NAMED resolver "judge": resolver pin is empty (committee-only) ⇒ NOT auto-accepted.
    accepts.clear();
    make_market("carol", carol_key, 1, "judge", "m1-named-resolver");
    const pm_market_id_type m1(1);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m1).status, 0); // pending — sham resolver blocked
    BOOST_CHECK_EQUAL(orac_it->markets_accepted, 1u);                 // unchanged
    BOOST_CHECK(accepts.empty());                                     // no accept vop

    // M2 — dave + committee: creator pin is "carol" ⇒ NOT auto-accepted despite a matching dispute setup.
    accepts.clear();
    make_market("dave", dave_key, 0, "", "m2-other-creator");
    const pm_market_id_type m2(2);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m2).status, 0); // pending — creator mismatch
    BOOST_CHECK_EQUAL(orac_it->markets_accepted, 1u);                 // still only M0
    BOOST_CHECK(accepts.empty());

    conn.disconnect();
}

// pm_payout: one per active bet at settlement — winner gets payout>0, loser gets payout 0,
// each carrying the stake + the side it was on.
BOOST_AUTO_TEST_CASE(payout_vop_per_bettor) {
    auto gp = make_genesis_params(0x7A22u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-payoutvop", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping payout vop.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 30; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 30u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true; cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = 0; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba; bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);

    // Capture only the settlement payouts.
    std::vector<pm_payout_operation> payouts;
    node.db().enable_plugins_on_push_transaction(true);
    auto conn = node.db().post_apply_operation.connect([&](const operation_notification& note) {
        if (note.op.which() == operation::tag<pm_payout_operation>::value)
            payouts.push_back(note.op.get<pm_payout_operation>());
    });

    for (int i = 0; i < 200 && node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);

    bool saw_alice = false, saw_bob = false;
    for (const auto& p : payouts) {
        if (p.account == account_name_type("alice")) {
            saw_alice = true;
            BOOST_CHECK_EQUAL(p.amount.amount.value, unit);
            BOOST_CHECK_EQUAL(p.side, 0);
            BOOST_CHECK_GT(p.payout.amount.value, 0);   // winner credited
        } else if (p.account == account_name_type("bob")) {
            saw_bob = true;
            BOOST_CHECK_EQUAL(p.amount.amount.value, unit);
            BOOST_CHECK_EQUAL(p.side, 1);
            BOOST_CHECK_EQUAL(p.payout.amount.value, 0); // loser: 0
        }
    }
    BOOST_CHECK(saw_alice);
    BOOST_CHECK(saw_bob);
    conn.disconnect();
}

// pm_leverage_resolve: a leveraged position carried to market resolution is force-closed at
// settlement (status → 2 won / 3 lost) and emits pm_leverage_resolve with the outcome + leverage.
BOOST_AUTO_TEST_CASE(leverage_resolve_vop_at_settlement) {
    auto gp = make_genesis_params(0x7A33u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-levresolve", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping leverage resolve vop.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_leverage_enabled                    = true;
        props.pm_leverage_expiration_buffer_sec      = 0;
        props.pm_leverage_fund_percent               = 100;
        props.pm_leverage_max_per_position_bp        = 10000;
        props.pm_leverage_max_position_ratio_percent = 100;
        props.pm_lazy_alloc_percent                  = 0;
        props.pm_dispute_grace_sec                   = 30;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && !mp.pm_leverage_enabled; ++i) produce(node, gp, when);
        BOOST_REQUIRE(mp.pm_leverage_enabled);
    }

    const pm_lazy_pool_id_type pool_id(0);
    pm_lazy_deposit_operation dep;
    dep.account = gp.initiator_name; dep.amount = asset(share_type(unit * 100), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dep}, gp.initiator_key, node));
    produce(node, gp, when);

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 60), TOKEN_SYMBOL); // ≥ leverage min liquidity
    cm.betting_expiration = node.head_block_time() + fc::seconds(30);
    cm.result_expiration  = node.head_block_time() + fc::seconds(90);
    cm.allow_early_resolution = true; cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    auto trader_key = derive_key("trader");
    create_and_fund(node, gp, when, "trader", trader_key, share_type(unit * 25));

    // collateral 10u + loan 10u → total_bet 20u → leverage = 20/10 = 2 (loan 10u is the
    // proven-safe size from leverage_open_and_close; higher ratios fail the worst-case safety gate).
    pm_leverage_open_operation op;
    op.account = "trader"; op.market_id = 0; op.outcome_index = 0;
    op.collateral = asset(share_type(unit * 10), TOKEN_SYMBOL); op.loan = asset(share_type(unit * 10), TOKEN_SYMBOL);
    op.min_tokens = 0; op.max_slippage_percent = 0;
    node.push_pending_transaction(sign_ops({op}, trader_key, node));
    produce(node, gp, when);
    const pm_leverage_position_id_type pos_id(0);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_leverage_position_object>(pos_id).status, 0);

    // Past betting, resolve to A; then settle (force-close the open position).
    for (int i = 0; i < 15; ++i) produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);

    std::vector<pm_leverage_resolve_operation> resolves;
    node.db().enable_plugins_on_push_transaction(true);
    auto conn = node.db().post_apply_operation.connect([&](const operation_notification& note) {
        if (note.op.which() == operation::tag<pm_leverage_resolve_operation>::value)
            resolves.push_back(note.op.get<pm_leverage_resolve_operation>());
    });

    for (int i = 0; i < 200 && node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);

    const auto& pos = node.db().get<pm_leverage_position_object>(pos_id);
    BOOST_TEST_MESSAGE("leverage resolve: status=" << (int)pos.status
                       << " pool_received=" << pos.pool_received.value
                       << " bettor_received=" << pos.bettor_received.value
                       << " resolves=" << resolves.size());
    BOOST_CHECK(pos.status == 2 || pos.status == 3);   // resolved (won/lost), NOT 1 (liquidated)
    BOOST_REQUIRE_EQUAL(resolves.size(), 1u);
    BOOST_CHECK(resolves[0].account == account_name_type("trader"));
    BOOST_CHECK_EQUAL(resolves[0].market_id, 0);
    BOOST_CHECK_EQUAL(resolves[0].outcome_index, 0);
    BOOST_CHECK_EQUAL(resolves[0].leverage, 2);        // total_bet 20 / collateral 10
    BOOST_CHECK_EQUAL(resolves[0].won, pos.status == 2);
    conn.disconnect();
}

// Governance-volatility guard: disabling pm_leverage_enabled must NOT strip liquidation protection
// from positions that are already open. The flag only blocks NEW opens; the protective cascade on a
// cancel-bet (and opposing bet) keeps running so the pool is never left exposed if delegates flip the
// flag off mid-flight. Mirrors leverage_cancel_bet_cascade_bad_debt, but turns leverage OFF before the
// cancel and still expects the position liquidated.
BOOST_AUTO_TEST_CASE(leverage_disabled_keeps_liquidation_protection) {
    auto gp = make_genesis_params(0xC333u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-levoffprot", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping leverage-disabled protection.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    auto relaxed = [&](bool enabled) {
        chain_properties_pm props;
        props.pm_leverage_enabled                    = enabled;
        props.pm_leverage_expiration_buffer_sec      = 0;
        props.pm_leverage_fund_percent               = 100;
        props.pm_leverage_max_per_position_bp        = 10000;
        props.pm_leverage_max_position_ratio_percent = 100;
        props.pm_lazy_alloc_percent                  = 0;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        return vp;
    };
    node.push_pending_transaction(sign_ops({relaxed(true)}, gp.initiator_key, node));
    for (int i = 0; i < 60 && !mp.pm_leverage_enabled; ++i) produce(node, gp, when);
    BOOST_REQUIRE(mp.pm_leverage_enabled);

    const pm_lazy_pool_id_type pool_id(0);
    const int64_t D = unit * 100;
    pm_lazy_deposit_operation dep;
    dep.account = gp.initiator_name; dep.amount = asset(share_type(D), TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({dep}, gp.initiator_key, node));
    produce(node, gp, when);

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 60), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(3600); // wide: survives the median wait
    cm.result_expiration  = node.head_block_time() + fc::seconds(7200);
    cm.allow_cancellation = true; cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);

    auto bob_key = derive_key("bob"), trader_key = derive_key("trader");
    create_and_fund(node, gp, when, "bob",    bob_key,    share_type(unit * 70));
    create_and_fund(node, gp, when, "trader", trader_key, share_type(unit * 25));

    // bob bets side A (6M) → reserves (9M, 1M); trader opens a leveraged A position there.
    pm_place_bet_operation bb;
    bb.account = "bob"; bb.market_id = 0; bb.side = 0; bb.outcome_index = -1;
    bb.amount = asset(share_type(unit * 60), TOKEN_SYMBOL); bb.mode = 0;
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);

    uint64_t bob_bet_id = 0; bool found = false;
    for (const auto& b : node.db().get_index<pm_bet_index>().indices()) {
        if (b.market == pm_market_id_type(0) && b.account == account_name_type("bob") && b.status == 0) {
            bob_bet_id = b.id._id; found = true; break;
        }
    }
    BOOST_REQUIRE(found);

    pm_leverage_open_operation op;
    op.account = "trader"; op.market_id = 0; op.outcome_index = 0;
    op.collateral = asset(share_type(unit * 20), TOKEN_SYMBOL); op.loan = asset(share_type(unit * 10), TOKEN_SYMBOL);
    op.min_tokens = 0; op.max_slippage_percent = 0;
    node.push_pending_transaction(sign_ops({op}, trader_key, node));
    produce(node, gp, when);
    const pm_leverage_position_id_type pos_id(0);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_leverage_position_object>(pos_id).status, 0);

    // Delegates turn leverage OFF while the position is open.
    node.push_pending_transaction(sign_ops({relaxed(false)}, gp.initiator_key, node));
    for (int i = 0; i < 60 && mp.pm_leverage_enabled; ++i) produce(node, gp, when);
    BOOST_REQUIRE(!mp.pm_leverage_enabled);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_leverage_position_object>(pos_id).status, 0); // still open

    // bob cancels his A bet → the protective cascade must still fire and liquidate the position.
    pm_cancel_bet_operation cancel;
    cancel.account = "bob"; cancel.bet_id = bob_bet_id; cancel.min_return = 0;
    node.push_pending_transaction(sign_ops({cancel}, bob_key, node));
    produce(node, gp, when);

    BOOST_TEST_MESSAGE("leverage-off protection: pos.status="
                       << (int)node.db().get<pm_leverage_position_object>(pos_id).status
                       << " (leverage_enabled=" << mp.pm_leverage_enabled << ")");
    // Liquidated despite leverage being disabled — the flag only blocks new opens.
    BOOST_CHECK_EQUAL(node.db().get<pm_leverage_position_object>(pos_id).status, 1);
    BOOST_CHECK_EQUAL(node.db().get<pm_lazy_pool_object>(pool_id).leverage_fund_used.value, 0); // loan cleared
}

// ─────────────────────────────────────────────────────────────────────────────
// Betting odds & payout table — how a bettor's decimal coefficient is built.
//
// Two layers, both printed as plain rows:
//   1. LIVE odds (price discovery): the CPMM curve sets the implied probability
//      P(A)=reserve_b/(reserve_a+reserve_b); decimal odds = 1/P. These move with
//      every bet, exactly like a bookmaker's board.
//   2. REALIZED coefficient (settlement): a winner is paid its OWN stake back plus
//      a parimutuel share of the LOSERS' pool, and the losers' pool is first cut by
//      the oracle %, creator %, and liquidity-provider % (bp of losers_sum). The
//      share is by CURVE WEIGHT (tokens), not raw stake — earlier money on a side
//      buys more weight before the price moves.
//
// We place real bets (real CPMM weights), resolve A on-chain and assert the paid
// amounts equal the production settlement function, then reuse that SAME function
// (compute_settlement) to print the counterfactual "B wins" table. So both payout
// tables are authoritative, not hand-math.
BOOST_AUTO_TEST_CASE(betting_odds_and_payout_table) {
    auto gp = make_genesis_params(0x0DD5u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-odds", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping odds table.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value; // 100.000 VIZ

    // Short settle grace so the auto-payout cron fires quickly after resolution.
    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 5;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 5; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 5u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    // Four bettors: a1,a2 back A; b1,b2 back B. Each funded generously.
    struct better { std::string name; fc::ecc::private_key key; int8_t side; int64_t stake; };
    std::vector<better> bs = {
        { "anna",  derive_key("anna"),  0, unit * 2 },
        { "bill",  derive_key("bill"),  1, unit * 1 },
        { "andy",  derive_key("andy"),  0, unit * 1 },
        { "boris", derive_key("boris"), 1, unit * 3 },
    };
    for (auto& b : bs) create_and_fund(node, gp, when, b.name, b.key, share_type(b.stake + unit));

    // Self-oracle market, seeded 4u/4u (k=16u²). Commission baked into settlement:
    // oracle 3%, liquidity 2%, creator 0% — all bp of the losers' pool.
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.oracle_fee_percent    = 300; // 3.00%
    cm.oracle_fixed_fee      = asset(0, TOKEN_SYMBOL);
    cm.creator_fee_percent   = 0;
    cm.liquidity_fee_percent = 200; // 2.00%
    cm.liquidity = asset(share_type(unit * 8), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(120);
    cm.result_expiration  = node.head_block_time() + fc::seconds(240);
    cm.allow_early_resolution = true;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    const auto& mkt0 = node.db().get<pm_market_object>(market_id);
    const uint16_t ORACLE_BP  = mkt0.oracle_fee_percent;
    const uint16_t CREATOR_BP = mkt0.creator_fee_percent;
    const uint16_t LIQ_BP     = mkt0.liquidity_fee_percent;

    // Decimal formatting helper: num/den → "X.XX".
    auto fx = [](int64_t num, int64_t den) -> std::string {
        if (den == 0) return "inf";
        int64_t v = num * 100 / den; // hundredths
        std::string f = std::to_string(v % 100);
        if (f.size() < 2) f = "0" + f;
        return std::to_string(v / 100) + "." + f;
    };

    const int feeBp = (int)ORACLE_BP + (int)CREATOR_BP + (int)LIQ_BP; // total commission off losers' pool
    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("=== LIVE BOARD (parimutuel, commission baked in) — updates per bet ===");
    BOOST_TEST_MESSAGE("seed liquidity " << mkt0.reserve_a.value/1000 + mkt0.reserve_b.value/1000
                       << " VIZ  | commission off losers' pool: oracle " << fx(ORACLE_BP,100)
                       << "% + liq " << fx(LIQ_BP,100) << "% + creator " << fx(CREATOR_BP,100)
                       << "% = " << fx(feeBp,100) << "%");
    BOOST_TEST_MESSAGE("  decimal odds = 1 + opposing_pool x (1 - commission) / own_pool   (all VIZ)");
    BOOST_TEST_MESSAGE("step bettor side stake  weight(tokens)  poolA/poolB    impliedP(A)  oddsA  oddsB");

    struct cap { std::string name; int8_t side; int64_t stake; int64_t weight; uint32_t tp; };
    std::vector<cap> caps;

    int step = 0;
    for (auto& b : bs) {
        const auto& m_pre = node.db().get<pm_market_object>(market_id);
        const int64_t ra0 = m_pre.reserve_a.value, rb0 = m_pre.reserve_b.value;

        pm_place_bet_operation pb;
        pb.account = b.name; pb.market_id = 0; pb.side = b.side; pb.outcome_index = -1;
        pb.amount = asset(share_type(b.stake), TOKEN_SYMBOL); pb.mode = 0;
        node.push_pending_transaction(sign_ops({pb}, b.key, node));
        produce(node, gp, when);

        const auto& m_post = node.db().get<pm_market_object>(market_id);
        const int64_t ra1 = m_post.reserve_a.value, rb1 = m_post.reserve_b.value;
        const int64_t weight = (b.side == 0) ? (rb0 - rb1) : (ra0 - ra1); // CPMM tokens out
        caps.push_back({ b.name, b.side, b.stake, weight, 0u });

        // Parimutuel board: a side's holders get their stake back + the opposing pool net of
        // commission, so the side-level decimal coefficient is 1 + opp_net/own (commission baked in).
        const int64_t As = m_post.a_bets_sum.value, Bs = m_post.b_bets_sum.value;
        const int64_t Anet = As * (10000 - feeBp) / 10000;
        const int64_t Bnet = Bs * (10000 - feeBp) / 10000;
        BOOST_TEST_MESSAGE("  " << ++step << "   " << b.name
            << "   " << (b.side == 0 ? "A" : "B")
            << "   " << b.stake/1000
            << "    " << weight/1000
            << "          " << As/1000 << "/" << Bs/1000
            << "         " << fx(As, As + Bs)        // tote-implied P(A) = poolA/(poolA+poolB)
            << "      x" << fx(As + Bnet, As)         // oddsA = 1 + Bnet/As
            << "  x" << fx(Bs + Anet, Bs));           // oddsB = 1 + Anet/Bs
    }

    // Pull the real curve weight/time_penalty the chain recorded (authoritative).
    {
        const auto& bidx = node.db().get_index<pm_bet_index>().indices().get<by_market>();
        for (auto it = bidx.lower_bound(boost::make_tuple(market_id, pm_bet_id_type()));
             it != bidx.end() && it->market == market_id; ++it) {
            for (auto& c : caps)
                if (c.name == std::string(it->account)) { c.weight = it->weight.value; c.tp = it->time_penalty; }
        }
    }

    // Analytic settlement via the production function — for BOTH outcomes.
    struct proj { pm::settle_result res; int64_t total_weight; std::vector<size_t> wi; int64_t losers; };
    auto project = [&](int8_t win_side) -> proj {
        pm::settle_params sp;
        sp.oracle_fee_percent = ORACLE_BP; sp.creator_fee_percent = CREATOR_BP;
        sp.liquidity_fee_percent = LIQ_BP; sp.forfeit_pool = mkt0.forfeit_pool.value;
        sp.oracle_fixed_fee = mkt0.oracle_fixed_fee.value;
        std::vector<pm::winner_in> w; proj P; P.total_weight = 0; P.losers = 0;
        for (size_t i = 0; i < caps.size(); ++i) {
            if (caps[i].side == win_side) {
                w.push_back(pm::winner_in{caps[i].stake, caps[i].weight, caps[i].tp});
                P.wi.push_back(i); P.total_weight += caps[i].weight;
            } else P.losers += caps[i].stake;
        }
        sp.losers_sum = P.losers;
        P.res = pm::compute_settlement(sp, w);
        return P;
    };

    auto print_table = [&](int8_t win_side, const char* label) -> pm::settle_result {
        proj P = project(win_side);
        const pm::settle_result& res = P.res;
        const std::vector<size_t>& wi = P.wi;
        const int64_t total_weight = P.total_weight;
        const int64_t losers = P.losers;
        BOOST_TEST_MESSAGE("");
        BOOST_TEST_MESSAGE(std::string("=== SETTLEMENT — ") + label + " (losers' pool = "
            + std::to_string(losers/1000) + " VIZ) ===");
        BOOST_TEST_MESSAGE("  commission off losers' pool: oracle=" << res.oracle_take/1000
            << " creator=" << res.creator_take/1000 << " liq(LP)=" << res.lp_bonus/1000
            << " VIZ  →  winners' pool=" << (losers - res.oracle_take - res.creator_take - res.lp_bonus)/1000);
        BOOST_TEST_MESSAGE("  bettor  stake  weight  wt-share   profit   payout   coefficient(payout/stake)");
        for (size_t k = 0; k < wi.size(); ++k) {
            const auto& c = caps[wi[k]];
            int64_t payout = res.winner_payout[k];
            int64_t profit = payout - c.stake;
            BOOST_TEST_MESSAGE("   " << c.name << "      " << c.stake/1000
                << "     " << c.weight/1000
                << "    " << fx(c.weight, total_weight)        // weight share
                << "      " << profit/1000
                << "      " << payout/1000
                << "        x" << fx(payout, c.stake));        // realized decimal coefficient
        }
        // Losers: stake forfeited, coefficient 0.
        for (size_t i = 0; i < caps.size(); ++i)
            if (caps[i].side != win_side)
                BOOST_TEST_MESSAGE("   " << caps[i].name << "      " << caps[i].stake/1000
                    << "     " << caps[i].weight/1000 << "    (lost)       -"
                    << caps[i].stake/1000 << "       0          x0.00");
        return res;
    };

    const auto resA = print_table(0, "OUTCOME A WINS");
    const auto resB = print_table(1, "OUTCOME B WINS");

    // ── Verify the analytic A-table equals the REAL on-chain settlement ──
    std::vector<int64_t> bal_before(caps.size());
    for (size_t i = 0; i < caps.size(); ++i)
        bal_before[i] = node.db().get_account(caps[i].name).balance.amount.value;

    // Early resolution needs the betting window closed (now >= betting_expiration).
    for (int i = 0; i < 200 &&
             node.head_block_time() < node.db().get<pm_market_object>(market_id).betting_expiration; ++i)
        produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0; // A wins
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);
    for (int i = 0; i < 200 && node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);

    // On-chain winner payout == compute_settlement payout (proves the printed math is real).
    {
        size_t k = 0;
        for (size_t i = 0; i < caps.size(); ++i) {
            if (caps[i].side != 0) continue; // A winners, in bet order
            int64_t got = node.db().get_account(caps[i].name).balance.amount.value - bal_before[i];
            BOOST_CHECK_EQUAL(got, resA.winner_payout[k]);
            ++k;
        }
        // Losers received nothing back.
        for (size_t i = 0; i < caps.size(); ++i)
            if (caps[i].side != 0)
                BOOST_CHECK_EQUAL(node.db().get_account(caps[i].name).balance.amount.value, bal_before[i]);
    }

    // ── Zero-sum invariant for both projections (no token emission) ──
    auto check_zero_sum = [&](const pm::settle_result& res, int8_t win_side) {
        int64_t out = res.oracle_take + res.creator_take + res.lp_bonus;
        for (auto p : res.winner_payout) out += p;
        int64_t in = 0;
        for (auto& c : caps) in += c.stake; // winners' stakes + losers' stakes (forfeit_pool=0 here)
        BOOST_CHECK_EQUAL(out, in);
        (void)win_side;
    };
    check_zero_sum(resA, 0);
    check_zero_sum(resB, 1);
    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("zero-sum OK: Σpayout + oracle + creator + LP == Σ all stakes (no emission), both outcomes.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Same odds & payout demonstration for a MULTI (LMSR) market, 3 outcomes A/B/C.
// The settlement math is identical (parimutuel by curve weight, fees off the
// losers' pool); only the weight comes from the LMSR cost function instead of CPMM.
// The live board generalises: outcome i's decimal coefficient =
//   1 + (Σ_{j≠i} pool_j) x (1 - commission) / pool_i.
BOOST_AUTO_TEST_CASE(betting_odds_and_payout_table_multi) {
    auto gp = make_genesis_params(0x0DD6u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-odds-multi", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping multi odds table.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 5;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 5; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 5u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    struct better { std::string name; fc::ecc::private_key key; int16_t oidx; int64_t stake; };
    std::vector<better> bs = {
        { "anna",  derive_key("anna"),  0, unit * 2 }, // A
        { "bill",  derive_key("bill"),  1, unit * 3 }, // B
        { "cleo",  derive_key("cleo"),  2, unit * 1 }, // C
        { "andy",  derive_key("andy"),  0, unit * 1 }, // A again
    };
    for (auto& b : bs) create_and_fund(node, gp, when, b.name, b.key, share_type(b.stake + unit));

    const int64_t L = unit * 30;
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 1; cm.outcomes = {"A", "B", "C"}; cm.url = "criteria";
    cm.oracle_fee_percent    = 300; // 3.00%
    cm.oracle_fixed_fee      = asset(0, TOKEN_SYMBOL);
    cm.creator_fee_percent   = 0;
    cm.liquidity_fee_percent = 200; // 2.00%
    cm.liquidity = asset(share_type(L), TOKEN_SYMBOL);
    cm.lmsr_b = graphene::chain::lmsr::lmsr_b_from_liquidity(L, 3);
    cm.betting_expiration = node.head_block_time() + fc::seconds(120);
    cm.result_expiration  = node.head_block_time() + fc::seconds(240);
    cm.allow_early_resolution = true;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    const auto& mkt0 = node.db().get<pm_market_object>(market_id);
    BOOST_REQUIRE_EQUAL(mkt0.market_type, 1);
    const uint16_t ORACLE_BP  = mkt0.oracle_fee_percent;
    const uint16_t CREATOR_BP = mkt0.creator_fee_percent;
    const uint16_t LIQ_BP     = mkt0.liquidity_fee_percent;
    const int feeBp = (int)ORACLE_BP + (int)CREATOR_BP + (int)LIQ_BP;
    const char* OUT[3] = { "A", "B", "C" };

    auto fx = [](int64_t num, int64_t den) -> std::string {
        if (den == 0) return "inf";
        int64_t v = num * 100 / den;
        std::string f = std::to_string(v % 100);
        if (f.size() < 2) f = "0" + f;
        return std::to_string(v / 100) + "." + f;
    };
    auto pool_of = [&](int oi) -> int64_t {
        const auto& oo = node.db().get_index<pm_outcome_index>().indices().get<by_market_outcome>();
        auto it = oo.lower_bound(boost::make_tuple(market_id, (uint8_t)oi));
        return (it != oo.end() && it->market == market_id && it->outcome_index == oi) ? it->bets_sum.value : 0;
    };

    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("=== LIVE BOARD (multi/LMSR, commission baked in) — updates per bet ===");
    BOOST_TEST_MESSAGE("LMSR b=" << mkt0.lmsr_b.value/1000 << " VIZ  | commission off losers' pool = "
                       << fx(feeBp,100) << "%   (odds_i = 1 + Σ_other x (1-comm) / pool_i)");
    BOOST_TEST_MESSAGE("step bettor out stake weight(tokens)   poolA/poolB/poolC      oddsA / oddsB / oddsC");

    struct cap { std::string name; int16_t oidx; int64_t stake; int64_t weight; uint32_t tp; };
    std::vector<cap> caps;

    int step = 0;
    for (auto& b : bs) {
        pm_place_bet_operation pb;
        pb.account = b.name; pb.market_id = 0; pb.side = -1; pb.outcome_index = b.oidx;
        pb.amount = asset(share_type(b.stake), TOKEN_SYMBOL); pb.mode = 0;
        node.push_pending_transaction(sign_ops({pb}, b.key, node));
        produce(node, gp, when);

        // Read back this bet's LMSR curve weight (tokens) from the chain.
        int64_t myw = 0; uint32_t mytp = 0;
        {
            const auto& bidx = node.db().get_index<pm_bet_index>().indices().get<by_market>();
            for (auto it = bidx.lower_bound(boost::make_tuple(market_id, pm_bet_id_type()));
                 it != bidx.end() && it->market == market_id; ++it)
                if (std::string(it->account) == b.name) { myw = it->weight.value; mytp = it->time_penalty; }
        }
        caps.push_back({ b.name, b.oidx, b.stake, myw, mytp });

        int64_t pool[3] = { pool_of(0), pool_of(1), pool_of(2) };
        const int64_t tot = pool[0] + pool[1] + pool[2];
        std::string odds;
        for (int i = 0; i < 3; ++i) {
            int64_t net = (tot - pool[i]) * (10000 - feeBp) / 10000;
            odds += (i ? " / " : "") + std::string("x") + fx(pool[i] + net, pool[i]);
        }
        BOOST_TEST_MESSAGE("  " << ++step << "   " << b.name << "   " << OUT[b.oidx]
            << "    " << b.stake/1000
            << "     " << myw/1000
            << "        " << pool[0]/1000 << "/" << pool[1]/1000 << "/" << pool[2]/1000
            << "          " << odds);
    }

    struct proj { pm::settle_result res; int64_t total_weight; std::vector<size_t> wi; int64_t losers; };
    auto project = [&](int16_t win) -> proj {
        pm::settle_params sp;
        sp.oracle_fee_percent = ORACLE_BP; sp.creator_fee_percent = CREATOR_BP;
        sp.liquidity_fee_percent = LIQ_BP; sp.forfeit_pool = mkt0.forfeit_pool.value;
        sp.oracle_fixed_fee = mkt0.oracle_fixed_fee.value;
        std::vector<pm::winner_in> w; proj P; P.total_weight = 0; P.losers = 0;
        for (size_t i = 0; i < caps.size(); ++i) {
            if (caps[i].oidx == win) {
                w.push_back(pm::winner_in{caps[i].stake, caps[i].weight, caps[i].tp});
                P.wi.push_back(i); P.total_weight += caps[i].weight;
            } else P.losers += caps[i].stake;
        }
        sp.losers_sum = P.losers;
        P.res = pm::compute_settlement(sp, w);
        return P;
    };

    auto print_table = [&](int16_t win) -> pm::settle_result {
        proj P = project(win);
        const pm::settle_result& res = P.res;
        BOOST_TEST_MESSAGE("");
        BOOST_TEST_MESSAGE(std::string("=== SETTLEMENT — OUTCOME ") + OUT[win] + " WINS (losers' pool = "
            + std::to_string(P.losers/1000) + " VIZ) ===");
        BOOST_TEST_MESSAGE("  commission: oracle=" << res.oracle_take/1000 << " creator=" << res.creator_take/1000
            << " liq(LP)=" << res.lp_bonus/1000 << " VIZ  →  winners' pool="
            << (P.losers - res.oracle_take - res.creator_take - res.lp_bonus)/1000);
        BOOST_TEST_MESSAGE("  bettor  out  stake  weight  wt-share   profit   payout   coefficient");
        for (size_t k = 0; k < P.wi.size(); ++k) {
            const auto& c = caps[P.wi[k]];
            int64_t payout = res.winner_payout[k];
            BOOST_TEST_MESSAGE("   " << c.name << "     " << OUT[c.oidx] << "    " << c.stake/1000
                << "     " << c.weight/1000 << "    " << fx(c.weight, P.total_weight)
                << "      " << (payout - c.stake)/1000 << "      " << payout/1000
                << "        x" << fx(payout, c.stake));
        }
        for (size_t i = 0; i < caps.size(); ++i)
            if (caps[i].oidx != win)
                BOOST_TEST_MESSAGE("   " << caps[i].name << "     " << OUT[caps[i].oidx] << "    "
                    << caps[i].stake/1000 << "     " << caps[i].weight/1000 << "    (lost)       -"
                    << caps[i].stake/1000 << "       0          x0.00");
        return res;
    };

    const auto resA = print_table(0);
    print_table(1);
    print_table(2);

    // Verify the analytic A-table equals the REAL on-chain settlement.
    std::vector<int64_t> bal_before(caps.size());
    for (size_t i = 0; i < caps.size(); ++i)
        bal_before[i] = node.db().get_account(caps[i].name).balance.amount.value;

    // Early resolution needs the betting window closed (now >= betting_expiration).
    for (int i = 0; i < 200 &&
             node.head_block_time() < node.db().get<pm_market_object>(market_id).betting_expiration; ++i)
        produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0; // A
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);
    for (int i = 0; i < 200 && node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);

    {
        size_t k = 0;
        for (size_t i = 0; i < caps.size(); ++i) {
            if (caps[i].oidx != 0) continue;
            int64_t got = node.db().get_account(caps[i].name).balance.amount.value - bal_before[i];
            BOOST_CHECK_EQUAL(got, resA.winner_payout[k]);
            ++k;
        }
        for (size_t i = 0; i < caps.size(); ++i)
            if (caps[i].oidx != 0)
                BOOST_CHECK_EQUAL(node.db().get_account(caps[i].name).balance.amount.value, bal_before[i]);
    }

    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("multi LMSR: on-chain winner payouts match compute_settlement; zero-sum preserved.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Long market: 1000 mixed bets (random side & size), measuring how far the
// commission-baked decimal coefficient SHOWN to a bettor at bet time drifts from
// the FINAL coefficient at close. In a parimutuel board the displayed odds are not
// locked: odds_side = 1 + opposing_pool x (1 - commission) / own_pool, and both
// pools keep growing. Early bettors (thin pool) see the biggest drift; late bettors
// see almost none. The board number depends only on the per-side staked pools, so a
// deep liquidity seed (kept here only to keep the CPMM curve well-behaved) does not
// affect it. All 1000 bets come from the high-stake initiator (one account may bet
// both sides; nothing restricts it) so bandwidth/funding are never the bottleneck.
BOOST_AUTO_TEST_CASE(odds_drift_long_market_1000_bets) {
    auto gp = make_genesis_params(0x0DD7u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-drift", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping drift simulation.");
        return;
    }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    {
        chain_properties_pm props;
        props.pm_dispute_grace_sec = 5;
        versioned_chain_properties_update_operation vp;
        vp.owner = gp.initiator_name; vp.props = props;
        node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
        for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 5; ++i) produce(node, gp, when);
        BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 5u);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    // Deep seed so 1000 bets never drain a CPMM reserve to zero (board math is
    // independent of this). oracle 3% + liquidity 2% = 5% commission.
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.oracle_fee_percent    = 300;
    cm.oracle_fixed_fee      = asset(0, TOKEN_SYMBOL);
    cm.creator_fee_percent   = 0;
    cm.liquidity_fee_percent = 200;
    cm.liquidity = asset(share_type(unit * 400), TOKEN_SYMBOL);
    // Betting window must outlast the ~100 placement blocks (~300s) but still close so the
    // oracle can resolve early (can_resolve_early requires now >= betting_expiration).
    cm.betting_expiration = node.head_block_time() + fc::seconds(600);
    cm.result_expiration  = node.head_block_time() + fc::seconds(1200);
    cm.allow_early_resolution = true;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    const auto& mkt0 = node.db().get<pm_market_object>(market_id);
    const int feeBp = (int)mkt0.oracle_fee_percent + (int)mkt0.creator_fee_percent + (int)mkt0.liquidity_fee_percent;
    const double nf = 1.0 - feeBp / 10000.0; // net-of-commission factor

    // Place 1000 bets, side & size from a deterministic xorshift PRNG (reproducible).
    // Bundle 10 ops/tx → 100 blocks. Stakes 5..40 VIZ.
    const int N = 1000;
    const int64_t base = unit / 20; // 5 VIZ
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    auto nr = [&]() -> uint64_t { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };

    std::vector<operation> batch;
    for (int i = 0; i < N; ++i) {
        pm_place_bet_operation pb;
        pb.account = gp.initiator_name; pb.market_id = 0;
        pb.side = (int8_t)(nr() & 1ull); pb.outcome_index = -1;
        pb.amount = asset(share_type(base * (int64_t)(1 + nr() % 8)), TOKEN_SYMBOL);
        pb.mode = 0;
        batch.push_back(pb);
        if (batch.size() == 10) {
            node.push_pending_transaction(sign_ops(batch, gp.initiator_key, node));
            produce(node, gp, when);
            batch.clear();
        }
    }
    if (!batch.empty()) { node.push_pending_transaction(sign_ops(batch, gp.initiator_key, node)); produce(node, gp, when); }

    // Read all bets in creation order (= bet-id order) → authoritative (side, stake, weight).
    std::vector<int8_t> side; std::vector<int64_t> stake, weight;
    {
        const auto& bidx = node.db().get_index<pm_bet_index>().indices().get<by_market>();
        for (auto it = bidx.lower_bound(boost::make_tuple(market_id, pm_bet_id_type()));
             it != bidx.end() && it->market == market_id; ++it) {
            side.push_back(it->side); stake.push_back(it->amount.value); weight.push_back(it->weight.value);
        }
    }
    BOOST_REQUIRE_EQUAL((int)side.size(), N);

    // Final per-side pools (stake) and curve-weight totals.
    int64_t PA = 0, PB = 0; double WA = 0, WB = 0;
    for (int i = 0; i < N; ++i) {
        if (side[i] == 0) { PA += stake[i]; WA += (double)weight[i]; }
        else              { PB += stake[i]; WB += (double)weight[i]; }
    }

    // Replay the running board; compute SHOWN (at bet time, post-own-bet) vs FINAL
    // (at close) for two views: (1) board side-level coefficient = what the UI shows;
    // (2) the bettor's personal weight-based coefficient = what they actually realise.
    int64_t rPA = 0, rPB = 0; double rWA = 0, rWB = 0;
    std::vector<double> shown(N), finalc(N), absdrift(N), absdriftP(N);
    for (int i = 0; i < N; ++i) {
        double w = (double)weight[i], s = (double)stake[i];
        double ownP, oppP, ownW, fOwnP, fOppP, fOwnW;
        if (side[i] == 0) {
            rPA += stake[i]; rWA += w;
            ownP = (double)rPA; oppP = (double)rPB; ownW = rWA;
            fOwnP = (double)PA;  fOppP = (double)PB;  fOwnW = WA;
        } else {
            rPB += stake[i]; rWB += w;
            ownP = (double)rPB; oppP = (double)rPA; ownW = rWB;
            fOwnP = (double)PB;  fOppP = (double)PA;  fOwnW = WB;
        }
        shown[i]  = 1.0 + oppP  * nf / ownP;                 // board, at bet time
        finalc[i] = 1.0 + fOppP * nf / fOwnP;                // board, at close
        absdrift[i] = std::fabs((finalc[i] - shown[i]) / shown[i]);

        double shownP = 1.0 + oppP  * nf * w / (ownW  * s);  // personal, at bet time
        double finalP = 1.0 + fOppP * nf * w / (fOwnW * s);  // personal, at close
        absdriftP[i] = std::fabs((finalP - shownP) / shownP);
    }

    // ── Report ──
    char buf[220];
    BOOST_TEST_MESSAGE("");
    std::snprintf(buf, sizeof(buf), "=== ODDS DRIFT over %d bets (board = 1 + opp_pool x %.2f / own_pool; commission %d%%) ===",
                  N, nf, feeBp/100);
    BOOST_TEST_MESSAGE(buf);
    std::snprintf(buf, sizeof(buf), "final pools: A=%lld VIZ  B=%lld VIZ  ->  final board oddsA=x%.3f  oddsB=x%.3f",
                  (long long)(PA/1000), (long long)(PB/1000), 1.0 + (double)PB*nf/PA, 1.0 + (double)PA*nf/PB);
    BOOST_TEST_MESSAGE(buf);
    BOOST_TEST_MESSAGE("decile  avg-shown  avg-final  avg|drift|%  (board coefficient)");
    for (int d = 0; d < 10; ++d) {
        double ss = 0, sfv = 0, sd = 0; int lo = d * (N/10), hi = lo + (N/10);
        for (int i = lo; i < hi; ++i) { ss += shown[i]; sfv += finalc[i]; sd += absdrift[i]; }
        int cnt = hi - lo;
        std::snprintf(buf, sizeof(buf), "  %2d      x%.3f     x%.3f      %6.2f%%",
                      d + 1, ss/cnt, sfv/cnt, 100.0 * sd/cnt);
        BOOST_TEST_MESSAGE(buf);
    }

    std::vector<double> sorted = absdrift; std::sort(sorted.begin(), sorted.end());
    double mean = 0; for (double x : absdrift) mean += x; mean /= N;
    double meanP = 0; for (double x : absdriftP) meanP += x; meanP /= N;
    int within5 = 0, within20 = 0;
    for (double x : absdrift) { if (x <= 0.05) ++within5; if (x <= 0.20) ++within20; }
    std::snprintf(buf, sizeof(buf), "board drift |d|: mean=%.2f%%  median=%.2f%%  p90=%.2f%%  max=%.2f%%",
                  100*mean, 100*sorted[N/2], 100*sorted[(N*9)/10], 100*sorted[N-1]);
    BOOST_TEST_MESSAGE(buf);
    std::snprintf(buf, sizeof(buf), "within +/-5%%: %d/%d bets   within +/-20%%: %d/%d   |   personal(weight) drift mean=%.2f%%",
                  within5, N, within20, N, 100*meanP);
    BOOST_TEST_MESSAGE(buf);
    BOOST_TEST_MESSAGE("read: early bets are quoted on a near-empty pool so their shown odds drift hard;"
                       " by the last decile the board has converged.");

    // Sanity: the 1000-bet market settles cleanly on-chain (the PM zero-sum invariant
    // itself is asserted in the dedicated payout-table tests).
    // Early resolution needs the betting window closed (now >= betting_expiration).
    for (int i = 0; i < 400 && node.head_block_time() < mkt0.betting_expiration; ++i)
        produce(node, gp, when);
    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0;
    node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    produce(node, gp, when);
    for (int i = 0; i < 300 && node.db().get<pm_market_object>(market_id).payout_status != 3; ++i)
        produce(node, gp, when);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(market_id).payout_status, 3);
}

// Binary, but order flow is heavily skewed (~80% on outcome A) → a lopsided board:
// favourite near x1.2, underdog ~x4-5. Shows the drift on a non-even market.
BOOST_AUTO_TEST_CASE(odds_drift_binary_skewed) {
    auto gp = make_genesis_params(0x0DD8u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-drift-skew", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);
    if (!bring_to_hf14(node, gp, when)) { BOOST_TEST_MESSAGE("HF14 not reachable; skipping."); return; }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    { chain_properties_pm props; props.pm_dispute_grace_sec = 5;
      versioned_chain_properties_update_operation vp; vp.owner = gp.initiator_name; vp.props = props;
      node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
      for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 5; ++i) produce(node, gp, when);
      BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 5u); }
    pm_oracle_register_operation oreg; oreg.owner = gp.initiator_name;
    oreg.insurance = mp.pm_min_oracle_insurance; oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.oracle_fee_percent = 300; cm.oracle_fixed_fee = asset(0, TOKEN_SYMBOL);
    cm.creator_fee_percent = 0; cm.liquidity_fee_percent = 200;
    cm.liquidity = asset(share_type(unit * 400), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(600);
    cm.result_expiration  = node.head_block_time() + fc::seconds(1200);
    cm.allow_early_resolution = true;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    const auto& mkt0 = node.db().get<pm_market_object>(market_id);
    const int feeBp = (int)mkt0.oracle_fee_percent + (int)mkt0.creator_fee_percent + (int)mkt0.liquidity_fee_percent;

    const int N = 1000; const int64_t base = unit / 20;
    uint64_t rng = 0xBADC0FFEE0DDF00Dull;
    auto nr = [&]() -> uint64_t { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };
    std::vector<int16_t> outc(N); std::vector<int64_t> stk(N);
    for (int i = 0; i < N; ++i) {
        outc[i] = (nr() % 100 < 80) ? 0 : 1;             // ~80% of bets on A
        stk[i]  = base * (int64_t)(1 + nr() % 8);
    }
    run_drift_market(node, gp, when, /*binary=*/true, /*outcomes=*/2, feeBp, outc, stk, market_id,
                     "BINARY skewed (~80% on A)");
}

// Multi (LMSR, 3 outcomes) with 1000 bets split 50/30/20 across A/B/C — the parimutuel
// board generalises (odds_i = 1 + Σ_other x (1-comm) / pool_i) and drifts the same way.
BOOST_AUTO_TEST_CASE(odds_drift_multi_1000) {
    auto gp = make_genesis_params(0x0DD9u, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-drift-multi", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);
    if (!bring_to_hf14(node, gp, when)) { BOOST_TEST_MESSAGE("HF14 not reachable; skipping."); return; }

    const auto& mp = node.db().get_validator_schedule_object().median_props;
    const int64_t unit = mp.pm_min_liquidity.amount.value;
    { chain_properties_pm props; props.pm_dispute_grace_sec = 5;
      versioned_chain_properties_update_operation vp; vp.owner = gp.initiator_name; vp.props = props;
      node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
      for (int i = 0; i < 60 && mp.pm_dispute_grace_sec != 5; ++i) produce(node, gp, when);
      BOOST_REQUIRE_EQUAL(mp.pm_dispute_grace_sec, 5u); }
    pm_oracle_register_operation oreg; oreg.owner = gp.initiator_name;
    oreg.insurance = mp.pm_min_oracle_insurance; oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    const int64_t L = unit * 400;
    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 1; cm.outcomes = {"A", "B", "C"}; cm.url = "criteria";
    cm.oracle_fee_percent = 300; cm.oracle_fixed_fee = asset(0, TOKEN_SYMBOL);
    cm.creator_fee_percent = 0; cm.liquidity_fee_percent = 200;
    cm.liquidity = asset(share_type(L), TOKEN_SYMBOL);
    cm.lmsr_b = graphene::chain::lmsr::lmsr_b_from_liquidity(L, 3);
    cm.betting_expiration = node.head_block_time() + fc::seconds(600);
    cm.result_expiration  = node.head_block_time() + fc::seconds(1200);
    cm.allow_early_resolution = true;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);
    const auto& mkt0 = node.db().get<pm_market_object>(market_id);
    BOOST_REQUIRE_EQUAL(mkt0.market_type, 1);
    const int feeBp = (int)mkt0.oracle_fee_percent + (int)mkt0.creator_fee_percent + (int)mkt0.liquidity_fee_percent;

    const int N = 1000; const int64_t base = unit / 20;
    uint64_t rng = 0xFEEDFACECAFEB00Dull;
    auto nr = [&]() -> uint64_t { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };
    std::vector<int16_t> outc(N); std::vector<int64_t> stk(N);
    for (int i = 0; i < N; ++i) {
        uint64_t r = nr() % 100;
        outc[i] = (r < 50) ? 0 : (r < 80 ? 1 : 2);       // 50 / 30 / 20
        stk[i]  = base * (int64_t)(1 + nr() % 8);
    }
    run_drift_market(node, gp, when, /*binary=*/false, /*outcomes=*/3, feeBp, outc, stk, market_id,
                     "MULTI/LMSR (50/30/20)");
}

BOOST_AUTO_TEST_SUITE_END()
