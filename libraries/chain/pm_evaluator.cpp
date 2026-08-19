#include <graphene/chain/pm_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/chain/pm/lmsr_q96.hpp>
#include <graphene/chain/pm/parimutuel.hpp>
#include <graphene/chain/pm/leverage.hpp>
#include <graphene/protocol/pm_operations.hpp>
#include <graphene/protocol/pm_virtual_operations.hpp>
#include <graphene/protocol/config.hpp>

#include <fc/crypto/sha256.hpp>

#include <graphene/chain/pm_evaluator_detail.hpp>

namespace graphene { namespace chain {

using namespace graphene::protocol;

// ─── Internal helpers now live in pm_process_markets.cpp (namespace pm_detail) ──

using namespace pm_detail;


// ─── 1. pm_oracle_register ───────────────────────────────────────────────────

void pm_oracle_register_evaluator::do_apply(const pm_oracle_register_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);

    const auto& owner = db.get_account(o.owner);
    FC_ASSERT(o.insurance.symbol == TOKEN_SYMBOL, "Insurance must be VIZ");
    FC_ASSERT(o.insurance.amount >= mp.pm_min_oracle_insurance.amount, "Insurance below minimum");
    FC_ASSERT(o.fee_percent <= mp.pm_max_oracle_fee_percent, "fee_percent exceeds maximum");
    FC_ASSERT(o.fixed_fee.symbol == TOKEN_SYMBOL, "fixed_fee must be VIZ");
    FC_ASSERT(o.fixed_fee.amount >= 0, "fixed_fee cannot be negative");
    FC_ASSERT(o.rules_url.size() <= MAX_PM_PROFILE_URL_LEN, "rules_url too long");

    const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
    FC_ASSERT(oidx.find(o.owner) == oidx.end(), "Oracle already registered");

    if (mp.pm_oracle_registration_fee.amount > 0) {
        FC_ASSERT(owner.balance >= mp.pm_oracle_registration_fee, "Insufficient balance for registration fee");
        db.adjust_balance(owner, -mp.pm_oracle_registration_fee);
        db.modify(db.get_dynamic_global_properties(), [&](dynamic_global_property_object& dgp) {
            dgp.committee_fund += mp.pm_oracle_registration_fee; // protocol fee → DAO fund
        });
    }

    FC_ASSERT(owner.balance >= o.insurance, "Insufficient balance for insurance");
    db.adjust_balance(owner, -o.insurance);

    db.create<pm_oracle_object>([&](pm_oracle_object& oracle) {
        oracle.owner          = o.owner;
        oracle.insurance      = o.insurance.amount;
        oracle.fee_percent   = o.fee_percent;
        oracle.fixed_fee      = o.fixed_fee.amount;
        from_string(oracle.rules_url, o.rules_url);
        oracle.active_since   = db.head_block_time();
        oracle.last_active_time = db.head_block_time();
        oracle.auto_accept_creator  = o.auto_accept_creator;
        oracle.auto_accept_resolver = o.auto_accept_resolver;
        oracle.auto_accept          = o.auto_accept;
    });
}

// ─── 2. pm_oracle_update ─────────────────────────────────────────────────────

void pm_oracle_update_evaluator::do_apply(const pm_oracle_update_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);

    const auto& owner = db.get_account(o.owner);
    const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
    auto it = oidx.find(o.owner);
    FC_ASSERT(it != oidx.end(), "Oracle not found");
    const auto& oracle = *it;

    if (o.fee_percent.valid())
        FC_ASSERT(*o.fee_percent <= mp.pm_max_oracle_fee_percent, "fee_percent exceeds maximum");
    if (o.fixed_fee.valid()) {
        FC_ASSERT(o.fixed_fee->symbol == TOKEN_SYMBOL, "fixed_fee must be VIZ");
        FC_ASSERT(o.fixed_fee->amount >= 0, "fixed_fee cannot be negative");
    }
    if (o.rules_url.valid())
        FC_ASSERT(o.rules_url->size() <= MAX_PM_PROFILE_URL_LEN, "rules_url too long");

    if (o.insurance_delta.valid()) {
        const asset& delta = *o.insurance_delta;
        FC_ASSERT(delta.symbol == TOKEN_SYMBOL, "insurance_delta must be VIZ");
        if (delta.amount > 0) {
            FC_ASSERT(owner.balance >= delta, "Insufficient balance for insurance top-up");
            db.adjust_balance(owner, -delta);
        } else if (delta.amount < 0) {
            share_type withdraw = share_type(-delta.amount);
            // #3 (audit 2026-08-12, owner choice A): top-ups are always allowed, but an oracle may not
            // WITHDRAW insurance while it still carries an OPEN OBLIGATION — a market it could still be
            // slashed on. Pulling insurance to the floor right before a deterministic slash made it
            // theater. A market is slashable until it is fully SETTLED (finalized_time set), NOT merely
            // resolved: between resolve and settlement it sits in the dispute grace window, where a
            // dispute can still be filed and lost (owner-found race — checking only ALREADY-open disputes
            // missed the resolve→withdraw→dispute sequence). finalized_time==0 captures exactly that
            // window (status 1 awaiting resolution AND status 3 resolved-but-unsettled). Cheap: the
            // by_oracle_finalized index puts finalized_time==0 first, so we walk only this oracle's small
            // set of still-live markets, not its resolved history. Skip status 0 (pending-accept, not yet
            // an obligation) so a creator cannot grief-lock an oracle's insurance with a sham market.
            const auto& midx = db.get_index<pm_market_index>().indices().get<by_oracle_finalized>();
            for (auto mit = midx.lower_bound(boost::make_tuple(o.owner, time_point_sec()));
                 mit != midx.end() && mit->oracle == o.owner && mit->finalized_time == time_point_sec(); ++mit)
                FC_ASSERT(mit->status < 1,
                          "Cannot withdraw insurance while this oracle has unsettled markets (awaiting "
                          "resolution or still in the dispute window); settle them first (top-ups are "
                          "always allowed)");
            FC_ASSERT(oracle.insurance.value - withdraw.value >= mp.pm_min_oracle_insurance.amount.value,
                      "Withdrawal would push insurance below minimum");
            db.adjust_balance(owner, asset(withdraw, TOKEN_SYMBOL));
        }
    }

    db.modify(oracle, [&](pm_oracle_object& ora) {
        if (o.insurance_delta.valid()) {
            ora.insurance += o.insurance_delta->amount;
        }
        if (o.fee_percent.valid()) ora.fee_percent = *o.fee_percent;
        if (o.fixed_fee.valid())    ora.fixed_fee    = o.fixed_fee->amount;
        if (o.rules_url.valid())    from_string(ora.rules_url, *o.rules_url);
        if (o.auto_accept_creator.valid())  ora.auto_accept_creator  = *o.auto_accept_creator;
        if (o.auto_accept_resolver.valid()) ora.auto_accept_resolver = *o.auto_accept_resolver;
        if (o.auto_accept.valid())          ora.auto_accept          = *o.auto_accept;
        ora.last_active_time = db.head_block_time();
    });
}

// ─── 3. pm_create_market ─────────────────────────────────────────────────────

void pm_create_market_evaluator::do_apply(const pm_create_market_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);
    const auto now = db.head_block_time();

    FC_ASSERT(o.liquidity.symbol == TOKEN_SYMBOL, "Liquidity must be VIZ");
    FC_ASSERT(o.liquidity.amount >= mp.pm_min_liquidity.amount, "Liquidity below minimum");
    FC_ASSERT(o.url.size() <= MAX_PM_MARKET_TITLE_LEN, "Market url too long");
    // Open-ended market: betting_expiration == 0 (epoch) means betting stays open until the
    // oracle resolves. result_expiration then acts purely as the emergency backstop deadline
    // (<= now + pm_max_market_duration, i.e. <= 1 year): if the oracle never resolves by then,
    // process_pm_markets() refunds every bet and slashes the oracle insurance (missed-resolution
    // path). Requires allow_early_resolution so the oracle can resolve at any time (with
    // betting_expiration == 0, now >= betting_expiration is always true, so the early-resolution
    // branch in pm_resolve_market is the only way to resolve before the backstop fires).
    if (o.betting_expiration == time_point_sec()) {
        FC_ASSERT(o.allow_early_resolution,
                  "open-ended market (betting_expiration=0) requires allow_early_resolution");
        FC_ASSERT(o.result_expiration > now, "result_expiration must be in the future");
        FC_ASSERT(o.result_expiration <= now + fc::seconds(mp.pm_max_market_duration),
                  "Market duration exceeds maximum");
    } else {
        FC_ASSERT(o.betting_expiration > now, "betting_expiration must be in the future");
        FC_ASSERT(o.result_expiration > o.betting_expiration, "result_expiration must be after betting_expiration");
        FC_ASSERT(o.result_expiration <= now + fc::seconds(mp.pm_max_market_duration),
                  "Market duration exceeds maximum");
    }

    // Fee solvency (sum of bp fees <= 100%) is enforced statically in validate(). The oracle terms
    // in this op are only the creator's OFFER CEILING; the governed cap (pm_max_oracle_fee_percent)
    // is checked against the oracle's actual quote at accept (or, for a self-oracle, just below).

    if (o.market_type == 0) {
        FC_ASSERT(o.outcomes.size() == 2, "Binary market must have exactly 2 outcomes");
    } else {
        FC_ASSERT(o.market_type == 1, "Invalid market_type");
        FC_ASSERT(o.outcomes.size() >= 3 && (int)o.outcomes.size() <= mp.pm_max_outcomes,
                  "Multi-outcome count out of range");
    }
    for (const auto& label : o.outcomes)
        FC_ASSERT(label.size() <= MAX_PM_OUTCOME_LABEL_LEN, "Outcome label too long");

    if (o.dispute_mode == 1) {
        FC_ASSERT(o.dispute_resolver.size() > 0, "dispute_resolver required");
        FC_ASSERT(o.dispute_resolver != o.oracle, "dispute_resolver must differ from oracle");
        FC_ASSERT(o.dispute_resolver != o.creator, "dispute_resolver must differ from creator");
        db.get_account(o.dispute_resolver);
    }

    const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
    auto oracle_it = oidx.find(o.oracle);
    bool self_oracle = (o.oracle == o.creator);
    if (!self_oracle) {
        FC_ASSERT(oracle_it != oidx.end(), "Oracle not registered");
        FC_ASSERT(oracle_it->banned_until < now, "Oracle is banned");
        // The oracle's actual fee is quoted (<= this offer) at accept; nothing to bind here.
    }

    const auto& creator = db.get_account(o.creator);

    // Creator ban (scenario #14): an account-mode dispute resolver may bar a creator from
    // opening new markets for a period (or permanently). Honour an unexpired ban here.
    {
        const auto& cbidx = db.get_index<pm_creator_ban_index>().indices().get<by_ban_account>();
        auto cb = cbidx.find(o.creator);
        FC_ASSERT(cb == cbidx.end() || cb->banned_until < now, "Creator is banned from creating markets");
    }

    share_type market_fee = mp.pm_market_creation_fee.amount;
    share_type total_need = share_type(market_fee.value + o.liquidity.amount.value);
    FC_ASSERT(creator.balance.amount >= total_need, "Insufficient balance");

    if (market_fee.value > 0) {
        db.adjust_balance(creator, -mp.pm_market_creation_fee);
        db.modify(db.get_dynamic_global_properties(), [&](dynamic_global_property_object& dgp) {
            dgp.committee_fund += mp.pm_market_creation_fee; // protocol fee → DAO fund
        });
    }
    db.adjust_balance(creator, -o.liquidity);
    db.pm_adjust_frozen(o.creator, 0, o.liquidity.amount); // LOCK: creator liquidity → live market

    share_type lmsr_b_val = 0;
    if (o.market_type == 1) {
        int64_t expected_b = lmsr::lmsr_b_from_liquidity(o.liquidity.amount.value, (int)o.outcomes.size());
        FC_ASSERT(o.lmsr_b == expected_b, "lmsr_b mismatch");
        lmsr_b_val = expected_b;
    }

    bool is_self = self_oracle || (oracle_it == oidx.end());
    // Self-oracle auto-accepts at creation, so its oracle fee is final now and must satisfy the
    // governed cap immediately. (External markets defer this check to accept.)
    if (is_self)
        FC_ASSERT(o.oracle_fee_percent <= mp.pm_max_oracle_fee_percent, "oracle_fee_percent exceeds cap");

    // Auto-accept (anti-collusion): an external oracle that opted into auto-accept takes the market
    // live at creation ONLY if it matches the oracle's pre-set policy — allowed creator + allowed
    // resolver (empty resolver ⇒ committee-mode only), and the oracle's own profile fee terms within
    // the creator's offered ceiling + the governed cap. Otherwise the market stays pending (status 0)
    // for the oracle to review manually. This stops a creator from slipping in a sham resolver.
    bool auto_accept = false;
    uint16_t aa_fee = 0; share_type aa_fixed = 0;
    if (!is_self && oracle_it != oidx.end()) {
        const auto& ora = *oracle_it;
        const bool creator_ok  = (ora.auto_accept_creator == account_name_type())
                                 || (ora.auto_accept_creator == o.creator);
        const bool resolver_ok = (ora.auto_accept_resolver == account_name_type())
                                     ? (o.dispute_mode == 0)
                                     : (o.dispute_mode == 1 && o.dispute_resolver == ora.auto_accept_resolver);
        const bool terms_ok    = (ora.fee_percent <= o.oracle_fee_percent)
                                 && (ora.fixed_fee <= o.oracle_fixed_fee.amount)
                                 && (ora.fee_percent <= mp.pm_max_oracle_fee_percent);
        const bool eligible    = (ora.insurance >= mp.pm_min_oracle_insurance.amount)
                                 && (ora.banned_until < now);
        auto_accept = ora.auto_accept && creator_ok && resolver_ok && terms_ok && eligible;
        aa_fee = ora.fee_percent; aa_fixed = ora.fixed_fee;
    }
    const bool active_at_create = is_self || auto_accept;

    const auto& mkt = db.create<pm_market_object>([&](pm_market_object& m) {
        m.creator               = o.creator;
        m.oracle                = o.oracle;
        m.market_type           = o.market_type;
        m.outcome_count         = (uint8_t)o.outcomes.size();
        from_string(m.url, o.url);
        // o.metadata is intentionally NOT persisted in consensus state — the
        // prediction_market_api plugin ingests it off-chain (prunable). See pm_objects.hpp.
        m.status                = active_at_create ? 1 : 0;
        m.created_time          = now;
        // Pending markets get an acceptance deadline; markets that are live at creation (self-oracle /
        // auto-accept) never enter the pending sweep, so leave it at 0.
        m.accept_deadline       = active_at_create
                                    ? time_point_sec()
                                    : time_point_sec(now + fc::seconds(mp.pm_oracle_accept_window_sec));
        m.betting_expiration    = o.betting_expiration;
        m.result_expiration     = o.result_expiration;
        m.resolved_outcome      = -1;
        m.lmsr_b                = lmsr_b_val;
        m.lmsr_subsidy          = o.liquidity.amount;
        m.liquidity_sum         = o.liquidity.amount;
        // Oracle terms = creator's offer ceiling (final immediately for a self-oracle; an external
        // oracle narrows these down at accept). creator/liquidity fees are final at creation.
        m.oracle_fee_percent    = auto_accept ? aa_fee   : o.oracle_fee_percent;
        m.oracle_fixed_fee      = auto_accept ? aa_fixed : o.oracle_fixed_fee.amount;
        m.creator_fee_percent   = o.creator_fee_percent;
        m.liquidity_fee_percent = o.liquidity_fee_percent;
        m.time_penalty_type     = o.time_penalty_type;
        m.time_penalty_value    = o.time_penalty_value;
        m.penalty_curve_type    = o.penalty_curve_type;
        m.allow_early_resolution = o.allow_early_resolution;
        m.allow_cancellation    = o.allow_cancellation;
        m.allow_batch           = o.allow_batch && mp.pm_commit_reveal_enabled;
        m.allow_instant_bet     = (o.market_type == 1) ? true : o.allow_instant_bet;
        m.endogeneity_tier      = o.endogeneity_tier;
        m.dispute_mode          = o.dispute_mode;
        m.dispute_resolver      = o.dispute_resolver;
        m.dispute_penalty_percent = o.dispute_penalty_percent;

        if (o.market_type == 0) {
            m.reserve_a = share_type(o.liquidity.amount.value / 2);
            m.reserve_b = share_type(o.liquidity.amount.value - m.reserve_a.value);
            m.k = fc::uint128_t((uint64_t)m.reserve_a.value) * fc::uint128_t((uint64_t)m.reserve_b.value);
        }
    });

    for (uint8_t i = 0; i < (uint8_t)o.outcomes.size(); ++i) {
        db.create<pm_outcome_object>([&](pm_outcome_object& out) {
            out.market        = mkt.id;
            out.outcome_index = i;
            from_string(out.label, o.outcomes[i]);
        });
    }

    db.create<pm_liquidity_object>([&](pm_liquidity_object& lp) {
        lp.market       = mkt.id;
        lp.provider     = o.creator;
        lp.amount       = o.liquidity.amount;
        lp.deposit_time = now;
        lp.status       = 0;
        if (o.market_type == 1) lp.b_share = lmsr_b_val;
    });

    if (!is_self && !auto_accept && oracle_it != oidx.end()) {
        db.modify(*oracle_it, [&](pm_oracle_object& ora) { ora.last_active_time = now; });
    }

    if (is_self) {
        maybe_allocate_lazy(db, mkt); // self-oracle markets are active at creation
        // Auto-accepted: announce the launch + frozen terms so history parsers see it go live.
        db.push_virtual_operation(pm_market_accepted_operation(
            o.oracle, o.creator, mkt.id._id,
            o.oracle_fee_percent, asset(o.oracle_fixed_fee.amount, TOKEN_SYMBOL), true));
    } else if (auto_accept && oracle_it != oidx.end()) {
        // External oracle auto-accepted the market at creation under its policy. Same effect as a manual
        // accept: count it, allocate lazy-pool subsidy, and emit pm_market_accepted with the frozen quote.
        db.modify(*oracle_it, [&](pm_oracle_object& ora) {
            ora.markets_accepted++;
            ora.last_active_time = now;
        });
        maybe_allocate_lazy(db, mkt);
        db.push_virtual_operation(pm_market_accepted_operation(
            o.oracle, o.creator, mkt.id._id,
            aa_fee, asset(aa_fixed, TOKEN_SYMBOL), false));
    }

    // A market that is live at creation (self-oracle or auto-accepted) enters the active set now.
    if (active_at_create) pm_oracle_inc_active(db, o.oracle);
}

