/*
 * Supply-invariant checks over the live state (issue #204).
 *
 * database::validate_invariants() was declared in database.hpp but its
 * definition was removed in 2018 (faa45ff4 "Remove unused methods") and the
 * declaration outlived the removal, so --validate-database-invariants accepted
 * the flag and never ran anything.  This TU restores the definition.
 *
 * The accounting mirrors the post-import checks in plugins/snapshot/plugin.cpp
 * on purpose: that copy stays where it is, as an independent witness at import
 * time (owner decision 2026-10-30 — "let it watch additionally").  If a PM pool
 * is ever added or re-scoped, BOTH copies must change together; the numbers in
 * the two logs are expected to match object for object.
 */
#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/invite_objects.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/chain/validator_objects.hpp>

namespace graphene {
    namespace chain {

        void database::validate_invariants(int64_t pm_legacy_residual, bool verbose) const {
            const auto& dgp = get_dynamic_global_properties();

            // ── (1) SHARES ────────────────────────────────────────────────────
            // dgp.total_vesting_shares is minted at inflation; account.vesting_shares is the
            // only place it lives.  Delegation objects redistribute vests that are ALREADY in
            // the delegator's account.vesting_shares (delegated_/received_ net to zero
            // network-wide), so they are deliberately not summed — adding them would
            // double-count.
            int64_t summed_vesting_shares = 0;
            {
                const auto& idx = get_index<account_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    summed_vesting_shares += itr->vesting_shares.amount.value;
            }
            const int64_t expected_vesting_shares = dgp.total_vesting_shares.amount.value;
            const int64_t shares_delta = summed_vesting_shares - expected_vesting_shares;

            // ── (2) TOKEN ─────────────────────────────────────────────────────
            // dgp.current_supply must equal every pool that holds TOKEN outside account
            // balances.  Base (chain-core) pools were verified satoshi-exact against six
            // mainnet snapshots in PR #126; PM never mints, it only MOVES tokens from
            // account.balance into a PM object field (or back), so the PM-held float must be
            // added here or the equality is off by exactly that float.
            int64_t acc_balance = 0, acc_reserved = 0;
            {
                const auto& idx = get_index<account_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr) {
                    acc_balance  += itr->balance.amount.value;
                    acc_reserved += itr->reserved_balance.amount.value;
                }
            }
            int64_t escrow_balance = 0, escrow_fee = 0;
            {
                const auto& idx = get_index<escrow_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr) {
                    escrow_balance += itr->token_balance.amount.value;
                    escrow_fee     += itr->pending_fee.amount.value;
                }
            }
            int64_t invite_balance = 0;
            {
                const auto& idx = get_index<invite_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    invite_balance += itr->balance.amount.value;
            }
            int64_t validator_pending = 0;
            {
                // current_supply already includes validator pending_stakeholder_reward
                // (credited at inflation, moved into total_vesting_fund at create_vesting).
                const auto& idx = get_index<validator_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    validator_pending += itr->pending_stakeholder_reward.value;
            }

            const int64_t vesting_fund   = dgp.total_vesting_fund.amount.value;
            const int64_t reward_fund    = dgp.total_reward_fund.amount.value;
            const int64_t committee_fund = dgp.committee_fund.amount.value;
            const int64_t base_token = acc_balance + acc_reserved
                + escrow_balance + escrow_fee + invite_balance
                + validator_pending + vesting_fund + reward_fund + committee_fund;

            // Minimal NON-OVERLAPPING PM holdings (duplicates excluded: market/outcome
            // bets_sum and CPMM reserves are aggregates of pm_bet rows; the empty-provider
            // lazy LP row duplicates pm_lazy_pool.allocated_balance; pm_lazy_deposit.principal
            // is a per-depositor claim on the lazy pool).
            int64_t pm_bets = 0;
            {
                const auto& idx = get_index<pm_bet_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    if (itr->status == 0 || itr->status == 5 || itr->status == 6) // active/queued/revealed-pending
                        pm_bets += itr->amount.value;
            }
            int64_t pm_user_lp = 0;
            {
                const auto& idx = get_index<pm_liquidity_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    if (itr->status == 0 && itr->provider.size() > 0)
                        pm_user_lp += itr->amount.value;
            }
            int64_t pm_lazy = 0;
            {
                const auto& idx = get_index<pm_lazy_pool_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    pm_lazy += itr->free_balance.value + itr->allocated_balance.value;
            }
            int64_t pm_commit = 0;
            {
                const auto& idx = get_index<pm_commit_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    if (itr->status == 0)                        // escrow held only while committed
                        pm_commit += itr->escrow_amount.value;
            }
            int64_t pm_dispute = 0;
            {
                const auto& idx = get_index<pm_dispute_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    if (itr->status == 0)                        // fee held only while dispute open
                        pm_dispute += itr->dispute_fee.value;
            }
            int64_t pm_forfeit = 0;
            {
                const auto& idx = get_index<pm_market_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    pm_forfeit += itr->forfeit_pool.value;       // zeroed post-settlement
            }
            int64_t pm_insurance = 0;
            {
                const auto& idx = get_index<pm_oracle_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    pm_insurance += itr->insurance.value;
            }
            int64_t pm_leverage = 0;
            {
                const auto& idx = get_index<pm_leverage_position_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    if (itr->status == 0)                        // collateral+loan held only while active
                        pm_leverage += itr->collateral.value + itr->loan.value;
            }
            // A settlement spans blocks, so a check can catch one mid-flight.  Rows already
            // released no longer count above, and payouts already made are in account
            // balances — `escrow` is the difference and keeps both sides equal at any block
            // boundary.  SIGNED: phase 3 pays early-exit claims out of a pot whose rows are
            // still standing, so it is legitimately negative until phase 4 releases them.
            int64_t pm_settling = 0;
            {
                const auto& idx = get_index<pm_settlement_index>().indices();
                for (auto itr = idx.begin(); itr != idx.end(); ++itr)
                    pm_settling += itr->escrow.value;
            }
            const int64_t pm_token = pm_bets + pm_user_lp + pm_lazy + pm_commit
                + pm_dispute + pm_forfeit + pm_insurance + pm_leverage + pm_settling;

