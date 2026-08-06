#pragma once

#include <graphene/protocol/asset.hpp>
#include <graphene/chain/chain_object_types.hpp>

#include <fc/uint128_t.hpp>
#include <fc/array.hpp>
#include <boost/multi_index/composite_key.hpp>

// HF14 Prediction Markets (Onix) — consensus ChainBase objects. All financial sums are
// share_type. Variable-length strings are shared_string capped at MAX_PM_* (enforced on
// insert in the evaluators — the cap is consensus-mechanical, see config.hpp). Time indexes
// are (status, time, id) so the bounded per-block cron scans oldest-first deterministically.

namespace graphene { namespace chain {

        using protocol::string_less;

        // ───────────────────────────── 1.1 pm_oracle_object ─────────────────────────────
        class pm_oracle_object : public object<pm_oracle_object_type, pm_oracle_object> {
        public:
            pm_oracle_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_oracle_object(Constructor&& c, allocator<Allocator> a) : rules_url(a) { c(*this); }

            id_type           id;
            account_name_type owner;
            share_type        insurance;
            uint16_t          fee_percent = 0;
            share_type        fixed_fee;
            shared_string     rules_url;
            time_point_sec    active_since;
            time_point_sec    last_active_time;
            time_point_sec    banned_until;   ///< 0 not banned; time_point_sec::maximum() = permanent

            // 14 reputation counters (score computed on read in the API plugin).
            uint32_t   markets_accepted = 0;
            uint32_t   markets_resolved = 0;
            uint32_t   no_contest_count = 0;
            uint32_t   missed_count = 0;
            uint32_t   disputes_received = 0;
            uint32_t   disputes_lost = 0;
            uint32_t   disputes_won = 0;
            uint32_t   disputes_auto_closed = 0;
            uint32_t   dispute_responses_missed = 0;
            share_type total_volume_resolved;
            share_type total_insurance_slashed;
            uint32_t   avg_resolution_time = 0;
            uint32_t   penalty_stamps = 0;
            uint32_t   bans_received = 0;
            time_point_sec last_penalty_stamp_time; ///< time of most recent stamp (10-day decay)
            // Auto-accept policy (anti-collusion): if `auto_accept`, a new market naming this oracle is
            // accepted at creation ONLY when it matches this policy — so a creator cannot slip in a
            // sham dispute resolver. `auto_accept_creator` empty = any creator. `auto_accept_resolver`
            // empty = only committee-mode markets (dispute_mode 0); set = only account-mode markets
            // whose dispute_resolver equals it. The oracle's profile fee terms must also be within the
            // creator's offered ceiling, else the market stays pending for manual review.
            account_name_type auto_accept_creator;
            account_name_type auto_accept_resolver;
            bool              auto_accept = false;
            account_name_type banned_by;   ///< resolver that set banned_until (empty if unset); may pm_unban
            // Live count of this oracle's markets currently in the active(1) state. Display-only O(1)
            // read for get_oracle / watchdogs (avoids paging list_markets). Maintained by
            // pm_oracle_inc/dec_active at create-active/accept and resolve/no_contest/missed-void;
            // seeded from live markets on the first block after upgrade (dgpo.pm_active_markets_seeded).
            // NEVER gates consensus.
            uint32_t          active_markets = 0;

            // Live workload gauges (display-only, O(1) reads for oracle dashboards/watchdogs;
            // NEVER gate consensus). Maintained incrementally by pm_oracle_gauge_adj at the
            // relevant state transitions, and seeded once on the first block after upgrade
            // (dgpo.pm_oracle_gauges_seeded) with a drift-check. Invariant each tracks:
            //   markets_in_dispute_window   — this oracle's resolved(3)+payout-pending(1) markets
            //                                 that have NO dispute filed yet (still disputable).
            //   disputes_awaiting_response  — open(0) disputes whose oracle has NOT yet responded.
            //   disputes_awaiting_decision  — open(0) disputes whose oracle HAS responded (awaiting
            //                                 the committee vote / account-resolver verdict).
            // (markets_awaiting_resolution — status1 past betting close — is computed on read in the
            //  prediction_market_api from this oracle's small active set, not stored here.)
            uint32_t          markets_in_dispute_window  = 0;
            uint32_t          disputes_awaiting_response = 0;
            uint32_t          disputes_awaiting_decision = 0;

            // Resolution-timeliness telemetry (display-only, forward-accumulating from this upgrade;
            // no seed — increments deterministically at pm_resolve_market). `resolved_late_count` is
            // the number of markets this oracle resolved AFTER their result_expiration (past the
            // advertised deadline but before the missed-resolution void). `avg_resolution_time`
            // (declared above, previously never maintained) now tracks the running mean latency from
            // betting close to resolution. Oldest-unresolved age and median/p95 latency are exposed
            // via the prediction_market_api (computed on read / histogram — see P5).
            uint32_t          resolved_late_count = 0;
            // Resolution-latency histogram (P5 pt.2): 8 buckets by seconds from betting close to
            // resolve — (≤1h, ≤6h, ≤24h, ≤3d, ≤7d, ≤14d, ≤30d, >30d]; open-ended resolves count as 0
            // (bucket 0), matching avg_resolution_time. share_type (not uint32) so the fc::array is
            // guaranteed zero-initialized — the generic fc::array<uint32_t> ctor leaves data
            // indeterminate, which would be non-deterministic. p50/p95 are derived on read in the API.
            fc::array<share_type, 8> resolution_time_hist;
        };