// ─── 4. pm_oracle_accept_market ──────────────────────────────────────────────

void pm_oracle_accept_market_evaluator::do_apply(const pm_oracle_accept_market_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");

    const auto& mp = median(db);
    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.oracle == o.oracle, "Not the market oracle");
    FC_ASSERT(mkt.status == 0, "Market not pending acceptance");

    if (o.accept) {
        // The oracle quotes its actual terms now. They must not exceed the creator's offer ceiling
        // (the values currently on the market) nor the governed cap; the quote is then frozen and
        // the market goes live. The worst-case solvency was already validated at creation, and the
        // quote only lowers the oracle fee, so the sum stays <= 100%.
        FC_ASSERT(o.oracle_fee_percent <= mkt.oracle_fee_percent,
                  "oracle_fee_percent exceeds the creator's offer");
        FC_ASSERT(o.oracle_fixed_fee.amount <= mkt.oracle_fixed_fee,
                  "oracle_fixed_fee exceeds the creator's offer");
        FC_ASSERT(o.oracle_fee_percent <= mp.pm_max_oracle_fee_percent, "oracle_fee_percent exceeds cap");

        db.modify(mkt, [&](pm_market_object& m) {
            m.status           = 1;
            m.oracle_fee_percent = o.oracle_fee_percent;     // freeze the agreed terms
            m.oracle_fixed_fee   = o.oracle_fixed_fee.amount;
        });
        const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
        auto it = oidx.find(o.oracle);
        if (it != oidx.end())
            db.modify(*it, [&](pm_oracle_object& ora) {
                ora.markets_accepted++;
                ora.active_markets++;                 // market goes live (status 0 → 1)
                ora.last_active_time = db.head_block_time();
            });
        maybe_allocate_lazy(db, mkt); // pool subsidy on activation
        db.push_virtual_operation(pm_market_accepted_operation(
            o.oracle, mkt.creator, mkt.id._id,
            o.oracle_fee_percent, o.oracle_fixed_fee, false));
    } else {
        // Refund the creator's seed exactly once: return_liquidity already credits the seed
        // LP object (provider == creator, created in pm_create_market). A pending market
        // cannot have received pm_add_liquidity (that requires status==1), so liquidity_sum
        // equals that single LP amount — crediting liquidity_sum here too would emit tokens.
        return_liquidity(db, mkt);
        db.modify(mkt, [&](pm_market_object& m) { m.status = -1; m.finalized_time = db.head_block_time(); });
    }
}

// ─── 5. pm_place_bet ─────────────────────────────────────────────────────────

void pm_place_bet_evaluator::do_apply(const pm_place_bet_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();
    const auto& mp = median(db); // for B9 late-bet penalty scale (pm_max_time_penalty)

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.status == 1, "Market not active");
    FC_ASSERT(mkt.betting_expiration == time_point_sec() || now < mkt.betting_expiration, "Betting period ended");

    // Betting-mode gate (scenario #55): a market may disable instant bets (allow_instant_bet=false)
    // to force the front-run-resistant batch / commit-reveal flow. mode 0 = instant, mode 1 = batch.
    if (o.mode == 0) FC_ASSERT(mkt.allow_instant_bet, "Instant betting is disabled for this market");
    else             FC_ASSERT(mkt.allow_batch,       "Batch betting is not enabled for this market");

    FC_ASSERT(o.amount.symbol == TOKEN_SYMBOL, "Amount must be VIZ");
    FC_ASSERT(o.amount.amount > 0, "Amount must be positive");

    // #432 fix A — anti-dust floor on BOTH pm_place_bet paths. Each call creates a brand-new
    // pm_bet_object, and settlement has to touch every row of the market; the only floor that
    // existed was pm_min_batch_bet on the COMMIT path, so instant bets (and queued batch bets,
    // which run through this same evaluator) could be placed at 1 raw = 0.001 VIZ and multiply
    // rows almost for free. Charging pm_min_bet / pm_min_batch_bet per row lifts the price of
    // row-spam by ~3 orders of magnitude. It bounds the COST of rows, not their NUMBER — the
    // number is bounded by the incremental settlement sweep (fix D). See
    // docs/prediction-markets/settlement-work-bounds.md.
    if (o.mode == 0)
        FC_ASSERT(o.amount.amount >= mp.pm_min_bet.amount,
                  "Bet below the minimum instant bet (pm_min_bet)");
    else
        FC_ASSERT(o.amount.amount >= mp.pm_min_batch_bet.amount,
                  "Bet below the minimum batch bet (pm_min_batch_bet)");

    const auto& acct = db.get_account(o.account);
    FC_ASSERT(acct.balance >= o.amount, "Insufficient balance");

    if (mkt.market_type == 0) {
        FC_ASSERT(o.side == 0 || o.side == 1, "Binary market requires side 0 or 1");
        FC_ASSERT(o.outcome_index == -1, "Binary market does not use outcome_index");

        // Case A (spec §5): an opposing bet moves price against leveraged positions on the
        // OTHER side — cascade-liquidate them at PRE-bet reserves so the pool stays whole.
        // NOT gated by pm_leverage_enabled: that flag only blocks NEW opens (pm_leverage_open).
        // Once a loan is out, governance toggling leverage off must never strip the pool's
        // liquidation protection. No-op (cheap index probe) when the market has no positions.
        cascade_liquidate(db, mkt.id, (int16_t)(1 - o.side), 0);

        share_type delta = o.amount.amount;
        share_type reserve_in  = (o.side == 0) ? mkt.reserve_a : mkt.reserve_b;
        share_type reserve_out = (o.side == 0) ? mkt.reserve_b : mkt.reserve_a;

        fc::uint128_t denom = fc::uint128_t((uint64_t)(reserve_in.value + delta.value));
        FC_ASSERT(denom.lo > 0 || denom.hi > 0, "CPMM overflow");
        fc::uint128_t new_out_u128 = mkt.k / denom;
        share_type new_reserve_out = share_type((int64_t)new_out_u128.lo);
        share_type tokens_out = share_type(reserve_out.value - new_reserve_out.value);
        FC_ASSERT(tokens_out.value > 0, "Zero tokens out");
        FC_ASSERT(tokens_out.value >= o.min_tokens, "Slippage: tokens below min_tokens");

        db.adjust_balance(acct, -o.amount);
        db.pm_adjust_frozen(o.account, 1, o.amount.amount); // LOCK: stake → open bet (binary/CPMM)
        db.modify(mkt, [&](pm_market_object& m) {
            if (o.side == 0) {
                m.reserve_a += delta;  m.reserve_b = new_reserve_out;  m.a_bets_sum += delta;
            } else {
                m.reserve_b += delta;  m.reserve_a = new_reserve_out;  m.b_bets_sum += delta;
            }
            m.bets_sum += delta;
        });

        db.create<pm_bet_object>([&](pm_bet_object& bet) {
            bet.market       = mkt.id;
            bet.account      = o.account;
            bet.side         = o.side;
            bet.outcome_index = -1;
            bet.amount       = delta;
            bet.weight       = tokens_out;
            bet.mode         = o.mode;
            bet.status       = 0;
            bet.created_time = now;
            bet.entry_liquidity = mkt.liquidity_sum; // #1-C: depth at entry for depth-neutral cancel
            bet.time_penalty = compute_time_penalty(mkt, now, mp.pm_max_time_penalty); // B9
        });

    } else {
        FC_ASSERT(o.side == -1, "Multi market does not use side");
        FC_ASSERT(o.outcome_index >= 0 && o.outcome_index < (int16_t)mkt.outcome_count,
                  "outcome_index out of range");

        const auto& oidx_out = db.get_index<pm_outcome_index>().indices().get<by_market_outcome>();
        std::vector<int64_t> q;
        q.reserve(mkt.outcome_count);
        for (uint8_t i = 0; i < mkt.outcome_count; ++i) {
            auto it = oidx_out.lower_bound(boost::make_tuple(mkt.id, i));
            FC_ASSERT(it != oidx_out.end() && it->market == mkt.id && it->outcome_index == i, "Outcome missing");
            q.push_back(it->q.value);
        }

        int64_t tokens = lmsr::lmsr_tokens_for_amount(q, mkt.lmsr_b.value, (int)o.outcome_index, o.amount.amount.value);
        FC_ASSERT(tokens > 0, "Zero LMSR tokens");
        FC_ASSERT(tokens >= o.min_tokens, "Slippage: LMSR tokens below min_tokens");

        db.adjust_balance(acct, -o.amount);
        db.pm_adjust_frozen(o.account, 1, o.amount.amount); // LOCK: stake → open bet (LMSR)
        db.modify(mkt, [&](pm_market_object& m) { m.bets_sum += o.amount.amount; });

        auto it = oidx_out.lower_bound(boost::make_tuple(mkt.id, (uint8_t)o.outcome_index));
        db.modify(*it, [&](pm_outcome_object& out) {
            out.q        += tokens;
            out.bets_sum += o.amount.amount;
            out.bets_count++;
        });

        db.create<pm_bet_object>([&](pm_bet_object& bet) {
            bet.market        = mkt.id;
            bet.account       = o.account;
            bet.side          = -1;
            bet.outcome_index = o.outcome_index;
            bet.amount        = o.amount.amount;
            bet.weight        = tokens;
            bet.mode          = o.mode;
            bet.status        = 0;
            bet.created_time  = now;
            bet.time_penalty  = compute_time_penalty(mkt, now, mp.pm_max_time_penalty); // B9
        });
    }
}

