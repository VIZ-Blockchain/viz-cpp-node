/*
 * Hardfork application for graphene::chain::database.
 *
 * Extracted verbatim from database.cpp to shrink that translation unit (the
 * single longest-to-compile file in the project) and give the hardfork logic
 * its own compilation unit. No behavior change: the method bodies below are
 * byte-for-byte the ones that previously lived in database.cpp.
 *
 * These methods rely only on the database members declared in database.hpp
 * (_hardfork_times, _hardfork_versions, _log_hardforks) and on the same object
 * indexes and helpers the rest of the chain library uses. The include set below
 * mirrors the graphene/chain/* headers database.cpp pulls in for the hardfork
 * path (apply_hardfork touches proposal_index, required_approval_index, and the
 * full range of core object indexes), so keep it in sync with database.cpp.
 */

#include <graphene/chain/database.hpp>
#include <graphene/chain/database_exceptions.hpp>
#include <graphene/chain/db_with.hpp>
#include <graphene/chain/index.hpp>
#include <graphene/chain/evaluator_registry.hpp>
#include <graphene/chain/custom_operation_interpreter.hpp>
#include <graphene/chain/operation_notification.hpp>
#include <graphene/chain/chain_evaluator.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/compound.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/shared_db_merkle.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/committee_objects.hpp>
#include <graphene/chain/invite_objects.hpp>
#include <graphene/chain/paid_subscription_objects.hpp>
#include <graphene/chain/pm_objects.hpp>   // HF14: apply_hardfork instantiates pm_lazy_pool_object

#include <graphene/protocol/chain_operations.hpp>

#include <fc/uint128_t.hpp>

namespace graphene { namespace chain {

// Virtual-schedule lap length, mirrored from database.cpp. The reset in
// reset_virtual_schedule_time (below) must use the same constant the scheduler
// in database.cpp uses; keep the two definitions in sync if either changes.
#define VIRTUAL_SCHEDULE_LAP_LENGTH  ( fc::uint128_t(uint64_t(-1)) )
#define VIRTUAL_SCHEDULE_LAP_LENGTH2 ( fc::uint128_t::max_value() )

// Local mirror of the u256 promotion helper used by apply_hardfork. Defined the
// same way in database.cpp and chain_evaluator.cpp; kept file-local here so this
// TU is self-contained.
inline u256 to256(const fc::uint128_t &t) {
    u256 v(t.hi);
    v <<= 64;
    v += t.lo;
    return v;
}

        void database::init_hardforks() {
            const dynamic_global_property_object &dpo = get_dynamic_global_properties();
            _hardfork_times[0] = dpo.genesis_time;
            _hardfork_versions[0] = hardfork_version(CHAIN_STARTUP_VERSION);

            _hardfork_times[CHAIN_HARDFORK_1] = fc::time_point_sec(CHAIN_HARDFORK_1_TIME);
            _hardfork_versions[CHAIN_HARDFORK_1] = CHAIN_HARDFORK_1_VERSION;

            _hardfork_times[CHAIN_HARDFORK_2] = fc::time_point_sec(CHAIN_HARDFORK_2_TIME);
            _hardfork_versions[CHAIN_HARDFORK_2] = CHAIN_HARDFORK_2_VERSION;

            _hardfork_times[CHAIN_HARDFORK_3] = fc::time_point_sec(CHAIN_HARDFORK_3_TIME);
            _hardfork_versions[CHAIN_HARDFORK_3] = CHAIN_HARDFORK_3_VERSION;

            _hardfork_times[CHAIN_HARDFORK_4] = fc::time_point_sec(CHAIN_HARDFORK_4_TIME);
            _hardfork_versions[CHAIN_HARDFORK_4] = CHAIN_HARDFORK_4_VERSION;

            _hardfork_times[CHAIN_HARDFORK_5] = fc::time_point_sec(CHAIN_HARDFORK_5_TIME);
            _hardfork_versions[CHAIN_HARDFORK_5] = CHAIN_HARDFORK_5_VERSION;

            _hardfork_times[CHAIN_HARDFORK_6] = fc::time_point_sec(CHAIN_HARDFORK_6_TIME);
            _hardfork_versions[CHAIN_HARDFORK_6] = CHAIN_HARDFORK_6_VERSION;

            _hardfork_times[CHAIN_HARDFORK_7] = fc::time_point_sec(CHAIN_HARDFORK_7_TIME);
            _hardfork_versions[CHAIN_HARDFORK_7] = CHAIN_HARDFORK_7_VERSION;

            _hardfork_times[CHAIN_HARDFORK_8] = fc::time_point_sec(CHAIN_HARDFORK_8_TIME);
            _hardfork_versions[CHAIN_HARDFORK_8] = CHAIN_HARDFORK_8_VERSION;

            _hardfork_times[CHAIN_HARDFORK_9] = fc::time_point_sec(CHAIN_HARDFORK_9_TIME);
            _hardfork_versions[CHAIN_HARDFORK_9] = CHAIN_HARDFORK_9_VERSION;

            _hardfork_times[CHAIN_HARDFORK_10] = fc::time_point_sec(CHAIN_HARDFORK_10_TIME);
            _hardfork_versions[CHAIN_HARDFORK_10] = CHAIN_HARDFORK_10_VERSION;

            _hardfork_times[CHAIN_HARDFORK_11] = fc::time_point_sec(CHAIN_HARDFORK_11_TIME);
            _hardfork_versions[CHAIN_HARDFORK_11] = CHAIN_HARDFORK_11_VERSION;

            _hardfork_times[CHAIN_HARDFORK_12] = fc::time_point_sec(CHAIN_HARDFORK_12_TIME);
            _hardfork_versions[CHAIN_HARDFORK_12] = CHAIN_HARDFORK_12_VERSION;

            _hardfork_times[CHAIN_HARDFORK_13] = fc::time_point_sec(CHAIN_HARDFORK_13_TIME);
            _hardfork_versions[CHAIN_HARDFORK_13] = CHAIN_HARDFORK_13_VERSION;

            _hardfork_times[CHAIN_HARDFORK_14] = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
            _hardfork_versions[CHAIN_HARDFORK_14] = CHAIN_HARDFORK_14_VERSION;

            const auto &hardforks = get_hardfork_property_object();
            FC_ASSERT(
                hardforks.last_hardfork <= CHAIN_NUM_HARDFORKS,
                "Chain knows of more hardforks than configuration",
                ("hardforks.last_hardfork", hardforks.last_hardfork)
                ("CHAIN_NUM_HARDFORKS", CHAIN_NUM_HARDFORKS));
            FC_ASSERT(
                _hardfork_versions[hardforks.last_hardfork] <= CHAIN_VERSION,
                "Blockchain version is older than last applied hardfork");
            FC_ASSERT(CHAIN_HARDFORK_VERSION == _hardfork_versions[CHAIN_NUM_HARDFORKS]);
        }

