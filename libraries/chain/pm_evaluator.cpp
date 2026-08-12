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

namespace graphene { namespace chain {

using namespace graphene::protocol;

// ─── Internal helpers ────────────────────────────────────────────────────────

namespace {

    const chain_properties_pm& median(const database& db) {
        return db.get_validator_schedule_object().median_props;
    }

    // ── B9 (spec §8): late-bet anti-sniping penalty ──────────────────────────────
    // Returns the 1e6-scaled penalty applied ONLY to a winning bet's PROFIT at settlement
    // (never principal — see compute_settlement). It is 0 outside the penalty window, when the
    // market configured none (time_penalty_value == 0), or for open-ended markets (no deadline).
    // `risk_time` is when the market exposure was actually taken — bet placement for instant bets,
    // the COMMIT time for commit-reveal (blind, not the later reveal), the leverage OPEN time for a
    // converted position — so honest early risk-takers are never penalised for later mechanics.
    // Integer-only (consensus); the field was defined + read but never assigned before this fix.
    uint32_t compute_time_penalty(const pm_market_object& mkt, time_point_sec risk_time,
                                  uint32_t max_time_penalty) {
        if (mkt.time_penalty_value == 0) return 0;
        if (mkt.betting_expiration == time_point_sec()) return 0; // open-ended: no window
        const int64_t be = (int64_t)mkt.betting_expiration.sec_since_epoch();
        const int64_t ct = (int64_t)mkt.created_time.sec_since_epoch();
        int64_t window;
        if (mkt.time_penalty_type == 0) {
            window = (int64_t)mkt.time_penalty_value;                 // fixed seconds before expiry
        } else {
            const int64_t duration = be - ct;                        // percentage of market lifetime
            if (duration <= 0) return 0;
            window = (int64_t)(fc::uint128_t((uint64_t)mkt.time_penalty_value)
                     * fc::uint128_t((uint64_t)duration) / fc::uint128_t(100u)).lo;
        }
        if (window <= 0) return 0;
        int64_t tte = be - (int64_t)risk_time.sec_since_epoch();      // time left when risk was taken
        if (tte < 0) tte = 0;
        if (tte >= window) return 0;                                 // placed before the window opened
        const int64_t into = window - tte;                           // deeper into window = later = harsher
        fc::uint128_t num, den;
        if (mkt.penalty_curve_type == 1) {                           // quadratic
            num = fc::uint128_t((uint64_t)into) * fc::uint128_t((uint64_t)into);
            den = fc::uint128_t((uint64_t)window) * fc::uint128_t((uint64_t)window);
        } else {                                                     // linear
            num = fc::uint128_t((uint64_t)into);
            den = fc::uint128_t((uint64_t)window);
        }
        int64_t penalty = (int64_t)(fc::uint128_t((uint64_t)max_time_penalty) * num / den).lo;
        if (penalty < 0) penalty = 0;
        if (penalty > (int64_t)max_time_penalty) penalty = max_time_penalty;
        return (uint32_t)penalty;
    }

    // ── Per-oracle live active-market counter (display-only, pm_oracle_object.active_markets) ──
    // O(1) upkeep so get_oracle/watchdogs read the count without paging list_markets. NEVER gates
    // consensus. A market is "active" == status 1; inc when it enters (create-active / accept),
    // dec when it leaves (resolve / no_contest / missed-void). Both no-op when the oracle has no
    // pm_oracle_object (self-oracle accounts that never registered) — such markets aren't counted
    // anywhere, and are correctly excluded by the seed/verify walks too.
    void pm_oracle_inc_active(database& db, const account_name_type& oracle) {
        const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
        auto it = oidx.find(oracle);
        if (it != oidx.end())
            db.modify(*it, [](pm_oracle_object& o) { o.active_markets++; });
    }
    // Guarded: decrements only if the market is still active(1), so it is safe (idempotent) to call
    // at any terminal transition even if the market was already non-active.
    void pm_oracle_dec_active(database& db, const pm_market_object& mkt) {
        if (mkt.status != 1) return;
        const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
        auto it = oidx.find(mkt.oracle);
        if (it != oidx.end())
            db.modify(*it, [](pm_oracle_object& o) { if (o.active_markets > 0) o.active_markets--; });
    }

    // ── Per-oracle workload gauges (display-only) ─────────────────────────────────
    // Generic ±1 on any uint32_t gauge of the oracle named `oracle`. Decrement clamps at 0 so a
    // stray double-dec can never wrap the counter. No-op when the oracle has no pm_oracle_object.
    // NEVER gates consensus (cosmetic aggregate; a drift is caught by pm_verify_oracle_gauges).
    void pm_oracle_gauge_adj(database& db, const account_name_type& oracle,
                             uint32_t pm_oracle_object::* field, int delta) {
        if (delta == 0) return;
        const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
        auto it = oidx.find(oracle);
        if (it == oidx.end()) return;
        db.modify(*it, [&](pm_oracle_object& o) {
            if (delta > 0) (o.*field) += (uint32_t)delta;
            else { uint32_t d = (uint32_t)(-delta); (o.*field) = (o.*field) > d ? (o.*field) - d : 0u; }
        });
    }
    // A dispute is leaving the open(0) state (responded/finalized/auto-closed): decrement whichever
    // open-dispute gauge it currently sits in, keyed on whether the oracle had responded. Call while
    // the dispute is still status 0 (oracle_response_time is stable across the transition either way).
    void pm_oracle_dispute_left_open(database& db, const account_name_type& oracle,
                                     const pm_dispute_object& d) {
        pm_oracle_gauge_adj(db, oracle,
            (d.oracle_response_time == time_point_sec())
                ? &pm_oracle_object::disputes_awaiting_response
                : &pm_oracle_object::disputes_awaiting_decision,
            -1);
    }

    // Resolution-latency histogram bucket for rt seconds — 8 buckets (≤1h,≤6h,≤24h,≤3d,≤7d,≤14d,≤30d,>30d).
    inline int pm_rt_bucket(uint64_t rt) {
        static const uint64_t ub[7] = {3600u, 21600u, 86400u, 259200u, 604800u, 1209600u, 2592000u};
        for (int i = 0; i < 7; ++i) if (rt <= ub[i]) return i;
        return 7;
    }

    // Pay queued lazy-pool withdrawals FIFO (oldest id first) from whatever is liquid in
    // free_balance right now. Each request is paid in full or in part; when free is exhausted we
    // stop. Called at every point capital returns to free_balance (LP return, deposit, leverage
    // repay) so queued withdrawers have first claim on returning capital and free_balance never
    // goes negative — the ledger never hands out more than the pool holds liquid.
    void service_lazy_withdraw_queue(database& db) {
        const auto* poolp = db.find<pm_lazy_pool_object>(pm_lazy_pool_id_type(0));
        if (!poolp) return;
        for (;;) {
            const auto& pool = db.get<pm_lazy_pool_object, by_id>(pm_lazy_pool_id_type(0));
            if (pool.free_balance.value <= 0) break;
            const auto& qidx = db.get_index<pm_lazy_withdraw_request_index>().indices().get<by_id>();
            if (qidx.empty()) break;
            const auto& req = *qidx.begin();                 // FIFO: lowest id == oldest
            const int64_t pay = std::min(pool.free_balance.value, req.amount.value);
            if (pay <= 0) break;
            db.adjust_balance(db.get_account(req.account), asset(share_type(pay), TOKEN_SYMBOL));
            db.modify(pool, [&](pm_lazy_pool_object& p) {
                p.free_balance        -= share_type(pay);
                p.pending_withdrawals -= share_type(pay);
            });
            if (req.amount.value == pay) {
                db.remove(req);                              // fully settled
            } else {
                db.modify(req, [&](pm_lazy_withdraw_request_object& r) { r.amount -= share_type(pay); });
                break;                                       // free_balance exhausted
            }
        }
    }

    // Return a lazy-pool LP position's capital to the pool: principal to free_balance,
    // yield (its share of the LP bonus) into the MasterChef accumulator (reward_per_share
    // ×1e9) so depositors can claim it. Mirrors the lazy deposit/withdraw accounting.
    void route_pool_lp_return(database& db, int64_t principal, int64_t yield) {
        const auto* pool = db.find<pm_lazy_pool_object>(pm_lazy_pool_id_type(0));
        if (!pool) return;
        db.modify(*pool, [&](pm_lazy_pool_object& p) {
            // Principal leaves allocation; principal AND yield both land in free_balance so the
            // yield is spendable on withdrawal (spec lazy-pool Transition 4: free += return).
            p.free_balance      += share_type(principal + yield);
            p.allocated_balance -= share_type(principal);
            if (yield > 0) {
                p.earned_balance += share_type(yield);
                if (p.total_shares.value > 0)
                    p.reward_per_share += fc::uint128_t((uint64_t)yield)
                                        * fc::uint128_t((uint64_t)1000000000)
                                        / fc::uint128_t((uint64_t)p.total_shares.value);
            }
        });
        service_lazy_withdraw_queue(db);   // returning capital first pays queued withdrawers
    }

    // Close the market's lazy allocation (recall-tracking object) once its LP position
    // has been settled/returned, so the recall cron skips it.
    void mark_alloc_settled(database& db, pm_market_id_type market) {
        const auto& aidx = db.get_index<pm_lazy_allocation_index>().indices().get<by_market>();
        auto it = aidx.find(market);
        if (it != aidx.end() && it->status == 0)
            db.modify(*it, [&](pm_lazy_allocation_object& a) {
                a.returned_amount += a.amount; a.amount = 0; a.status = 1;
            });
    }

    // ── Leverage funding (spec §7 — perpetual carry cost) ────────────────────────
    // Charge any whole 24h funding periods that have come due on an ACTIVE position.
    // funding_per_period = loan × pm_leverage_funding_rate_ppm_per_day / 1e6, accrued into
    // pos.funding_paid, which raises the effective pool obligation (liquidation_threshold +
    // funding_paid) and therefore pulls the liquidation point up over time. Funding is realized
    // out of the bettor's own equity (the collateral leg of the position value) at settlement.
    // Idempotent: after charging, funding_due_time is advanced strictly past `now`. One-shot
    // catch-up (multiplies by the number of whole periods due) so a cap-throttled sweep can never
    // under-charge. rate 0 → no-op except advancing the clock (no retroactive charge on re-enable).
    void accrue_leverage_funding(database& db, const pm_leverage_position_object& pos,
                                 uint32_t rate_ppm_per_day, time_point_sec now) {
        if (pos.status != 0) return;
        if (pos.funding_due_time == time_point_sec()) return;   // not initialized (pre-funding position)
        if (pos.funding_due_time > now) return;
        uint32_t due = ((now.sec_since_epoch() - pos.funding_due_time.sec_since_epoch()) / 86400u) + 1u;
        int64_t add = 0;
        if (rate_ppm_per_day > 0)
            add = (int64_t)(fc::uint128_t((uint64_t)pos.loan.value)
                          * fc::uint128_t((uint64_t)rate_ppm_per_day)
                          * fc::uint128_t((uint64_t)due)
                          / fc::uint128_t((uint64_t)1000000u)).lo;
        db.modify(pos, [&](pm_leverage_position_object& p) {
            p.funding_paid    += share_type(add);
            p.funding_due_time = time_point_sec(p.funding_due_time.sec_since_epoch() + 86400u * due);
            p.last_update      = now;
        });
    }