        struct by_owner;
        struct by_status;
        struct by_insurance;
        typedef multi_index_container<
            pm_oracle_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_oracle_object, pm_oracle_id_type, &pm_oracle_object::id>>,
                ordered_unique<tag<by_owner>, member<pm_oracle_object, account_name_type, &pm_oracle_object::owner>, string_less>,
                ordered_non_unique<tag<by_status>, member<pm_oracle_object, time_point_sec, &pm_oracle_object::banned_until>>,
                ordered_non_unique<tag<by_insurance>, member<pm_oracle_object, share_type, &pm_oracle_object::insurance>>
            >,
            allocator<pm_oracle_object>
        > pm_oracle_index;

        // ───────────────────────────── 1.2 pm_market_object ─────────────────────────────
        class pm_market_object : public object<pm_market_object_type, pm_market_object> {
        public:
            pm_market_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_market_object(Constructor&& c, allocator<Allocator> a)
                : url(a), decision_url(a), decision_reason(a) { c(*this); }

            id_type           id;
            account_name_type creator;
            account_name_type oracle;
            uint8_t           market_type = 0;   ///< 0 binary (CPMM), 1 multi (LMSR)
            uint8_t           outcome_count = 2;
            shared_string     url;
            // NB: the free-form client `metadata` is intentionally NOT stored in consensus state.
            // It is opaque to consensus (never read here), so the prediction_market_api plugin
            // ingests it off-chain from pm_create_market_operation into its own prunable index.
            // This keeps chainbase lean and lets each node prune it (see --pmm-ttl-days).
            int8_t            status = 0;         ///< -1 deleted, 0 waiting, 1 active, 2 closed, 3 resolved
            uint8_t           payout_status = 0;  ///< 0 none, 1 pending, 2 disputed, 3 finalized (paid/closed)
            time_point_sec    created_time;
            time_point_sec    accept_deadline;    ///< pending markets only: created_time + median
                                                  ///< pm_oracle_accept_window_sec. The cron voids the
                                                  ///< market (refund seed) if the oracle hasn't acted by
                                                  ///< then. 0 for markets active at creation (never scanned).
            time_point_sec    betting_expiration;
            time_point_sec    result_expiration;
            time_point_sec    finalized_time;     ///< 0 while live; set to head-block time the moment the
                                                  ///< market becomes terminal (resolved+paid, void/no-contest,
                                                  ///< oracle-rejected, or accept-window expired). The cron GCs
                                                  ///< the whole market cluster pm_closed_market_retention_sec
                                                  ///< after this — deterministic, so every node prunes alike.
            int16_t           resolved_outcome = -1;

            // Binary CPMM
            share_type        reserve_a;
            share_type        reserve_b;
            fc::uint128_t     k = 0;              ///< reserve_a * reserve_b
            share_type        a_bets_sum;
            share_type        b_bets_sum;

            // Multi LMSR
            share_type        lmsr_b;
            share_type        lmsr_subsidy;

            // Common
            share_type        bets_sum;
            share_type        liquidity_sum;
            uint16_t          oracle_fee_percent = 0;
            uint16_t          creator_fee_percent = 0;
            uint16_t          liquidity_fee_percent = 0;
            share_type        oracle_fixed_fee;
            share_type        liquidity_fee_earned;
            share_type        forfeit_pool;
            uint8_t           time_penalty_type = 0;
            uint32_t          time_penalty_value = 0;
            uint8_t           penalty_curve_type = 0;
            bool              allow_early_resolution = false;
            bool              allow_cancellation = false;
            bool              allow_batch = false;
            bool              allow_instant_bet = true;
            uint8_t           endogeneity_tier = 2;
            uint32_t          current_epoch = 0;
            uint8_t           dispute_mode = 0;   ///< 0 committee / 1 account
            account_name_type dispute_resolver;
            int16_t           dispute_penalty_percent = 0; ///< −10000..+10000: oracle penalty on a
                                                           ///< successful dispute (>0 slash % of
                                                           ///< insurance ×consensus; <0 good-faith
                                                           ///< oracle bonus from fee; 0 none)
            // Oracle's resolution statement, stored on-chain (like an oracle/validator's rules_url):
            // set by pm_resolve_market (both) or pm_no_contest (reason → decision_reason). Empty until
            // resolved. Readable directly via get_market — no history scan needed.
            shared_string     decision_url;       ///< evidence link the oracle cited when resolving
            shared_string     decision_reason;    ///< oracle's free-text justification (or NO-CONTEST reason)
        };