        void database::reset_virtual_schedule_time() {
            const validator_schedule_object &wso = get_validator_schedule_object();
            modify(wso, [&](validator_schedule_object &o) {
                o.current_virtual_time = fc::uint128_t(); // reset it 0
            });

            const auto &idx = get_index<validator_index>().indices();
            for (const auto &validator : idx) {
                modify(validator, [&](validator_object &wobj) {
                    wobj.virtual_position = fc::uint128_t();
                    wobj.virtual_last_update = wso.current_virtual_time;
                    wobj.virtual_scheduled_time = VIRTUAL_SCHEDULE_LAP_LENGTH2 /
                                                  (wobj.counted_votes.value + 1);
                });
            }
        }

        void database::process_hardforks() {
            try {
                // If there are upcoming hardforks and the next one is later, do nothing
                const auto &hardforks = get_hardfork_property_object();

                while (_hardfork_versions[hardforks.last_hardfork] <
                       hardforks.next_hardfork
                       &&
                       hardforks.next_hardfork_time <= head_block_time()) {
                    if (hardforks.last_hardfork < CHAIN_NUM_HARDFORKS) {
                        apply_hardfork(hardforks.last_hardfork + 1);
                    } else {
                        throw unknown_hardfork_exception();
                    }
                }
            }
            FC_CAPTURE_AND_RETHROW()
        }

        bool database::has_hardfork(uint32_t hardfork) const {
            return get_hardfork_property_object().processed_hardforks.size() >
                   hardfork;
        }

        void database::set_hardfork(uint32_t hardfork, bool apply_now) {
            auto const &hardforks = get_hardfork_property_object();

            for (uint32_t i = hardforks.last_hardfork + 1;
                 i <= hardfork && i <= CHAIN_NUM_HARDFORKS; i++) {
                modify(hardforks, [&](hardfork_property_object &hpo) {
                    hpo.next_hardfork = _hardfork_versions[i];
                    hpo.next_hardfork_time = head_block_time();
                });

                if (apply_now) {
                    apply_hardfork(i);
                }
            }
        }

