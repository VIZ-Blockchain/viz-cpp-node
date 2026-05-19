#include <graphene/chain/chain_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/paid_subscription_objects.hpp>

#include <graphene/utilities/key_conversion.hpp>

namespace graphene { namespace chain {

    void set_paid_subscription_evaluator::do_apply(const set_paid_subscription_operation& o) {
        FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_5), "set_paid_subscription_evaluator not enabled until HF 5" );
        const auto &account = _db.get_account(o.account);
        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
        //    FC_ASSERT(!account.valid, "Account flagged as invalid");

        const auto &idx = _db.get_index<paid_subscription_index>().indices().get<by_creator>();
        auto itr = idx.find(o.account);
        if (itr != idx.end()) {
            _db.modify(*itr, [&](paid_subscription_object& ps) {
                from_string(ps.url, o.url);
                ps.levels = o.levels;
                ps.amount = o.amount.amount;
                ps.period = o.period;
                ps.update_time = _db.head_block_time();
            });
        }
        else{
            if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                const auto& median_props = _db.get_validator_schedule_object().median_props;
                const dynamic_global_property_object &dgp = _db.get_dynamic_global_properties();

                FC_ASSERT(account.balance >=
                          median_props.create_paid_subscription_fee, "Account does not have sufficient funds to create a paid subscription: required ${a}.",("a",median_props.create_paid_subscription_fee));

                _db.adjust_balance(account, -median_props.create_paid_subscription_fee);
                _db.modify(dgp, [&](dynamic_global_property_object &dgp) {
                    dgp.committee_fund += median_props.create_paid_subscription_fee;
                });
            }
            _db.create<paid_subscription_object>([&](paid_subscription_object& ps) {
                ps.creator = o.account;
                from_string(ps.url, o.url);
                ps.levels = o.levels;
                ps.amount = o.amount.amount;
                ps.period = o.period;
                ps.update_time = _db.head_block_time();
            });
        }
    }

    void paid_subscribe_evaluator::do_apply(const paid_subscribe_operation& o) {
        FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_5), "paid_subscribe_evaluator not enabled until HF 5" );
        FC_ASSERT(o.level > 0, "Paid subscibe can not be turn off manually, please wait subscribe end time or turn off auto_renewal.");

        const auto& subscriber = _db.get_account(o.subscriber);
        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
        //    FC_ASSERT(!subscriber.valid, "Account flagged as invalid");
        const auto& account = _db.get_account(o.account);
        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
        //    FC_ASSERT(!account.valid, "Account flagged as invalid");

        const auto &idx = _db.get_index<paid_subscription_index>().indices().get<by_creator>();
        auto itr = idx.find(o.account);
        FC_ASSERT(itr != idx.end(), "Paid subscription was not found.");

        if(itr->levels == 0){
            FC_ASSERT(false, "Paid subscription was closed.");
        }
        FC_ASSERT(itr->levels >= o.level, "Paid subscribe level greater that allowed in subscription options.");
        FC_ASSERT(itr->period == o.period, "Paid subscribe period must be equal period in subscription options.");
        FC_ASSERT(itr->amount == o.amount.amount, "Paid subscribe amount must be equal amount per level in subscription options.");

        share_type summary_token_amount=o.amount.amount * o.level;
        asset summary_amount=asset(summary_token_amount,TOKEN_SYMBOL);
        uint64_t period_sec=o.period * 86400;

        bool new_subscribe = true;
        const auto &idx2 = _db.get_index<paid_subscribe_index>().indices().get<by_subscribe>();
        auto itr2 = idx2.find(boost::make_tuple(o.subscriber,o.account));
        if (itr2 != idx2.end()) {
            if(itr2->active){
                new_subscribe = false;
                bool change_subscription_model = true;
                if(itr2->amount == o.amount.amount){
                    if(itr2->level == o.level){
                        if(itr2->period == o.period){
                            FC_ASSERT(itr2->auto_renewal != o.auto_renewal, "Paid subscribe auto_renewal must be change if subscription model not changed.");
                            change_subscription_model=false;
                            _db.modify(*itr2, [&](paid_subscribe_object& ps) {
                                ps.auto_renewal = o.auto_renewal;
                            });
                        }
                    }
                }

                if(change_subscription_model){
                    share_type old_summary_token_amount=itr2->amount * itr2->level;
                    uint64_t old_period_sec=itr2->period * 86400;
                    share_type old_summary_token_amount_remain=(uint128_t(old_summary_token_amount) * (itr2->next_time - _db.head_block_time()).to_seconds() / old_period_sec).to_uint64();

                    if(old_summary_token_amount_remain >= summary_token_amount){
                        uint64_t new_period_sec=uint128_t(uint128_t(uint128_t(old_summary_token_amount_remain) * uint128_t(o.period) * 86400) / uint128_t(summary_token_amount)).to_uint64();

                        _db.modify(*itr2, [&](paid_subscribe_object& ps) {
                            ps.level = o.level;
                            ps.amount = o.amount.amount;
                            ps.period = o.period;
                            ps.start_time = _db.head_block_time();
                            ps.next_time = ps.start_time + fc::seconds(new_period_sec);
                            ps.end_time = fc::time_point_sec::maximum();
                            ps.active = true;
                            ps.auto_renewal = o.auto_renewal;
                        });

                        _db.push_virtual_operation(paid_subscription_action_operation(o.subscriber, o.account, o.level, o.amount, o.period, new_period_sec, asset(0, TOKEN_SYMBOL)));
                    }
                    else{
                        asset paid_subscription_cost=asset(summary_token_amount - old_summary_token_amount_remain,TOKEN_SYMBOL);

                        FC_ASSERT(_db.get_balance(subscriber, TOKEN_SYMBOL) >=
                                  paid_subscription_cost, "Account does not have sufficient TOKEN for paid subscribe.");

                        _db.adjust_balance(subscriber, -paid_subscription_cost);
                        _db.adjust_balance(account, paid_subscription_cost);

                        _db.modify(*itr2, [&](paid_subscribe_object& ps) {
                            ps.level = o.level;
                            ps.amount = o.amount.amount;
                            ps.period = o.period;
                            ps.start_time = _db.head_block_time();
                            ps.next_time = ps.start_time + fc::seconds(period_sec);
                            ps.end_time = fc::time_point_sec::maximum();
                            ps.active = true;
                            ps.auto_renewal = o.auto_renewal;
                        });

                        _db.push_virtual_operation(paid_subscription_action_operation(o.subscriber, o.account, o.level, o.amount, o.period, period_sec, paid_subscription_cost));
                    }
                }
            }
            else{
                _db.remove(*itr2);
                new_subscribe = true;
            }
        }
        if(new_subscribe){
            FC_ASSERT(_db.get_balance(subscriber, TOKEN_SYMBOL) >=
                      summary_amount, "Account does not have sufficient TOKEN for paid subscribe.");

            _db.adjust_balance(subscriber, -summary_amount);
            _db.adjust_balance(account, summary_amount);

            _db.create<paid_subscribe_object>([&](paid_subscribe_object& ps) {
                ps.subscriber = o.subscriber;
                ps.creator = o.account;
                ps.level = o.level;
                ps.amount = o.amount.amount;
                ps.period = o.period;
                ps.start_time = _db.head_block_time();
                ps.next_time = ps.start_time + fc::seconds(period_sec);
                ps.end_time = fc::time_point_sec::maximum();
                ps.active = true;
                ps.auto_renewal = o.auto_renewal;
            });

            _db.push_virtual_operation(paid_subscription_action_operation(o.subscriber, o.account, o.level, o.amount, o.period, period_sec, summary_amount));
        }
    }
} } // graphene::chain