// ─── 6. pm_commit_bet ────────────────────────────────────────────────────────

void pm_commit_bet_evaluator::do_apply(const pm_commit_bet_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);
    FC_ASSERT(mp.pm_commit_reveal_enabled, "Commit-reveal not enabled");
    const auto now = db.head_block_time();

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.status == 1, "Market not active");
    FC_ASSERT(mkt.allow_batch, "Batch mode not enabled");
    FC_ASSERT(mkt.betting_expiration == time_point_sec() || now < mkt.betting_expiration, "Betting period ended");

    FC_ASSERT(o.escrow_amount.symbol == TOKEN_SYMBOL, "Escrow must be VIZ");
    FC_ASSERT(o.escrow_amount.amount >= mp.pm_min_batch_bet.amount, "Escrow below minimum batch bet");
    FC_ASSERT(o.no_reveal_fee_percent == mp.pm_commit_no_reveal_penalty_percent,
              "no_reveal_fee_percent must equal current consensus value");
    // M4: cap the per-market unrevealed commit backlog (O(1) counter, decremented on reveal/forfeit).
    FC_ASSERT(mkt.open_commits < MAX_PM_OPEN_COMMITS_PER_MARKET, "Open commit cap reached for this market");

    const auto& acct = db.get_account(o.account);
    FC_ASSERT(acct.balance >= o.escrow_amount, "Insufficient balance for escrow");
    db.adjust_balance(acct, -o.escrow_amount);

    uint32_t epoch_blocks  = mp.pm_batch_epoch_blocks;
    uint32_t reveal_window = mp.pm_reveal_window_blocks;
    uint32_t cur_block     = db.head_block_num();
    uint32_t epoch_end     = ((cur_block / epoch_blocks) + 1) * epoch_blocks;
    time_point_sec reveal_dl = now + fc::seconds((int64_t)(epoch_end - cur_block + reveal_window) * CHAIN_BLOCK_INTERVAL);

    db.create<pm_commit_object>([&](pm_commit_object& c) {
        c.market                = mkt.id;
        c.account               = o.account;
        c.commitment            = o.commitment;
        c.escrow_amount         = o.escrow_amount.amount;
        c.no_reveal_fee_percent = o.no_reveal_fee_percent;
        c.commit_time           = now;
        c.reveal_deadline       = reveal_dl;
        c.status                = 0;
    });
    db.modify(mkt, [](pm_market_object& m) { m.open_commits++; }); // M4: paired with reveal/forfeit dec
}

// ─── 7. pm_reveal_bet ────────────────────────────────────────────────────────

void pm_reveal_bet_evaluator::do_apply(const pm_reveal_bet_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();

    const auto& commit = db.get<pm_commit_object, by_id>(pm_commit_id_type(o.commit_id));
    FC_ASSERT(commit.account == o.account, "Not your commitment");
    FC_ASSERT(commit.status == 0, "Already revealed or forfeited");
    FC_ASSERT(now <= commit.reveal_deadline, "Reveal window passed");

    FC_ASSERT(o.amount.symbol == TOKEN_SYMBOL, "Amount must be VIZ");
    FC_ASSERT(o.amount.amount > 0 && o.amount.amount <= commit.escrow_amount, "Invalid reveal amount");

    FC_ASSERT(verify_commit(commit, o.side, o.outcome_index, o.amount.amount, o.min_tokens, o.salt),
              "Commitment hash mismatch");

    const auto& mkt = get_market(db, commit.market._id);
    FC_ASSERT(mkt.status == 1, "Market not active");
    FC_ASSERT(mkt.betting_expiration == time_point_sec() || now < mkt.betting_expiration, "Betting period ended");

    share_type surplus = share_type(commit.escrow_amount.value - o.amount.amount.value);
    if (surplus.value > 0)
        db.adjust_balance(db.get_account(o.account), asset(surplus, TOKEN_SYMBOL));

    db.modify(commit, [](pm_commit_object& c) { c.status = 1; });
    // M4: the commit left the unrevealed backlog (clamp: pre-M4 snapshots import the counter as 0).
    db.modify(mkt, [](pm_market_object& m) { if (m.open_commits > 0) m.open_commits--; });

    db.create<pm_bet_object>([&](pm_bet_object& bet) {
        bet.market        = commit.market;
        bet.account       = o.account;
        bet.side          = o.side;
        bet.outcome_index = o.outcome_index;
        bet.amount        = o.amount.amount;
        bet.weight        = 0;
        bet.min_tokens    = o.min_tokens;
        bet.mode          = 1;
        bet.epoch         = mkt.current_epoch;
        bet.status        = 5; // queued
        bet.created_time  = now;
        // B9: penalise by the blind COMMIT time, not the reveal — honest commit-reveal bettors
        // are not punished for revealing late within the window.
        bet.time_penalty  = compute_time_penalty(mkt, commit.commit_time, median(db).pm_max_time_penalty);
    });
    // LOCK: revealed stake becomes a queued bet. The escrow left balance at commit but is not a
    // tracked category; count it as frozen from the moment it materializes as a bet object.
    db.pm_adjust_frozen(o.account, 1, o.amount.amount);
}

// ─── 8. pm_cancel_bet ────────────────────────────────────────────────────────

void pm_cancel_bet_evaluator::do_apply(const pm_cancel_bet_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");

    const auto& bet = db.get<pm_bet_object, by_id>(pm_bet_id_type(o.bet_id));
    FC_ASSERT(bet.account == o.account, "Not your bet");
    FC_ASSERT(bet.status == 0, "Bet not active");

    const auto& mkt = db.get<pm_market_object, by_id>(bet.market);
    FC_ASSERT(mkt.allow_cancellation, "Cancellation not allowed");
    FC_ASSERT(mkt.status == 1, "Market not active");
    // B7: cancellation is a pre-close action only. Once betting closes the outcome starts
    // becoming known, so a late cancel would be a free option to unwind a losing bet.
    // Open-ended markets (no betting deadline) stay cancellable while active.
    const auto now = db.head_block_time();
    FC_ASSERT(mkt.betting_expiration == time_point_sec() || now < mkt.betting_expiration,
              "Cannot cancel bets after betting closes");

    // Binary markets re-price the refund on the current curve (F2, below); type-1/LMSR stays
    // nominal. min_return is checked once the final refund is known (after the branch).
    share_type refund = bet.amount;

    if (mkt.market_type == 0) {
        // F2: a cancel is the MIRROR OF THE BUY — sell bet.weight back at the CURRENT reserves so
        // k stays invariant. The old nominal reversal (refund bet.amount, hand back bet.weight at
        // today's price) corrupted k on any curve that had moved since the bet — and an opposing bet
        // is enough to move it — leaving the retained position an inflated claim weight (up to 103×)
        // that settles as real money, plus a free option to unwind a losing bet at 100%. Curve-priced:
        //   new_reserve_in = k / (reserve_out + weight);  refund = reserve_in − new_reserve_in.
        // new_reserve_in < reserve_in always (weight > 0 ⇒ larger denominator), so refund > 0 and no
        // reserve can underflow — this subsumes the B6 guard. min_return (checked below) protects the
        // bettor from an adverse move. The gap between the original stake and the curve-priced refund
        // routes to forfeit_pool (signed): a loss accrues to the rest of the market, a gain is charged
        // to LP principal at settlement via the F1 shortfall path. (PR #124 finding 2.)
        const bool a = (bet.side == 0);
        share_type reserve_in  = a ? mkt.reserve_a : mkt.reserve_b;
        share_type reserve_out = a ? mkt.reserve_b : mkt.reserve_a;
        fc::uint128_t denom = fc::uint128_t((uint64_t)(reserve_out.value + bet.weight.value));
        FC_ASSERT(denom.lo > 0 || denom.hi > 0, "CPMM overflow");
        share_type new_reserve_in = share_type((int64_t)(mkt.k / denom).lo);
        const int64_t curve_refund = reserve_in.value - new_reserve_in.value; // > 0 by construction
        FC_ASSERT(curve_refund >= 0, "curve-priced refund underflow");
        // #1-C (audit 2026-08-12): re-price the capped/tail split at the bet's ENTRY depth so an early
        // exit is never rewarded for depth the bettor inflated themselves (self-liquidity tail). Scale
        // both reserves by entry_liquidity/liquidity_sum (same ratio, entry k) and mirror-of-buy on
        // those: bets keep k invariant, liquidity ops scale k by f² and liquidity_sum by f, so
        // sqrt(k_entry/k_now) == L_entry/L_now EXACTLY → deterministic, sqrt-free. CLAMP to the real
        // curve_refund: normalization may only REDUCE the payout, never raise it (kills the inflation
        // vector without opening a shrink-side one). Only the payout split uses this; the REAL reserve
        // mutation below is unchanged (k invariant). See pm-fix-1c-depth-normalized-cancel.md.
        int64_t curve_refund_pricing = curve_refund;
        {
            const int64_t Lentry = bet.entry_liquidity.value;
            const int64_t Lnow   = mkt.liquidity_sum.value;
            if (Lentry > 0 && Lnow > 0 && Lnow != Lentry) {
                const fc::uint128_t rin_n  = fc::uint128_t((uint64_t)reserve_in.value)  * fc::uint128_t((uint64_t)Lentry) / fc::uint128_t((uint64_t)Lnow);
                const fc::uint128_t rout_n = fc::uint128_t((uint64_t)reserve_out.value) * fc::uint128_t((uint64_t)Lentry) / fc::uint128_t((uint64_t)Lnow);
                const fc::uint128_t denom_n = rout_n + fc::uint128_t((uint64_t)bet.weight.value);
                if (rin_n.hi == 0 && rout_n.hi == 0 && rin_n.lo > 0 && (denom_n.hi > 0 || denom_n.lo > 0)) {
                    const int64_t new_rin_n = (int64_t)(rin_n * rout_n / denom_n).lo;
                    int64_t cref_n = (int64_t)rin_n.lo - new_rin_n;
                    if (cref_n < 0) cref_n = 0;
                    if (cref_n < curve_refund_pricing) curve_refund_pricing = cref_n; // clamp: only reduce
                }
            }
        }
        // F1/#300: pay at most the stake (min(curve_refund, stake)) — a cancel cuts losses or breaks
        // even but never realizes curve PROFIT against LP depth. The profit tail becomes an OUTCOME-
        // CONTINGENT deferred claim, paid from the bounded early-exit bucket at settlement (winning side
        // only). residual = stake − paid ≥ 0, so forfeit_pool never goes negative on a cancel.
        const int64_t capped = curve_refund_pricing < bet.amount.value ? curve_refund_pricing : bet.amount.value;
        const int64_t tail   = curve_refund_pricing - capped; // ≥ 0: curve profit, deferred as a claim
        refund = share_type(capped);
        const int64_t residual = bet.amount.value - capped; // ≥ 0 → forfeit_pool
        db.modify(mkt, [&](pm_market_object& m) {
            if (a) { m.reserve_a = new_reserve_in; m.reserve_b += bet.weight; m.a_bets_sum -= bet.amount; }
            else   { m.reserve_b = new_reserve_in; m.reserve_a += bet.weight; m.b_bets_sum -= bet.amount; }
            m.bets_sum     -= bet.amount;
            m.forfeit_pool += residual; // stake = refund + residual; k unchanged (mirror of buy)
        });
        if (tail > 0 && mkt.deferred_claim_count < MAX_PM_DEFERRED_CLAIMS_PER_MARKET) {
            // #349: skip once the per-market cap is hit — the tail stays in the curve and pays 0 at
            // settlement (like bucket-exhaustion), keeping settle_market's claim loop bounded.
            db.create<pm_deferred_claim_object>([&](pm_deferred_claim_object& c) {
                c.market = mkt.id; c.account = bet.account; c.kind = 0;
                c.outcome_index = (uint8_t)bet.side; c.claim_amount = share_type(tail); c.exit_time = now;
            });
            db.modify(mkt, [](pm_market_object& m) { m.deferred_claim_count++; });
        }
    } else {
        const auto& oidx_out = db.get_index<pm_outcome_index>().indices().get<by_market_outcome>();
        auto it = oidx_out.lower_bound(boost::make_tuple(mkt.id, (uint8_t)bet.outcome_index));
        if (it != oidx_out.end() && it->market == mkt.id && it->outcome_index == (uint8_t)bet.outcome_index) {
            db.modify(*it, [&](pm_outcome_object& out) {
                out.q        -= bet.weight;
                out.bets_sum -= bet.amount;
                if (out.bets_count > 0) out.bets_count--;
            });
        }
        db.modify(mkt, [&](pm_market_object& m) { m.bets_sum -= bet.amount; });
    }

    FC_ASSERT(refund.value >= o.min_return, "Refund below min_return");
    db.adjust_balance(db.get_account(o.account), asset(refund, TOKEN_SYMBOL));
    db.pm_adjust_frozen(o.account, 1, -bet.amount); // UNLOCK: original stake leaves the bet set on cancel
    db.modify(bet, [](pm_bet_object& b) { b.status = 1; });

    // Case B (spec §5): the cancel-bettor was paid first at current reserves; now cascade-
    // liquidate same-side leveraged positions the cancel pushed below threshold (bad debt,
    // if any, is borne by the pool — the initiator is never penalized). NOT gated by
    // pm_leverage_enabled — existing positions stay protected even if new leverage was disabled.
    if (mkt.market_type == 0 && bet.side >= 0)
        cascade_liquidate(db, mkt.id, (int16_t)bet.side, 1);
}