        void database::apply_hardfork(uint32_t hardfork) {
            if (_log_hardforks) {
                elog("HARDFORK ${hf} at block ${b}", ("hf", hardfork)("b", head_block_num()));
            }

            switch (hardfork) {
                case CHAIN_HARDFORK_1:
                    break;
                case CHAIN_HARDFORK_2:
                    break;
                case CHAIN_HARDFORK_3:
                    break;
                case CHAIN_HARDFORK_4:
                {
                    const auto &props = get_dynamic_global_properties();
                    u256 summary_awarded_rshares_u256 = 0;
                    const auto block_time = head_block_time();
                    //need to calc summary content net_rshares for competition with summary_awarded_rshares from accounts
                    //also set cashout time for current block_time for init process_content_cashout after summary_awarded_rshares payouts
                    const auto &cidx = get_index<content_index>().indices().get<by_cashout_time>();

                    auto current = cidx.begin();
                    while (current != cidx.end() && current->cashout_time > block_time) {
                        summary_awarded_rshares_u256 += to256(current->net_rshares.value);
                        modify(*current, [&](content_object &c) {
                            c.cashout_time = block_time;
                        });
                        current = cidx.begin();
                    }

                    //recalc summary_awarded_rshares and split reward fund between all accounts contains awarded_rshares
                    const auto &sidx = get_index<account_index>().indices().get<by_id>();
                    for (auto itr = sidx.begin(); itr != sidx.end(); ++itr) {
                        if(itr->awarded_rshares > 0){
                            summary_awarded_rshares_u256 += to256(itr->awarded_rshares);
                        }
                    }

                    u256 reward_fund = to256(props.total_reward_fund.amount.value);
                    u256 payout_u256 = 0;
                    u256 awarded_rshares_u256 = 0;
                    uint64_t payout = 0;
                    uint64_t summary_payout = 0;

                    const auto &eidx = get_index<account_index>().indices().get<by_id>();
                    for (auto itr = eidx.begin(); itr != eidx.end(); ++itr) {
                        if(itr->awarded_rshares > 0){
                            awarded_rshares_u256 = to256(itr->awarded_rshares);
                            payout_u256 = (awarded_rshares_u256 * reward_fund) / summary_awarded_rshares_u256;
                            FC_ASSERT(payout_u256 <= u256(uint64_t(std::numeric_limits<int64_t>::max())));

                            payout = static_cast< uint64_t >( payout_u256 );
                            summary_payout += payout;
                            share_type payout_tokens = payout;
                            asset account_payout=asset(payout_tokens,TOKEN_SYMBOL);
                            elog("HF4 awarded payment for ${a}: ${n}", ("a", itr->name)("n", payout_tokens));
                            adjust_balance(get_account(itr->name), account_payout);
                            modify(*itr, [&](account_object &a) {
                                a.awarded_rshares = 0;
                            });
                        }
                    }
                    modify(props, [&](dynamic_global_property_object &p) {
                        p.total_reward_fund.amount -= summary_payout;
                    });

                    //finally exec process_content_cashout for content objects (using remaining reward fund)
                    process_content_cashout();

                    //remove all content vote index objects
                    const auto &r1idx = get_index<content_vote_index>().indices();
                    auto r1itr = r1idx.begin();
                    while(r1itr != r1idx.end())
                    {
                        const auto& current = *r1itr;
                        ++r1itr;
                        remove(current);
                    }
                    //remove all content index objects
                    const auto &r2idx = get_index<content_index>().indices();
                    auto r2itr = r2idx.begin();
                    while(r2itr != r2idx.end())
                    {
                        const auto& current = *r2itr;
                        ++r2itr;
                        remove(current);
                    }
                    //remove all content type index objects
                    const auto &r3idx = get_index<content_type_index>().indices();
                    auto r3itr = r3idx.begin();
                    while(r3itr != r3idx.end())
                    {
                        const auto& current = *r3itr;
                        ++r3itr;
                        remove(current);
                    }

                    modify(props, [&](dynamic_global_property_object &p) {
                        p.total_reward_shares=0;
                    });

                    //recalc validator votes for fair DPOS
                    const auto &widx = get_index<validator_vote_index>().indices();
                    for(auto witr = widx.begin(); witr != widx.end(); ++witr) {
                        const auto &voter = get(witr->account);
                        share_type old_weight=voter.validator_vote_weight();
                        share_type new_weight=voter.validator_vote_fair_weight_prehf5();
                        adjust_validator_vote(get(witr->validator), -old_weight);
                        adjust_validator_vote(get(witr->validator), new_weight);
                    }

                    break;
                }
                case CHAIN_HARDFORK_5:
                {
                    //clear votes for each validator
                    const auto &widx = get_index<validator_index>().indices().get<by_id>();
                    for (auto itr = widx.begin();
                         itr != widx.end();
                         ++itr) {
                        modify(*itr, [&](validator_object &w) {
                            elog("HF5 validator ${a} was votes: ${n}", ("a", w.owner)("n", w.votes));
                            w.votes = 0;
                            w.counted_votes = 0;
                        });
                    }
                    //recalc validator votes for fair DPOS
                    const auto &widx2 = get_index<validator_vote_index>().indices();
                    for(auto witr = widx2.begin(); witr != widx2.end(); ++witr) {
                        const auto &voter = get(witr->account);
                        const auto &validator = get(witr->validator);

                        share_type fair_weight=voter.validator_vote_fair_weight();
                        modify(voter, [&](account_object &a) {
                            a.validators_vote_weight = fair_weight;
                        });
                        elog("HF5 validator ${a} calc votes: ${n}", ("a", validator.owner)("n", fair_weight));

                        adjust_validator_vote(get(witr->validator), fair_weight);
                    }
                    break;
                }
                case CHAIN_HARDFORK_6:
                {
                    const auto &props = get_dynamic_global_properties();
                    //recalc shares to change ratio with VIZ token 1:1
                    auto old_price=props.get_vesting_share_price();
                    price new_price(asset(1000, TOKEN_SYMBOL), asset(1000000, SHARES_SYMBOL));
                    asset new_total_vesting_shares(0, SHARES_SYMBOL);

                    const auto &eidx = get_index<account_index>().indices().get<by_id>();
                    for (auto itr = eidx.begin(); itr != eidx.end(); ++itr) {
                        elog("HF6 account shares recalc ${a}:", ("a", itr->name));
                        elog(" - old vesting_shares: ${a}", ("a", itr->vesting_shares.amount));
                        if(itr->proxy != CHAIN_PROXY_TO_SELF_ACCOUNT){
                            elog("- proxy found: ${a}", ("a", itr->proxy));
                            std::array<share_type, CHAIN_MAX_PROXY_RECURSION_DEPTH + 1> delta;
                            delta[0] = -(itr->vesting_shares.amount);
                            for (int i = 0; i < CHAIN_MAX_PROXY_RECURSION_DEPTH; ++i) {
                                delta[i + 1] = -(itr->proxied_vsf_votes[i]);
                            }
                            adjust_proxied_validator_votes(get_account(itr->name), delta);
                        }

                        auto old_shares=itr->vesting_shares;
                        old_shares=old_shares*1000;
                        auto tokens=old_shares*old_price;
                        auto new_shares=tokens*new_price;
                        new_shares=new_shares/1000;
                        new_total_vesting_shares+=new_shares;

                        auto old_withdraw_rate=itr->vesting_withdraw_rate;
                        old_withdraw_rate=old_withdraw_rate*1000;
                        auto withdraw_tokens=old_withdraw_rate*old_price;
                        auto new_withdraw_rate=withdraw_tokens*new_price;
                        new_withdraw_rate=new_withdraw_rate/1000;

                        modify(*itr, [&](account_object &a) {
                            a.vesting_shares = new_shares;
                            a.vesting_withdraw_rate = new_withdraw_rate;
                            a.delegated_vesting_shares = asset(0, SHARES_SYMBOL);
                            a.received_vesting_shares = asset(0, SHARES_SYMBOL);
                        });

                        elog(" - new vesting_shares: ${a}", ("a", itr->vesting_shares.amount));
                        if(itr->proxy != CHAIN_PROXY_TO_SELF_ACCOUNT){
                            elog("- proxy votes updated: ${a}", ("a", itr->proxy));
                            std::array<share_type, CHAIN_MAX_PROXY_RECURSION_DEPTH + 1> delta;
                            delta[0] = itr->vesting_shares.amount;
                            for (int i = 0; i < CHAIN_MAX_PROXY_RECURSION_DEPTH; ++i) {
                                delta[i + 1] = itr->proxied_vsf_votes[i];
                            }
                            adjust_proxied_validator_votes(get_account(itr->name), delta);
                        }
                    }

                    elog("New total_vesting_shares: ${a}", ("a", new_total_vesting_shares.amount));
                    modify(props, [&](dynamic_global_property_object &p) {
                        p.total_vesting_shares = new_total_vesting_shares;
                    });

                    //recalc delegations
                    const auto& delegations = get_index<vesting_delegation_index>().indices().get<by_id>();
                    for (auto itr = delegations.begin();
                         itr != delegations.end();
                         ++itr) {
                        auto old_delegation_shares=itr->vesting_shares;
                        //fix excess delegated_vesting_shares (need hf7 fix object)
                        create<fix_vesting_delegation_object>([&](fix_vesting_delegation_object& o) {
                            o.delegator = itr->delegator;
                            o.delegatee = itr->delegatee;
                            o.vesting_shares = old_delegation_shares;
                            elog("!!! HF6 delay fix decrease ${a} delegated_vesting_shares: ${n}", ("a", itr->delegator)("n", old_delegation_shares));
                            elog("!!! HF6 delay fix decrease ${a} received_vesting_shares: ${n}", ("a", itr->delegatee)("n", old_delegation_shares));
                        });
                        /*
                        modify(get_account(itr->delegator), [&](account_object& a) {
                            a.delegated_vesting_shares -= old_delegation_shares;
                        });
                        modify(get_account(itr->delegatee), [&](account_object& a) {
                            a.received_vesting_shares -= old_delegation_shares;
                        });
                        */
                        old_delegation_shares=old_delegation_shares*1000;
                        auto old_delegation_tokens=old_delegation_shares*old_price;
                        auto new_delegation_shares=old_delegation_tokens*new_price;
                        new_delegation_shares=new_delegation_shares/1000;
                        modify(*itr, [&](vesting_delegation_object &a) {
                            a.vesting_shares = new_delegation_shares;
                        });
                        modify(get_account(itr->delegator), [&](account_object& a) {
                            a.delegated_vesting_shares += new_delegation_shares;
                        });
                        modify(get_account(itr->delegatee), [&](account_object& a) {
                            a.received_vesting_shares += new_delegation_shares;
                        });
                    }

                    //recalc expiring delegations
                    const auto& delegations_by_exp = get_index<vesting_delegation_expiration_index>().indices().get<by_id>();
                    for (auto itr = delegations_by_exp.begin();
                         itr != delegations_by_exp.end();
                         ++itr) {
                        auto old_delegation_shares=itr->vesting_shares;
                        //fix excess delegated_vesting_shares (need hf7 fix object)
                        create<fix_vesting_delegation_object>([&](fix_vesting_delegation_object& o) {
                            o.delegator = itr->delegator;
                            o.vesting_shares = old_delegation_shares;
                            elog("!!! HF6 delay fix decrease ${a} delegated_vesting_shares: ${n}", ("a", itr->delegator)("n", old_delegation_shares));
                        });
                        /*
                        modify(get_account(itr->delegator), [&](account_object& a) {
                            a.delegated_vesting_shares -= old_delegation_shares;
                        });
                        */
                        old_delegation_shares=old_delegation_shares*1000;
                        auto old_delegation_tokens=old_delegation_shares*old_price;
                        auto new_delegation_shares=old_delegation_tokens*new_price;
                        new_delegation_shares=new_delegation_shares/1000;
                        modify(*itr, [&](vesting_delegation_expiration_object &a) {
                            a.vesting_shares = new_delegation_shares;
                        });
                        modify(get_account(itr->delegator), [&](account_object& a) {
                            a.delegated_vesting_shares += new_delegation_shares;
                        });
                    }

                    //clear votes for each validator
                    const auto &widx = get_index<validator_index>().indices().get<by_id>();
                    for (auto itr = widx.begin();
                         itr != widx.end();
                         ++itr) {
                        modify(*itr, [&](validator_object &w) {
                            elog("HF6 validator ${a} has votes: ${n}", ("a", w.owner)("n", w.votes));
                            w.votes = 0;
                            w.counted_votes = 0;
                        });
                    }
                    //recalc validator votes after shares ratio fix
                    const auto &widx2 = get_index<validator_vote_index>().indices();
                    for(auto witr = widx2.begin(); witr != widx2.end(); ++witr) {
                        const auto &voter = get(witr->account);
                        const auto &validator = get(witr->validator);

                        share_type fair_weight=voter.validator_vote_fair_weight();
                        modify(voter, [&](account_object &a) {
                            a.validators_vote_weight = fair_weight;
                        });
                        elog("HF6 validator ${a} recalc votes from ${a}: ${n}", ("a", validator.owner)("n", fair_weight));

                        adjust_validator_vote(get(witr->validator), fair_weight);
                    }
                    break;
                }
                case CHAIN_HARDFORK_7:
                {
                    const auto &committee_account = get_account(CHAIN_COMMITTEE_ACCOUNT);

                    const auto &fix_idx = get_index<fix_vesting_delegation_index>().indices();
                    auto fix_itr = fix_idx.begin();
                    while(fix_itr != fix_idx.end()) {
                        const auto &fix_current = *fix_itr;
                        ++fix_itr;
                        if(fix_current.delegator!=""){
                            modify(get_account(fix_current.delegator), [&](account_object& a) {
                                a.delegated_vesting_shares -= fix_current.vesting_shares;
                                elog("!!! HF7 delayed fix decrease ${a} delegated_vesting_shares: ${n}", ("a", fix_current.delegator)("n", fix_current.vesting_shares));
                            });
                        }
                        if(fix_current.delegatee!=""){
                            modify(get_account(fix_current.delegatee), [&](account_object& a) {
                                a.received_vesting_shares -= fix_current.vesting_shares;
                                elog("!!! HF7 delayed fix decrease ${a} received_vesting_shares: ${n}", ("a", fix_current.delegatee)("n", fix_current.vesting_shares));
                            });
                        }
                        remove(fix_current);
                    }

                    const auto &eidx = get_index<account_index>().indices().get<by_id>();
                    auto itr = eidx.begin();
                    while(itr != eidx.end()){
                        const auto &current = *itr;
                        ++itr;
                        if(!current.valid){
                            elog("HF7 found invalid account ${a}", ("a", current.name));
                            if(current.proxy != CHAIN_PROXY_TO_SELF_ACCOUNT){
                                elog("- remove proxy: ${a}", ("a", current.proxy));
                                std::array<share_type, CHAIN_MAX_PROXY_RECURSION_DEPTH + 1> delta;
                                delta[0] = -(current.vesting_shares.amount);
                                for (int i = 0; i < CHAIN_MAX_PROXY_RECURSION_DEPTH; ++i) {
                                    delta[i + 1] = -(current.proxied_vsf_votes[i]);
                                }
                                adjust_proxied_validator_votes(get_account(current.name), delta);
                            }
                            //move shares and balance to committee
                            elog("- add to committee funds: ${a} SHARES, ${b} TOKEN", ("a", current.vesting_shares)("b", current.balance));
                            modify(committee_account, [&](account_object &a) {
                                a.vesting_shares += current.vesting_shares;
                                a.balance += current.balance;
                            });

                            //remove from master_authority_history_index
                            const auto &d1idx = get_index<master_authority_history_index>().indices().get<by_account>();
                            auto delete_itr1 = d1idx.lower_bound(current.name);
                            while(delete_itr1 != d1idx.end() &&
                                   delete_itr1->account == current.name) {
                                const auto &delete_current = *delete_itr1;
                                ++delete_itr1;
                                remove(delete_current);
                            }

                            //remove from account_metadata_index
                            const auto &d2idx = get_index<account_metadata_index>().indices().get<by_account>();
                            auto delete_itr2 = d2idx.lower_bound(current.name);
                            while(delete_itr2 != d2idx.end() &&
                                   delete_itr2->account == current.name) {
                                const auto &delete_current = *delete_itr2;
                                ++delete_itr2;
                                remove(delete_current);
                            }

                            //remove from account_authority_index
                            const auto &d3idx = get_index<account_authority_index>().indices().get<by_account>();
                            auto delete_itr3 = d3idx.lower_bound(current.name);
                            while(delete_itr3 != d3idx.end() &&
                                   delete_itr3->account == current.name) {
                                const auto &delete_current = *delete_itr3;
                                ++delete_itr3;
                                remove(delete_current);
                            }

                            //remove from vesting_delegation_expiration_index by_account_expiration
                            const auto& d4idx = get_index<vesting_delegation_expiration_index>().indices().get<by_account_expiration>();
                            auto delete_itr4 = d4idx.lower_bound(std::make_tuple(current.name, fc::time_point_sec()));
                            while(delete_itr4 != d4idx.end() &&
                                   delete_itr4->delegator == current.name) {
                                const auto &delete_current = *delete_itr4;
                                ++delete_itr4;
                                modify(get_account(delete_current.delegator), [&](account_object& a) {
                                    a.delegated_vesting_shares -= delete_current.vesting_shares;
                                });
                                remove(delete_current);
                            }

                            //remove from vesting_delegation_index by_delegation
                            const auto &d5idx = get_index<vesting_delegation_index>().indices().get<by_delegation>();
                            auto delete_itr5 = d5idx.lower_bound(std::make_tuple(current.name,account_name_type()));
                            while(delete_itr5 != d5idx.end() &&
                                   delete_itr5->delegator == current.name) {
                                const auto &delete_current = *delete_itr5;
                                ++delete_itr5;
                                modify(get_account(delete_current.delegator), [&](account_object& a) {
                                    a.delegated_vesting_shares -= delete_current.vesting_shares;
                                });
                                modify(get_account(delete_current.delegatee), [&](account_object& a) {
                                    a.received_vesting_shares -= delete_current.vesting_shares;
                                });
                                remove(delete_current);
                            }

                            //remove from vesting_delegation_index by_received
                            const auto &d6idx = get_index<vesting_delegation_index>().indices().get<by_received>();
                            auto delete_itr6 = d6idx.lower_bound(std::make_tuple(current.name,account_name_type()));
                            while(delete_itr6 != d6idx.end() &&
                                   delete_itr6->delegatee == current.name) {
                                const auto &delete_current = *delete_itr6;
                                ++delete_itr6;
                                modify(get_account(delete_current.delegator), [&](account_object& a) {
                                    a.delegated_vesting_shares -= delete_current.vesting_shares;
                                });
                                modify(get_account(delete_current.delegatee), [&](account_object& a) {
                                    a.received_vesting_shares -= delete_current.vesting_shares;
                                });
                                remove(delete_current);
                            }

                            //remove from account_recovery_request_index
                            const auto &d7idx = get_index<account_recovery_request_index>().indices().get<by_account>();
                            auto delete_itr7 = d7idx.lower_bound(std::make_tuple(current.name,account_recovery_request_id_type()));
                            while(delete_itr7 != d7idx.end() &&
                                   delete_itr7->account_to_recover == current.name) {
                                const auto &delete_current = *delete_itr7;
                                ++delete_itr7;
                                remove(delete_current);
                            }

                            //remove from change_recovery_account_request_index
                            const auto &d8idx = get_index<change_recovery_account_request_index>().indices().get<by_account>();
                            auto delete_itr8 = d8idx.lower_bound(std::make_tuple(current.name,change_recovery_account_request_id_type()));
                            while(delete_itr8 != d8idx.end() &&
                                   delete_itr8->account_to_recover == current.name) {
                                const auto &delete_current = *delete_itr8;
                                ++delete_itr8;
                                remove(delete_current);
                            }

                            //decrease validators_vote_weight from all votes by invalid account
                            const auto &vidx = get_index<validator_vote_index>().indices().get<by_account_validator>();
                            auto vitr = vidx.lower_bound(boost::make_tuple(current.id, validator_id_type()));
                            while (vitr != vidx.end() && vitr->account == current.id) {
                                adjust_validator_vote(get(vitr->validator),-current.validators_vote_weight);
                                ++vitr;
                            }

                            //remove from validator_vote_index by_account_validator (remove all votes from invalid account)
                            const auto &d10idx = get_index<validator_vote_index>().indices().get<by_account_validator>();
                            auto delete_itr10 = d10idx.lower_bound(boost::make_tuple(current.id, validator_id_type()));
                            while(delete_itr10 != d10idx.end() &&
                                   delete_itr10->account == current.id) {
                                const auto &delete_current = *delete_itr10;
                                adjust_validator_vote(get(delete_itr10->validator),-current.validators_vote_weight);
                                modify(current, [&](account_object &a) {
                                    a.validators_voted_for--;
                                });
                                ++delete_itr10;
                                remove(delete_current);
                            }

                            //recalc validators_vote_weight (must be 0 after remove all validator votes from invalid account)
                            share_type current_fair_vote_weight = current.validator_vote_fair_weight();
                            modify(current, [&](account_object &a) {
                                a.validators_vote_weight = current_fair_vote_weight;
                            });

                            //look validator object from invalid account
                            const auto &invalid_validator = find<validator_object, by_name>(current.name);
                            if(invalid_validator != nullptr){//found validator
                                //remove invalid validator account from penalty index
                                const auto &d8idx = get_index<validator_penalty_expire_index>().indices().get<by_account>();
                                auto delete_itr8 = d8idx.lower_bound(invalid_validator->owner);
                                while(delete_itr8 != d8idx.end() &&
                                       delete_itr8->validator == invalid_validator->owner) {
                                    const auto &delete_current = *delete_itr8;
                                    ++delete_itr8;
                                    remove(delete_current);
                                }
                                //remove invalid validator account from schedule
                                const validator_schedule_object &wso = get_validator_schedule_object();
                                modify(wso, [&](validator_schedule_object &_wso) {
                                    for (int i = 0; i < _wso.num_scheduled_validators; i+=CHAIN_BLOCK_VALIDATOR_REPEAT) {
                                        if(_wso.current_shuffled_validators[i] == invalid_validator->owner){
                                            _wso.current_shuffled_validators[i] = account_name_type();
                                        }
                                    }
                                });
                                //recalc validators_vote_weight from all votes to invalid validator account (remove votes to invalid validator account)
                                const auto &vidx = get_index<validator_vote_index>().indices().get<by_validator_account>();
                                auto vitr = vidx.lower_bound(boost::make_tuple(invalid_validator->id, account_id_type()));
                                while (vitr != vidx.end() && vitr->validator == invalid_validator->id) {
                                    const auto &voter_account = get(vitr->account);
                                    const auto &vidx2 = get_index<validator_vote_index>().indices().get<by_account_validator>();
                                    auto vitr2 = vidx2.lower_bound(boost::make_tuple(voter_account.id, validator_id_type()));
                                    while (vitr2 != vidx2.end() && vitr2->account == voter_account.id) {
                                        adjust_validator_vote(get(vitr2->validator), -voter_account.validators_vote_weight);
                                        ++vitr2;
                                    }

                                    remove(*vitr);

                                    modify(voter_account, [&](account_object &a) {
                                        a.validators_voted_for--;
                                    });

                                    share_type fair_vote_weight = voter_account.validator_vote_fair_weight();
                                    modify(voter_account, [&](account_object &a) {
                                        a.validators_vote_weight = fair_vote_weight;
                                    });

                                    const auto &vidx3 = get_index<validator_vote_index>().indices().get<by_account_validator>();
                                    auto vitr3 = vidx3.lower_bound(boost::make_tuple(voter_account.id, validator_id_type()));
                                    while (vitr3 != vidx3.end() && vitr3->account == voter_account.id) {
                                        adjust_validator_vote(get(vitr3->validator), voter_account.validators_vote_weight);
                                        ++vitr3;
                                    }
                                    ++vitr;
                                }

                                //remove from validator_index invalid validator account
                                const auto &d9idx = get_index<validator_index>().indices().get<by_name>();
                                auto delete_itr9 = d9idx.lower_bound(current.name);
                                while(delete_itr9 != d9idx.end() &&
                                       delete_itr9->owner == current.name) {
                                    const auto &delete_current = *delete_itr9;
                                    ++delete_itr9;
                                    remove(delete_current);
                                }
                            }

                            //remove all committee votes
                            const auto &d11idx = get_index<committee_vote_index>().indices().get<by_voter>();
                            auto delete_itr11 = d11idx.lower_bound(current.name);
                            while(delete_itr11 != d11idx.end() &&
                                   delete_itr11->voter == current.name) {
                                const auto &delete_current = *delete_itr11;
                                ++delete_itr11;
                                remove(delete_current);
                            }

                            //remove all committee requests as creator
                            const auto &d12idx = get_index<committee_request_index>().indices().get<by_creator>();
                            auto delete_itr12 = d12idx.lower_bound(current.name);
                            while(delete_itr12 != d12idx.end() &&
                                   delete_itr12->creator == current.name) {
                                const auto &delete_current = *delete_itr12;
                                ++delete_itr12;
                                remove(delete_current);
                            }

                            //remove all committee requests as worker
                            const auto &d13idx = get_index<committee_request_index>().indices().get<by_worker>();
                            auto delete_itr13 = d13idx.lower_bound(current.name);
                            while(delete_itr13 != d13idx.end() &&
                                   delete_itr13->worker == current.name) {
                                const auto &delete_current = *delete_itr13;
                                ++delete_itr13;
                                remove(delete_current);
                            }

                            //remove all withdraw routes from
                            const auto &d14idx = get_index<withdraw_vesting_route_index>().indices().get<by_withdraw_route>();
                            auto delete_itr14 = d14idx.lower_bound(std::make_tuple(current.id,account_id_type()));
                            while(delete_itr14 != d14idx.end() &&
                                   delete_itr14->from_account == current.id) {
                                const auto &delete_current = *delete_itr14;
                                ++delete_itr14;
                                remove(delete_current);
                            }
                            //remove all withdraw routes to
                            const auto &d15idx = get_index<withdraw_vesting_route_index>().indices().get<by_withdraw_route>();
                            auto delete_itr15 = d15idx.lower_bound(std::make_tuple(account_id_type(),current.id));
                            while(delete_itr15 != d15idx.end() &&
                                   delete_itr15->to_account == current.id) {
                                const auto &delete_current = *delete_itr15;
                                ++delete_itr15;
                                remove(delete_current);
                            }

                            //change all escrow routes from
                            const auto &d16idx = get_index<escrow_index>().indices().get<by_from_id>();
                            auto itr16 = d16idx.lower_bound(std::make_tuple(current.name,0));
                            while(itr16 != d16idx.end() &&
                                   itr16->from == current.name) {
                                const auto &e_current = *itr16;
                                ++itr16;
                                modify(e_current, [&](escrow_object &esc) {
                                    esc.from = CHAIN_COMMITTEE_ACCOUNT;
                                });
                            }

                            //change all escrow routes from
                            const auto &d17idx = get_index<escrow_index>().indices().get<by_to>();
                            auto itr17 = d17idx.lower_bound(std::make_tuple(current.name,0));
                            while(itr17 != d17idx.end() &&
                                   itr17->to == current.name) {
                                const auto &e_current = *itr17;
                                ++itr17;
                                modify(e_current, [&](escrow_object &esc) {
                                    esc.to = CHAIN_COMMITTEE_ACCOUNT;
                                });
                            }

                            //change all escrow routes from
                            const auto &d18idx = get_index<escrow_index>().indices().get<by_agent>();
                            auto itr18 = d18idx.lower_bound(std::make_tuple(current.name,0));
                            while(itr18 != d18idx.end() &&
                                   itr18->agent == current.name) {
                                const auto &e_current = *itr18;
                                ++itr18;
                                modify(e_current, [&](escrow_object &esc) {
                                    esc.agent = CHAIN_COMMITTEE_ACCOUNT;
                                });
                            }

                            //change all invites creator
                            const auto &d19idx = get_index<invite_index>().indices().get<by_creator>();
                            auto itr19 = d19idx.lower_bound(current.name);
                            while(itr19 != d19idx.end() &&
                                   itr19->creator == current.name) {
                                const auto &i_current = *itr19;
                                ++itr19;
                                modify(i_current, [&](invite_object &i) {
                                    i.creator = CHAIN_COMMITTEE_ACCOUNT;
                                });
                            }

                            //change all invites receiver
                            const auto &d19idx2 = get_index<invite_index>().indices().get<by_receiver>();
                            auto itr19_2 = d19idx2.lower_bound(current.name);
                            while(itr19_2 != d19idx2.end() &&
                                   itr19_2->receiver == current.name) {
                                const auto &i_current = *itr19_2;
                                ++itr19_2;
                                modify(i_current, [&](invite_object &i) {
                                    i.receiver = CHAIN_COMMITTEE_ACCOUNT;
                                });
                            }

                            //remove all paid subscribes by subscriber
                            const auto &d20idx = get_index<paid_subscribe_index>().indices().get<by_subscriber>();
                            auto itr20 = d20idx.lower_bound(current.name);
                            while(itr20 != d20idx.end() &&
                                   itr20->subscriber == current.name) {
                                const auto &i_current = *itr20;
                                ++itr20;
                                remove(i_current);
                            }

                            //remove all paid subscribes by creator
                            const auto &d21idx = get_index<paid_subscribe_index>().indices().get<by_creator>();
                            auto itr21 = d21idx.lower_bound(current.name);
                            while(itr21 != d21idx.end() &&
                                   itr21->creator == current.name) {
                                const auto &i_current = *itr21;
                                ++itr21;
                                remove(i_current);
                            }

                            //remove all paid subscription by creator
                            const auto &d21idx2 = get_index<paid_subscription_index>().indices().get<by_creator>();
                            auto itr21_2 = d21idx2.lower_bound(current.name);
                            while(itr21_2 != d21idx2.end() &&
                                   itr21_2->creator == current.name) {
                                const auto &i_current = *itr21_2;
                                ++itr21_2;
                                remove(i_current);
                            }

                            //remove all proposals by account
                            const auto &d22idx = get_index<proposal_index>().indices().get<by_account>();
                            auto itr22 = d22idx.lower_bound(current.name);
                            while(itr22 != d22idx.end() &&
                                   itr22->author == current.name) {
                                const auto &i_current = *itr22;
                                ++itr22;
                                remove(i_current);
                            }

                            //remove all proposals required approval by account
                            const auto &d23idx = get_index<required_approval_index>().indices().get<by_account>();
                            auto itr23 = d23idx.lower_bound(current.name);
                            while(itr23 != d23idx.end() &&
                                   itr23->account == current.name) {
                                const auto &i_current = *itr23;
                                ++itr23;
                                remove(i_current);
                            }

                            //remove invalid account
                            remove(current);
                        }
                    }
                    break;
                }
                case CHAIN_HARDFORK_8:
                {
                    //refresh delegated_vesting_shares and received_vesting_shares
                    const auto &eidx = get_index<account_index>().indices().get<by_id>();
                    auto itr = eidx.begin();
                    while(itr != eidx.end()){
                        const auto &current = *itr;
                        ++itr;
                        modify(current, [&](account_object& a) {
                            a.delegated_vesting_shares=asset(0, SHARES_SYMBOL);
                            a.received_vesting_shares=asset(0, SHARES_SYMBOL);
                        });
                    }

                    //recalc delegations
                    const auto& delegations = get_index<vesting_delegation_index>().indices().get<by_id>();
                    for (auto itr = delegations.begin();
                         itr != delegations.end();
                         ++itr) {
                        modify(get_account(itr->delegator), [&](account_object& a) {
                            a.delegated_vesting_shares += itr->vesting_shares;
                        });
                        modify(get_account(itr->delegatee), [&](account_object& a) {
                            a.received_vesting_shares += itr->vesting_shares;
                        });
                    }

                    //recalc expiring delegations
                    const auto& delegations_by_exp = get_index<vesting_delegation_expiration_index>().indices().get<by_id>();
                    for (auto itr = delegations_by_exp.begin();
                         itr != delegations_by_exp.end();
                         ++itr) {
                        modify(get_account(itr->delegator), [&](account_object& a) {
                            a.delegated_vesting_shares += itr->vesting_shares;
                        });
                    }
                    break;
                }
                case CHAIN_HARDFORK_9:
                {
                    //remove validators without signed block
                    const auto &idx = get_index<validator_index>().indices().get<by_id>();
                    auto itr = idx.begin();
                    while(itr != idx.end()){
                        const auto &current = *itr;
                        ++itr;
                        if(0==current.last_confirmed_block_num){
                            //MUST be corrected: the validator_vote_object must also be deleted
                            //remove invalid validator account from penalty index
                            const auto &d8idx = get_index<validator_penalty_expire_index>().indices().get<by_account>();
                            auto delete_itr8 = d8idx.lower_bound(current.owner);
                            while(delete_itr8 != d8idx.end() &&
                                    delete_itr8->validator == current.owner) {
                                const auto &delete_current = *delete_itr8;
                                ++delete_itr8;
                                remove(delete_current);
                            }
                            //recalc validators_vote_weight from all votes to invalid validator account (remove votes to invalid validator account)
                            const auto &vidx = get_index<validator_vote_index>().indices().get<by_validator_account>();
                            auto vitr = vidx.lower_bound(boost::make_tuple(current.id, account_id_type()));
                            while (vitr != vidx.end() && vitr->validator == current.id) {
                                const auto &voter_account = get(vitr->account);
                                const auto &vidx2 = get_index<validator_vote_index>().indices().get<by_account_validator>();
                                auto vitr2 = vidx2.lower_bound(boost::make_tuple(voter_account.id, validator_id_type()));
                                while (vitr2 != vidx2.end() && vitr2->account == voter_account.id) {
                                    adjust_validator_vote(get(vitr2->validator), -voter_account.validators_vote_weight);
                                    ++vitr2;
                                }

                                remove(*vitr);

                                modify(voter_account, [&](account_object &a) {
                                    a.validators_voted_for--;
                                    a.valid=false;
                                });

                                share_type fair_vote_weight = voter_account.validator_vote_fair_weight();
                                modify(voter_account, [&](account_object &a) {
                                    a.validators_vote_weight = fair_vote_weight;
                                });

                                const auto &vidx3 = get_index<validator_vote_index>().indices().get<by_account_validator>();
                                auto vitr3 = vidx3.lower_bound(boost::make_tuple(voter_account.id, validator_id_type()));
                                while (vitr3 != vidx3.end() && vitr3->account == voter_account.id) {
                                    adjust_validator_vote(get(vitr3->validator), voter_account.validators_vote_weight);
                                    ++vitr3;
                                }
                                ++vitr;
                            }
                            elog("HF9 remove empty/spam validator ${a}", ("a", current.owner));
                            remove(current);
                        }
                    }

                    //remove committee requests without votes
                    const auto &idx2 = get_index<committee_request_index>().indices().get<by_id>();
                    auto itr2 = idx2.begin();
                    while(itr2 != idx2.end()){
                        const auto &current = *itr2;
                        ++itr2;
                        if(1==current.status){
                            if(0==current.votes_count){
                                remove(current);
                            }
                        }
                    }

                    //remove all paid subscriptions without subscribes
                    const auto &idx3 = get_index<paid_subscription_index>().indices().get<by_id>();
                    auto itr3 = idx3.begin();
                    while(itr3 != idx3.end()) {
                        const auto &current = *itr3;
                        ++itr3;

                        bool find_subscribe=false;

                        const auto &idx4 = get_index<paid_subscribe_index>().indices().get<by_creator>();
                        auto itr4 = idx4.find(current.creator);
                        if(itr4 != idx4.end()){
                            find_subscribe=true;
                        }

                        if(!find_subscribe){
                            remove(current);
                        }
                    }
                    break;
                }
                case CHAIN_HARDFORK_10:
                    break;
                case CHAIN_HARDFORK_11:
                {
                    //fixed error from CHAIN_HARDFORK_9 by replay, but need toggle flag valid for spam accounts
                    const auto &eidx = get_index<account_index>().indices().get<by_id>();
                    auto itr = eidx.begin();
                    while(itr != eidx.end()){
                        const auto &current = *itr;
                        ++itr;
                        if(!current.valid){
                            //toggle invalid accounts to valid
                            modify(current, [&](account_object &a) {
                                a.valid=true;
                            });
                        }
                    }
                    break;
                }
                case CHAIN_HARDFORK_12:
                    break;
                case CHAIN_HARDFORK_13:
                    // Validator reward sharing: new fields sharing_rate and
                    // pending_stakeholder_reward on validator_object default to 0 (replay
                    // initialises them), no extra migration needed.
                    break;
                case CHAIN_HARDFORK_14:
                    // Prediction Markets: instantiate the lazy-liquidity pool singleton
                    // (id 0) so pm_lazy_deposit and the allocation/leverage paths can rely
                    // on get<pm_lazy_pool_object>. All balances start at zero (empty pool).
                    create<pm_lazy_pool_object>([](pm_lazy_pool_object&) {});
                    break;
                default:
                    break;
            }

            modify(get_hardfork_property_object(), [&](hardfork_property_object &hfp) {
                FC_ASSERT(hardfork == hfp.last_hardfork +
                                      1, "Hardfork being applied out of order", ("hardfork", hardfork)("hfp.last_hardfork", hfp.last_hardfork));
                FC_ASSERT(hfp.processed_hardforks.size() ==
                          hardfork, "Hardfork being applied out of order");
                hfp.processed_hardforks.push_back(_hardfork_times[hardfork]);
                hfp.last_hardfork = hardfork;
                hfp.current_hardfork_version = _hardfork_versions[hardfork];
                FC_ASSERT(hfp.processed_hardforks[hfp.last_hardfork] ==
                          _hardfork_times[hfp.last_hardfork], "Hardfork processing failed sanity check...");
            });

            push_virtual_operation(hardfork_operation(hardfork), true);
        }
} } // graphene::chain