        struct by_creator;
        struct by_oracle;
        struct by_oracle_status;
        struct by_accept_deadline;
        struct by_betting_expiration;
        struct by_result_expiration;
        struct by_payout_status;
        struct by_finalized;
        typedef multi_index_container<
            pm_market_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_market_object, pm_market_id_type, &pm_market_object::id>>,
                ordered_non_unique<tag<by_creator>, member<pm_market_object, account_name_type, &pm_market_object::creator>, string_less>,
                ordered_non_unique<tag<by_oracle>, member<pm_market_object, account_name_type, &pm_market_object::oracle>, string_less>,
                ordered_non_unique<tag<by_status>, member<pm_market_object, int8_t, &pm_market_object::status>>,
                // (oracle, status, id): one oracle's markets filtered by a single status in a bounded
                // walk — e.g. its still-active (1) or already-resolved (3) rows — without scanning the
                // oracle's entire (mostly resolved) history the way plain by_oracle does.
                ordered_unique<tag<by_oracle_status>,
                    composite_key<pm_market_object,
                        member<pm_market_object, account_name_type, &pm_market_object::oracle>,
                        member<pm_market_object, int8_t, &pm_market_object::status>,
                        member<pm_market_object, pm_market_id_type, &pm_market_object::id>
                    >,
                    composite_key_compare<string_less, std::less<int8_t>, std::less<pm_market_id_type>>
                >,
                // Pending-acceptance sweep: (status, accept_deadline, id). The cron lower_bounds at
                // status 0 and stops at the first accept_deadline > now, so voiding never-accepted
                // markets stays bounded. Accepted (1)/rejected (-1) markets sit in other status buckets.
                ordered_unique<tag<by_accept_deadline>,
                    composite_key<pm_market_object,
                        member<pm_market_object, int8_t, &pm_market_object::status>,
                        member<pm_market_object, time_point_sec, &pm_market_object::accept_deadline>,
                        member<pm_market_object, pm_market_id_type, &pm_market_object::id>
                    >,
                    composite_key_compare<std::less<int8_t>, std::less<time_point_sec>, std::less<pm_market_id_type>>
                >,
                ordered_unique<tag<by_betting_expiration>,
                    composite_key<pm_market_object,
                        member<pm_market_object, int8_t, &pm_market_object::status>,
                        member<pm_market_object, time_point_sec, &pm_market_object::betting_expiration>,
                        member<pm_market_object, pm_market_id_type, &pm_market_object::id>
                    >,
                    composite_key_compare<std::less<int8_t>, std::less<time_point_sec>, std::less<pm_market_id_type>>
                >,
                ordered_unique<tag<by_result_expiration>,
                    composite_key<pm_market_object,
                        member<pm_market_object, int8_t, &pm_market_object::status>,
                        member<pm_market_object, time_point_sec, &pm_market_object::result_expiration>,
                        member<pm_market_object, pm_market_id_type, &pm_market_object::id>
                    >,
                    composite_key_compare<std::less<int8_t>, std::less<time_point_sec>, std::less<pm_market_id_type>>
                >,
                ordered_non_unique<tag<by_payout_status>, member<pm_market_object, uint8_t, &pm_market_object::payout_status>>,
                // GC sweep: terminal markets ordered by finalized_time (0 = still live, sorts first
                // and is skipped). The cron lower_bounds just past 0 and stops at the first
                // finalized_time newer than the retention cutoff → bounded, deterministic pruning.
                ordered_unique<tag<by_finalized>,
                    composite_key<pm_market_object,
                        member<pm_market_object, time_point_sec, &pm_market_object::finalized_time>,
                        member<pm_market_object, pm_market_id_type, &pm_market_object::id>
                    >,
                    composite_key_compare<std::less<time_point_sec>, std::less<pm_market_id_type>>
                >
            >,
            allocator<pm_market_object>
        > pm_market_index;

        // ───────────────────────────── 1.3 pm_outcome_object ─────────────────────────────
        class pm_outcome_object : public object<pm_outcome_object_type, pm_outcome_object> {
        public:
            pm_outcome_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_outcome_object(Constructor&& c, allocator<Allocator> a) : label(a) { c(*this); }

            id_type         id;
            pm_market_id_type market;
            uint8_t         outcome_index = 0;
            shared_string   label;
            share_type      q;            ///< LMSR quantity
            share_type      bets_sum;
            share_type      weight_sum;
            uint32_t        bets_count = 0;
        };

        struct by_market_outcome;
        typedef multi_index_container<
            pm_outcome_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_outcome_object, pm_outcome_id_type, &pm_outcome_object::id>>,
                ordered_unique<tag<by_market_outcome>,
                    composite_key<pm_outcome_object,
                        member<pm_outcome_object, pm_market_id_type, &pm_outcome_object::market>,
                        member<pm_outcome_object, uint8_t, &pm_outcome_object::outcome_index>
                    >,
                    composite_key_compare<std::less<pm_market_id_type>, std::less<uint8_t>>
                >
            >,
            allocator<pm_outcome_object>
        > pm_outcome_index;

        // ───────────────────────────── 1.4 pm_bet_object ─────────────────────────────
        class pm_bet_object : public object<pm_bet_object_type, pm_bet_object> {
        public:
            pm_bet_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_bet_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            pm_market_id_type market;
            account_name_type account;
            int8_t            side = -1;
            int16_t           outcome_index = -1;
            share_type        amount;
            share_type        weight;
            uint64_t          price = 0;
            uint32_t          time_penalty = 0;
            uint8_t           mode = 0;
            uint32_t          epoch = 0;
            uint8_t           status = 0;   ///< 0 active,1 cancelled,2 refunded,3 resolved,5 queued,6 revealed-pending
            share_type        min_tokens = 0; ///< slippage floor for queued batch bets
            share_type        resolved_amount;
            time_point_sec    created_time;
        };