    // ── Leverage liquidation (spec §5/§6) ────────────────────────────────────────
    // Liquidate one position at CURRENT reserves: sell its tokens back, the pool recovers
    // min(cancel_value, obligation), the bettor gets any remainder. Zero-sum: the C+L that
    // entered the curve at open returns as cancel_value (split pool/bettor); the price-impact
    // difference accrues to the rest of the market. reason: 0 opposing-bet, 1 cancel-bet, 2 expiry,
    // 3 funding (position pulled underwater by accrued carry cost), 4 terminal void/no-contest
    // (refund the residual immediately — no outcome to defer a claim to). See F1/#300.
    void liquidate_position(database& db, const pm_leverage_position_object& pos, uint8_t reason) {
        // Bring funding current before settling so the obligation reflects carry owed to now.
        accrue_leverage_funding(db, pos,
            db.get_validator_schedule_object().median_props.pm_leverage_funding_rate_ppm_per_day,
            db.head_block_time());
        const auto& mkt = db.get<pm_market_object, by_id>(pos.market);
        int64_t cv = pm::leverage::cancel_value(mkt.reserve_a.value, mkt.reserve_b.value, mkt.k,
                                            pos.tokens.value, (int)pos.outcome_index);
        // Effective obligation = base pool markup (loan×(1+R%)) + accrued funding. The funding
        // portion is captured by pool_profit = pool_received − loan and thus flows to LP yield.
        int64_t obligation      = pos.liquidation_threshold.value + pos.funding_paid.value;
        int64_t pool_received   = cv < obligation ? cv : obligation;
        int64_t bettor_received = cv - pool_received; // ≥ 0
        int64_t pool_profit     = pool_received - pos.loan.value;

        // F1/#300 deferred-claim conservation. (C+L)=total_bet entered the curve at open. The pool
        // recovers its obligation from cv; everything the pool does NOT take stays in the market pot
        // (forfeit_pool). The bettor's residual (cv − pool_received) is NOT paid against the curve now —
        // it becomes an OUTCOME-CONTINGENT deferred claim paid at settlement from a bounded slice of the
        // losing pool (or nothing if its outcome loses). So forfeit gets `total_bet − pool_received`
        // (≥ 0 for normal closes → no negative-forfeit / uncovered mint, no LP hit). Model validated:
        // early-exit-deferred-claim.md. EXCEPTION reason 4 = terminal void/no-contest: there is no
        // outcome, so the bettor is REFUNDED immediately (old path) and forfeit gets total_bet − cv.
        const bool voiding = (reason == 4);
        // Clamp pot_retained at 0: accrued funding can push obligation above total_bet for a
        // long-lived position (funding_paid grows unbounded in accrue_leverage_funding), which
        // would drive forfeit_pool negative and manufacture an `uncovered` shortfall (LP hit /
        // mint) at settlement — defeating the F1/#300 no-uncovered guarantee. The pool still
        // recovers its obligation from cv (reserves); the pot simply never goes negative.
        const int64_t pot_retained = voiding ? (pos.total_bet.value - cv)
                                             : (pos.total_bet.value - pool_received);
        const int64_t pot_retained_capped = pot_retained > 0 ? pot_retained : 0;
        db.modify(mkt, [&](pm_market_object& m) { // unwind tokens (k preserved)
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
        db.modify(db.get<pm_lazy_pool_object, by_id>(pm_lazy_pool_id_type(0)), [&](pm_lazy_pool_object& p) {
            p.free_balance       += share_type(pool_received);
            p.leverage_fund_used -= pos.loan;
            if (pool_profit > 0) {
                p.earned_balance += share_type(pool_profit);
                if (p.total_shares.value > 0)
                    p.reward_per_share += fc::uint128_t((uint64_t)pool_profit)
                                        * fc::uint128_t((uint64_t)1000000000) / fc::uint128_t((uint64_t)p.total_shares.value);
            }
        });
        service_lazy_withdraw_queue(db);   // returning leverage capital first pays queued withdrawers
        if (bettor_received > 0) {
            if (voiding) {
                // Terminal void/no-contest: refund the residual immediately (no outcome to defer to).
                db.adjust_balance(db.get_account(pos.account), asset(share_type(bettor_received), TOKEN_SYMBOL));
            } else if (mkt.deferred_claim_count < MAX_PM_DEFERRED_CLAIMS_PER_MARKET) {
                // Defer as an outcome-contingent claim (F1/#300) — paid at settlement from the bounded
                // early-exit bucket iff pos.outcome_index wins; otherwise it pays nothing. #349: skip
                // once the per-market cap is hit (the residual stays in the curve and pays 0, exactly
                // like bucket-exhaustion) so settle_market's claim loop stays bounded.
                db.create<pm_deferred_claim_object>([&](pm_deferred_claim_object& c) {
                    c.market = pos.market; c.account = pos.account; c.kind = 1;
                    c.outcome_index = pos.outcome_index; c.claim_amount = share_type(bettor_received);
                    c.exit_time = db.head_block_time();
                });
                db.modify(mkt, [](pm_market_object& m) { m.deferred_claim_count++; });
            }
        }

        // reason 2 = resolving, 4 = voiding → SETTLEMENT (pm_leverage_resolve); 0/1/3 = mid-market
        // liquidation (pm_leverage_liquidate). `won` = the position was solvent.
        const bool settle = (reason == 2 || reason == 4);
        const bool won    = (cv >= obligation);
        db.modify(pos, [&](pm_leverage_position_object& p) {
            p.status = settle ? (won ? (uint8_t)2 : (uint8_t)3) : (uint8_t)1;
            p.cancel_value_at_liquidation = share_type(cv);
            p.pool_received   = share_type(pool_received);
            p.bettor_received = share_type(bettor_received);
            p.last_update     = db.head_block_time();
        });
        db.pm_adjust_frozen(pos.account, 2, -pos.collateral); // UNLOCK: collateral leaves active leverage (liquidate/settle)
        if (settle) {
            uint16_t lev = pos.collateral.value > 0
                         ? (uint16_t)(pos.total_bet.value / pos.collateral.value) : (uint16_t)0;
            db.push_virtual_operation(pm_leverage_resolve_operation(
                pos.account, pos.id._id, pos.market._id, won,
                asset(share_type(pool_received), TOKEN_SYMBOL),
                asset(share_type(bettor_received), TOKEN_SYMBOL),
                pos.outcome_index, lev));
        } else {
            db.push_virtual_operation(pm_leverage_liquidate_operation(
                pos.account, pos.id._id, pos.market._id,
                asset(share_type(cv), TOKEN_SYMBOL), asset(share_type(pool_received), TOKEN_SYMBOL),
                asset(share_type(bettor_received), TOKEN_SYMBOL), reason));
        }
    }

    // Cascade: liquidate EVERY active position on `market` (restricted to `side`, -1 = any)
    // whose cancel_value ≤ threshold, re-evaluating after each liquidation (reserves change).
    // No iteration cap — the pool must never be left exposed by a partial cascade. Termination
    // is guaranteed: each liquidation flips a position to status 1 (excluded from the scan),
    // so the loop runs at most once per position. One-directional (only same-side worsen).
    void cascade_liquidate(database& db, pm_market_id_type market, int16_t side, uint8_t reason) {
        for (;;) {
            const auto& mkt = db.get<pm_market_object, by_id>(market);
            const auto& idx = db.get_index<pm_leverage_position_index>().indices().get<by_lev_market_status>();
            auto it = idx.lower_bound(boost::make_tuple(market, (uint8_t)0, pm_leverage_position_id_type()));
            const pm_leverage_position_object* victim = nullptr;
            for (; it != idx.end() && it->market == market && it->status == 0; ++it) {
                if (side >= 0 && it->outcome_index != side) continue;
                int64_t cv = pm::leverage::cancel_value(mkt.reserve_a.value, mkt.reserve_b.value, mkt.k,
                                                    it->tokens.value, (int)it->outcome_index);
                if (cv <= it->liquidation_threshold.value + it->funding_paid.value) { victim = &*it; break; }
            }
            if (!victim) break;
            liquidate_position(db, *victim, reason);
        }
    }

    // Force-close ALL active positions on a market (terminal: market resolving/voided).
    // Unbounded by design — every position must be closed; terminates because each
    // liquidation flips status 0→1 (excluded from the next lower_bound).
    // reason 2 = normal resolution (defer residuals as claims); reason 4 = terminal void/no-contest
    // (refund residuals immediately, no outcome to defer to). See liquidate_position.
    void force_close_positions(database& db, pm_market_id_type market, uint8_t reason = 2) {
        for (;;) {
            const auto& idx = db.get_index<pm_leverage_position_index>().indices().get<by_lev_market_status>();
            auto it = idx.lower_bound(boost::make_tuple(market, (uint8_t)0, pm_leverage_position_id_type()));
            if (it == idx.end() || it->market != market || it->status != 0) break;
            liquidate_position(db, *it, reason);
        }
    }

    // Delete every deferred early-exit claim of a market WITHOUT paying (terminal void/no-contest:
    // no winning outcome, so outcome-contingent claims are worthless). F1/#300.
    void purge_deferred_claims(database& db, pm_market_id_type market) {
        const auto& cidx = db.get_index<pm_deferred_claim_index>().indices().get<by_claim_market>();
        std::vector<const pm_deferred_claim_object*> consumed;
        for (auto it = cidx.lower_bound(boost::make_tuple(market, pm_deferred_claim_id_type()));
             it != cidx.end() && it->market == market; ++it) consumed.push_back(&*it);
        for (const auto* c : consumed) db.remove(*c);
    }

    // Graduated recall: withdraw `amount` of the lazy pool's LP position from an idle
    // market and return it to free_balance. Mirrors pm_withdraw_liquidity — shrink
    // liquidity_sum and shrink the pricing curve price-neutrally (LMSR b for multi,
    // both CPMM reserves proportionally for binary) so depth tracks the remaining
    // capital while the odds stay put. Returns the amount actually recalled.
    share_type recall_pool_liquidity(database& db, const pm_market_object& mkt, share_type amount) {
        if (amount.value <= 0) return share_type(0);
        const auto& lidx = db.get_index<pm_liquidity_index>().indices().get<by_market>();
        const pm_liquidity_object* poollp = nullptr;
        for (auto it = lidx.lower_bound(boost::make_tuple(mkt.id, pm_liquidity_id_type()));
             it != lidx.end() && it->market == mkt.id; ++it) {
            if (it->status == 0 && it->provider.size() == 0) { poollp = &*it; break; }
        }
        if (!poollp) return share_type(0);

        share_type w = amount;
        if (w.value > poollp->amount.value) w = poollp->amount;
        share_type b_remove = 0;
        if (mkt.market_type == 1 && poollp->b_share.value > 0 && poollp->amount.value > 0) {
            b_remove = (w == poollp->amount) ? poollp->b_share :
                share_type((int64_t)(fc::uint128_t((uint64_t)poollp->b_share.value)
                          * fc::uint128_t((uint64_t)w.value) / fc::uint128_t((uint64_t)poollp->amount.value)).lo);
        }
        db.modify(mkt, [&](pm_market_object& m) {
            const int64_t L = m.liquidity_sum.value; // capital BEFORE this recall
            m.liquidity_sum -= w;
            if (m.market_type == 1) {
                m.lmsr_b -= b_remove;
            } else if (L > 0) {
                // CPMM: price-neutral shrink, mirroring pm_withdraw_liquidity so the
                // reserve ratio (odds) is preserved and depth tracks liquidity_sum.
                const fc::uint128_t num((uint64_t)(L - w.value));
                const fc::uint128_t den((uint64_t)L);
                m.reserve_a = share_type((int64_t)(fc::uint128_t((uint64_t)m.reserve_a.value) * num / den).lo);
                m.reserve_b = share_type((int64_t)(fc::uint128_t((uint64_t)m.reserve_b.value) * num / den).lo);
                m.k = fc::uint128_t((uint64_t)m.reserve_a.value) * fc::uint128_t((uint64_t)m.reserve_b.value);
            }
        });
        if (w == poollp->amount)
            db.modify(*poollp, [](pm_liquidity_object& l) { l.status = 3; });
        else
            db.modify(*poollp, [&](pm_liquidity_object& l) { l.amount -= w; l.b_share -= b_remove; });
        route_pool_lp_return(db, w.value, 0); // principal back to pool, no yield
        return w;
    }

    // ── Liquidity settlement ────────────────────────────────────────────────────
    // Returns every active LP's principal UNCONDITIONALLY and distributes `bonus`
    // (liquidity fee + time-penalty pool + undistributed winners' pool) weighted by
    // principal × time-in-market (pure, unit-tested distribute_lp; last entry absorbs
    // rounding). The lazy pool is a real LP (pm_liquidity_object with empty provider);
    // its principal + yield route back into the pool instead of to an account.
    // `uncovered` (F1): the parimutuel shortfall compute_settlement could not fund from the
    // losers'+forfeit pot (see settle_result::uncovered). LPs are the leverage counterparty, so the
    // shortfall is charged against LP principal pro-rata here — otherwise flooring winners_pool at 0
    // would silently emit exactly `uncovered` tokens. Default 0 for void/refund callers.
    void settle_liquidity(database& db, const pm_market_object& mkt, share_type bonus,
                          share_type uncovered = share_type(0)) {
        const auto& lidx = db.get_index<pm_liquidity_index>().indices().get<by_market>();
        auto first = lidx.lower_bound(boost::make_tuple(mkt.id, pm_liquidity_id_type()));

        const fc::time_point_sec now = db.head_block_time();

        // #348 / goal #290: F1 guarantees `uncovered == 0` by construction (winners_pool ≥ (1−cap)·losers
        // − fees ≥ 0). If it is ever > 0, some invariant broke (e.g. a new unbounded accrual like the
        // #141 funding path) and LP principal is being silently eroded (or tokens emitted when no LP).
        // Emit ONE always-on loud signal here — regardless of LP presence — so a regression is visible in
        // node logs / acceptance instead of slipping through silently the way #141 did. Log-only:
        // deterministic, no consensus effect, and does NOT halt (a hard assert would take the chain down
        // on an unforeseen edge, worse than charging LP).
        if (uncovered.value > 0)
            elog("PM F1 INVARIANT VIOLATED: uncovered shortfall ${u} on market ${m} — winners_pool<0 was "
                 "floored, shortfall charged to LP principal (or emitted if no LP). This must be 0 by "
                 "construction; investigate the exit/settle path.", ("u", uncovered.value)("m", mkt.id._id));

        std::vector<const pm_liquidity_object*> active;
        std::vector<pm::lp_in> lps;
        for (auto it = first; it != lidx.end() && it->market == mkt.id; ++it) {
            if (it->status != 0) continue;
            active.push_back(&*it);
            int64_t sec = (int64_t)now.sec_since_epoch() - (int64_t)it->deposit_time.sec_since_epoch();
            lps.push_back(pm::lp_in{ it->amount.value, sec });
        }
        if (active.empty()) {
            // No LP principal to absorb the F1 shortfall → 100% of it is emitted. This is the worst
            // case and must be logged here, above the early return, since the diagnostic in the charge
            // block below is unreachable when there are no LPs. (PR #124 review.)
            if (uncovered.value > 0)
                wlog("PM settle: no active LP to absorb uncovered ${u} on market ${m} — fully emitted",
                     ("u", uncovered.value)("m", mkt.id._id));
            return;
        }

        const std::vector<int64_t> shares = pm::distribute_lp(lps, bonus.value);

        // F1: split `uncovered` across LP principal pro-rata (by principal, not time — it's a capital
        // loss, not a fee). Each floor charge is <= its LP's principal (to_charge <= total_principal);
        // the rounding remainder is then spread ONLY over LPs that still have headroom, so no single
        // LP is ever charged above its principal (a naive "remainder → last LP" could exceed a small
        // last LP's principal, clamp to 0, and re-emit the difference — PR #124 review).
        std::vector<int64_t> charge(active.size(), 0);
        if (uncovered.value > 0) {
            int64_t total_principal = 0;
            for (auto* lp : active) total_principal += lp->amount.value;
            if (total_principal > 0) {
                int64_t to_charge = uncovered.value < total_principal ? uncovered.value : total_principal;
                int64_t assigned = 0;
                for (size_t i = 0; i < active.size(); ++i) {
                    int64_t c = (int64_t)(fc::uint128_t((uint64_t)to_charge)
                                * fc::uint128_t((uint64_t)active[i]->amount.value)
                                / fc::uint128_t((uint64_t)total_principal)).lo;
                    charge[i] = c; assigned += c;
                }
                // Distribute the floor remainder over LPs with headroom (charge < principal). Total
                // headroom (total_principal - assigned) >= (to_charge - assigned), so it always fits.
                int64_t remainder = to_charge - assigned;
                for (size_t i = 0; i < active.size() && remainder > 0; ++i) {
                    int64_t room = active[i]->amount.value - charge[i];
                    int64_t add  = remainder < room ? remainder : room;
                    charge[i] += add; remainder -= add;
                }
                // uncovered beyond the whole pool cannot be charged to anyone — it is a real (bounded)
                // over-emission the strict supply invariant (#126) would only catch on a later import,
                // so flag it here where it happens. pos_cap should make this unreachable.
                if (uncovered.value > total_principal)
                    wlog("PM settle: uncovered ${u} exceeds LP principal ${p} on market ${m} — ${e} emitted",
                         ("u", uncovered.value)("p", total_principal)("m", mkt.id._id)("e", uncovered.value - total_principal));
            } else if (uncovered.value > 0) {
                // active LPs exist but their principals sum to zero → nothing to charge, so 100% of
                // uncovered is emitted. Same over-emission as the no-active-LP case above, one level
                // deeper; log it here too so the supply-invariant trail is never silent. (PR #124 review.)
                wlog("PM settle: active LPs hold zero principal — uncovered ${u} fully emitted on market ${m}",
                     ("u", uncovered.value)("m", mkt.id._id));
            }
        }

        for (size_t i = 0; i < active.size(); ++i) {
            const pm_liquidity_object& lp = *active[i];
            share_type share(shares[i]);
            int64_t principal_ret = lp.amount.value - charge[i];
            if (principal_ret < 0) principal_ret = 0; // defensive; charge[i] ≤ amount by construction
            if (lp.provider.size() > 0) {
                share_type ret = share_type(principal_ret + share.value);
                if (ret.value > 0)
                    db.adjust_balance(db.get_account(lp.provider), asset(ret, TOKEN_SYMBOL));
                db.pm_adjust_frozen(lp.provider, 0, -lp.amount); // UNLOCK: full committed principal releases
            } else {
                route_pool_lp_return(db, principal_ret, share.value); // lazy pool LP
            }
            db.modify(lp, [&](pm_liquidity_object& l) { l.earned_fee += share; l.status = 3; });
        }

        mark_alloc_settled(db, mkt.id);
    }

    // ── Market settlement (unified parimutuel, spec parimutuel-settlement.md §2) ──
    // Winners are paid by curve WEIGHT out of the losers' stakes:
    //   losers_sum   = Σ amount of losing bets
    //   winners_pool = losers_sum − oracle_fee − creator_fee − liq_fee − oracle_fixed + forfeit_pool
    //   payout_i     = bet_amount_i + floor(winners_pool × weight_i / Σweight) − time_penalty(profit)
    // LP principal is returned unconditionally; liq_fee + time-penalty pool + any
    // undistributed winners_pool form the LP bonus. Money is conserved exactly:
    //   Σ outputs == Σ all bet amounts + LP principal + forfeit_pool.
    // oracle_fixed_fee is funded FROM the pool (capped), never minted.
    void settle_market(database& db, const pm_market_object& mkt) {
        const bool binary = (mkt.market_type == 0);
        const int16_t win = mkt.resolved_outcome;

        // Any leveraged positions still open at settlement are force-closed first (normally the
        // expiration buffer prevents this; terminal safety net). On a normal resolution (win≥0) the
        // residuals defer as outcome-contingent claims; on void/no-contest (win<0) they refund now.
        force_close_positions(db, mkt.id, win < 0 ? (uint8_t)4 : (uint8_t)2);

        // Zero-volume resolution → fault stamp on the oracle (§4.10 lazy-pool defense:
        // discourages spam markets that lock pool capital with no betting volume).
        if (win >= 0 && mkt.bets_sum.value == 0) {
            const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
            auto oit = oidx.find(mkt.oracle);
            if (oit != oidx.end())
                db.modify(*oit, [&](pm_oracle_object& o) {
                    o.penalty_stamps++;
                    o.last_penalty_stamp_time = db.head_block_time();
                });
        }

        const auto& bidx = db.get_index<pm_bet_index>().indices().get<by_market>();
        auto first = bidx.lower_bound(boost::make_tuple(mkt.id, pm_bet_id_type()));

        // Void / no-contest finalization: refund every active bet, return LP, and slash the
        // no-contest penalty (pm_no_contest_penalty_percent of the dispute fee) from the oracle,
        // distributed pro-rata to the refunded bettors as compensation (spec §3.9). Only the
        // no-contest path reaches settle with win<0 (missed/auto-close refund in their own crons).
        if (win < 0) {
            const auto& mp = median(db);
            std::vector<std::pair<account_name_type, int64_t>> participants;
            int64_t total_bets = 0;
            for (auto it = first; it != bidx.end() && it->market == mkt.id; ++it)
                if (it->status == 0) { participants.emplace_back(it->account, it->amount.value); total_bets += it->amount.value; }

            for (auto it = first; it != bidx.end() && it->market == mkt.id; ) {
                const auto& bet = *it; ++it;
                if (bet.status != 0) continue;
                db.adjust_balance(db.get_account(bet.account), asset(bet.amount, TOKEN_SYMBOL));
                db.pm_adjust_frozen(bet.account, 1, -bet.amount); // UNLOCK: stake refunded on void
                db.modify(bet, [](pm_bet_object& b) { b.status = 2; });
            }
            settle_liquidity(db, mkt, 0);

            share_type penalty(0);
            if (total_bets > 0) {
                const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
                auto oit = oidx.find(mkt.oracle);
                if (oit != oidx.end()) {
                    int64_t want = (int64_t)(fc::uint128_t((uint64_t)mp.pm_dispute_fee.amount.value)
                        * fc::uint128_t((uint64_t)mp.pm_no_contest_penalty_percent) / fc::uint128_t(10000)).lo;
                    penalty = share_type(want > oit->insurance.value ? oit->insurance.value : want);
                    if (penalty.value > 0)
                        db.modify(*oit, [&](pm_oracle_object& o) {
                            o.insurance               -= penalty;
                            o.total_insurance_slashed += penalty;
                        });
                }
            }
            if (penalty.value > 0) {
                int64_t paid = 0;
                for (size_t i = 0; i < participants.size(); ++i) {
                    int64_t share = (i + 1 == participants.size())
                        ? penalty.value - paid
                        : (int64_t)(fc::uint128_t((uint64_t)penalty.value)
                            * fc::uint128_t((uint64_t)participants[i].second) / fc::uint128_t((uint64_t)total_bets)).lo;
                    if (share > 0) {
                        db.adjust_balance(db.get_account(participants[i].first), asset(share_type(share), TOKEN_SYMBOL));
                        paid += share;
                    }
                }
            }
            // #5 (audit 2026-08-12): route forfeit_pool on void instead of orphaning it. On a normal
            // resolution forfeit_pool flows into winners_pool (paid via adjust_balance); zeroing it to
            // nowhere here left those real tokens in current_supply with no owner → a growing
            // conservation deficit (replay-halt risk). Bettors present → return pro-rata by stake
            // (mirrors the win≥0 path, tokens re-enter accounted balances); none → burn from supply.
            const int64_t fpool = mkt.forfeit_pool.value; // ≥ 0 by construction
            if (fpool > 0) {
                if (total_bets > 0) {
                    int64_t paid = 0;
                    for (size_t i = 0; i < participants.size(); ++i) {
                        int64_t share = (i + 1 == participants.size())
                            ? fpool - paid
                            : (int64_t)(fc::uint128_t((uint64_t)fpool)
                                * fc::uint128_t((uint64_t)participants[i].second) / fc::uint128_t((uint64_t)total_bets)).lo;
                        if (share > 0) {
                            db.adjust_balance(db.get_account(participants[i].first), asset(share_type(share), TOKEN_SYMBOL));
                            paid += share;
                        }
                    }
                } else {
                    db.burn_asset(asset(share_type(-fpool), TOKEN_SYMBOL)); // no bettors → remove from supply
                }
            }
            purge_deferred_claims(db, mkt.id); // F1/#300: no winning outcome → early-exit claims pay 0
            db.modify(mkt, [](pm_market_object& m) { m.forfeit_pool = 0; });
            return;
        }

        auto is_winner = [&](const pm_bet_object& b) {
            return binary ? (b.side == win) : (b.outcome_index == win);
        };

        // Pass 1: collect winners (curve weight = claim) and losing stakes, then
        // delegate the money split to the pure, unit-tested parimutuel math.
        pm::settle_params sp;
        sp.oracle_fee_percent    = mkt.oracle_fee_percent;
        sp.creator_fee_percent   = mkt.creator_fee_percent;
        sp.liquidity_fee_percent = mkt.liquidity_fee_percent;
        sp.forfeit_pool           = mkt.forfeit_pool.value;
        sp.oracle_fixed_fee       = mkt.oracle_fixed_fee.value;

        std::vector<pm::winner_in> winners;
        std::vector<const pm_bet_object*> winner_bets;
        std::vector<const pm_bet_object*> loser_bets;
        int64_t losers_sum = 0;
        for (auto it = first; it != bidx.end() && it->market == mkt.id; ++it) {
            if (it->status != 0) continue;
            if (is_winner(*it)) {
                winners.push_back(pm::winner_in{it->amount.value, it->weight.value, it->time_penalty});
                winner_bets.push_back(&*it);
            } else {
                losers_sum += it->amount.value;
                loser_bets.push_back(&*it);
            }
        }
        sp.losers_sum = losers_sum;

        // F1/#300: pay outcome-contingent deferred claims (early bet-cancels + leverage closes) BEFORE
        // the parimutuel split, from a BOUNDED slice of the losing pool. Only claims on the winning
        // outcome are funded, FIFO by exit order (== id via by_claim_market), capped so the total drawn
        // from losers ≤ pm_early_exit_reward_cap_percent × losers_sum. The unfunded remainder is a
        // haircut; the unused slice simply stays in the pot (we subtract only what is PAID from
        // winners_pool, so leftover flows to winners). Losing-outcome claims pay 0. Every claim for this
        // market is then consumed. Conservation model: docs/prediction-markets/early-exit-deferred-claim.md.
        // (Inert until the exit paths record claims; a market with no claims iterates nothing.)
        int64_t paid_claims = 0;
        {
            const auto& mp = median(db);
            int64_t bucket = (int64_t)(fc::uint128_t((uint64_t)losers_sum)
                * fc::uint128_t(mp.pm_early_exit_reward_cap_percent) / fc::uint128_t(10000u)).lo;
            // ADVERSARIAL FIX (own F1 review, goal #350 paper Theorem 2): the reward CAP alone does
            // NOT bound solvency. Per-market fees are capped only by oracle+creator+liquidity ≤ 100%
            // at creation (pm_operations.cpp:64) — NOT by any chain param, so validate() can't guard
            // it — and a valid market can push fees near 100%. With the default early-exit cap (33%)
            // that makes fees + bucket exceed losers_sum, and compute_settlement would floor
            // winners_pool at 0 and charge the shortfall to LP principal (`uncovered`, F1) — reachable
            // with VALID default params, not just extreme medians. Clamp the bucket to the settlement
            // headroom (== winners_pool BEFORE claims) so paid_claims can never drive winners_pool
            // negative → Theorem 2 holds unconditionally, no LP hit, no mint. This only ever REDUCES
            // the bucket (min), so it is strictly more conservative than before. Fee math MUST mirror
            // parimutuel.cpp:13-25 exactly (same int64 order/flooring) so headroom == the pot the
            // split will actually see.
            {
                const int64_t oracle_fee  = losers_sum * (int64_t)mkt.oracle_fee_percent    / 10000;
                const int64_t creator_fee = losers_sum * (int64_t)mkt.creator_fee_percent   / 10000;
                const int64_t liq_fee     = losers_sum * (int64_t)mkt.liquidity_fee_percent / 10000;
                int64_t avail = losers_sum - oracle_fee - creator_fee - liq_fee;
                if (avail < 0) avail = 0;
                const int64_t fixed_paid = (mkt.oracle_fixed_fee.value < avail)
                    ? mkt.oracle_fixed_fee.value : avail;
                int64_t headroom = avail - fixed_paid + mkt.forfeit_pool.value;
                if (headroom < 0) headroom = 0;
                if (bucket > headroom) bucket = headroom;
            }
            const auto& cidx = db.get_index<pm_deferred_claim_index>().indices().get<by_claim_market>();
            auto cit = cidx.lower_bound(boost::make_tuple(mkt.id, pm_deferred_claim_id_type()));
            std::vector<const pm_deferred_claim_object*> consumed;
            for (; cit != cidx.end() && cit->market == mkt.id; ++cit) {
                const pm_deferred_claim_object& c = *cit;
                if ((int16_t)c.outcome_index == win) {
                    int64_t remaining = bucket - paid_claims;
                    int64_t pay = c.claim_amount.value < remaining ? c.claim_amount.value : remaining;
                    if (pay > 0) {
                        db.adjust_balance(db.get_account(c.account), asset(share_type(pay), TOKEN_SYMBOL));
                        paid_claims += pay;
                        // Put the credit in the early-exiter's account history (adjust_balance alone
                        // leaves no trace); `claimed` vs `paid` exposes any bucket-exhaustion haircut.
                        db.push_virtual_operation(pm_early_exit_claim_paid_operation(
                            c.account, mkt.id._id, c.kind, c.outcome_index,
                            asset(c.claim_amount, TOKEN_SYMBOL), asset(share_type(pay), TOKEN_SYMBOL)));
                    }
                }
                consumed.push_back(&c);
            }
            for (const auto* c : consumed) db.remove(*c);
        }
        // Fold the paid claims into forfeit_pool so the parimutuel winners' pool drops by exactly what
        // early-exiters were paid (never below (1−cap)·losers − fees ≥ 0 → no uncovered mint, no LP hit).
        sp.forfeit_pool = mkt.forfeit_pool.value - paid_claims;

        const pm::settle_result res = pm::compute_settlement(sp, winners);

        if (res.oracle_take > 0)
            db.adjust_balance(db.get_account(mkt.oracle),  asset(share_type(res.oracle_take),  TOKEN_SYMBOL));
        if (res.creator_take > 0)
            db.adjust_balance(db.get_account(mkt.creator), asset(share_type(res.creator_take), TOKEN_SYMBOL));

        // Per-bettor settlement record (winners + losers) so history parsers see each result.
        for (const auto* lb : loser_bets) {
            db.pm_adjust_frozen(lb->account, 1, -lb->amount); // UNLOCK: losing stake leaves the bet set at settle
            db.modify(*lb, [](pm_bet_object& b) { b.status = 3; b.resolved_amount = 0; });
            db.push_virtual_operation(pm_payout_operation(
                lb->account, mkt.id._id, lb->id._id, lb->side, lb->outcome_index,
                asset(lb->amount, TOKEN_SYMBOL), asset(share_type(0), TOKEN_SYMBOL)));
        }

        for (size_t i = 0; i < winner_bets.size(); ++i) {
            share_type payout(res.winner_payout[i]);
            if (payout.value > 0)
                db.adjust_balance(db.get_account(winner_bets[i]->account), asset(payout, TOKEN_SYMBOL));
            db.pm_adjust_frozen(winner_bets[i]->account, 1, -winner_bets[i]->amount); // UNLOCK: original stake leaves the bet set (payout is winnings)
            db.modify(*winner_bets[i], [&](pm_bet_object& b) { b.status = 3; b.resolved_amount = payout; });
            db.push_virtual_operation(pm_payout_operation(
                winner_bets[i]->account, mkt.id._id, winner_bets[i]->id._id,
                winner_bets[i]->side, winner_bets[i]->outcome_index,
                asset(winner_bets[i]->amount, TOKEN_SYMBOL), asset(payout, TOKEN_SYMBOL)));
        }

        settle_liquidity(db, mkt, share_type(res.lp_bonus), share_type(res.uncovered)); // F1: charge shortfall to LP principal
        db.modify(mkt, [](pm_market_object& m) { m.forfeit_pool = 0; });
    }

    // Garbage-collect a fully-finalized market and its whole object cluster. A settled market
    // (status 3 / payout_status 3) can no longer be acted on — no betting, dispute, resolve or
    // payout is possible — it only lingers "for history". process_pm_markets() calls this a few
    // days after closure. Deletion is driven purely by chain time, so every node prunes exactly
    // the same markets at the same block → shared-memory state and snapshots stay identical
    // network-wide. Nothing else holds an id-reference to a settled market, so no dangling refs.
    void gc_market(database& db, const pm_market_object& mkt) {
        const pm_market_id_type mid = mkt.id;
        // Composite (market, …)-keyed indexes: drop the whole market range.
        auto drop_range = [&](const auto& idx) {
            for (auto it = idx.lower_bound(boost::make_tuple(mid));
                 it != idx.end() && it->market == mid; ) {
                const auto& obj = *it; ++it; db.remove(obj);
            }
        };
        // Single-key (unique per market) by_market indexes: at most one row.
        auto drop_unique = [&](const auto& idx) {
            auto it = idx.find(mid);
            if (it != idx.end()) db.remove(*it);
        };
        drop_range(db.get_index<pm_outcome_index>().indices().get<by_market_outcome>());
        drop_range(db.get_index<pm_bet_index>().indices().get<by_market>());
        drop_range(db.get_index<pm_liquidity_index>().indices().get<by_market>());
        drop_range(db.get_index<pm_commit_index>().indices().get<by_market>());
        drop_range(db.get_index<pm_dispute_vote_index>().indices().get<by_market_voter>());
        drop_range(db.get_index<pm_leverage_position_index>().indices().get<by_lev_market_status>());
        // Backstop: every terminal path that can hold deferred claims already clears them —
        // settle_market consumes them, refund_all_bets purges them on void/no-contest/missed-
        // resolution, and accept-expired markets are pending (never had bets). This drop is a
        // defensive net so no overlooked or future finalize path can leave a claim row dangling
        // in shared memory / drift the snapshot. Money-neutral: the range is normally empty here.
        drop_range(db.get_index<pm_deferred_claim_index>().indices().get<by_claim_market>());
        drop_unique(db.get_index<pm_dispute_index>().indices().get<by_market>());
        drop_unique(db.get_index<pm_lazy_allocation_index>().indices().get<by_market>());
        db.remove(mkt);
    }

    void refund_all_bets(database& db, const pm_market_object& mkt) {
        const auto& bidx = db.get_index<pm_bet_index>().indices().get<by_market>();
        auto it = bidx.lower_bound(boost::make_tuple(mkt.id, pm_bet_id_type()));
        while (it != bidx.end() && it->market == mkt.id) {
            if (it->status == 0) {
                db.adjust_balance(db.get_account(it->account), asset(it->amount, TOKEN_SYMBOL));
                db.pm_adjust_frozen(it->account, 1, -it->amount); // UNLOCK: stake refunded (missed-resolution/auto-close)
                db.modify(*it, [](pm_bet_object& b) { b.status = 2; });
            }
            ++it;
        }
    }

    void return_liquidity(database& db, const pm_market_object& mkt) {
        // Void/refund terminal: force-close open leveraged positions (reason 4 = refund residuals now,
        // no outcome to defer to) and discard any deferred early-exit claims (no winning outcome).
        force_close_positions(db, mkt.id, 4);
        purge_deferred_claims(db, mkt.id);
        const auto& lidx = db.get_index<pm_liquidity_index>().indices().get<by_market>();
        auto it = lidx.lower_bound(boost::make_tuple(mkt.id, pm_liquidity_id_type()));
        while (it != lidx.end() && it->market == mkt.id) {
            if (it->status == 0) {
                if (it->provider.size() > 0) {
                    db.adjust_balance(db.get_account(it->provider), asset(it->amount, TOKEN_SYMBOL));
                    db.pm_adjust_frozen(it->provider, 0, -it->amount); // UNLOCK: LP principal on void/refund
                } else
                    route_pool_lp_return(db, it->amount.value, 0); // lazy pool LP, no yield on refund
                db.modify(*it, [](pm_liquidity_object& l) { l.status = 3; });
            }
            ++it;
        }
        // Void/refund paths (no_contest, missed, auto-close) bypass settle_liquidity, so
        // close the lazy allocation's recall-tracking object here.
        mark_alloc_settled(db, mkt.id);
    }

    // Allocate a slice of idle lazy-pool capital to a newly-active market as a REAL LP
    // position (enters CPMM reserves / LMSR b, provider = pool) — spec lazy-pool-properties
    // Transition 2. Triggered at activation (status 0→1, before bets). One allocation per
    // market, bounded by pm_lazy_alloc_percent of free_balance and pm_lazy_max_total_alloc_percent
    // of total pool capital, minus the §4.10 per-oracle exposure penalties.
    void maybe_allocate_lazy(database& db, const pm_market_object& mkt) {
        const auto& mp = median(db);
        if (!mp.pm_lazy_pool_enabled) return;
        // Reward floor: the pool only subsidizes markets whose LP fee pays it enough. Below the
        // governed minimum it stays out entirely (spec lazy-pool §min-fee gate).
        if (mkt.liquidity_fee_percent < mp.pm_lazy_min_liquidity_fee_percent) return;
        const auto* pool = db.find<pm_lazy_pool_object>(pm_lazy_pool_id_type(0));
        if (!pool || pool->free_balance.value <= 0) return;

        const auto& aidx = db.get_index<pm_lazy_allocation_index>().indices().get<by_market>();
        if (aidx.find(mkt.id) != aidx.end()) return; // already allocated

        int64_t total_capital = pool->free_balance.value + pool->allocated_balance.value;
        int64_t max_total = (int64_t)(fc::uint128_t((uint64_t)total_capital)
                            * fc::uint128_t(mp.pm_lazy_max_total_alloc_percent) / fc::uint128_t(10000)).lo;
        int64_t headroom = max_total - pool->allocated_balance.value;
        if (headroom <= 0) return;

        int64_t alloc = (int64_t)(fc::uint128_t((uint64_t)pool->free_balance.value)
                       * fc::uint128_t(mp.pm_lazy_alloc_percent) / fc::uint128_t(10000)).lo;
        if (alloc > pool->free_balance.value) alloc = pool->free_balance.value;
        if (alloc > headroom) alloc = headroom;
        if (alloc <= 0) return;

        const auto now = db.head_block_time();

        // Per-oracle exposure penalties (security-threat-model §4.10) — limit how much
        // pool capital a single oracle can attract into idle/abusive markets.
        const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
        auto oit = oidx.find(mkt.oracle);
        {
            // (a) Active-market penalty: 5% recursive reduction per OTHER active market
            //     of this oracle (bounded scan; cap 20 markets).
            const auto& midx = db.get_index<pm_market_index>().indices().get<by_oracle>();
            uint32_t active_cnt = 0, scanned = 0;
            for (auto mit = midx.lower_bound(mkt.oracle);
                 mit != midx.end() && mit->oracle == mkt.oracle && scanned < 128 && active_cnt < 20;
                 ++mit, ++scanned) {
                if (mit->status == 1 && mit->id != mkt.id) ++active_cnt;
            }
            for (uint32_t i = 0; i < active_cnt; ++i) alloc = alloc * 95 / 100;

            // (b) Fault penalty stamps: zero-volume resolutions stamp the oracle; each
            //     non-expired stamp halves the allocation (cap 4). Stamps expire 10 days
            //     after the most recent one (coarse, deterministic decay).
            if (oit != oidx.end() && oit->penalty_stamps > 0 &&
                now < oit->last_penalty_stamp_time + fc::seconds(864000)) {
                uint32_t st = oit->penalty_stamps > 4 ? 4 : oit->penalty_stamps;
                for (uint32_t i = 0; i < st; ++i) alloc /= 2;
            }
        }
        if (alloc <= 0) return;

        // Deploy the allocation into the curve as a REAL LP position (provider = pool),
        // exactly like pm_add_liquidity. This happens at activation (status 0→1, before
        // any bets) — the genesis of the active curve, not a mid-life edit.
        share_type b_share = 0;
        if (mkt.market_type == 1 && mkt.liquidity_sum.value > 0) {
            b_share = share_type((int64_t)(fc::uint128_t((uint64_t)mkt.lmsr_b.value)
                      * fc::uint128_t((uint64_t)alloc) / fc::uint128_t((uint64_t)mkt.liquidity_sum.value)).lo);
        }
        db.modify(mkt, [&](pm_market_object& m) {
            m.liquidity_sum += share_type(alloc);
            if (m.market_type == 1) {
                m.lmsr_b       += b_share;
                m.lmsr_subsidy += share_type(alloc);
            } else {
                share_type half = share_type(alloc / 2);
                m.reserve_a += half;
                m.reserve_b += share_type(alloc - half.value);
                m.k = fc::uint128_t((uint64_t)m.reserve_a.value) * fc::uint128_t((uint64_t)m.reserve_b.value);
            }
        });
        db.create<pm_liquidity_object>([&](pm_liquidity_object& lp) {
            lp.market       = mkt.id;
            lp.provider     = account_name_type(); // empty = lazy pool
            lp.amount       = share_type(alloc);
            lp.deposit_time = now;
            lp.status       = 0;
            lp.b_share      = b_share;
        });
        db.create<pm_lazy_allocation_object>([&](pm_lazy_allocation_object& a) {
            a.market            = mkt.id;
            a.amount            = share_type(alloc);
            a.original_amount   = share_type(alloc);
            a.bets_sum_at_check = mkt.bets_sum;
            a.last_check_time   = now;
            a.check_step        = 0;
            a.status            = 0;
        });
        db.modify(*pool, [&](pm_lazy_pool_object& p) {
            p.free_balance      -= share_type(alloc);
            p.allocated_balance += share_type(alloc);
        });
    }

    bool verify_commit(const pm_commit_object& commit,
                       int8_t side, int16_t outcome_index,
                       share_type amount, share_type min_tokens, const std::string& salt) {
        fc::sha256::encoder enc;
        int64_t mid = commit.market._id;
        enc.write(reinterpret_cast<const char*>(&mid),          (uint32_t)sizeof(mid));
        enc.write(reinterpret_cast<const char*>(&commit.account.data), (uint32_t)sizeof(commit.account.data));
        enc.write(reinterpret_cast<const char*>(&side),         (uint32_t)sizeof(side));
        enc.write(reinterpret_cast<const char*>(&outcome_index),(uint32_t)sizeof(outcome_index));
        int64_t amt = amount.value;
        enc.write(reinterpret_cast<const char*>(&amt),          (uint32_t)sizeof(amt));
        int64_t mnt = min_tokens.value;
        enc.write(reinterpret_cast<const char*>(&mnt),          (uint32_t)sizeof(mnt));
        enc.write(salt.data(),                                   (uint32_t)salt.size());
        return enc.result() == commit.commitment;
    }

    // Get pm_market_object by raw int64 id
    const pm_market_object& get_market(const database& db, int64_t market_id) {
        return db.get<pm_market_object, by_id>(pm_market_id_type(market_id));
    }

} // anonymous namespace

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
        db.create<pm_dispute_vote_object>([&](pm_dispute_vote_object& v) {
            v.market       = mkt.id;
            v.voter        = o.voter;
            v.vote_outcome = o.vote_outcome;
            v.vote_percent = o.vote_percent;
            v.time         = now;
        });
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

    const auto& acct = db.get_account(o.account);
    FC_ASSERT(acct.balance >= o.collateral, "Insufficient balance for collateral");

    const auto& pool = db.get<pm_lazy_pool_object, by_id>(pm_lazy_pool_id_type(0));
    const int64_t loan       = o.loan.amount.value;
    const int64_t collateral = o.collateral.amount.value;

    // Constraint 1: leverage-fund availability + per-position cap.
    int64_t free_amount = pool.free_balance.value - pool.leverage_fund_used.value;
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
// Uses the anonymous-namespace helpers (settle_market, refund_all_bets,
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

void database::process_pm_markets() {
    if (!has_hardfork(CHAIN_HARDFORK_14)) return;

    // First seed OR the task #266 corrective re-seed (fixes the over-counted live counters). The
    // (re)seed zeroes then re-sums from objects, so running it again converges to the exact totals.
    if (!get_dynamic_global_properties().pm_frozen_counters_reseeded_v1) {
        const bool first = !get_dynamic_global_properties().pm_frozen_counters_seeded;
        pm_seed_frozen_counters();
        modify(get_dynamic_global_properties(), [](dynamic_global_property_object& d) {
            d.pm_frozen_counters_seeded      = true;
            d.pm_frozen_counters_reseeded_v1 = true;
        });
        ilog("pm frozen-counters ${w}; drift-check ${r}",
             ("w", first ? "seeded" : "re-seeded (#266 correction)")
             ("r", pm_verify_frozen_counters() ? "clean" : "MISMATCH"));
    }

    // One-time seed of the per-oracle live active-market counter (display-only). Runs once on the
    // first block after this upgrade; thereafter maintained incrementally by pm_oracle_inc/dec_active.
    if (!get_dynamic_global_properties().pm_active_markets_seeded) {
        pm_seed_oracle_active_markets();
        modify(get_dynamic_global_properties(), [](dynamic_global_property_object& d) {
            d.pm_active_markets_seeded = true;
        });
        ilog("pm oracle active_markets seeded; drift-check ${r}",
             ("r", pm_verify_oracle_active_markets() ? "clean" : "MISMATCH"));
    }

    // One-time seed of the per-oracle workload gauges (display-only). Same pattern as above.
    if (!get_dynamic_global_properties().pm_oracle_gauges_seeded) {
        pm_seed_oracle_gauges();
        modify(get_dynamic_global_properties(), [](dynamic_global_property_object& d) {
            d.pm_oracle_gauges_seeded = true;
        });
        ilog("pm oracle workload gauges seeded; drift-check ${r}",
             ("r", pm_verify_oracle_gauges() ? "clean" : "MISMATCH"));
    }

    const auto  now = head_block_time();
    const auto& mp  = get_validator_schedule_object().median_props;
    uint32_t cap  = (mp.pm_processing_cap_per_block > 0) ? (uint32_t)mp.pm_processing_cap_per_block : 20u;
    uint32_t done = 0;

    // ── 1. Commit forfeits ────────────────────────────────────────────────────
    // Commitments that were never revealed within the reveal window.
    {
        const auto& idx = get_index<pm_commit_index>().indices().get<by_reveal_deadline>();
        auto it = idx.lower_bound(boost::make_tuple(
            (uint8_t)0, time_point_sec(0), pm_commit_id_type()));
        while (it != idx.end() && it->status == 0 && it->reveal_deadline <= now && done < cap) {
            const auto& commit = *it; ++it;

            share_type penalty = share_type(commit.escrow_amount.value *
                                            commit.no_reveal_fee_percent / 10000);
            share_type refund  = share_type(commit.escrow_amount.value - penalty.value);

            if (refund.value > 0)
                adjust_balance(get_account(commit.account), asset(refund, TOKEN_SYMBOL));

            if (penalty.value > 0) {
                const auto* mkt_ptr = find<pm_market_object>(commit.market);
                if (mkt_ptr)
                    modify(*mkt_ptr, [&](pm_market_object& m) { m.forfeit_pool += penalty; });
            }

            push_virtual_operation(pm_commit_forfeit_operation(
                commit.account, commit.id._id, commit.market._id,
                asset(penalty, TOKEN_SYMBOL), asset(refund, TOKEN_SYMBOL)));

            modify(commit, [](pm_commit_object& c) { c.status = 2; }); // forfeited
            ++done;
        }
    }

    // ── 2. Oracle missed resolution deadline ──────────────────────────────────
    // Active markets whose result_expiration passed with no oracle report, plus a resolution grace.
    // The grace is not optional politeness: process_pm_markets() runs at the END of a block, after
    // dgp.time has advanced to this block's timestamp (database.cpp update_global_dynamic_data),
    // whereas in-block transactions still saw the PREVIOUS block's time. A zero-slack void
    // (result_expiration <= now) therefore fires one block before any resolve transaction's clock
    // can first reach result_expiration, making pm_resolve_market unreachable for a fixed-deadline
    // market without allow_early_resolution — it would always die here as missed-resolution and slash
    // the oracle for a deadline it had no reachable block to meet. Voiding only once
    // result_expiration + pm_dispute_grace_sec has elapsed (the same cutoff the settle sweep in §5
    // uses) leaves the oracle a real window [result_expiration, result_expiration + grace] to report.
    {
        const time_point_sec cutoff(
            (now.sec_since_epoch() > mp.pm_dispute_grace_sec)
                ? (now.sec_since_epoch() - mp.pm_dispute_grace_sec)
                : 0u);
        const auto& idx = get_index<pm_market_index>().indices().get<by_result_expiration>();
        auto it = idx.lower_bound(boost::make_tuple(
            (int8_t)1, time_point_sec(0), pm_market_id_type()));
        while (it != idx.end() && it->status == 1 && it->result_expiration <= cutoff && done < cap) {
            const auto& mkt = *it; ++it;

            share_type slashed(0);
            const auto& oidx = get_index<pm_oracle_index>().indices().get<by_owner>();
            auto oit = oidx.find(mkt.oracle);
            if (oit != oidx.end() && oit->insurance.value > 0) {
                slashed = share_type(oit->insurance.value * mp.pm_oracle_penalty_percent / 10000);
                if (slashed.value > 0) {
                    modify(*oit, [&](pm_oracle_object& o) {
                        o.insurance -= slashed;
                        o.missed_count++;
                        o.total_insurance_slashed += slashed;
                    });
                    adjust_balance(get_account(CHAIN_COMMITTEE_ACCOUNT),
                                   asset(slashed, TOKEN_SYMBOL));
                }
            }

            refund_all_bets(*this, mkt);
            return_liquidity(*this, mkt);

            pm_oracle_dec_active(*this, mkt);   // leaves active set (1 → 3, missed-resolution void)
            modify(mkt, [&](pm_market_object& m) {
                m.status           = 3;
                m.payout_status    = 3; // closed — no payout
                m.resolved_outcome = -1;
                m.finalized_time   = now;
            });

            push_virtual_operation(pm_oracle_missed_penalty_operation(
                mkt.oracle, mkt.id._id, asset(slashed, TOKEN_SYMBOL)));
            ++done;
        }
    }

    // ── 2b. Oracle acceptance window expired ───────────────────────────────────
    // Pending markets (status 0) the named oracle never accepted nor rejected within
    // pm_oracle_accept_window_sec. Refund the creator's seed liquidity (return_liquidity)
    // and void the market (status -1). The non-refundable creation fee already went to the
    // DAO fund at creation and is NOT returned. Mirrors the oracle-reject path, minus the
    // oracle action. liquidity_sum == the single seed LP (a pending market cannot receive
    // pm_add_liquidity), so it is the amount reported as refunded.
    {
        const auto& idx = get_index<pm_market_index>().indices().get<by_accept_deadline>();
        auto it = idx.lower_bound(boost::make_tuple(
            (int8_t)0, time_point_sec(0), pm_market_id_type()));
        while (it != idx.end() && it->status == 0 && it->accept_deadline <= now && done < cap) {
            const auto& mkt = *it; ++it;
            share_type refunded = mkt.liquidity_sum;
            return_liquidity(*this, mkt);
            modify(mkt, [&](pm_market_object& m) {
                m.status        = -1;
                m.payout_status = 3; // closed — no payout
                m.finalized_time = now;
            });
            push_virtual_operation(pm_market_expired_operation(
                mkt.oracle, mkt.creator, mkt.id._id, asset(refunded, TOKEN_SYMBOL)));
            ++done;
        }
    }

    // ── 2c. Leverage funding accrual + liquidation check ──────────────────────
    // Active leverage positions whose 24h funding period has come due: charge the carry cost
    // (raising the effective obligation via accrue_leverage_funding), then re-price at current
    // reserves — if the higher obligation now exceeds cancel_value, liquidate (reason 3). This is
    // the "recompute the liquidation point + maybe liquidate" step. by_lev_funding_due keeps the
    // due set contiguous (status 0, funding_due_time ascending); accrue advances the clock so a
    // position is revisited only once per period. funding_due_time > 0 sentinel skips legacy rows.
    {
        const uint32_t frate = mp.pm_leverage_funding_rate_ppm_per_day;
        const auto& idx = get_index<pm_leverage_position_index>().indices().get<by_lev_funding_due>();
        auto it = idx.lower_bound(boost::make_tuple((uint8_t)0, time_point_sec(1), pm_leverage_position_id_type()));
        while (it != idx.end() && it->status == 0 && it->funding_due_time <= now && done < cap) {
            const auto& pos = *it; ++it;
            accrue_leverage_funding(*this, pos, frate, now);
            const auto& mkt = get<pm_market_object>(pos.market);
            int64_t cv = pm::leverage::cancel_value(mkt.reserve_a.value, mkt.reserve_b.value, mkt.k,
                                                    pos.tokens.value, (int)pos.outcome_index);
            if (cv <= pos.liquidation_threshold.value + pos.funding_paid.value)
                liquidate_position(*this, pos, 3);   // reason 3 = funding pulled the position underwater
            ++done;
        }
    }

    // ── 2d. Force-close leverage once betting can no longer happen ─────────────
    // A leveraged position is a bet on the market PRICE (crowd sentiment) and settles at its
    // cancel_value — it does NOT depend on the oracle outcome. So it must not linger open through
    // resolution + dispute grace, bleeding carry (funding still accrues, secs 2c) and staying
    // funding-liquidatable while nobody can even bet anymore. Close it the moment NEW betting is
    // impossible: at betting_expiration for fixed-deadline markets (before the oracle resolves), or
    // at resolve/void (status >= 3) for open-ended markets (betting_expiration == 0, no deadline).
    // settle_market / return_liquidity still force-close as a backstop; both are idempotent (a
    // position already out of status 0 is skipped). Anchored on the OPEN-position set (status 0)
    // via by_lev_funding_due so each position is force-closed exactly once and, once it flips out
    // of status 0, never re-scanned. done is charged per close only (cap = settlements/block); the
    // status-0 working set is bounded by pool free capital, so a full scan per block is cheap.
    {
        const auto& idx = get_index<pm_leverage_position_index>().indices().get<by_lev_funding_due>();
        auto it = idx.lower_bound(boost::make_tuple(
            (uint8_t)0, time_point_sec(), pm_leverage_position_id_type()));
        while (it != idx.end() && it->status == 0 && done < cap) {
            const auto& pos = *it; ++it;
            const auto& mkt = get<pm_market_object>(pos.market);
            const bool betting_over =
                (mkt.betting_expiration != time_point_sec() && mkt.betting_expiration <= now)
                || mkt.status >= 3;
            if (betting_over) {
                liquidate_position(*this, pos, 2); // reason 2 = settlement force-close (pm_leverage_resolve)
                ++done;
            }
        }
    }

    // ── 3. Dispute auto-close (priority over voting finalize) ─────────────────
    {
        const auto& idx = get_index<pm_dispute_index>().indices().get<by_auto_close>();
        auto it = idx.lower_bound(boost::make_tuple(
            (uint8_t)0, time_point_sec(0), pm_dispute_id_type()));
        while (it != idx.end() && it->status == 0 && it->auto_close_time <= now && done < cap) {
            const auto& disp = *it; ++it;
            const auto& mkt  = get<pm_market_object>(disp.market);

            share_type slashed(0);
            const auto& oidx = get_index<pm_oracle_index>().indices().get<by_owner>();
            auto oit = oidx.find(mkt.oracle);
            if (oit != oidx.end() && oit->insurance.value > 0) {
                slashed = share_type(oit->insurance.value * mp.pm_oracle_penalty_percent / 10000);
                if (slashed.value > 0) {
                    modify(*oit, [&](pm_oracle_object& o) {
                        o.insurance -= slashed;
                        o.dispute_responses_missed++;
                        o.disputes_auto_closed++;
                        o.total_insurance_slashed += slashed;
                    });
                    adjust_balance(get_account(CHAIN_COMMITTEE_ACCOUNT),
                                   asset(slashed, TOKEN_SYMBOL));
                }
            }

            refund_all_bets(*this, mkt);
            return_liquidity(*this, mkt);

            // Anti-freeze: the dispute stalled through no fault of the disputer → return the
            // escrowed dispute fee (spec §4 pm_dispute_auto_close — "disputer fee return").
            if (disp.dispute_fee.value > 0)
                adjust_balance(get_account(disp.disputer),
                               asset(disp.dispute_fee, TOKEN_SYMBOL));

            modify(mkt, [&](pm_market_object& m) {
                m.status           = 3;
                m.payout_status    = 3;
                m.resolved_outcome = -1;
                m.finalized_time   = now;
            });
            pm_oracle_dispute_left_open(*this, mkt.oracle, disp); // drop from open-dispute gauge
            modify(disp, [](pm_dispute_object& d) { d.status = 3; }); // auto-closed

            push_virtual_operation(pm_dispute_auto_close_operation(
                mkt.oracle, mkt.id._id, asset(slashed, TOKEN_SYMBOL)));
            ++done;
        }
    }

    // ── 4. Dispute voting finalize ────────────────────────────────────────────
    {
        // HF14 governance bridge: VIZ parked in the lazy pool would otherwise lose its committee-vote
        // weight (only vested SHARES count). A depositor's claim on the pool — its net asset value
        // (free + allocated + loans-out) times their share of total pool shares — is converted to
        // vesting-SHARES with the SAME token↔shares price as create_vesting (drifts with dust), and
        // added to their dispute-vote weight. The pool's whole NAV is likewise folded into the
        // participation-threshold denominator so the quorum scales with the full electorate.
        const auto& gpo = get_dynamic_global_properties();
        const auto vprice = gpo.get_vesting_share_price();
        const pm_lazy_pool_object* lpool = find<pm_lazy_pool_object>(pm_lazy_pool_id_type(0));
        const int64_t pool_nav = (lpool && lpool->total_shares.value > 0)
            ? lpool->free_balance.value + lpool->allocated_balance.value + lpool->leverage_fund_used.value : 0;
        const int64_t pool_total_shares = (lpool ? lpool->total_shares.value : 0);
        const int64_t pool_nav_shares = (pool_nav > 0)
            ? (asset(share_type(pool_nav), TOKEN_SYMBOL) * vprice).amount.value : 0;
        auto lazy_vote_weight = [&](const account_name_type& acct) -> int64_t {
            if (pool_nav <= 0 || pool_total_shares <= 0) return 0;
            const auto& ldidx = get_index<pm_lazy_deposit_index>().indices().get<by_deposit_account>();
            auto d = ldidx.find(acct);
            if (d == ldidx.end() || d->shares.value <= 0) return 0;
            int64_t viz = (int64_t)(fc::uint128_t((uint64_t)pool_nav)
                          * fc::uint128_t((uint64_t)d->shares.value)
                          / fc::uint128_t((uint64_t)pool_total_shares)).lo;
            if (viz <= 0) return 0;
            return (asset(share_type(viz), TOKEN_SYMBOL) * vprice).amount.value;
        };

        const auto& idx = get_index<pm_dispute_index>().indices().get<by_voting_end>();
        auto it = idx.lower_bound(boost::make_tuple(
            (uint8_t)0, time_point_sec(0), pm_dispute_id_type()));
        while (it != idx.end() && it->status == 0 && it->voting_end_time <= now && done < cap) {
            const auto& disp = *it; ++it;
            const auto& mkt  = get<pm_market_object>(disp.market);

            // Dispute is being finalized (uphold or override below): drop from the open-dispute gauge.
            pm_oracle_dispute_left_open(*this, mkt.oracle, disp);

            // Step 1 — stake-weighted tally (mirrors committee-request finalize). Weight is the
            // voter's vesting shares. vote_percent>0 on a real outcome supports that change;
            // anything else (negative conviction, or vote_outcome<0) defends the oracle.
            // committee-dao-and-prediction-markets.md §Resolution.
            share_type max_rshares(0), oracle_defense(0), total_change(0);
            std::vector<share_type> outcome_rshares(mkt.outcome_count, share_type(0));
            {
                const auto& vidx =
                    get_index<pm_dispute_vote_index>().indices().get<by_market_voter>();
                auto vit = vidx.lower_bound(boost::make_tuple(disp.market, account_name_type()));
                for (; vit != vidx.end() && vit->market == disp.market; ++vit) {
                    int64_t w = get_account(vit->voter).effective_vesting_shares().amount.value
                              + lazy_vote_weight(vit->voter); // + lazy-pool stake as vesting-shares
                    max_rshares += w;
                    int32_t pct = (int32_t)vit->vote_percent;
                    if (pct > 0 && vit->vote_outcome >= 0 &&
                        vit->vote_outcome < (int16_t)mkt.outcome_count) {
                        share_type r = share_type(w * pct / CHAIN_100_PERCENT);
                        outcome_rshares[vit->vote_outcome] += r;
                        total_change += r;
                    } else {
                        int32_t a = pct < 0 ? -pct : pct;
                        oracle_defense += share_type(w * a / CHAIN_100_PERCENT);
                    }
                }
            }

            // Step 2 — participation threshold; Step 3 — oracle defense vs change votes.
            share_type approve_min = share_type((int64_t)(
                fc::uint128_t((uint64_t)(gpo.total_vesting_shares.amount.value + pool_nav_shares))
                * mp.pm_dispute_approve_min_percent / fc::uint128_t(CHAIN_100_PERCENT)).lo);
            bool uphold = (max_rshares < approve_min) ||
                          (total_change.value <= 0) ||
                          (oracle_defense >= total_change);

            // Step 4 — winning outcome among change votes; Step 5 — consensus strength.
            int16_t winning_outcome = mkt.resolved_outcome;
            share_type winning_rshares(0);
            if (!uphold) {
                int best = 0;
                for (uint8_t i = 1; i < mkt.outcome_count; ++i)
                    if (outcome_rshares[i] > outcome_rshares[best]) best = (int)i;
                winning_outcome = (int16_t)best;
                winning_rshares = outcome_rshares[best];
            }

            asset oracle_penalty(0, TOKEN_SYMBOL);
            if (uphold) {
                // Oracle upheld: original outcome stands; the escrowed dispute fee compensates
                // the oracle (committee-dao §Resolution step 3 — "fee goes to oracle").
                const auto& oidx = get_index<pm_oracle_index>().indices().get<by_owner>();
                auto oit = oidx.find(mkt.oracle);
                if (oit != oidx.end())
                    modify(*oit, [](pm_oracle_object& o) { o.disputes_won++; });
                if (disp.dispute_fee.value > 0)
                    adjust_balance(get_account(mkt.oracle), asset(disp.dispute_fee, TOKEN_SYMBOL));
                modify(mkt, [&](pm_market_object& m) {
                    m.payout_status     = 1;
                    m.result_expiration = time_point_sec(1);
                });
                modify(disp, [](pm_dispute_object& d) { d.status = 2; }); // oracle right
            } else {
                // Override. The oracle penalty policy is per-market (dispute_penalty_percent),
                // scaled by consensus strength (winning / total participation):
                //   >0 → slash that % of insurance; fund the disputer reward + winners' forfeit;
                //   <0 → good-faith oracle: no slash, oracle keeps a carve-out of the fee;
                //    0 → outcome corrected, nobody penalized (disputer just refunded).
                fc::uint128_t cs = max_rshares.value > 0
                    ? fc::uint128_t((uint64_t)winning_rshares.value) * fc::uint128_t(CHAIN_100_PERCENT)
                          / fc::uint128_t((uint64_t)max_rshares.value)
                    : fc::uint128_t(0);
                int32_t pp  = (int32_t)mkt.dispute_penalty_percent;
                int64_t fee = disp.dispute_fee.value > 0 ? disp.dispute_fee.value : 0;

                const auto& oidx = get_index<pm_oracle_index>().indices().get<by_owner>();
                auto oit = oidx.find(mkt.oracle);

                if (pp < 0) {
                    // Good-faith oracle: keep a slice of the dispute fee; refund the rest to the
                    // disputer. Insurance untouched.
                    int64_t oracle_bonus = (int64_t)(fc::uint128_t((uint64_t)fee)
                        * fc::uint128_t((uint64_t)(-(int64_t)pp)) / fc::uint128_t(10000)).lo;
                    if (oracle_bonus > fee) oracle_bonus = fee;
                    if (oracle_bonus > 0)
                        adjust_balance(get_account(mkt.oracle), asset(share_type(oracle_bonus), TOKEN_SYMBOL));
                    if (fee - oracle_bonus > 0)
                        adjust_balance(get_account(disp.disputer),
                                       asset(share_type(fee - oracle_bonus), TOKEN_SYMBOL));
                    if (oit != oidx.end())
                        modify(*oit, [](pm_oracle_object& o) { o.disputes_lost++; });
                } else {
                    share_type slash(0);
                    if (pp > 0 && oit != oidx.end() && oit->insurance.value > 0) {
                        int64_t base = (int64_t)(fc::uint128_t((uint64_t)oit->insurance.value)
                            * fc::uint128_t((uint64_t)pp) / fc::uint128_t(10000)).lo;
                        slash = share_type((int64_t)(fc::uint128_t((uint64_t)base) * cs
                            / fc::uint128_t(CHAIN_100_PERCENT)).lo);
                        if (slash.value > oit->insurance.value) slash = oit->insurance;
                        if (slash.value > 0) {
                            modify(*oit, [&](pm_oracle_object& o) {
                                o.insurance               -= slash;
                                o.disputes_lost++;
                                o.total_insurance_slashed += slash;
                            });
                            oracle_penalty = asset(slash, TOKEN_SYMBOL);
                        }
                    } else if (oit != oidx.end()) {
                        modify(*oit, [](pm_oracle_object& o) { o.disputes_lost++; });
                    }

                    // Disputer: refund the escrowed fee + a reward carve-out
                    // (pm_dispute_reward_multiplier ‰ — total target fee×mult/1000) drawn from the
                    // slash; the remainder of the slash boosts the winners via forfeit_pool.
                    int64_t reward_target = (int64_t)(fc::uint128_t((uint64_t)fee)
                        * fc::uint128_t((uint64_t)mp.pm_dispute_reward_multiplier) / fc::uint128_t(10000)).lo;
                    int64_t bonus = reward_target - fee;
                    if (bonus < 0) bonus = 0;
                    if (bonus > slash.value) bonus = slash.value;
                    if (fee + bonus > 0)
                        adjust_balance(get_account(disp.disputer),
                                       asset(share_type(fee + bonus), TOKEN_SYMBOL));
                    modify(mkt, [&](pm_market_object& m) {
                        m.forfeit_pool += share_type(slash.value - bonus);
                    });
                }

                modify(mkt, [&](pm_market_object& m) {
                    m.resolved_outcome  = winning_outcome;
                    m.payout_status     = 1;
                    m.result_expiration = time_point_sec(1);
                });
                modify(disp, [](pm_dispute_object& d) { d.status = 1; }); // oracle wrong
            }

            push_virtual_operation(pm_dispute_finalize_operation(
                mkt.oracle, mkt.id._id, mkt.resolved_outcome, oracle_penalty));
            ++done;
        }
    }

    // ── 5. Auto-payouts (dispute grace elapsed) ───────────────────────────────
    // Cutoff = now - pm_dispute_grace_sec; markets with result_expiration <= cutoff
    // and payout_status==1 have their dispute window closed and are ready to settle.
    {
        const time_point_sec cutoff(
            (now.sec_since_epoch() > mp.pm_dispute_grace_sec)
                ? (now.sec_since_epoch() - mp.pm_dispute_grace_sec)
                : 0u);

        const auto& idx = get_index<pm_market_index>().indices().get<by_result_expiration>();
        auto it = idx.lower_bound(boost::make_tuple(
            (int8_t)3, time_point_sec(0), pm_market_id_type()));
        while (it != idx.end() && it->status == 3 && it->result_expiration <= cutoff && done < cap) {
            const auto& mkt = *it; ++it;

            if (mkt.payout_status != 1) continue;

            // If this market was never disputed it is still counted in markets_in_dispute_window;
            // settling closes that window. A market whose dispute finalized (payout returned to 1)
            // already left the gauge at dispute_create and carries a dispute row — don't double-dec.
            {
                const auto& didx = get_index<pm_dispute_index>().indices().get<by_market>();
                if (didx.find(mkt.id) == didx.end())
                    pm_oracle_gauge_adj(*this, mkt.oracle,
                                        &pm_oracle_object::markets_in_dispute_window, -1);
            }

            settle_market(*this, mkt);
            modify(mkt, [&](pm_market_object& m) { m.payout_status = 3; m.finalized_time = now; });

            push_virtual_operation(pm_auto_payout_operation(
                mkt.oracle, mkt.id._id, -1LL, asset(mkt.bets_sum, TOKEN_SYMBOL)));
            ++done;
        }
    }

    // ── 5b. Garbage-collect terminal markets ──────────────────────────────────
    // Any market that has become terminal — resolved+paid, void/no-contest, oracle-rejected,
    // or accept-window-expired — carries a non-zero finalized_time and can never be acted on
    // again; it only lingers "for history". Reclaim its whole object cluster
    // pm_closed_market_retention_sec after that moment. The retention is median-voted, so it is
    // identical on every node at any block → pruning is deterministic: every node deletes the
    // same markets at the same block, keeping shared-memory state and snapshots in lock-step
    // network-wide (a node syncing from a snapshot ends up with the same market set as all).
    {
        const uint32_t retention = mp.pm_closed_market_retention_sec; // median-voted, 5 d default
        const time_point_sec cutoff(
            (now.sec_since_epoch() > retention) ? (uint32_t)(now.sec_since_epoch() - retention) : 0u);

        // finalized_time == 0 for every live market (sorts first) — start just past them.
        const auto& idx = get_index<pm_market_index>().indices().get<by_finalized>();
        auto it = idx.lower_bound(boost::make_tuple(time_point_sec(1), pm_market_id_type()));
        while (it != idx.end() && it->finalized_time <= cutoff && done < cap) {
            const auto& mkt = *it; ++it;
            gc_market(*this, mkt);
            ++done;
        }
    }

    // ── 6. Batch epoch settle ─────────────────────────────────────────────────
    // At global epoch boundary execute all queued (status=5) bets for batch markets.
    if (mp.pm_commit_reveal_enabled && mp.pm_batch_epoch_blocks > 0 &&
        (head_block_num() % (uint32_t)mp.pm_batch_epoch_blocks == 0)) {

        const auto& midx = get_index<pm_market_index>().indices().get<by_status>();
        const auto& bidx = get_index<pm_bet_index>().indices().get<by_epoch>();

        // Round-robin: resume where the previous boundary scan stopped on the cap, wrap
        // once. A fixed scan start would let ~cap always-busy low-id markets permanently
        // starve newer ones. The walk itself stays O(active batch markets) per boundary
        // (one bet-index probe each); if that ever hurts, index queued bets by
        // (status, market) and drive the scan from that instead.
        const uint64_t start_id = get_dynamic_global_properties().pm_batch_settle_cursor;
        bool second_pass = false;
        auto mit = midx.lower_bound(boost::make_tuple((int8_t)1, pm_market_id_type(start_id)));

        while (done < cap) {
            if (mit == midx.end() || mit->status != 1) {
                if (second_pass || start_id == 0) break;   // full circle
                second_pass = true;
                mit = midx.lower_bound((int8_t)1);         // wrap to the lowest active id
                continue;
            }
            if (second_pass && mit->id._id >= start_id) break; // full circle
            const auto& mkt = *mit; ++mit;
            if (!mkt.allow_batch) continue;

            // Idle fast-path: nothing queued at this epoch — skip before the LMSR
            // q-vector snapshot, so an idle market costs one index probe, keeps its
            // epoch, and does not consume the cap.
            auto bit = bidx.lower_bound(boost::make_tuple(
                mkt.id, (uint32_t)mkt.current_epoch, pm_bet_id_type()));
            if (bit == bidx.end() || bit->market != mkt.id ||
                bit->epoch  != (uint32_t)mkt.current_epoch)
                continue;

            // Snapshot LMSR q-vector
            std::vector<int64_t> q_vec;
            if (mkt.market_type == 1) {
                const auto& oidx = get_index<pm_outcome_index>().indices().get<by_market_outcome>();
                for (uint8_t i = 0; i < mkt.outcome_count; i++) {
                    auto oit = oidx.find(boost::make_tuple(mkt.id, i));
                    q_vec.push_back(oit != oidx.end() ? oit->q.value : 0LL);
                }
            }

            uint32_t settled = 0;
            bool     had_queued = false;

            while (bit != bidx.end() &&
                   bit->market == mkt.id &&
                   bit->epoch  == (uint32_t)mkt.current_epoch) {
                const auto& bet = *bit; ++bit;
                if (bet.status != 5) continue;
                had_queued = true;

                share_type tokens(0);

                if (mkt.market_type == 0) { // CPMM
                    share_type ra = mkt.reserve_a, rb = mkt.reserve_b;
                    if (ra.value > 0 && rb.value > 0) {
                        fc::uint128_t k128 = fc::uint128_t((uint64_t)ra.value) *
                                             fc::uint128_t((uint64_t)rb.value);
                        // Side convention MUST match pm_place_bet: side 0 adds VIZ to reserve_a
                        // (tokens = reserve_b drop); side 1 adds to reserve_b (tokens = reserve_a
                        // drop). Otherwise batch and instant bets on the same side move the curve
                        // in opposite directions and their weights become incomparable at settle.
                        if (bet.side == 0) {
                            share_type new_ra = share_type(ra.value + bet.amount.value);
                            share_type new_rb = share_type((int64_t)
                                (k128 / fc::uint128_t((uint64_t)new_ra.value)).lo);
                            tokens = share_type(rb.value - new_rb.value);
                        } else {
                            share_type new_rb = share_type(rb.value + bet.amount.value);
                            share_type new_ra = share_type((int64_t)
                                (k128 / fc::uint128_t((uint64_t)new_rb.value)).lo);
                            tokens = share_type(ra.value - new_ra.value);
                        }
                    }
                } else { // LMSR
                    if ((size_t)bet.outcome_index < q_vec.size()) {
                        int64_t t = lmsr::lmsr_tokens_for_amount(
                            q_vec, mkt.lmsr_b.value, (int)bet.outcome_index, bet.amount.value);
                        if (t > 0) {
                            tokens = share_type(t);
                            q_vec[(size_t)bet.outcome_index] += t; // update in-memory
                        }
                    }
                }

                if (tokens.value > 0 && tokens.value >= bet.min_tokens.value) {
                    // Execute
                    if (mkt.market_type == 0) {
                        modify(mkt, [&](pm_market_object& m) {
                            if (bet.side == 0) {
                                m.reserve_a += bet.amount; m.reserve_b -= tokens;
                                m.a_bets_sum += bet.amount;
                            } else {
                                m.reserve_b += bet.amount; m.reserve_a -= tokens;
                                m.b_bets_sum += bet.amount;
                            }
                            m.bets_sum += bet.amount;
                            m.k = fc::uint128_t((uint64_t)m.reserve_a.value) *
                                  fc::uint128_t((uint64_t)m.reserve_b.value);
                        });
                    } else {
                        const auto& oidx = get_index<pm_outcome_index>().indices().get<by_market_outcome>();
                        auto oit = oidx.find(boost::make_tuple(mkt.id, (uint8_t)bet.outcome_index));
                        if (oit != oidx.end())
                            modify(*oit, [&](pm_outcome_object& out) {
                                out.q        = share_type(q_vec[(size_t)bet.outcome_index]);
                                out.bets_sum += bet.amount;
                                out.bets_count++;
                            });
                        modify(mkt, [&](pm_market_object& m) { m.bets_sum += bet.amount; });
                    }
                    modify(bet, [&](pm_bet_object& b) {
                        b.status = 0; b.weight = tokens;
                        if (mkt.market_type == 0) b.entry_liquidity = mkt.liquidity_sum; // #1-C: depth at fill
                    });
                    settled++;
                } else {
                    // Slippage: refund
                    adjust_balance(get_account(bet.account), asset(bet.amount, TOKEN_SYMBOL));
                    pm_adjust_frozen(bet.account, 1, -bet.amount); // UNLOCK: queued stake refunded (slippage)
                    modify(bet, [](pm_bet_object& b) { b.status = 2; });
                }
            }

            if (settled > 0)
                push_virtual_operation(pm_batch_settle_operation(
                    mkt.id._id, mkt.current_epoch, settled));

            // Idle markets keep their epoch and don't consume the cap, so the scan can
            // reach markets with queued bets past the cap.
            if (had_queued) {
                modify(mkt, [](pm_market_object& m) { m.current_epoch++; });
                ++done;
            }
        }

        // Persist the resume point: the next unvisited active market when the cap cut
        // the scan short, 0 after a completed full circle.
        uint64_t next_cursor = 0;
        if (done >= cap && mit != midx.end() && mit->status == 1 &&
            !(second_pass && mit->id._id >= start_id))
            next_cursor = mit->id._id;
        if (next_cursor != start_id)
            modify(get_dynamic_global_properties(), [&](dynamic_global_property_object& d) {
                d.pm_batch_settle_cursor = next_cursor;
            });
    }

    // ── 7. Lazy pool recall step ──────────────────────────────────────────────
    if (mp.pm_lazy_pool_enabled) {
        const auto& idx = get_index<pm_lazy_allocation_index>().indices().get<by_alloc_check>();
        auto it = idx.lower_bound(boost::make_tuple(
            (uint8_t)0, time_point_sec(0), pm_lazy_allocation_id_type()));

        while (it != idx.end() && it->status == 0 && done < cap) {
            const auto& alloc = *it; ++it;

            const auto* mkt_ptr = find<pm_market_object>(alloc.market);
            if (!mkt_ptr || mkt_ptr->status == -1) {
                // Market rejected/gone (never settled) — pull the LP position fully.
                if (mkt_ptr && alloc.amount.value > 0) {
                    share_type r = recall_pool_liquidity(*this, *mkt_ptr, alloc.amount);
                    if (r.value > 0)
                        push_virtual_operation(pm_lazy_recall_operation(
                            alloc.market._id, asset(r, TOKEN_SYMBOL)));
                }
                modify(alloc, [](pm_lazy_allocation_object& a) { a.status = 1; });
            } else if (mkt_ptr->status >= 2) {
                // Resolved/closed: settle_liquidity / return_liquidity returns the LP
                // position (with yield). Defer the scan so we don't pre-empt it.
                modify(alloc, [&](pm_lazy_allocation_object& a) { a.last_check_time = now; });
            } else {
                const auto& mkt = *mkt_ptr;
                bool idle = (mkt.bets_sum.value <= alloc.bets_sum_at_check.value);

                // Graduated recall is spread across the market's lifetime: up to 10 steps,
                // each gated by ~10% of the (creation → result_expiration) window. This cron
                // runs every block, so without a time gate an idle market's whole subsidy
                // would be drained within ~10 blocks instead of over its full duration
                // (spec lazy-pool-properties: check_step = "10% duration step", last_check_time).
                int64_t window   = (int64_t)mkt.result_expiration.sec_since_epoch()
                                 - (int64_t)mkt.created_time.sec_since_epoch();
                int64_t step_dur = window > 0 ? window / 10 : 0;
                bool step_due = (int64_t)now.sec_since_epoch()
                              >= (int64_t)alloc.last_check_time.sec_since_epoch() + step_dur;

                if (!idle) {
                    // New bets since the last check → market is live; reset the recall schedule.
                    modify(alloc, [&](pm_lazy_allocation_object& a) {
                        a.bets_sum_at_check = mkt.bets_sum;
                        a.last_check_time   = now;
                        a.check_step        = 0;
                    });
                } else if (alloc.check_step >= 10 || alloc.amount.value <= 0) {
                    modify(alloc, [](pm_lazy_allocation_object& a) { a.status = 1; });
                } else if (step_due) {
                    share_type want = share_type(
                        alloc.amount.value * mp.pm_lazy_recall_step_percent / 10000);
                    if (want.value <= 0) want = share_type(1);
                    if (want.value > alloc.amount.value) want = alloc.amount;

                    share_type recalled = recall_pool_liquidity(*this, mkt, want);
                    if (recalled.value > 0) {
                        modify(alloc, [&](pm_lazy_allocation_object& a) {
                            a.amount           -= recalled;
                            a.recalled_amount  += recalled;
                            a.bets_sum_at_check = mkt.bets_sum;
                            a.last_check_time   = now;
                            a.check_step++;
                        });
                        push_virtual_operation(pm_lazy_recall_operation(
                            mkt.id._id, asset(recalled, TOKEN_SYMBOL)));
                    } else {
                        modify(alloc, [](pm_lazy_allocation_object& a) { a.status = 1; });
                    }
                }
                // else: idle, steps remain, but this step isn't due yet → leave untouched.
            }
            ++done;
        }
    }

    // ── 8. Ban expiry sweep ───────────────────────────────────────────────────
    // Temporary oracle/creator bans lapse at banned_until. We clear the expired ones (banned_until
    // → 0) and emit pm_ban_expired so history/indexers see the lift; cleared bans fall into the
    // 0-bucket and are never re-swept, permanent bans (maximum()) sort past `now` and are skipped.
    // lower_bound at epoch+1 skips the huge never-banned/cleared 0-cluster.
    {
        const auto& oidx = get_index<pm_oracle_index>().indices().get<by_status>();
        auto it = oidx.lower_bound(time_point_sec(1));
        while (it != oidx.end() && it->banned_until <= now && done < cap) {
            const auto& ora = *it; ++it;
            const account_name_type owner = ora.owner;
            modify(ora, [](pm_oracle_object& o) { o.banned_until = time_point_sec(0); o.banned_by = account_name_type(); });
            push_virtual_operation(pm_ban_expired_operation(owner, true, false));
            ++done;
        }
    }
    {
        const auto& cbidx = get_index<pm_creator_ban_index>().indices().get<by_ban_expiry>();
        auto it = cbidx.lower_bound(boost::make_tuple(time_point_sec(1), pm_creator_ban_id_type()));
        while (it != cbidx.end() && it->banned_until <= now && done < cap) {
            const auto& cb = *it; ++it;
            const account_name_type who = cb.creator;
            modify(cb, [](pm_creator_ban_object& b) { b.banned_until = time_point_sec(0); b.banned_by = account_name_type(); });
            push_virtual_operation(pm_ban_expired_operation(who, false, true));
            ++done;
        }
    }
}

}} // graphene::chain