// ─── 9. pm_add_liquidity ─────────────────────────────────────────────────────

void pm_add_liquidity_evaluator::do_apply(const pm_add_liquidity_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.status == 1, "Market not active");
    FC_ASSERT(mkt.betting_expiration == time_point_sec() || now < mkt.betting_expiration, "Cannot add liquidity after betting ends");
    FC_ASSERT(o.amount.symbol == TOKEN_SYMBOL, "Amount must be VIZ");
    FC_ASSERT(o.amount.amount > 0, "Amount must be positive");
    // #432 fix A (owner decision q#661=A): every call mints a NEW pm_liquidity_object — deposits are
    // not aggregated per provider — and settle_liquidity walks all of them, twice. With no floor
    // that made liquidity the fourth way to mint settlement rows at 1 raw apiece, alongside the
    // three bet paths. Floor it at pm_min_liquidity, the same minimum as opening a market, so the
    // ticket for putting up liquidity does not depend on whether you create the market or top it up
    // later. See docs/prediction-markets/settlement-work-bounds.md.
    {
        const auto& mp = median(db);
        FC_ASSERT(o.amount.amount >= mp.pm_min_liquidity.amount,
                  "Liquidity below the minimum (pm_min_liquidity); the floor applies to topping up a market too");
    }

    const auto& provider = db.get_account(o.provider);
    FC_ASSERT(provider.balance >= o.amount, "Insufficient balance");
    db.adjust_balance(provider, -o.amount);
    db.pm_adjust_frozen(o.provider, 0, o.amount.amount); // LOCK: LP liquidity → live market

    share_type b_share = 0;
    if (mkt.market_type == 1 && mkt.liquidity_sum.value > 0) {
        b_share = share_type((int64_t)(fc::uint128_t((uint64_t)mkt.lmsr_b.value) *
                  fc::uint128_t((uint64_t)o.amount.amount.value) /
                  fc::uint128_t((uint64_t)mkt.liquidity_sum.value)).lo);
    }

    db.modify(mkt, [&](pm_market_object& m) {
        if (m.market_type == 1) {
            m.liquidity_sum += o.amount.amount;
            m.lmsr_b       += b_share;
            m.lmsr_subsidy += o.amount.amount;
        } else {
            // CPMM: price-neutral add. Scale both reserves by (L + amount) / L so the
            // reserve ratio (the odds) is unchanged and only depth grows with capital.
            // A round-trip (add then withdraw the same amount) restores the reserves.
            const int64_t L = m.liquidity_sum.value; // capital BEFORE this deposit
            if (L > 0) {
                const fc::uint128_t num((uint64_t)(L + o.amount.amount.value));
                const fc::uint128_t den((uint64_t)L);
                m.reserve_a = share_type((int64_t)(fc::uint128_t((uint64_t)m.reserve_a.value) * num / den).lo);
                m.reserve_b = share_type((int64_t)(fc::uint128_t((uint64_t)m.reserve_b.value) * num / den).lo);
            } else {
                // No prior capital: seed a balanced curve (matches market genesis).
                const share_type half = share_type(o.amount.amount.value / 2);
                m.reserve_a += half;
                m.reserve_b += share_type(o.amount.amount.value - half.value);
            }
            m.liquidity_sum += o.amount.amount;
            m.k = fc::uint128_t((uint64_t)m.reserve_a.value) * fc::uint128_t((uint64_t)m.reserve_b.value);
        }
    });

    db.create<pm_liquidity_object>([&](pm_liquidity_object& lp) {
        lp.market       = mkt.id;
        lp.provider     = o.provider;
        lp.amount       = o.amount.amount;
        lp.deposit_time = now;
        lp.status       = 0;
        lp.b_share      = b_share;
    });
}

// ─── 10. pm_withdraw_liquidity ───────────────────────────────────────────────

void pm_withdraw_liquidity_evaluator::do_apply(const pm_withdraw_liquidity_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();

    const auto& lp = db.get<pm_liquidity_object, by_id>(pm_liquidity_id_type(o.liquidity_id));
    FC_ASSERT(lp.provider == o.provider, "Not your liquidity position");
    FC_ASSERT(lp.status == 0, "Position already closed");

    const auto& mkt = db.get<pm_market_object, by_id>(lp.market);
    // LP positions may be withdrawn early *during* the betting window (Early Withdrawal,
    // spec §9) but are locked once betting closes — for open-ended markets (no betting
    // deadline) the lock instead begins at resolution — and stay locked until SETTLEMENT,
    // not merely until resolution: this keeps the LP set stable for the time-weighted fee
    // settlement and backs the pending F1 charge. The lock is keyed on finalized_time and the
    // betting window below, never on a status>=2 sentinel (status is only ever 0/1/-1/3). The
    // withdrawal shrinks the reserves price-neutrally below, so an add→withdraw round-trip
    // is exploit-free (this is what B4 fixed; the earlier assert *message* wrongly implied
    // withdrawal was blocked during betting — the condition itself is correct).
    // F1-escape fix (PR #124): resolution is the lock trigger, settlement is the unlock. Settlement
    // runs from the deferred cron sweep (result_expiration + ≥12h dispute grace; resolution moves
    // result_expiration to the report time — early OR late — so it always settles ~grace after the
    // report and disputers always get the full window),
    // and resolution is exactly when the F1 `uncovered` LP charge
    // becomes computable+public (forfeit_pool is on get_market). If an LP could withdraw in that
    // window it would empty settle_liquidity's `active` set and dodge its share of the charge.
    // Withdrawable iff (a) already finalized (finalized_time stamped — settle/void/expire), or
    // (b) still pre-resolution AND inside the betting window (status < 2 gates out resolved markets;
    // the betting-window clause keeps EARLY withdrawal working, incl. open-ended betting_expiration==0
    // — clause 1 alone would have left open-ended markets withdrawable at status 3, floored only to
    // pm_min_liquidity, re-emitting uncovered − 100 VIZ; and open-ended is where uncovered is MOST
    // likely, as the leverage expiration buffer is waived there).
    FC_ASSERT(mkt.finalized_time != time_point_sec()
                  || (mkt.status < 2
                      && (mkt.betting_expiration == time_point_sec() || now < mkt.betting_expiration)),
              "LP positions are locked once betting closes or the market resolves, until settlement");

    share_type withdraw = (o.amount.amount == 0) ? lp.amount : o.amount.amount;
    FC_ASSERT(withdraw.value > 0 && withdraw.value <= lp.amount.value, "Invalid withdrawal amount");

    // A live market's pricing curve must stay funded above the minimum: the reserves
    // (CPMM) / lmsr_b (Multi) now track liquidity_sum, so an unchecked full exit could
    // drain them to zero and brick pricing. Only already-finalized markets (finalized_time
    // stamped) skip the floor — a resolved-but-unsettled market is still locked above (its
    // curve is dead but its principal backs the pending F1 charge). Positions that cannot
    // exit early settle in full at settlement.
    if (mkt.finalized_time == time_point_sec()) {
        const auto& mp = median(db);
        int64_t floor = mp.pm_min_liquidity.amount.value;
        // #2 (audit 2026-08-12): a live market with OPEN leverage backs outstanding loans with its
        // curve depth. pm_min_liquidity (~100) is ~50× below pm_leverage_min_market_liquidity (~5000),
        // so an unchecked withdraw could shrink the curve far under what the loans need and leave the
        // pool holding under-collateralized positions. Raise the floor to the leverage minimum while
        // any position on this market is open.
        const auto& lidx = db.get_index<pm_leverage_position_index>().indices().get<by_lev_market_status>();
        auto lit = lidx.lower_bound(boost::make_tuple(mkt.id, (uint8_t)0, pm_leverage_position_id_type()));
        const bool has_open_leverage = (lit != lidx.end() && lit->market == mkt.id && lit->status == 0);
        if (has_open_leverage && mp.pm_leverage_min_market_liquidity.amount.value > floor)
            floor = mp.pm_leverage_min_market_liquidity.amount.value;
        FC_ASSERT(mkt.liquidity_sum.value - withdraw.value >= floor,
                  "Withdrawal would drop market liquidity below the minimum");
    }

    share_type total = share_type(withdraw.value + lp.earned_fee.value);
    db.adjust_balance(db.get_account(o.provider), asset(total, TOKEN_SYMBOL));
    db.pm_adjust_frozen(o.provider, 0, -withdraw); // UNLOCK: principal back to free (earned_fee is profit, not frozen)

    db.modify(mkt, [&](pm_market_object& m) {
        const int64_t L = m.liquidity_sum.value; // capital BEFORE this withdrawal
        m.liquidity_sum -= withdraw;
        if (m.market_type == 1) {
            if (lp.b_share.value > 0 && lp.amount.value > 0) {
                share_type b_remove = (withdraw == lp.amount) ? lp.b_share :
                    share_type((int64_t)(fc::uint128_t((uint64_t)lp.b_share.value) *
                                fc::uint128_t((uint64_t)withdraw.value) /
                                fc::uint128_t((uint64_t)lp.amount.value)).lo);
                m.lmsr_b -= b_remove;
            }
        } else if (L > 0) {
            // CPMM: price-neutral withdraw. Shrink both reserves by (L - withdraw) / L so
            // the reserve ratio (the odds) is unchanged and depth falls with capital.
            // Mirrors the proportional add — a round-trip leaves the curve untouched.
            const fc::uint128_t num((uint64_t)(L - withdraw.value));
            const fc::uint128_t den((uint64_t)L);
            m.reserve_a = share_type((int64_t)(fc::uint128_t((uint64_t)m.reserve_a.value) * num / den).lo);
            m.reserve_b = share_type((int64_t)(fc::uint128_t((uint64_t)m.reserve_b.value) * num / den).lo);
            m.k = fc::uint128_t((uint64_t)m.reserve_a.value) * fc::uint128_t((uint64_t)m.reserve_b.value);
        }
    });

    if (withdraw == lp.amount) {
        db.modify(lp, [](pm_liquidity_object& l) { l.status = 3; l.earned_fee = 0; });
    } else {
        db.modify(lp, [&](pm_liquidity_object& l) { l.amount -= withdraw; l.earned_fee = 0; });
    }

    // #2 cascade backstop: the raised floor above bounds AGGREGATE depth, but shrinking the reserves
    // can still push an individual position (opened when depth was higher) under its threshold. Re-run
    // the liquidation cascade so the pool is never left holding an underwater position after a
    // legitimate withdraw. reason 0 = curve-move liquidation (residuals defer as outcome claims; the
    // market is live, so not a void). No-op when nothing crossed its threshold.
    if (mkt.market_type == 0)
        cascade_liquidate(db, mkt.id, -1, 0);
}

// ─── 11. pm_resolve_market ───────────────────────────────────────────────────

void pm_resolve_market_evaluator::do_apply(const pm_resolve_market_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.oracle == o.oracle, "Not the market oracle");
    FC_ASSERT(mkt.status == 1, "Market not active");
    FC_ASSERT(mkt.payout_status != 4, "Market is being refunded"); // #432 D3: void in flight
    FC_ASSERT(o.winning_outcome >= 0 && o.winning_outcome < (int16_t)mkt.outcome_count,
              "Invalid winning_outcome");
    FC_ASSERT(o.decision_url.size() <= MAX_PM_DECISION_URL_LEN, "decision_url too long");

    bool can_resolve_early = mkt.allow_early_resolution && now >= mkt.betting_expiration;
    FC_ASSERT(can_resolve_early || now >= mkt.result_expiration, "Cannot resolve yet");

    // P5 timeliness telemetry, captured BEFORE the modify below rewrites result_expiration on an
    // early resolve. `late` = resolved past the advertised deadline. `rt` = latency from betting
    // close to now (0 for open-ended, which has no forced wait).
    const bool late = (now > mkt.result_expiration);
    const uint64_t rt = (mkt.betting_expiration != time_point_sec() && now > mkt.betting_expiration)
        ? (uint64_t)(now.sec_since_epoch() - mkt.betting_expiration.sec_since_epoch()) : 0;

    // Anchor the whole downstream schedule (dispute window + LP-principal lock + settle wait) to the
    // moment the result is ANNOUNCED: result_expiration = now, unconditionally — exactly like
    // pm_no_contest. This guarantees disputers ALWAYS get the full pm_dispute_grace_sec window
    // measured from the announcement, whether the oracle reported early OR late.
    //   * Early report → pulls the schedule forward (collapses the LP lock / settle wait to
    //     now + grace instead of a far-future result_expiration; open-ended markets could otherwise
    //     stay locked ~pm_max_market_duration).
    //   * Late report → pushes result_expiration forward to the report time. Previously we only ever
    //     shifted EARLIER, which let an oracle GRIEF disputers: by stalling until ~result_expiration
    //     + grace and only then reporting, the dispute deadline (result_expiration + grace) was
    //     already spent, leaving a near-zero (or zero) window. Anchoring to `now` closes that hole —
    //     the settle wait extends by however late the report was, which is the necessary price of a
    //     fair, always-full dispute window. `late`/`rt` telemetry above was captured pre-shift, so
    //     resolved_late_count and latency stats stay honest.
    db.modify(mkt, [&](pm_market_object& m) {
        m.status           = 3;
        m.payout_status    = 1;
        m.resolved_outcome = o.winning_outcome;
        m.result_expiration = now;   // dispute/settle grace ALWAYS starts from the announcement
        from_string(m.decision_url, o.decision_url);
        from_string(m.decision_reason, o.decision_reason);
    });

    const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
    auto it = oidx.find(o.oracle);
    if (it != oidx.end())
        db.modify(*it, [&](pm_oracle_object& ora) {
            ora.markets_resolved++;
            if (ora.active_markets > 0) ora.active_markets--;   // leaves active set (1 → 3)
            ora.markets_in_dispute_window++;   // enters disputable window (status3, payout1, no dispute)
            ora.total_volume_resolved += mkt.bets_sum;
            if (late) ora.resolved_late_count++;
            // Running mean resolution latency over all resolves (n just incremented above).
            // uint128 intermediate: avg_resolution_time (uint32) * (n-1) can overflow uint64
            // once n grows large (e.g. ~4.3e9 markets), so accumulate in 128 bits.
            {
                const uint64_t n = ora.markets_resolved;
                const uint64_t sum =
                    ((fc::uint128_t((uint64_t)ora.avg_resolution_time) * (uint64_t)(n - 1)
                      + fc::uint128_t(rt)) / n).lo;
                ora.avg_resolution_time = (uint32_t)sum;
            }
            ora.resolution_time_hist[pm_rt_bucket(rt)] += share_type(1);   // P5 latency distribution
            ora.last_active_time = now;
        });
}