        struct by_market;
        struct by_account;
        struct by_market_account;
        struct by_market_outcome_bet;
        struct by_epoch;
        typedef multi_index_container<
            pm_bet_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_bet_object, pm_bet_id_type, &pm_bet_object::id>>,
                ordered_unique<tag<by_market>,
                    composite_key<pm_bet_object,
                        member<pm_bet_object, pm_market_id_type, &pm_bet_object::market>,
                        member<pm_bet_object, pm_bet_id_type, &pm_bet_object::id>
                    >,
                    composite_key_compare<std::less<pm_market_id_type>, std::less<pm_bet_id_type>>
                >,
                ordered_unique<tag<by_account>,
                    composite_key<pm_bet_object,
                        member<pm_bet_object, account_name_type, &pm_bet_object::account>,
                        member<pm_bet_object, pm_bet_id_type, &pm_bet_object::id>
                    >,
                    composite_key_compare<string_less, std::less<pm_bet_id_type>>
                >,
                ordered_unique<tag<by_market_account>,
                    composite_key<pm_bet_object,
                        member<pm_bet_object, pm_market_id_type, &pm_bet_object::market>,
                        member<pm_bet_object, account_name_type, &pm_bet_object::account>,
                        member<pm_bet_object, pm_bet_id_type, &pm_bet_object::id>
                    >,
                    composite_key_compare<std::less<pm_market_id_type>, string_less, std::less<pm_bet_id_type>>
                >,
                ordered_unique<tag<by_market_outcome_bet>,
                    composite_key<pm_bet_object,
                        member<pm_bet_object, pm_market_id_type, &pm_bet_object::market>,
                        member<pm_bet_object, int16_t, &pm_bet_object::outcome_index>,
                        member<pm_bet_object, pm_bet_id_type, &pm_bet_object::id>
                    >,
                    composite_key_compare<std::less<pm_market_id_type>, std::less<int16_t>, std::less<pm_bet_id_type>>
                >,
                ordered_unique<tag<by_epoch>,
                    composite_key<pm_bet_object,
                        member<pm_bet_object, pm_market_id_type, &pm_bet_object::market>,
                        member<pm_bet_object, uint32_t, &pm_bet_object::epoch>,
                        member<pm_bet_object, pm_bet_id_type, &pm_bet_object::id>
                    >,
                    composite_key_compare<std::less<pm_market_id_type>, std::less<uint32_t>, std::less<pm_bet_id_type>>
                >
            >,
            allocator<pm_bet_object>
        > pm_bet_index;

        // ───────────────────────────── 1.5 pm_liquidity_object ─────────────────────────────
        class pm_liquidity_object : public object<pm_liquidity_object_type, pm_liquidity_object> {
        public:
            pm_liquidity_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_liquidity_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            pm_market_id_type market;
            account_name_type provider;       ///< empty = Lazy Pool
            share_type        amount;
            share_type        weight_a;
            share_type        weight_b;
            share_type        b_share;
            uint32_t          sec_to_expiration = 0;
            time_point_sec    deposit_time;
            share_type        earned_fee;
            uint8_t           status = 0;      ///< 0 active, 3 resolved/closed
        };

        struct by_provider;
        typedef multi_index_container<
            pm_liquidity_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_liquidity_object, pm_liquidity_id_type, &pm_liquidity_object::id>>,
                ordered_unique<tag<by_market>,
                    composite_key<pm_liquidity_object,
                        member<pm_liquidity_object, pm_market_id_type, &pm_liquidity_object::market>,
                        member<pm_liquidity_object, pm_liquidity_id_type, &pm_liquidity_object::id>
                    >,
                    composite_key_compare<std::less<pm_market_id_type>, std::less<pm_liquidity_id_type>>
                >,
                ordered_unique<tag<by_provider>,
                    composite_key<pm_liquidity_object,
                        member<pm_liquidity_object, account_name_type, &pm_liquidity_object::provider>,
                        member<pm_liquidity_object, pm_liquidity_id_type, &pm_liquidity_object::id>
                    >,
                    composite_key_compare<string_less, std::less<pm_liquidity_id_type>>
                >
            >,
            allocator<pm_liquidity_object>
        > pm_liquidity_index;

        // ───────────────────────────── 1.6 pm_commit_object ─────────────────────────────
        class pm_commit_object : public object<pm_commit_object_type, pm_commit_object> {
        public:
            pm_commit_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_commit_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            pm_market_id_type market;
            account_name_type account;
            fc::sha256        commitment;
            share_type        escrow_amount;
            uint16_t          no_reveal_fee_percent = 0;  ///< snapshotted at commit (consensus-checked)
            time_point_sec    commit_time;
            time_point_sec    reveal_deadline;
            uint8_t           status = 0;   ///< 0 committed, 1 revealed, 2 forfeited
        };

