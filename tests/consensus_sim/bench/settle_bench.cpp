// Settlement cost benchmark (#432).
//
// `settle_market()` pays out EVERY bet row of a market inside the single block where the market
// becomes payable, for one unit of pm_processing_cap_per_block. The per-block row budget that
// bounds this (pm_settle_rows_per_block) has to be set from a measured cost per row, not guessed,
// so this benchmark drives markets of growing row count through a real chain and reports the
// wall-clock of the block that settles them.
//
// It is NOT a ctest: it measures, it does not assert. It also deliberately does not link the
// sanitized consensus_sim_harness library — ASAN/UBSAN inflate timings several-fold, which would
// make the numbers useless for choosing a budget. The same harness sources are compiled into this
// target at the project's normal optimisation level instead.
//
// The number it produces is a LOWER bound on the real per-row cost:
//   * all bets come from a small pool of accounts, so account lookups stay cache-friendly, where a
//     real market has thousands of distinct accounts;
//   * the simulated node runs no account_history plugin, so the virtual operation pushed per row
//     costs a signal dispatch and nothing else. API nodes pay much more.
// Both effects push the real cost up, never down, which is the safe direction for a budget.
//
//   build:  make pm_settle_bench
//   run:    ./tests/consensus_sim/pm_settle_bench [rows ...]     (default 500 2000 8000)

#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"

#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/protocol/pm_operations.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/config.hpp>

#include <fc/crypto/sha256.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace consensus_sim;
using namespace graphene::chain;
using namespace graphene::protocol;