// ─── 12. pm_no_contest ───────────────────────────────────────────────────────

void pm_no_contest_evaluator::do_apply(const pm_no_contest_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.oracle == o.oracle, "Not the market oracle");
    FC_ASSERT(mkt.status == 1, "Market not active");
    FC_ASSERT(mkt.payout_status != 4, "Market is being refunded"); // #432 D3: void in flight
    FC_ASSERT(o.reason.size() <= MAX_PM_DISPUTE_REASON_LEN, "Reason too long");

    // Declaring no-contest does NOT settle immediately: it records an unresolved (-1) outcome and
    // opens the normal dispute window (spec §3.9 "Disputable", 3-outcome A/B/no-contest). Refund +
    // penalty are applied at settlement (settle_market, win<0) once the grace elapses — unless a
    // dispute overrides the no-contest to a real outcome first.
    const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
    auto it = oidx.find(o.oracle);
    if (it != oidx.end())
        db.modify(*it, [&](pm_oracle_object& ora) {
            ora.no_contest_count++;
            if (ora.active_markets > 0) ora.active_markets--;   // leaves active set (1 → 3)
            ora.markets_in_dispute_window++;   // enters disputable window (status3, payout1, no dispute)
            ora.last_active_time = now;
        });
    db.modify(mkt, [&](pm_market_object& m) {
        m.status            = 3;
        m.payout_status     = 1;
        m.resolved_outcome  = -1;
        m.result_expiration = now;  // start the dispute/settle grace from here
        from_string(m.decision_reason, o.reason); // NO-CONTEST rationale, readable via get_market
    });
}

// ─── 13. pm_dispute_create ───────────────────────────────────────────────────

void pm_dispute_create_evaluator::do_apply(const pm_dispute_create_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);
    const auto now = db.head_block_time();

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.status == 3, "Market not resolved");
    FC_ASSERT(mkt.payout_status == 1, "Payout not in pending state");
    FC_ASSERT(o.reason.size() <= MAX_PM_DISPUTE_REASON_LEN, "Reason too long");
    FC_ASSERT(o.proposed_outcome >= -1 && o.proposed_outcome < (int16_t)mkt.outcome_count,
              "proposed_outcome out of range");
    FC_ASSERT(now <= mkt.result_expiration + fc::seconds(mp.pm_dispute_grace_sec), "Grace period passed");

    const auto& didx = db.get_index<pm_dispute_index>().indices().get<by_market>();
    FC_ASSERT(didx.find(mkt.id) == didx.end(), "Dispute already filed");

    const auto& disputer = db.get_account(o.disputer);
    FC_ASSERT(disputer.balance >= mp.pm_dispute_fee, "Insufficient balance for dispute fee");
    db.adjust_balance(disputer, -mp.pm_dispute_fee);

    time_point_sec oracle_dl  = now + fc::seconds(mp.pm_oracle_dispute_response_sec);
    time_point_sec voting_end = oracle_dl + fc::seconds(mp.pm_dispute_vote_period_sec);
    time_point_sec auto_close = now + fc::seconds(mp.pm_dispute_auto_close_sec);

    db.create<pm_dispute_object>([&](pm_dispute_object& d) {
        d.market                   = mkt.id;
        d.disputer                 = o.disputer;
        d.dispute_fee              = mp.pm_dispute_fee.amount;
        from_string(d.reason, o.reason);
        d.filed_time               = now;
        d.oracle_response_deadline = oracle_dl;
        d.dispute_mode             = mkt.dispute_mode;
        d.voting_end_time          = voting_end;
        d.auto_close_time          = auto_close;
        d.proposed_outcome         = o.proposed_outcome;
        d.status                   = 0;
    });

    db.modify(mkt, [](pm_market_object& m) { m.payout_status = 2; });

    const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
    auto it = oidx.find(mkt.oracle);
    if (it != oidx.end())
        db.modify(*it, [](pm_oracle_object& ora) {
            ora.disputes_received++;
            if (ora.markets_in_dispute_window > 0) ora.markets_in_dispute_window--; // leaves disputable → disputed
            ora.disputes_awaiting_response++;   // new open dispute, oracle has not responded yet
        });

    // P1 oracle-metrics: surface the filing in the oracle's (and disputer's) history.
    db.push_virtual_operation(pm_dispute_opened_operation(
        mkt.oracle, o.disputer, mkt.id._id, o.proposed_outcome));
}

// ─── 14. pm_dispute_vote ─────────────────────────────────────────────────────

void pm_dispute_vote_evaluator::do_apply(const pm_dispute_vote_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.dispute_mode == 0, "Vote only for committee-mode disputes");

    const auto& didx = db.get_index<pm_dispute_index>().indices().get<by_market>();
    auto dit = didx.find(mkt.id);
    FC_ASSERT(dit != didx.end(), "No active dispute");
    FC_ASSERT(dit->status == 0, "Dispute already resolved");
    FC_ASSERT(now <= dit->voting_end_time, "Voting period ended");
    FC_ASSERT(o.vote_outcome >= -1 && o.vote_outcome < (int16_t)mkt.outcome_count,
              "vote_outcome out of range");
    FC_ASSERT(o.vote_percent >= -10000 && o.vote_percent <= 10000, "vote_percent out of range");

    // A committee dispute is a PUBLIC hearing (no commit-reveal — by design, see pm spec
    // §dispute-transparency): the running tally is visible so the DAO resolves it as truthfully
    // as possible. Consequently a voter may REVISE their ballot at any time while voting is open,
    // in case new evidence or arguments surface before voting_end_time. A repeat vote overwrites
    // the previous one (latest ballot wins) rather than being rejected.
    const auto& vidx = db.get_index<pm_dispute_vote_index>().indices().get<by_market_voter>();
    auto vit = vidx.find(boost::make_tuple(mkt.id, o.voter));
    if (vit != vidx.end()) {
        db.modify(*vit, [&](pm_dispute_vote_object& v) {
            v.vote_outcome = o.vote_outcome;
            v.vote_percent = o.vote_percent;
            v.time         = now;
        });
    } else {
        // M3: ballots are free — cap NEW rows per disputed market so a Sybil cannot build an
        // unbounded ballot set (the finalize cron walks every ballot, and get_dispute_votes
        // returns them all). The cap used to be enforced by recounting the market's ballots on
        // every new one: bounded per transaction, but O(n) per ballot and O(n²) to fill a market,
        // and per-tx work no cron budget covers — the same antipattern M4 removed from the commit
        // path with open_commits. The count now lives on the dispute row. Ballots are never
        // deleted individually (GC drops the whole cluster), so it only ever grows.
        FC_ASSERT(dit->ballots < MAX_PM_DISPUTE_VOTES_PER_MARKET, "Dispute ballot cap reached");
        db.create<pm_dispute_vote_object>([&](pm_dispute_vote_object& v) {
            v.market       = mkt.id;
            v.voter        = o.voter;
            v.vote_outcome = o.vote_outcome;
            v.vote_percent = o.vote_percent;
            v.time         = now;
        });
        db.modify(*dit, [&](pm_dispute_object& d) { ++d.ballots; });
    }
}

// ─── 15. pm_dispute_resolve ──────────────────────────────────────────────────

void pm_dispute_resolve_evaluator::do_apply(const pm_dispute_resolve_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);
    const auto now = db.head_block_time();

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.dispute_mode == 1, "resolve only for account-mode disputes");
    FC_ASSERT(mkt.dispute_resolver == o.resolver, "Not the market dispute resolver");
    FC_ASSERT(o.correct_outcome >= -1 && o.correct_outcome < (int16_t)mkt.outcome_count,
              "correct_outcome out of range");
    FC_ASSERT(o.penalty_amount.symbol == TOKEN_SYMBOL, "penalty_amount must be VIZ");

    const auto& didx = db.get_index<pm_dispute_index>().indices().get<by_market>();
    auto dit = didx.find(mkt.id);
    FC_ASSERT(dit != didx.end() && dit->status == 0, "No open dispute");
    FC_ASSERT(now <= dit->oracle_response_deadline + fc::seconds(mp.pm_dispute_vote_period_sec),
              "Resolution window passed");

    // Dispute leaves the open state either way (verdict below): drop it from the open-dispute gauge.
    pm_oracle_dispute_left_open(db, mkt.oracle, *dit);

    // Same post-verdict canon as committee mode (spec §3.9 "both modes converge"); only the
    // penalty size differs — here it is the resolver-specified penalty_amount (no vote to scale).
    if (o.correct_outcome != mkt.resolved_outcome) {
        const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
        auto it = oidx.find(mkt.oracle);
        share_type slash(0);
        if (it != oidx.end()) {
            slash = share_type(std::min(o.penalty_amount.amount.value, it->insurance.value));
            db.modify(*it, [&](pm_oracle_object& ora) {
                if (slash.value > 0) { ora.insurance -= slash; ora.total_insurance_slashed += slash; }
                ora.disputes_lost++;
                if (o.ban_oracle) { ora.banned_until = o.ban_oracle_until; ora.banned_by = o.resolver; }
            });
        }
        // Disputer was right: refund fee + reward carve-out from the slash; remainder → winners.
        int64_t fee = dit->dispute_fee.value > 0 ? dit->dispute_fee.value : 0;
        int64_t reward_target = (int64_t)(fc::uint128_t((uint64_t)fee)
            * fc::uint128_t((uint64_t)mp.pm_dispute_reward_multiplier) / fc::uint128_t(10000)).lo;
        int64_t bonus = reward_target - fee;
        if (bonus < 0) bonus = 0;
        if (bonus > slash.value) bonus = slash.value;
        if (fee + bonus > 0)
            db.adjust_balance(db.get_account(dit->disputer), asset(share_type(fee + bonus), TOKEN_SYMBOL));
        db.modify(mkt, [&](pm_market_object& m) {
            m.forfeit_pool    += share_type(slash.value - bonus);
            m.resolved_outcome = o.correct_outcome;
            m.status           = 3;
            m.payout_status    = 1;
        });
        db.modify(*dit, [](pm_dispute_object& d) { d.status = 1; }); // oracle wrong
    } else {
        // Uphold: the dispute fee compensates the oracle.
        const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
        auto it = oidx.find(mkt.oracle);
        if (it != oidx.end())
            db.modify(*it, [](pm_oracle_object& ora) { ora.disputes_won++; });
        if (dit->dispute_fee.value > 0)
            db.adjust_balance(db.get_account(mkt.oracle), asset(dit->dispute_fee, TOKEN_SYMBOL));
        db.modify(mkt, [&](pm_market_object& m) { m.payout_status = 1; });
        db.modify(*dit, [](pm_dispute_object& d) { d.status = 2; }); // oracle right
    }

    // Creator ban (scenario #14): an independent resolver sanction, applied regardless of the
    // verdict on the outcome. Upsert a pm_creator_ban row (one per creator) with the resolver's
    // ban_until; create_market consults it and rejects new markets while the ban is in force.
    if (o.ban_creator) {
        const auto& cbidx = db.get_index<pm_creator_ban_index>().indices().get<by_ban_account>();
        auto cb = cbidx.find(mkt.creator);
        if (cb == cbidx.end()) {
            db.create<pm_creator_ban_object>([&](pm_creator_ban_object& b) {
                b.creator = mkt.creator; b.banned_until = o.ban_creator_until; b.ban_count = 1;
                b.banned_by = o.resolver;
            });
        } else {
            db.modify(*cb, [&](pm_creator_ban_object& b) {
                if (o.ban_creator_until > b.banned_until) b.banned_until = o.ban_creator_until;
                b.ban_count++;
                b.banned_by = o.resolver;
            });
        }
    }
}

// ─── 16. pm_transfer_position ────────────────────────────────────────────────