        struct by_reveal_deadline;
        struct by_commit_account;
        typedef multi_index_container<
            pm_commit_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_commit_object, pm_commit_id_type, &pm_commit_object::id>>,
                ordered_unique<tag<by_market>,
                    composite_key<pm_commit_object,
                        member<pm_commit_object, pm_market_id_type, &pm_commit_object::market>,
                        member<pm_commit_object, pm_commit_id_type, &pm_commit_object::id>
                    >,
                    composite_key_compare<std::less<pm_market_id_type>, std::less<pm_commit_id_type>>
                >,
                ordered_unique<tag<by_commit_account>,
                    composite_key<pm_commit_object,
                        member<pm_commit_object, account_name_type, &pm_commit_object::account>,
                        member<pm_commit_object, pm_commit_id_type, &pm_commit_object::id>
                    >,
                    composite_key_compare<string_less, std::less<pm_commit_id_type>>
                >,
                ordered_unique<tag<by_reveal_deadline>,
                    composite_key<pm_commit_object,
                        member<pm_commit_object, uint8_t, &pm_commit_object::status>,
                        member<pm_commit_object, time_point_sec, &pm_commit_object::reveal_deadline>,
                        member<pm_commit_object, pm_commit_id_type, &pm_commit_object::id>
                    >,
                    composite_key_compare<std::less<uint8_t>, std::less<time_point_sec>, std::less<pm_commit_id_type>>
                >
            >,
            allocator<pm_commit_object>
        > pm_commit_index;

        // ───────────────────────────── 1.7 pm_dispute_object ─────────────────────────────
        class pm_dispute_object : public object<pm_dispute_object_type, pm_dispute_object> {
        public:
            pm_dispute_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_dispute_object(Constructor&& c, allocator<Allocator> a) : reason(a), oracle_response(a) { c(*this); }

            id_type           id;
            pm_market_id_type market;
            account_name_type disputer;
            share_type        dispute_fee;
            shared_string     reason;
            time_point_sec    filed_time;
            time_point_sec    oracle_response_deadline;
            uint8_t           dispute_mode = 0;
            time_point_sec    voting_end_time;
            time_point_sec    auto_close_time;
            int16_t           proposed_outcome = -1;
            uint8_t           status = 0;   ///< 0 open,1 oracle-wrong,2 oracle-right,3 auto-closed
            shared_string     oracle_response;          ///< oracle's public rebuttal (empty until it responds)
            time_point_sec    oracle_response_time;     ///< when the rebuttal was posted (0 = none)
        };

        struct by_voting_end;
        struct by_auto_close;
        typedef multi_index_container<
            pm_dispute_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_dispute_object, pm_dispute_id_type, &pm_dispute_object::id>>,
                ordered_unique<tag<by_market>, member<pm_dispute_object, pm_market_id_type, &pm_dispute_object::market>>,
                ordered_unique<tag<by_voting_end>,
                    composite_key<pm_dispute_object,
                        member<pm_dispute_object, uint8_t, &pm_dispute_object::status>,
                        member<pm_dispute_object, time_point_sec, &pm_dispute_object::voting_end_time>,
                        member<pm_dispute_object, pm_dispute_id_type, &pm_dispute_object::id>
                    >,
                    composite_key_compare<std::less<uint8_t>, std::less<time_point_sec>, std::less<pm_dispute_id_type>>
                >,
                ordered_unique<tag<by_auto_close>,
                    composite_key<pm_dispute_object,
                        member<pm_dispute_object, uint8_t, &pm_dispute_object::status>,
                        member<pm_dispute_object, time_point_sec, &pm_dispute_object::auto_close_time>,
                        member<pm_dispute_object, pm_dispute_id_type, &pm_dispute_object::id>
                    >,
                    composite_key_compare<std::less<uint8_t>, std::less<time_point_sec>, std::less<pm_dispute_id_type>>
                >
            >,
            allocator<pm_dispute_object>
        > pm_dispute_index;

        // ───────────────────────────── 1.8 pm_dispute_vote_object ─────────────────────────────
        class pm_dispute_vote_object : public object<pm_dispute_vote_object_type, pm_dispute_vote_object> {
        public:
            pm_dispute_vote_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_dispute_vote_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            pm_market_id_type market;
            account_name_type voter;
            int16_t           vote_outcome = -1;  ///< -1 = uphold oracle
            int16_t           vote_percent = 0;
            time_point_sec    time;
        };

        struct by_market_voter;
        struct by_voter;
        typedef multi_index_container<
            pm_dispute_vote_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_dispute_vote_object, pm_dispute_vote_id_type, &pm_dispute_vote_object::id>>,
                ordered_unique<tag<by_market_voter>,
                    composite_key<pm_dispute_vote_object,
                        member<pm_dispute_vote_object, pm_market_id_type, &pm_dispute_vote_object::market>,
                        member<pm_dispute_vote_object, account_name_type, &pm_dispute_vote_object::voter>
                    >,
                    composite_key_compare<std::less<pm_market_id_type>, string_less>
                >,
                ordered_unique<tag<by_voter>,
                    composite_key<pm_dispute_vote_object,
                        member<pm_dispute_vote_object, account_name_type, &pm_dispute_vote_object::voter>,
                        member<pm_dispute_vote_object, pm_dispute_vote_id_type, &pm_dispute_vote_object::id>
                    >,
                    composite_key_compare<string_less, std::less<pm_dispute_vote_id_type>>
                >
            >,
            allocator<pm_dispute_vote_object>
        > pm_dispute_vote_index;

