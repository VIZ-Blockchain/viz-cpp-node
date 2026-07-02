#pragma once

#include <graphene/chain/evaluator.hpp>

#define DEFINE_PM_EVALUATOR(X) \
class pm_##X##_evaluator : public graphene::chain::evaluator_impl<pm_##X##_evaluator> { \
public:                                                                                   \
    typedef graphene::protocol::pm_##X##_operation operation_type;                       \
    pm_##X##_evaluator(database& db)                                                     \
        : graphene::chain::evaluator_impl<pm_##X##_evaluator>(db) {}                    \
    void do_apply(const operation_type& o);                                               \
};

namespace graphene { namespace chain {

    DEFINE_PM_EVALUATOR(oracle_register)
    DEFINE_PM_EVALUATOR(oracle_update)
    DEFINE_PM_EVALUATOR(create_market)
    DEFINE_PM_EVALUATOR(oracle_accept_market)
    DEFINE_PM_EVALUATOR(place_bet)
    DEFINE_PM_EVALUATOR(commit_bet)
    DEFINE_PM_EVALUATOR(reveal_bet)
    DEFINE_PM_EVALUATOR(cancel_bet)
    DEFINE_PM_EVALUATOR(add_liquidity)
    DEFINE_PM_EVALUATOR(withdraw_liquidity)
    DEFINE_PM_EVALUATOR(resolve_market)
    DEFINE_PM_EVALUATOR(no_contest)
    DEFINE_PM_EVALUATOR(dispute_create)
    DEFINE_PM_EVALUATOR(dispute_vote)
    DEFINE_PM_EVALUATOR(dispute_resolve)
    DEFINE_PM_EVALUATOR(transfer_position)
    DEFINE_PM_EVALUATOR(lazy_deposit)
    DEFINE_PM_EVALUATOR(lazy_withdraw)
    DEFINE_PM_EVALUATOR(leverage_open)
    DEFINE_PM_EVALUATOR(leverage_close)
    DEFINE_PM_EVALUATOR(leverage_convert)
    DEFINE_PM_EVALUATOR(dispute_oracle_respond)
    DEFINE_PM_EVALUATOR(unban)

}} // graphene::chain