void pm_transfer_position_evaluator::do_apply(const pm_transfer_position_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");

    const auto& bet = db.get<pm_bet_object, by_id>(pm_bet_id_type(o.bet_id));
    FC_ASSERT(bet.account == o.from, "Not your bet");
    FC_ASSERT(bet.status == 0, "Bet not active");
    db.get_account(o.to);

    share_type transfer_weight = (o.amount == 0) ? bet.weight : share_type(o.amount);
    FC_ASSERT(transfer_weight.value > 0 && transfer_weight.value <= bet.weight.value, "Invalid transfer amount");

    const auto& mkt = db.get<pm_market_object, by_id>(bet.market);
    FC_ASSERT(mkt.status == 1, "Market not active");
    FC_ASSERT(mkt.payout_status != 4, "Market is being refunded"); // #432 D3: void in flight

    if (transfer_weight == bet.weight) {
        const share_type moved = bet.amount;
        db.modify(bet, [&](pm_bet_object& b) { b.account = o.to; });
        db.pm_adjust_frozen(o.from, 1, -moved); // MOVE: whole stake changes owner
        db.pm_adjust_frozen(o.to,   1,  moved);
    } else {
        share_type transferred_amount = share_type((int64_t)(
            fc::uint128_t((uint64_t)bet.amount.value) *
            fc::uint128_t((uint64_t)transfer_weight.value) /
            fc::uint128_t((uint64_t)bet.weight.value)).lo);

        // #432 fix A — a PARTIAL transfer splits one row into two and costs no stake at all, so it
        // is the cheapest row-multiplication path in the whole PM (bandwidth only): without a floor
        // one 1 VIZ bet becomes a thousand 0.001 VIZ rows that settlement still has to pay out, and
        // the pm_place_bet floor above would be trivially bypassed. Require BOTH resulting rows to
        // stay at or above pm_min_bet. A position smaller than the floor is not trapped — it can
        // still be handed over WHOLE (transfer_weight == bet.weight takes the branch above).
        {
            const auto& mp = median(db);
            FC_ASSERT(transferred_amount >= mp.pm_min_bet.amount,
                      "Transferred part is below the minimum bet (pm_min_bet)");
            FC_ASSERT(bet.amount - transferred_amount >= mp.pm_min_bet.amount,
                      "Remaining part is below the minimum bet (pm_min_bet); transfer the whole position instead");
        }

        db.modify(bet, [&](pm_bet_object& b) {
            b.weight -= transfer_weight;
            b.amount -= transferred_amount;
        });
        db.pm_adjust_frozen(o.from, 1, -transferred_amount); // MOVE: partial stake to recipient
        db.pm_adjust_frozen(o.to,   1,  transferred_amount);

        db.create<pm_bet_object>([&](pm_bet_object& nb) {
            nb.market        = bet.market;
            nb.account       = o.to;
            nb.side          = bet.side;
            nb.outcome_index = bet.outcome_index;
            nb.amount        = transferred_amount;
            nb.weight        = transfer_weight;
            nb.mode          = bet.mode;
            nb.epoch         = bet.epoch;
            nb.status        = 0;
            nb.created_time  = bet.created_time;
            nb.time_penalty  = bet.time_penalty; // B9: inherit — same risk taken at the same time
            // M2: inherit the #1-C depth anchor — defaulting to 0 would mark the split bet as
            // "legacy" and let its cancel-refund bypass the liquidity-growth cap.
            nb.entry_liquidity = bet.entry_liquidity;
        });
    }
}

// ─── 17. pm_lazy_deposit ─────────────────────────────────────────────────────

void pm_lazy_deposit_evaluator::do_apply(const pm_lazy_deposit_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);
    FC_ASSERT(mp.pm_lazy_pool_enabled, "Lazy pool not enabled");
    const auto now = db.head_block_time();

    FC_ASSERT(o.amount.symbol == TOKEN_SYMBOL, "Amount must be VIZ");
    FC_ASSERT(o.amount.amount > 0, "Amount must be positive");

    const auto& acct = db.get_account(o.account);
    FC_ASSERT(acct.balance >= o.amount, "Insufficient balance");
    db.adjust_balance(acct, -o.amount);

    const auto& pool = db.get<pm_lazy_pool_object, by_id>(pm_lazy_pool_id_type(0));

    share_type new_shares;
    // Share price is backed by the pool's LP EQUITY = free + allocated + leverage_fund_used −
    // pending_withdrawals — ALL the capital that actually backs outstanding shares, NOT just
    // free_balance. Pricing off free_balance alone (or omitting leverage_fund_used) over-issued
    // shares whenever capital was deployed in markets (allocated>0) or lent out as leverage
    // (leverage_fund_used>0) or owed to queued withdrawers, letting a new depositor time entry
    // against outstanding loans and mint a disproportionate reward weight. leverage_fund_used is
    // principal that returns to free on close (loan solvency-checked ≥ loan), so it is real equity —
    // this MUST match the governance NAV formula below (see get_vote_weight pool_nav).
    const int64_t pool_equity =
        pool.free_balance.value + pool.allocated_balance.value
        + pool.leverage_fund_used.value - pool.pending_withdrawals.value;
    if (pool.total_shares.value == 0 || pool_equity <= 0) {
        new_shares = o.amount.amount;                       // empty/insolvent pool → 1:1 reset
    } else {
        new_shares = share_type((int64_t)(
            fc::uint128_t((uint64_t)o.amount.amount.value) *
            fc::uint128_t((uint64_t)pool.total_shares.value) /
            fc::uint128_t((uint64_t)pool_equity)).lo);
    }
    FC_ASSERT(new_shares.value > 0, "Zero shares minted");

    fc::uint128_t rps = pool.reward_per_share;

    db.modify(pool, [&](pm_lazy_pool_object& p) {
        p.total_shares += new_shares;
        p.free_balance += o.amount.amount;
    });

    const auto& didx = db.get_index<pm_lazy_deposit_index>().indices().get<by_deposit_account>();
    auto it = didx.find(o.account);
    if (it != didx.end()) {
        fc::uint128_t pend = (rps - it->reward_snapshot) *
                              fc::uint128_t((uint64_t)it->shares.value) /
                              fc::uint128_t((uint64_t)1000000000ULL);
        db.modify(*it, [&](pm_lazy_deposit_object& d) {
            d.pending_rewards += share_type((int64_t)pend.lo);
            d.shares          += new_shares;
            d.principal       += o.amount.amount;
            d.reward_snapshot  = rps;
        });
    } else {
        db.create<pm_lazy_deposit_object>([&](pm_lazy_deposit_object& d) {
            d.account         = o.account;
            d.shares          = new_shares;
            d.principal       = o.amount.amount;
            d.reward_snapshot = rps;
            d.unlock_time     = now + fc::seconds(mp.pm_lazy_lock_sec);
        });
    }
    service_lazy_withdraw_queue(db);   // fresh capital first pays anyone already queued to withdraw
}

// ─── 18. pm_lazy_withdraw ────────────────────────────────────────────────────

void pm_lazy_withdraw_evaluator::do_apply(const pm_lazy_withdraw_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);
    FC_ASSERT(mp.pm_lazy_pool_enabled, "Lazy pool not enabled");
    const auto now = db.head_block_time();

    const auto& didx = db.get_index<pm_lazy_deposit_index>().indices().get<by_deposit_account>();
    auto it = didx.find(o.account);
    FC_ASSERT(it != didx.end(), "No lazy deposit found");
    const auto& dep = *it;

    const auto& pool = db.get<pm_lazy_pool_object, by_id>(pm_lazy_pool_id_type(0));
    FC_ASSERT(pool.total_shares.value > 0, "Pool empty");

    // Settle the depositor's full accrued rewards (MasterChef: rps delta × shares + carried).
    fc::uint128_t pend_u = (pool.reward_per_share - dep.reward_snapshot) *
                            fc::uint128_t((uint64_t)dep.shares.value) /
                            fc::uint128_t((uint64_t)1000000000ULL);
    share_type pending = share_type((int64_t)pend_u.lo + dep.pending_rewards.value);

    // A planned withdrawal is only allowed once the lock has elapsed; an emergency withdrawal is
    // allowed any time (penalised while still locked). Both support a PARTIAL amount: o.shares==0
    // withdraws the whole position, otherwise exactly o.shares are burned.
    const bool locked = now < dep.unlock_time;
    if (!o.emergency)
        FC_ASSERT(!locked, "Deposit is locked; use emergency withdrawal or wait for unlock");
    share_type burn_shares = (o.shares == 0) ? dep.shares : share_type(o.shares);
    FC_ASSERT(burn_shares.value > 0 && burn_shares.value <= dep.shares.value, "Invalid shares amount");

    // Pro-rate principal and accrued rewards to the shares being burned.
    share_type principal_out = share_type((int64_t)(
        fc::uint128_t((uint64_t)dep.principal.value) *
        fc::uint128_t((uint64_t)burn_shares.value) /
        fc::uint128_t((uint64_t)dep.shares.value)).lo);
    share_type pending_out = share_type((int64_t)(
        fc::uint128_t((uint64_t)pending.value) *
        fc::uint128_t((uint64_t)burn_shares.value) /
        fc::uint128_t((uint64_t)dep.shares.value)).lo);

    // Emergency-while-locked penalty applies to the withdrawn PROFIT only (never principal); it
    // stays in the pool and is redistributed to the remaining LPs via reward_per_share.
    share_type penalty(0);
    if (o.emergency && locked)
        penalty = share_type(pending_out.value * mp.pm_lazy_emergency_penalty_percent / 10000);
    share_type owed = share_type(principal_out.value + pending_out.value - penalty.value);

    // Burn the shares now, register the amount owed as a first-claim liability, and redistribute the
    // penalty to the remaining LPs. The payout itself is NOT made here — it is queued and serviced
    // FIFO from free_balance, so the pool never hands out capital it doesn't hold liquid.
    const share_type remaining = share_type(pool.total_shares.value - burn_shares.value);
    db.modify(pool, [&](pm_lazy_pool_object& p) {
        p.total_shares        -= burn_shares;
        p.pending_withdrawals += owed;
        if (penalty.value > 0 && remaining.value > 0)
            p.reward_per_share += fc::uint128_t((uint64_t)penalty.value)
                                * fc::uint128_t((uint64_t)1000000000ULL)
                                / fc::uint128_t((uint64_t)remaining.value);
    });

    if (burn_shares == dep.shares) {
        db.remove(dep);
    } else {
        const auto& pool2 = db.get<pm_lazy_pool_object, by_id>(pm_lazy_pool_id_type(0)); // penalty-bumped rps
        db.modify(dep, [&](pm_lazy_deposit_object& d) {
            d.shares          -= burn_shares;
            d.principal       -= principal_out;
            d.pending_rewards  = share_type(pending.value - pending_out.value);
            d.reward_snapshot  = pool2.reward_per_share;
        });
    }

    // Queue the owed amount (FIFO by id) and pay out as much as free_balance covers right now. Any
    // uncovered remainder waits in the queue and is paid as capital returns to the pool — older
    // queued withdrawers are always paid first and free_balance never goes negative.
    if (owed.value > 0) {
        db.create<pm_lazy_withdraw_request_object>([&](pm_lazy_withdraw_request_object& r) {
            r.account = o.account;
            r.amount  = owed;
            r.created = now;
        });
    }
    service_lazy_withdraw_queue(db);
}

// ─── 19. pm_leverage_open (margin position via lazy-pool loan) ───────────────

void pm_leverage_open_evaluator::do_apply(const pm_leverage_open_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);
    FC_ASSERT(mp.pm_leverage_enabled, "Leverage not enabled");
    const auto now = db.head_block_time();

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.status == 1, "Market not active");
    FC_ASSERT(mkt.market_type == 0, "Leverage is CPMM-binary only");
    FC_ASSERT(o.outcome_index == 0 || o.outcome_index == 1, "outcome_index must be 0/1");
    // The expiration buffer only applies to markets with a fixed betting deadline. Open-ended
    // markets (betting_expiration == 0) have no deadline to buffer against, so leverage stays
    // available for the market's entire active life — the extra volatility on an open-ended risk
    // is the bettor's own choice, not a consensus concern. Positions are force-closed on
    // resolve/void either way (settle_market / return_liquidity). Guarding the subtraction also
    // avoids the epoch-0 underflow of (betting_expiration - buffer).
    if (mkt.betting_expiration != time_point_sec()) {
        FC_ASSERT(now < mkt.betting_expiration - fc::seconds(mp.pm_leverage_expiration_buffer_sec),
                  "Too close to betting expiration for leverage");
    }
    FC_ASSERT(mkt.liquidity_sum >= mp.pm_leverage_min_market_liquidity.amount,
              "Market liquidity below leverage minimum");
    FC_ASSERT(o.collateral.symbol == TOKEN_SYMBOL && o.loan.symbol == TOKEN_SYMBOL, "Must be VIZ");
    // #536 defense-in-depth (audit 2026-08-12): floor the loan at pm_min_liquidity. validate() only
    // requires loan>0, so without this a Sybil could open unbounded near-zero-loan positions on a
    // market — each consuming a slice of the (capped) leverage fund and a settlement-cron force-close
    // slot, building a liquidation backlog that throttles PM cron throughput for ~N/cap blocks (see
    // consensus_sim leverage_sybil_settlement_is_cap_throttled, PR #151). A minimum loan bounds the
    // global open-position count to fund_total / pm_min_liquidity (every open position locks ≥ this
    // much of leverage_fund_used), so the backlog is bounded by construction. Reuses the existing
    // median-voted pm_min_liquidity (no new chain property / serialization change); governance can
    // still raise the floor by voting it up.
    FC_ASSERT(o.loan.amount >= mp.pm_min_liquidity.amount,
              "Leverage loan below minimum (pm_min_liquidity)");

    const auto& acct = db.get_account(o.account);
    FC_ASSERT(acct.balance >= o.collateral, "Insufficient balance for collateral");

    const auto& pool = db.get<pm_lazy_pool_object, by_id>(pm_lazy_pool_id_type(0));
    const int64_t loan       = o.loan.amount.value;
    const int64_t collateral = o.collateral.amount.value;

    // Constraint 1: leverage-fund availability + per-position cap.
    // M7: free_balance is ALREADY net of active loans — open does `free -= loan` and
    // close/convert/liquidate do `free += obligation/pool_received` with `leverage_fund_used -= loan`.
    // Subtracting leverage_fund_used here again double-deducted outstanding loans and understated the
    // pool's real lending capacity (conservative logic bug, not an exploit). free_balance IS the hard
    // solvency floor for a new loan. (Open design note: fund_total below is based on free vs NAV —
    // left as-is pending the spec call, see audit M7.)
    int64_t free_amount = pool.free_balance.value;
    FC_ASSERT(loan <= free_amount, "Loan exceeds pool free capital");
    int64_t fund_total = (int64_t)(fc::uint128_t((uint64_t)pool.free_balance.value)
                         * fc::uint128_t(mp.pm_leverage_fund_percent) / fc::uint128_t(100u)).lo;
    int64_t fund_available = fund_total - pool.leverage_fund_used.value;
    FC_ASSERT(fund_available > 0, "Leverage fund exhausted");
    int64_t per_pos_cap = (int64_t)(fc::uint128_t((uint64_t)fund_available)
                          * fc::uint128_t(mp.pm_leverage_max_per_position_bp) / fc::uint128_t(10000u)).lo;
    FC_ASSERT(loan <= per_pos_cap, "Loan exceeds per-position cap");

    // Constraint 3: max position size relative to market.
    const int64_t total_bet = collateral + loan;
    int64_t pos_cap = (int64_t)(fc::uint128_t((uint64_t)mkt.liquidity_sum.value)
                      * fc::uint128_t(mp.pm_leverage_max_position_ratio_percent) / fc::uint128_t(100u)).lo;
    FC_ASSERT(total_bet <= pos_cap, "Position exceeds market size limit");

    // Place (C+L) into the CPMM (k preserved).
    pm::leverage::cpmm_fill fill = pm::leverage::cpmm_buy(mkt.reserve_a.value, mkt.reserve_b.value, mkt.k,
                                                  total_bet, (int)o.outcome_index);
    FC_ASSERT(fill.tokens > 0, "Zero tokens");
    FC_ASSERT(fill.tokens >= o.min_tokens.value, "Slippage: tokens below min_tokens");

    // Constraint 2 / Rule 8: worst-case solvency guarantee for the pool.
    int64_t m = pm::leverage::worst_opposing_bet(fill.new_reserve_a, fill.new_reserve_b,
                    mp.pm_leverage_max_slippage_percent, mp.pm_leverage_m_factor_percent);
    int64_t cvw = pm::leverage::cancel_value_after_opposing(fill.new_reserve_a, fill.new_reserve_b, mkt.k,
                    fill.tokens, (int)o.outcome_index, m);
    int64_t threshold = pm::leverage::liquidation_threshold(loan, mp.pm_leverage_pool_profit_percent);
    int64_t threshold_safe = (int64_t)(fc::uint128_t((uint64_t)threshold)
                    * fc::uint128_t(100u + mp.pm_leverage_safety_margin_percent) / fc::uint128_t(100u)).lo;
    FC_ASSERT(cvw >= threshold_safe, "Position fails worst-case safety check");

    // Apply: bettor pays collateral, pool fronts the loan, capital enters the curve.
    db.adjust_balance(acct, -o.collateral);
    db.pm_adjust_frozen(o.account, 2, o.collateral.amount); // LOCK: collateral → active leverage (loan is pool capital, not counted)
    db.modify(pool, [&](pm_lazy_pool_object& p) {
        p.free_balance       -= share_type(loan);
        p.leverage_fund_used += share_type(loan);
    });
    db.modify(mkt, [&](pm_market_object& mm) {
        mm.reserve_a = share_type(fill.new_reserve_a);
        mm.reserve_b = share_type(fill.new_reserve_b);
    });
    db.create<pm_leverage_position_object>([&](pm_leverage_position_object& pos) {
        pos.market                = mkt.id;
        pos.account               = o.account;
        pos.outcome_index         = o.outcome_index;
        pos.collateral            = share_type(collateral);
        pos.loan                  = share_type(loan);
        pos.total_bet             = share_type(total_bet);
        pos.tokens                = share_type(fill.tokens);
        pos.pool_profit           = share_type(threshold - loan);
        pos.liquidation_threshold = share_type(threshold);
        pos.status                = 0;
        pos.created_time          = now;
        pos.last_update           = now;
        pos.funding_paid          = share_type(0);
        pos.funding_due_time      = time_point_sec(now + fc::seconds(86400)); // first 24h funding period
    });
}