        // ───────────────────────────── 1.9 Lazy Liquidity Pool (allocation-only) ─────────────────────────────
        class pm_lazy_pool_object : public object<pm_lazy_pool_object_type, pm_lazy_pool_object> {
        public:
            pm_lazy_pool_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_lazy_pool_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type    id;                 ///< singleton (id == 0)
            share_type total_shares;
            share_type free_balance;
            share_type allocated_balance;
            share_type earned_balance;     ///< monotonically non-decreasing
            fc::uint128_t reward_per_share = 0; ///< LAZY_POOL_PRECISION (1e9) accumulator
            share_type leverage_fund_used; ///< total active leverage loans (cap on free_balance)
            share_type pending_withdrawals; ///< VIZ owed to queued withdrawers not yet paid (first claim on returning capital); free_balance never goes negative
        };

        typedef multi_index_container<
            pm_lazy_pool_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_lazy_pool_object, pm_lazy_pool_id_type, &pm_lazy_pool_object::id>>
            >,
            allocator<pm_lazy_pool_object>
        > pm_lazy_pool_index;

        class pm_lazy_deposit_object : public object<pm_lazy_deposit_object_type, pm_lazy_deposit_object> {
        public:
            pm_lazy_deposit_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_lazy_deposit_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            account_name_type account;
            share_type        shares;
            share_type        principal;
            fc::uint128_t     reward_snapshot = 0;
            share_type        pending_rewards;
            time_point_sec    unlock_time;
        };

        struct by_deposit_account;
        typedef multi_index_container<
            pm_lazy_deposit_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_lazy_deposit_object, pm_lazy_deposit_id_type, &pm_lazy_deposit_object::id>>,
                ordered_unique<tag<by_deposit_account>, member<pm_lazy_deposit_object, account_name_type, &pm_lazy_deposit_object::account>, string_less>
            >,
            allocator<pm_lazy_deposit_object>
        > pm_lazy_deposit_index;

        // Queued lazy-pool withdrawal: created when a withdrawal cannot be paid in full from
        // free_balance at request time. Shares are already burned and rewards/penalty already
        // applied; `amount` is the fixed VIZ still owed. Serviced FIFO (by id) as capital returns
        // to free_balance, so the pool never pays out more than it holds liquid (free_balance
        // never goes negative). A withdrawal that IS fully covered creates and clears its request
        // within the same operation.
        class pm_lazy_withdraw_request_object : public object<pm_lazy_withdraw_request_object_type, pm_lazy_withdraw_request_object> {
        public:
            pm_lazy_withdraw_request_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_lazy_withdraw_request_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            account_name_type account;
            share_type        amount;   ///< VIZ still owed to this account
            time_point_sec    created;  ///< enqueue time (FIFO order == id)
        };

        struct by_request_account;
        typedef multi_index_container<
            pm_lazy_withdraw_request_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_lazy_withdraw_request_object, pm_lazy_withdraw_request_id_type, &pm_lazy_withdraw_request_object::id>>,
                ordered_non_unique<tag<by_request_account>, member<pm_lazy_withdraw_request_object, account_name_type, &pm_lazy_withdraw_request_object::account>, string_less>
            >,
            allocator<pm_lazy_withdraw_request_object>
        > pm_lazy_withdraw_request_index;

        class pm_lazy_allocation_object : public object<pm_lazy_allocation_object_type, pm_lazy_allocation_object> {
        public:
            pm_lazy_allocation_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_lazy_allocation_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            pm_market_id_type market;
            share_type        amount;
            share_type        original_amount;
            share_type        recalled_amount;
            share_type        returned_amount;
            share_type        bets_sum_at_check;
            uint32_t          check_step = 0;    ///< 0 just allocated, 1..10
            time_point_sec    last_check_time;
            uint8_t           status = 0;        ///< 0 active, 1 returned
        };

        struct by_alloc_check;
        typedef multi_index_container<
            pm_lazy_allocation_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_lazy_allocation_object, pm_lazy_allocation_id_type, &pm_lazy_allocation_object::id>>,
                ordered_unique<tag<by_market>, member<pm_lazy_allocation_object, pm_market_id_type, &pm_lazy_allocation_object::market>>,
                ordered_unique<tag<by_alloc_check>,
                    composite_key<pm_lazy_allocation_object,
                        member<pm_lazy_allocation_object, uint8_t, &pm_lazy_allocation_object::status>,
                        member<pm_lazy_allocation_object, time_point_sec, &pm_lazy_allocation_object::last_check_time>,
                        member<pm_lazy_allocation_object, pm_lazy_allocation_id_type, &pm_lazy_allocation_object::id>
                    >,
                    composite_key_compare<std::less<uint8_t>, std::less<time_point_sec>, std::less<pm_lazy_allocation_id_type>>
                >
            >,
            allocator<pm_lazy_allocation_object>
        > pm_lazy_allocation_index;

        // ───────────────────────────── 1.12 pm_leverage_position (HF14 margin) ─────────────────────────────
        class pm_leverage_position_object : public object<pm_leverage_position_object_type, pm_leverage_position_object> {
        public:
            pm_leverage_position_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_leverage_position_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            pm_market_id_type market;
            account_name_type account;
            int16_t           outcome_index = 0;
            share_type        collateral;
            share_type        loan;
            share_type        total_bet;
            share_type        tokens;             ///< weight received from the AMM
            pm_bet_id_type    bet;                ///< the underlying pm_bet
            share_type        pool_profit;        ///< loan × R% / 100
            share_type        liquidation_threshold; ///< loan × (1 + R%/100)
            uint8_t           status = 0;         ///< 0 active,1 liquidated,2 resolved_won,3 resolved_lost,4 closed_voluntary,5 converted
            time_point_sec    liquidated_at;
            pm_bet_id_type    liquidated_by_bet;
            share_type        cancel_value_at_liquidation;
            share_type        pool_received;
            share_type        bettor_received;
            time_point_sec    created_time;
            time_point_sec    last_update;
            share_type        funding_paid;       ///< cumulative funding charged from the bettor's equity → pool (raises effective obligation)
            time_point_sec    funding_due_time;   ///< next 24h funding-period boundary (created_time + N×24h); 0 = none yet
        };