namespace {

account_name_type next_validator(simulated_node& n) { return n.db().get_scheduled_validator(1); }

fc::ecc::private_key key_for(const genesis_params& gp, const account_name_type& v) {
    return (v == gp.initiator_name) ? gp.initiator_key : gp.genesis_witness_key;
}

// Produces one block and returns how long its application took, in milliseconds. Everything the
// benchmark reports is a difference between these numbers, so the signing cost that is included
// here cancels out against the empty-block baseline.
double produce_ms(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    when += fc::seconds(CHAIN_BLOCK_INTERVAL);
    const auto v = next_validator(n);
    const auto t0 = fc::time_point::now();
    n.produce_block(v, key_for(gp, v), when);
    return (double)(fc::time_point::now() - t0).count() / 1000.0;
}

void produce(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    produce_ms(n, gp, when);
}

// `nonce` shifts the expiration by a second per transaction. Batches of identical bets from the
// same account in the same block would otherwise hash to the SAME transaction id and be refused
// as duplicates — the chain has no per-transaction nonce, the expiration is the only free field.
signed_transaction sign_ops(const std::vector<operation>& ops, const fc::ecc::private_key& key,
                            const simulated_node& node, int nonce = 0) {
    signed_transaction tx;
    tx.set_reference_block(node.head_block_id());
    tx.set_expiration(node.head_block_time() + fc::seconds(60 + (nonce % 1800)));
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

bool bring_to_hf14(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when) {
    try {
        produce(node, gp, when);
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
        std::printf("could not reach HF14: %s\n", e.what());
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
    ac.master = single_key_auth(pub); ac.active = single_key_auth(pub);
    ac.regular = single_key_auth(pub); ac.memo_key = pub;
    transfer_operation tr;
    tr.from = gp.initiator_name; tr.to = name; tr.amount = asset(liquid, TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({ac, tr}, gp.initiator_key, node));
    produce(node, gp, when);
}

// Shorten the windows the benchmark has to wait through: the dispute grace has a one-hour
// consensus floor, and retention decides how soon gc_market() (the other unbounded per-block
// sweep) fires after settlement.
void publish_fast_pm_props(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when) {
    chain_properties_pm props{};
    props.pm_dispute_grace_sec           = 3600;
    props.pm_closed_market_retention_sec = 30;
    props.pm_batch_epoch_blocks          = 2;
    props.pm_reveal_window_blocks        = 5;
    versioned_chain_properties_update_operation vp;
    vp.owner = gp.initiator_name; vp.props = props;
    node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
    for (int i = 0; i < 60 &&
         node.db().get_validator_schedule_object().median_props.pm_closed_market_retention_sec != 30u; ++i)
        produce(node, gp, when);
}

struct result {
    int    rows       = 0;
    double baseline   = 0.0;  // median empty block, ms
    double settle_ms  = 0.0;
    double gc_ms      = 0.0;
    int    settle_blk = 0;
};

// One market carrying `rows` bet rows, from creation to settlement to garbage collection.
bool run_one(int rows, result& out) {
    auto gp = make_genesis_params(0x4320u + (uint32_t)rows, 1);
    fc::time_point_sec start(fc::time_point::now());
    fc::time_point_sec hf(CHAIN_HARDFORK_14_TIME);
    if (hf > start) start = hf;
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-settle-bench-" + std::to_string(rows), gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) { std::printf("HF14 unreachable, aborting\n"); return false; }
    publish_fast_pm_props(node, gp, when);

    const auto& mp    = node.db().get_validator_schedule_object().median_props;
    const int64_t bet = mp.pm_min_bet.amount.value;          // cheapest row the chain accepts
    const int64_t unit = mp.pm_min_liquidity.amount.value;

    // A pool of bettors: enough to keep account lookups from collapsing onto one hot object,
    // small enough that funding them does not dominate the run.
    const int ACCOUNTS = 10;
    std::vector<std::string> who;
    std::vector<fc::ecc::private_key> keys;
    for (int i = 0; i < ACCOUNTS; ++i) {
        const std::string name = "bencher" + std::to_string(i);
        auto k = derive_key(name);
        create_and_fund(node, gp, when, name, k, share_type(bet * (2 * rows / ACCOUNTS + 50)));
        who.push_back(name); keys.push_back(k);
    }

    pm_oracle_register_operation oreg;
    oreg.owner = gp.initiator_name; oreg.insurance = mp.pm_min_oracle_insurance;
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, gp.initiator_key, node));
    produce(node, gp, when);

    const int PER_TX = 20, TX_PER_BLOCK = 5;          // 100 rows per block
    const int bet_blocks = rows / (PER_TX * TX_PER_BLOCK) + 4;

    pm_create_market_operation cm;
    cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "bench";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds((bet_blocks + 10) * CHAIN_BLOCK_INTERVAL);
    cm.result_expiration  = cm.betting_expiration + fc::seconds(600);
    cm.dispute_mode = 0;
    // Resolve as soon as betting closes instead of idling through the result window: the
    // benchmark measures the settlement block, not the oracle's calendar.
    cm.allow_early_resolution = true;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node));
    produce(node, gp, when);
    const pm_market_id_type market_id(0);

    int placed = 0, nonce = 0;
    while (placed < rows) {
        for (int t = 0; t < TX_PER_BLOCK && placed < rows; ++t) {
            // Round-robin over the bettors PER TRANSACTION. Deriving this from `placed` would
            // have picked the same account every time (placed advances by a whole batch, and the
            // batch size divides the pool size), which starves one account and funds nine.
            const int a = nonce % ACCOUNTS;
            std::vector<operation> ops;
            for (int i = 0; i < PER_TX && placed < rows; ++i, ++placed) {
                pm_place_bet_operation pb;
                pb.account = who[a]; pb.market_id = 0;
                pb.side = (int8_t)(placed % 2); pb.outcome_index = -1;
                pb.amount = asset(share_type(bet), TOKEN_SYMBOL); pb.mode = 0;
                ops.push_back(pb);
            }
            try {
                node.push_pending_transaction(sign_ops(ops, keys[a], node, ++nonce));
            } catch (const std::exception& e) {
                std::printf("bet batch rejected at row %d: %s\n", placed, e.what());
                return false;
            }
        }
        produce(node, gp, when);
    }

    int actual = 0;
    {
        const auto& bidx = node.db().get_index<pm_bet_index>().indices().get<by_market>();
        for (auto it = bidx.lower_bound(boost::make_tuple(market_id, pm_bet_id_type()));
             it != bidx.end() && it->market == market_id; ++it) ++actual;
    }
    out.rows = actual;

    // Transactions in block N see the timestamp of block N-1, so betting is only provably closed
    // one block after the deadline passes.
    while (node.head_block_time() < cm.betting_expiration) produce(node, gp, when);
    produce(node, gp, when);

    pm_resolve_market_operation rm;
    rm.oracle = gp.initiator_name; rm.market_id = 0; rm.winning_outcome = 0; rm.decision_url = "";
    try {
        node.push_pending_transaction(sign_ops({rm}, gp.initiator_key, node));
    } catch (const std::exception& e) {
        std::printf("resolve rejected: %s\n", e.what());
        return false;
    }
    produce(node, gp, when);

    // Empty-block baseline measured with the whole market still in state, so the difference
    // against it isolates the settlement work rather than the size of the database.
    std::vector<double> idle;
    for (int i = 0; i < 40; ++i) idle.push_back(produce_ms(node, gp, when));
    std::sort(idle.begin(), idle.end());
    out.baseline = idle[idle.size() / 2];

    const int grace_blocks = (int)(mp.pm_dispute_grace_sec / CHAIN_BLOCK_INTERVAL) + 500;
    for (int i = 0; i < grace_blocks; ++i) {
        const double ms = produce_ms(node, gp, when);
        if (ms > out.settle_ms) { out.settle_ms = ms; out.settle_blk = i; }
        if (node.db().get<pm_market_object>(market_id).payout_status == 3) break;
    }
    if (node.db().get<pm_market_object>(market_id).payout_status != 3) {
        std::printf("market did not settle within the grace window\n");
        return false;
    }

    // gc_market() drops the whole object cluster of the market in one block — the same
    // unbounded shape as settlement, so it is worth its own number.
    for (int i = 0; i < 400; ++i) {
        const double ms = produce_ms(node, gp, when);
        if (ms > out.gc_ms) out.gc_ms = ms;
        if (node.db().find<pm_market_object>(market_id) == nullptr) break;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<int> sizes;
    for (int i = 1; i < argc; ++i) sizes.push_back(std::stoi(argv[i]));
    if (sizes.empty()) sizes = {500, 2000, 8000};

    std::printf("%8s %10s %12s %10s %12s %10s\n",
                "rows", "idle ms", "settle ms", "us/row", "gc ms", "gc us/row");
    for (int n : sizes) {
        result r;
        if (!run_one(n, r)) { std::printf("%8d  FAILED\n", n); continue; }
        const double net    = r.settle_ms - r.baseline;
        const double gcnet  = r.gc_ms - r.baseline;
        std::printf("%8d %10.2f %12.2f %10.2f %12.2f %10.2f\n",
                    r.rows, r.baseline, r.settle_ms, r.rows ? net * 1000.0 / r.rows : 0.0,
                    r.gc_ms, r.rows ? gcnet * 1000.0 / r.rows : 0.0);
        std::fflush(stdout);
    }
    return 0;
}