// ─── 20. pm_leverage_close (voluntary, only when cancel_value >= threshold) ────

void pm_leverage_close_evaluator::do_apply(const pm_leverage_close_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();

    const auto& pos = db.get<pm_leverage_position_object, by_id>(pm_leverage_position_id_type(o.position_id));
    FC_ASSERT(pos.account == o.account, "Not your position");
    FC_ASSERT(pos.status == 0, "Position not active");
    accrue_leverage_funding(db, pos,
        db.get_validator_schedule_object().median_props.pm_leverage_funding_rate_ppm_per_day, now);
    const auto& mkt = db.get<pm_market_object, by_id>(pos.market);

    int64_t cv = pm::leverage::cancel_value(mkt.reserve_a.value, mkt.reserve_b.value, mkt.k,
                                        pos.tokens.value, (int)pos.outcome_index);
    const int64_t obligation = pos.liquidation_threshold.value + pos.funding_paid.value;
    FC_ASSERT(cv >= obligation, "Position underwater: cannot voluntarily close");
    int64_t bettor_received = cv - obligation;
    FC_ASSERT(bettor_received >= o.min_return, "Return below min_return");

    // F1/#300: the pool recovers `obligation` from cv; everything it does not take stays in the pot
    // (forfeit_pool gets total_bet − obligation, ≥ 0). The bettor's profit (cv − obligation) is NOT
    // paid against the curve now — it becomes an OUTCOME-CONTINGENT deferred claim, paid at settlement
    // from the bounded early-exit bucket iff this outcome wins. Keeps forfeit ≥ 0 (no LP hit / mint).
    const int64_t pot_retained = pos.total_bet.value - obligation;
    // Clamp at 0 (see liquidate_position): long-lived positions accrue unbounded funding, so
    // obligation can exceed total_bet; a negative pot_retained would push forfeit_pool negative
    // and create an `uncovered` shortfall at settlement. The pool recovers obligation from cv
    // either way; the pot just never goes negative.
    const int64_t pot_retained_capped = pot_retained > 0 ? pot_retained : 0;
    // Unwind the tokens from the curve (k preserved).
    db.modify(mkt, [&](pm_market_object& m) {
        if (pos.outcome_index == 0) {
            int64_t new_rb = m.reserve_b.value + pos.tokens.value;
            m.reserve_b = share_type(new_rb);
            m.reserve_a = share_type((int64_t)(m.k / fc::uint128_t((uint64_t)new_rb)).lo);
        } else {
            int64_t new_ra = m.reserve_a.value + pos.tokens.value;
            m.reserve_a = share_type(new_ra);
            m.reserve_b = share_type((int64_t)(m.k / fc::uint128_t((uint64_t)new_ra)).lo);
        }
        m.forfeit_pool += share_type(pot_retained_capped);
    });
    int64_t pool_yield = pos.pool_profit.value + pos.funding_paid.value; // R-markup + accrued funding → LP yield
    db.modify(db.get<pm_lazy_pool_object, by_id>(pm_lazy_pool_id_type(0)), [&](pm_lazy_pool_object& p) {
        p.free_balance       += share_type(obligation);
        p.leverage_fund_used -= pos.loan;
        p.earned_balance     += share_type(pool_yield);
        if (p.total_shares.value > 0)
            p.reward_per_share += fc::uint128_t((uint64_t)pool_yield)
                                * fc::uint128_t((uint64_t)1000000000) / fc::uint128_t((uint64_t)p.total_shares.value);
    });
    service_lazy_withdraw_queue(db);   // returning leverage capital first pays queued withdrawers
    if (bettor_received > 0 && mkt.deferred_claim_count < MAX_PM_DEFERRED_CLAIMS_PER_MARKET) {
        // #349: skip once the per-market cap is hit — the residual stays in the curve and pays 0 at
        // settlement (like bucket-exhaustion), keeping settle_market's claim loop bounded.
        db.create<pm_deferred_claim_object>([&](pm_deferred_claim_object& c) {
            c.market = pos.market; c.account = o.account; c.kind = 1;
            c.outcome_index = pos.outcome_index; c.claim_amount = share_type(bettor_received); c.exit_time = now;
        });
        db.modify(mkt, [](pm_market_object& m) { m.deferred_claim_count++; });
    }
    db.modify(pos, [&](pm_leverage_position_object& p) {
        p.status = 4; p.pool_received = share_type(obligation);
        p.bettor_received = share_type(bettor_received); p.last_update = now;
    });
    db.pm_adjust_frozen(o.account, 2, -pos.collateral); // UNLOCK: collateral leaves active leverage (voluntary close)
}

// ─── 21. pm_leverage_convert (pay off loan, keep position as a normal bet) ─────

void pm_leverage_convert_evaluator::do_apply(const pm_leverage_convert_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto& mp = median(db);
    FC_ASSERT(o.conversion_profit_cost == mp.pm_conversion_profit_cost_percent,
              "conversion_profit_cost must equal current consensus value");
    const auto now = db.head_block_time();

    const auto& pos = db.get<pm_leverage_position_object, by_id>(pm_leverage_position_id_type(o.position_id));
    FC_ASSERT(pos.account == o.account, "Not your position");
    FC_ASSERT(pos.status == 0, "Position not active");
    accrue_leverage_funding(db, pos, mp.pm_leverage_funding_rate_ppm_per_day, now);
    const auto& mkt = db.get<pm_market_object, by_id>(pos.market);

    int64_t cv = pm::leverage::cancel_value(mkt.reserve_a.value, mkt.reserve_b.value, mkt.k,
                                        pos.tokens.value, (int)pos.outcome_index);
    const int64_t obligation = pos.liquidation_threshold.value + pos.funding_paid.value;
    FC_ASSERT(cv >= obligation, "Position underwater");
    int64_t current_profit = cv - obligation;
    FC_ASSERT(current_profit > 0, "No profit to convert");
    int64_t conversion_fee = (int64_t)(fc::uint128_t((uint64_t)current_profit)
                             * fc::uint128_t(o.conversion_profit_cost) / fc::uint128_t(100u)).lo;
    const int64_t total_payment = obligation + conversion_fee;

    const auto& acct = db.get_account(o.account);
    FC_ASSERT(acct.balance.amount >= total_payment, "Insufficient balance for conversion");
    db.adjust_balance(acct, -asset(share_type(total_payment), TOKEN_SYMBOL));

    int64_t pool_profit_total = pos.pool_profit.value + conversion_fee + pos.funding_paid.value;
    db.modify(db.get<pm_lazy_pool_object, by_id>(pm_lazy_pool_id_type(0)), [&](pm_lazy_pool_object& p) {
        p.free_balance       += share_type(total_payment);
        p.leverage_fund_used -= pos.loan;
        p.earned_balance     += share_type(pool_profit_total);
        if (p.total_shares.value > 0)
            p.reward_per_share += fc::uint128_t((uint64_t)pool_profit_total)
                                * fc::uint128_t((uint64_t)1000000000) / fc::uint128_t((uint64_t)p.total_shares.value);
    });
    service_lazy_withdraw_queue(db);   // returning leverage capital first pays queued withdrawers

    // Position becomes a normal parimutuel bet, 100% bettor-owned (reserves unchanged).
    db.create<pm_bet_object>([&](pm_bet_object& b) {
        b.market        = mkt.id;
        b.account       = o.account;
        b.side          = (int8_t)pos.outcome_index;
        b.outcome_index = -1;
        b.amount        = pos.total_bet;
        b.weight        = pos.tokens;
        b.status        = 0;
        b.created_time  = now;
        // B9: the exposure was taken when the leverage position was OPENED, not at conversion —
        // penalise by pos.created_time (leverage opens are gated ≥24h before expiry, so this is
        // virtually always 0 unless the market runs a very wide percentage window).
        b.time_penalty  = compute_time_penalty(mkt, pos.created_time, mp.pm_max_time_penalty);
        // M2: #1-C depth anchor at conversion (§6 "depth at fill" convention) — leaving it 0 would
        // mark the bet "legacy" and let its cancel-refund bypass the liquidity-growth cap.
        b.entry_liquidity = mkt.liquidity_sum;
    });
    db.modify(mkt, [&](pm_market_object& m) {
        if (pos.outcome_index == 0) m.a_bets_sum += pos.total_bet;
        else                        m.b_bets_sum += pos.total_bet;
        m.bets_sum += pos.total_bet;
    });
    db.modify(pos, [&](pm_leverage_position_object& p) { p.status = 5; p.last_update = now; });
    // MOVE: position converts to a plain bet — collateral leaves leverage, total_bet enters bets.
    db.pm_adjust_frozen(o.account, 2, -pos.collateral);
    db.pm_adjust_frozen(o.account, 1, pos.total_bet);
}

// ─── 22. pm_dispute_oracle_respond ───────────────────────────────────────────
// The oracle posts a public rebuttal onto the open dispute. Disputes are public hearings, so the
// text is stored on the dispute object (read by every voter/resolver via get_dispute). Allowed
// only while the dispute is open and within the oracle_response_deadline; re-posting overwrites.
void pm_dispute_oracle_respond_evaluator::do_apply(const pm_dispute_oracle_respond_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();

    const auto& mkt = get_market(db, o.market_id);
    FC_ASSERT(mkt.oracle == o.oracle, "Not the market oracle");
    FC_ASSERT(o.response.size() <= MAX_PM_DISPUTE_REASON_LEN, "response too long");

    const auto& didx = db.get_index<pm_dispute_index>().indices().get<by_market>();
    auto dit = didx.find(mkt.id);
    FC_ASSERT(dit != didx.end() && dit->status == 0, "No open dispute");
    FC_ASSERT(now <= dit->oracle_response_deadline, "Oracle response window passed");

    // First response moves the open dispute from awaiting-response to awaiting-decision. A repeat
    // response (re-posting overwrites) leaves it in awaiting-decision — do not double-count.
    if (dit->oracle_response_time == time_point_sec()) {
        pm_oracle_gauge_adj(db, mkt.oracle, &pm_oracle_object::disputes_awaiting_response, -1);
        pm_oracle_gauge_adj(db, mkt.oracle, &pm_oracle_object::disputes_awaiting_decision, +1);
    }

    db.modify(*dit, [&](pm_dispute_object& d) {
        from_string(d.oracle_response, o.response);
        d.oracle_response_time = now;
    });
}