        struct by_lev_market_status;
        struct by_lev_account;
        struct by_lev_bet;
        struct by_lev_funding_due;
        typedef multi_index_container<
            pm_leverage_position_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_leverage_position_object, pm_leverage_position_id_type, &pm_leverage_position_object::id>>,
                ordered_unique<tag<by_lev_market_status>,
                    composite_key<pm_leverage_position_object,
                        member<pm_leverage_position_object, pm_market_id_type, &pm_leverage_position_object::market>,
                        member<pm_leverage_position_object, uint8_t, &pm_leverage_position_object::status>,
                        member<pm_leverage_position_object, pm_leverage_position_id_type, &pm_leverage_position_object::id>
                    >,
                    composite_key_compare<std::less<pm_market_id_type>, std::less<uint8_t>, std::less<pm_leverage_position_id_type>>
                >,
                ordered_unique<tag<by_lev_account>,
                    composite_key<pm_leverage_position_object,
                        member<pm_leverage_position_object, account_name_type, &pm_leverage_position_object::account>,
                        member<pm_leverage_position_object, pm_leverage_position_id_type, &pm_leverage_position_object::id>
                    >,
                    composite_key_compare<string_less, std::less<pm_leverage_position_id_type>>
                >,
                // (bet, id): `bet` is left at its default (0) for every leverage position — the evaluator
                // never links an underlying pm_bet — so a bare ordered_unique on `bet` alone let only ONE
                // position exist chain-wide; the 2nd pm_leverage_open failed insert with a uniqueness
                // violation. Append `id` as tiebreaker (like every sibling index here) so inserts always
                // succeed while a bet-prefix lookup still works.
                ordered_unique<tag<by_lev_bet>,
                    composite_key<pm_leverage_position_object,
                        member<pm_leverage_position_object, pm_bet_id_type, &pm_leverage_position_object::bet>,
                        member<pm_leverage_position_object, pm_leverage_position_id_type, &pm_leverage_position_object::id>
                    >,
                    composite_key_compare<std::less<pm_bet_id_type>, std::less<pm_leverage_position_id_type>>
                >,
                ordered_unique<tag<by_lev_funding_due>,
                    composite_key<pm_leverage_position_object,
                        member<pm_leverage_position_object, uint8_t, &pm_leverage_position_object::status>,
                        member<pm_leverage_position_object, time_point_sec, &pm_leverage_position_object::funding_due_time>,
                        member<pm_leverage_position_object, pm_leverage_position_id_type, &pm_leverage_position_object::id>
                    >,
                    composite_key_compare<std::less<uint8_t>, std::less<time_point_sec>, std::less<pm_leverage_position_id_type>>
                >
            >,
            allocator<pm_leverage_position_object>
        > pm_leverage_position_index;

        // ───────────────────────────── 1.13 pm_creator_ban_object ─────────────────────────────
        // A creator may be temporarily (or permanently) barred from creating new markets as a
        // sanction issued by an account-mode dispute resolver (scenario #14). One row per banned
        // creator; create_market consults by_account and rejects while now < banned_until.
        class pm_creator_ban_object : public object<pm_creator_ban_object_type, pm_creator_ban_object> {
        public:
            pm_creator_ban_object() = delete;
            template<typename Constructor, typename Allocator>
            pm_creator_ban_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            account_name_type creator;
            time_point_sec    banned_until;   ///< time_point_sec::maximum() = permanent; past = not banned
            uint32_t          ban_count = 0;
            account_name_type banned_by;      ///< resolver that set the current ban (may pm_unban)
        };

        struct by_ban_account;
        struct by_ban_expiry;
        typedef multi_index_container<
            pm_creator_ban_object,
            indexed_by<
                ordered_unique<tag<by_id>, member<pm_creator_ban_object, pm_creator_ban_id_type, &pm_creator_ban_object::id>>,
                ordered_unique<tag<by_ban_account>, member<pm_creator_ban_object, account_name_type, &pm_creator_ban_object::creator>, string_less>,
                // (banned_until, id): the per-block cron sweeps expired temp bans oldest-first. Cleared
                // bans have banned_until = 0 (past), permanent bans = maximum(), so the sweep skips both.
                ordered_unique<tag<by_ban_expiry>,
                    composite_key<pm_creator_ban_object,
                        member<pm_creator_ban_object, time_point_sec, &pm_creator_ban_object::banned_until>,
                        member<pm_creator_ban_object, pm_creator_ban_id_type, &pm_creator_ban_object::id>
                    >,
                    composite_key_compare<std::less<time_point_sec>, std::less<pm_creator_ban_id_type>>
                >
            >,
            allocator<pm_creator_ban_object>
        > pm_creator_ban_index;

} } // graphene::chain