            const int64_t summed_token = base_token + pm_token;
            const int64_t expected_supply = dgp.current_supply.amount.value;
            const int64_t token_delta = summed_token - expected_supply;

            // The per-pool breakdown is ~600 bytes.  At a one-second block interval a silent
            // run would put ~50 MB a day into node.log for no information — so it is printed
            // when asked (startup, manual call) and on ANY divergence, never on a clean
            // per-block pass.  A divergence of either sum is "interesting" here regardless of
            // the anchor: a delta equal to the anchor still has to be visible.
            if (verbose || shares_delta != 0 || token_delta != pm_legacy_residual) {
                ilog("Database invariants: head_block_num=${h} SHARES summed=${sv} expected=${ev} "
                     "delta=${sd}; TOKEN current_supply=${cs} summed=${sm} (base=${bt} + pm=${pm}) "
                     "delta=${d}. Base: acc_balance=${ab} acc_reserved=${ar} escrow_balance=${eb} "
                     "escrow_fee=${ef} invite_balance=${ib} validator_pending=${vp} "
                     "vesting_fund=${vf} reward_fund=${rf} committee_fund=${cf}. "
                     "PM: bets=${pb} user_lp=${pl} lazy=${plz} commit=${pc} dispute=${pd} "
                     "forfeit=${pf} insurance=${pi} leverage=${plv} settling=${pst}",
                     ("h", dgp.head_block_number)("sv", summed_vesting_shares)
                     ("ev", expected_vesting_shares)("sd", shares_delta)
                     ("cs", expected_supply)("sm", summed_token)("bt", base_token)("pm", pm_token)
                     ("d", token_delta)
                     ("ab", acc_balance)("ar", acc_reserved)("eb", escrow_balance)("ef", escrow_fee)
                     ("ib", invite_balance)("vp", validator_pending)("vf", vesting_fund)
                     ("rf", reward_fund)("cf", committee_fund)
                     ("pb", pm_bets)("pl", pm_user_lp)("plz", pm_lazy)("pc", pm_commit)
                     ("pd", pm_dispute)("pf", pm_forfeit)("pi", pm_insurance)("plv", pm_leverage)
                     ("pst", pm_settling));
            }

            if (shares_delta != 0) {
                elog("Database invariant FAILED at block ${h}: SHARES conservation violated — "
                     "summed account.vesting_shares (${sv}) != dgp.total_vesting_shares (${ev}), "
                     "delta=${sd}",
                     ("h", dgp.head_block_number)("sv", summed_vesting_shares)
                     ("ev", expected_vesting_shares)("sd", shares_delta));
            }
            if (token_delta != pm_legacy_residual) {
                elog("Database invariant FAILED at block ${h}: TOKEN supply divergence — summed "
                     "(${sm}) != current_supply (${cs}), delta=${d}, expected=${exp}, "
                     "drift_from_anchor=${dr} (base_delta=${bd})",
                     ("h", dgp.head_block_number)("sm", summed_token)("cs", expected_supply)
                     ("d", token_delta)("exp", pm_legacy_residual)
                     ("dr", token_delta - pm_legacy_residual)
                     ("bd", base_token - expected_supply));
            } else if (token_delta != 0) {
                wlog("Database invariants: TOKEN delta=${d} matches the configured legacy anchor "
                     "— tolerated, but the anchor must be 0 on mainnet.", ("d", token_delta));
            }

            FC_ASSERT(shares_delta == 0 && token_delta == pm_legacy_residual,
                "Database invariant violated at block ${h}: SHARES delta=${sd}, TOKEN delta=${td} "
                "(expected ${exp}). The state no longer reconciles — refusing to continue.",
                ("h", dgp.head_block_number)("sd", shares_delta)("td", token_delta)
                ("exp", pm_legacy_residual));
        }

    }
} // namespace graphene::chain