// ─── 23. pm_unban ────────────────────────────────────────────────────────────
// Reverse a ban set by an account-mode pm_dispute_resolve. Only the resolver recorded in
// banned_by may lift it; the ban is set to epoch (past ⇒ not banned) and banned_by cleared.
void pm_unban_evaluator::do_apply(const pm_unban_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_14), "PM not enabled");
    const auto now = db.head_block_time();
    bool did = false;

    if (o.unban_oracle) {
        const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
        auto it = oidx.find(o.target);
        FC_ASSERT(it != oidx.end(), "Oracle not found");
        FC_ASSERT(it->banned_until > now, "Oracle is not currently banned");
        FC_ASSERT(it->banned_by == o.resolver, "Only the resolver that imposed the ban may lift it");
        db.modify(*it, [&](pm_oracle_object& ora) {
            ora.banned_until = time_point_sec(0);
            ora.banned_by    = account_name_type();
        });
        did = true;
    }
    if (o.unban_creator) {
        const auto& cbidx = db.get_index<pm_creator_ban_index>().indices().get<by_ban_account>();
        auto it = cbidx.find(o.target);
        FC_ASSERT(it != cbidx.end(), "No creator ban for target");
        FC_ASSERT(it->banned_until > now, "Creator is not currently banned");
        FC_ASSERT(it->banned_by == o.resolver, "Only the resolver that imposed the ban may lift it");
        db.modify(*it, [&](pm_creator_ban_object& b) {
            b.banned_until = time_point_sec(0);
            b.banned_by    = account_name_type();
        });
        did = true;
    }
    FC_ASSERT(did, "Nothing to unban");
}

// ─── Cron: process_pm_markets (called once per block) ───────────────────────
//
// Processes PM state-machine transitions bounded by pm_processing_cap_per_block.
// Uses the anonymous-namespace helpers (settle_market_step, refund_market_step,
// return_liquidity) that are visible within this translation unit.

// PM frozen-funds telemetry helper (display-only). kind: 0=liquidity, 1=bets, 2=leverage.
// Clamped at zero so a stray double-decrement can never wrap a share_type negative — these
// are cosmetic aggregates, never consensus inputs.
void database::pm_adjust_frozen(const account_name_type& account, uint8_t kind, share_type delta) {
    if (delta == 0) return;
    const auto* a = find_account(account);
    if (a == nullptr) return;
    modify(*a, [&](account_object& acc) {
        asset* f = (kind == 0) ? &acc.pm_liquidity_committed
                 : (kind == 1) ? &acc.pm_bets_staked
                               : &acc.pm_leverage_collateral;
        f->amount += delta;
        if (f->amount < 0) f->amount = 0;
    });
}

// (Re)seed the per-account frozen counters from existing PM objects. Runs once, on the first block
// after the upgrade (guarded by dgpo flags), so the counters are correct without a full replay (VIZ
// testnet is DLT-only, no genesis). **Idempotent:** every account's three counters are zeroed first,
// then re-summed from the live objects — so re-running it (the task #266 corrective re-seed) always
// converges to the exact object totals regardless of any prior over/under-count.
void database::pm_seed_frozen_counters() {
    // Reset first — makes the (re)seed idempotent. Counters are display-only (never gate consensus),
    // so recomputing them is safe. Without this a second run would double-count the objects.
    const auto& aidx = get_index<account_index>().indices().get<by_id>();
    for (auto it = aidx.begin(); it != aidx.end(); ++it) {
        if (it->pm_liquidity_committed.amount.value != 0 ||
            it->pm_bets_staked.amount.value       != 0 ||
            it->pm_leverage_collateral.amount.value != 0) {
            modify(*it, [&](account_object& a) {
                a.pm_liquidity_committed   = asset(0, TOKEN_SYMBOL);
                a.pm_bets_staked           = asset(0, TOKEN_SYMBOL);
                a.pm_leverage_collateral   = asset(0, TOKEN_SYMBOL);
            });
        }
    }
    // liquidity: own provider liquidity in live markets (skip empty-provider lazy-pool rows)
    const auto& lidx = get_index<pm_liquidity_index>().indices().get<by_provider>();
    for (auto it = lidx.begin(); it != lidx.end(); ++it) {
        if (it->status == 0 && it->provider.size() > 0)
            pm_adjust_frozen(it->provider, 0, it->amount.value);
    }
    // bets: own stake still held on-chain — status 0 active, 5 queued, 6 revealed-pending
    // (1 cancelled / 2 refunded / 3 resolved have already returned to free balance)
    const auto& bidx = get_index<pm_bet_index>().indices().get<by_account>();
    for (auto it = bidx.begin(); it != bidx.end(); ++it) {
        if (it->status == 0 || it->status == 5 || it->status == 6)
            pm_adjust_frozen(it->account, 1, it->amount.value);
    }
    // leverage: own collateral in active positions
    const auto& vidx = get_index<pm_leverage_position_index>().indices().get<by_lev_account>();
    for (auto it = vidx.begin(); it != vidx.end(); ++it) {
        if (it->status == 0)
            pm_adjust_frozen(it->account, 2, it->collateral.value);
    }
}

// Debug drift-check for the frozen counters (see header). Independently re-derives the expected
// per-account totals from the live objects and diffs them against the stored aggregates. O(accounts
// + PM objects); intended for tests / manual invocation, not per-block. Returns true when clean.
bool database::pm_verify_frozen_counters() const {
    std::map<account_name_type, std::array<int64_t, 3>> exp;
    const auto& lidx = get_index<pm_liquidity_index>().indices().get<by_provider>();
    for (auto it = lidx.begin(); it != lidx.end(); ++it)
        if (it->status == 0 && it->provider.size() > 0) exp[it->provider][0] += it->amount.value;
    const auto& bidx = get_index<pm_bet_index>().indices().get<by_account>();
    for (auto it = bidx.begin(); it != bidx.end(); ++it)
        if (it->status == 0 || it->status == 5 || it->status == 6) exp[it->account][1] += it->amount.value;
    const auto& vidx = get_index<pm_leverage_position_index>().indices().get<by_lev_account>();
    for (auto it = vidx.begin(); it != vidx.end(); ++it)
        if (it->status == 0) exp[it->account][2] += it->collateral.value;

    bool ok = true;
    const auto& aidx = get_index<account_index>().indices().get<by_id>();
    for (auto it = aidx.begin(); it != aidx.end(); ++it) {
        auto e = exp.find(it->name);
        int64_t el = e == exp.end() ? 0 : e->second[0];
        int64_t eb = e == exp.end() ? 0 : e->second[1];
        int64_t ev = e == exp.end() ? 0 : e->second[2];
        if (it->pm_liquidity_committed.amount.value != el ||
            it->pm_bets_staked.amount.value       != eb ||
            it->pm_leverage_collateral.amount.value != ev) {
            ok = false;
            elog("pm frozen-counter drift ${a}: liq ${cl}!=${el} bets ${cb}!=${eb} lev ${cv}!=${ev}",
                ("a", it->name)("cl", it->pm_liquidity_committed.amount.value)("el", el)
                ("cb", it->pm_bets_staked.amount.value)("eb", eb)
                ("cv", it->pm_leverage_collateral.amount.value)("ev", ev));
        }
    }
    return ok;
}

// One-time seed of pm_oracle_object.active_markets from live markets (see header). Zeroes every
// oracle's counter then re-sums the status==1 markets per registered oracle — idempotent, display-only.
void database::pm_seed_oracle_active_markets() {
    const auto& obyid = get_index<pm_oracle_index>().indices().get<by_id>();
    for (auto it = obyid.begin(); it != obyid.end(); ++it)
        if (it->active_markets != 0)
            modify(*it, [](pm_oracle_object& o) { o.active_markets = 0; });
    const auto& obyowner = get_index<pm_oracle_index>().indices().get<by_owner>();
    const auto& midx = get_index<pm_market_index>().indices().get<by_status>();
    for (auto it = midx.lower_bound((int8_t)1); it != midx.end() && it->status == 1; ++it) {
        auto oit = obyowner.find(it->oracle);
        if (oit != obyowner.end())
            modify(*oit, [](pm_oracle_object& o) { o.active_markets++; });
    }
}

// Debug drift-check for active_markets: re-derive the live count per oracle and diff the stored
// counter. O(active markets + oracles); read-only. Logs every mismatch, returns true when clean.
bool database::pm_verify_oracle_active_markets() const {
    std::map<account_name_type, uint32_t> exp;
    const auto& midx = get_index<pm_market_index>().indices().get<by_status>();
    for (auto it = midx.lower_bound((int8_t)1); it != midx.end() && it->status == 1; ++it)
        exp[it->oracle]++;
    bool ok = true;
    const auto& oidx = get_index<pm_oracle_index>().indices().get<by_id>();
    for (auto it = oidx.begin(); it != oidx.end(); ++it) {
        auto e = exp.find(it->owner);
        uint32_t ev = (e == exp.end()) ? 0u : e->second;
        if (it->active_markets != ev) {
            ok = false;
            elog("pm active_markets drift ${o}: ${c}!=${e}",
                ("o", it->owner)("c", it->active_markets)("e", ev));
        }
    }
    return ok;
}

// One-time seed of the per-oracle workload gauges (see header/pm_objects.hpp). Zeroes every
// oracle's three counters, then re-derives them from live state — idempotent, display-only.
//   markets_in_dispute_window   = status3 + payout_status1 markets with NO dispute row.
//   disputes_awaiting_response  = open(0) disputes with oracle_response_time == epoch.
//   disputes_awaiting_decision  = open(0) disputes with oracle_response_time  > epoch.
void database::pm_seed_oracle_gauges() {
    const auto& obyid = get_index<pm_oracle_index>().indices().get<by_id>();
    for (auto it = obyid.begin(); it != obyid.end(); ++it)
        if (it->markets_in_dispute_window || it->disputes_awaiting_response || it->disputes_awaiting_decision)
            modify(*it, [](pm_oracle_object& o) {
                o.markets_in_dispute_window  = 0;
                o.disputes_awaiting_response = 0;
                o.disputes_awaiting_decision = 0;
            });

    const auto& obyowner = get_index<pm_oracle_index>().indices().get<by_owner>();
    const auto& didx     = get_index<pm_dispute_index>().indices().get<by_market>();
    const auto& midx     = get_index<pm_market_index>().indices().get<by_status>();
    for (auto it = midx.lower_bound((int8_t)3); it != midx.end() && it->status == 3; ++it) {
        if (it->payout_status != 1) continue;
        if (didx.find(it->id) != didx.end()) continue;   // disputed → not in the disputable window
        auto oit = obyowner.find(it->oracle);
        if (oit != obyowner.end())
            modify(*oit, [](pm_oracle_object& o) { o.markets_in_dispute_window++; });
    }

    const auto& dclose = get_index<pm_dispute_index>().indices().get<by_auto_close>();
    for (auto it = dclose.lower_bound(boost::make_tuple((uint8_t)0, time_point_sec(0), pm_dispute_id_type()));
         it != dclose.end() && it->status == 0; ++it) {
        const auto& mbyid = get_index<pm_market_index>().indices().get<by_id>();
        auto mit = mbyid.find(it->market);
        if (mit == mbyid.end()) continue;
        auto oit = obyowner.find(mit->oracle);
        if (oit == obyowner.end()) continue;
        const bool responded = (it->oracle_response_time != time_point_sec());
        modify(*oit, [&](pm_oracle_object& o) {
            if (responded) o.disputes_awaiting_decision++; else o.disputes_awaiting_response++;
        });
    }
}

// Debug drift-check for the workload gauges: re-derive per-oracle from live state and diff the
// stored counters. O(resolved-pending markets + open disputes + oracles); read-only. Logs each
// mismatch, returns true when clean.
bool database::pm_verify_oracle_gauges() const {
    std::map<account_name_type, uint32_t> win, resp, dec;
    const auto& didx  = get_index<pm_dispute_index>().indices().get<by_market>();
    const auto& midx  = get_index<pm_market_index>().indices().get<by_status>();
    const auto& mbyid = get_index<pm_market_index>().indices().get<by_id>();
    for (auto it = midx.lower_bound((int8_t)3); it != midx.end() && it->status == 3; ++it) {
        if (it->payout_status != 1) continue;
        if (didx.find(it->id) != didx.end()) continue;
        win[it->oracle]++;
    }
    const auto& dclose = get_index<pm_dispute_index>().indices().get<by_auto_close>();
    for (auto it = dclose.lower_bound(boost::make_tuple((uint8_t)0, time_point_sec(0), pm_dispute_id_type()));
         it != dclose.end() && it->status == 0; ++it) {
        auto mit = mbyid.find(it->market);
        if (mit == mbyid.end()) continue;
        if (it->oracle_response_time != time_point_sec()) dec[mit->oracle]++;
        else                                              resp[mit->oracle]++;
    }
    bool ok = true;
    const auto& oidx = get_index<pm_oracle_index>().indices().get<by_id>();
    for (auto it = oidx.begin(); it != oidx.end(); ++it) {
        auto fw = win.find(it->owner);  uint32_t ew = (fw == win.end())  ? 0u : fw->second;
        auto fr = resp.find(it->owner); uint32_t er = (fr == resp.end()) ? 0u : fr->second;
        auto fd = dec.find(it->owner);  uint32_t ed = (fd == dec.end())  ? 0u : fd->second;
        if (it->markets_in_dispute_window != ew || it->disputes_awaiting_response != er
            || it->disputes_awaiting_decision != ed) {
            ok = false;
            elog("pm oracle gauges drift ${o}: win ${a}/${ea} resp ${b}/${eb} dec ${c}/${ec}",
                ("o", it->owner)("a", it->markets_in_dispute_window)("ea", ew)
                ("b", it->disputes_awaiting_response)("eb", er)
                ("c", it->disputes_awaiting_decision)("ec", ed));
        }
    }
    return ok;
}

} } // namespace graphene::chain