FC_REFLECT((graphene::chain::pm_oracle_object),
    (id)(owner)(insurance)(fee_percent)(fixed_fee)(rules_url)(active_since)(last_active_time)(banned_until)
    (markets_accepted)(markets_resolved)(no_contest_count)(missed_count)(disputes_received)(disputes_lost)
    (disputes_won)(disputes_auto_closed)(dispute_responses_missed)(total_volume_resolved)(total_insurance_slashed)
    (avg_resolution_time)(penalty_stamps)(bans_received)(last_penalty_stamp_time)
    (auto_accept_creator)(auto_accept_resolver)(auto_accept)(banned_by)(active_markets)
    (markets_in_dispute_window)(disputes_awaiting_response)(disputes_awaiting_decision)
    (resolved_late_count)(resolution_time_hist))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_oracle_object, graphene::chain::pm_oracle_index)

FC_REFLECT((graphene::chain::pm_market_object),
    (id)(creator)(oracle)(market_type)(outcome_count)(url)(status)(payout_status)(created_time)(accept_deadline)
    (betting_expiration)(result_expiration)(finalized_time)(resolved_outcome)(reserve_a)(reserve_b)(k)(a_bets_sum)(b_bets_sum)(lmsr_b)(lmsr_subsidy)
    (bets_sum)(liquidity_sum)(oracle_fee_percent)(creator_fee_percent)(liquidity_fee_percent)(oracle_fixed_fee)
    (liquidity_fee_earned)(forfeit_pool)(time_penalty_type)(time_penalty_value)(penalty_curve_type)
    (allow_early_resolution)(allow_cancellation)(allow_batch)(allow_instant_bet)(endogeneity_tier)(current_epoch)
    (dispute_mode)(dispute_resolver)(dispute_penalty_percent)(decision_url)(decision_reason))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_market_object, graphene::chain::pm_market_index)

FC_REFLECT((graphene::chain::pm_outcome_object),
    (id)(market)(outcome_index)(label)(q)(bets_sum)(weight_sum)(bets_count))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_outcome_object, graphene::chain::pm_outcome_index)

FC_REFLECT((graphene::chain::pm_bet_object),
    (id)(market)(account)(side)(outcome_index)(amount)(weight)(price)(time_penalty)(mode)(epoch)(status)
    (min_tokens)(resolved_amount)(created_time))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_bet_object, graphene::chain::pm_bet_index)

FC_REFLECT((graphene::chain::pm_liquidity_object),
    (id)(market)(provider)(amount)(weight_a)(weight_b)(b_share)(sec_to_expiration)(deposit_time)(earned_fee)(status))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_liquidity_object, graphene::chain::pm_liquidity_index)

FC_REFLECT((graphene::chain::pm_commit_object),
    (id)(market)(account)(commitment)(escrow_amount)(no_reveal_fee_percent)(commit_time)(reveal_deadline)(status))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_commit_object, graphene::chain::pm_commit_index)

FC_REFLECT((graphene::chain::pm_dispute_object),
    (id)(market)(disputer)(dispute_fee)(reason)(filed_time)(oracle_response_deadline)(dispute_mode)
    (voting_end_time)(auto_close_time)(proposed_outcome)(status)(oracle_response)(oracle_response_time))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_dispute_object, graphene::chain::pm_dispute_index)

FC_REFLECT((graphene::chain::pm_dispute_vote_object),
    (id)(market)(voter)(vote_outcome)(vote_percent)(time))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_dispute_vote_object, graphene::chain::pm_dispute_vote_index)

FC_REFLECT((graphene::chain::pm_lazy_pool_object),
    (id)(total_shares)(free_balance)(allocated_balance)(earned_balance)(reward_per_share)(leverage_fund_used)(pending_withdrawals))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_lazy_pool_object, graphene::chain::pm_lazy_pool_index)

FC_REFLECT((graphene::chain::pm_lazy_withdraw_request_object),
    (id)(account)(amount)(created))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_lazy_withdraw_request_object, graphene::chain::pm_lazy_withdraw_request_index)

FC_REFLECT((graphene::chain::pm_lazy_deposit_object),
    (id)(account)(shares)(principal)(reward_snapshot)(pending_rewards)(unlock_time))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_lazy_deposit_object, graphene::chain::pm_lazy_deposit_index)

FC_REFLECT((graphene::chain::pm_lazy_allocation_object),
    (id)(market)(amount)(original_amount)(recalled_amount)(returned_amount)(bets_sum_at_check)(check_step)
    (last_check_time)(status))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_lazy_allocation_object, graphene::chain::pm_lazy_allocation_index)

FC_REFLECT((graphene::chain::pm_leverage_position_object),
    (id)(market)(account)(outcome_index)(collateral)(loan)(total_bet)(tokens)(bet)(pool_profit)
    (liquidation_threshold)(status)(liquidated_at)(liquidated_by_bet)(cancel_value_at_liquidation)
    (pool_received)(bettor_received)(created_time)(last_update)(funding_paid)(funding_due_time))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_leverage_position_object, graphene::chain::pm_leverage_position_index)

FC_REFLECT((graphene::chain::pm_creator_ban_object),
    (id)(creator)(banned_until)(ban_count)(banned_by))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_creator_ban_object, graphene::chain::pm_creator_ban_index)
