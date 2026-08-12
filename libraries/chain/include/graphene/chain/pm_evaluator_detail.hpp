// LOCAL/COMMITTABLE BUILD SPLIT: pm_evaluator.cpp grew past the point where a single
// translation unit compiles comfortably under tight RAM (it instantiates ~15 boost::multi_index
// containers). The internal helpers below are shared by the operation evaluators
// (pm_evaluator.cpp) and the per-block cron (pm_process_markets.cpp). They are declared here in
// namespace graphene::chain::pm_detail and DEFINED exactly once in pm_process_markets.cpp, so
// there is no ODR/duplicate-symbol issue. Pure code motion — no behavioural change.
#pragma once

#include <graphene/chain/database.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/protocol/config.hpp>

namespace graphene { namespace chain { namespace pm_detail {

using namespace ::graphene::protocol;

const chain_properties_pm& median(const database& db);
uint32_t compute_time_penalty(const pm_market_object& mkt, fc::time_point_sec risk_time,
                              uint32_t max_time_penalty);
void pm_oracle_inc_active(database& db, const account_name_type& oracle);
void pm_oracle_dec_active(database& db, const pm_market_object& mkt);
void pm_oracle_gauge_adj(database& db, const account_name_type& oracle,
                         uint32_t pm_oracle_object::* field, int delta);
void pm_oracle_dispute_left_open(database& db, const account_name_type& oracle,
                                 const pm_dispute_object& d);
int pm_rt_bucket(uint64_t rt);
void service_lazy_withdraw_queue(database& db);
void route_pool_lp_return(database& db, int64_t principal, int64_t yield);
void mark_alloc_settled(database& db, pm_market_id_type market);
void accrue_leverage_funding(database& db, const pm_leverage_position_object& pos,
                             uint32_t rate_ppm_per_day, fc::time_point_sec now);
void liquidate_position(database& db, const pm_leverage_position_object& pos, uint8_t reason);
void cascade_liquidate(database& db, pm_market_id_type market, int16_t side, uint8_t reason);
void force_close_positions(database& db, pm_market_id_type market, uint8_t reason = 2);
void purge_deferred_claims(database& db, pm_market_id_type market);
share_type recall_pool_liquidity(database& db, const pm_market_object& mkt, share_type amount);
void settle_liquidity(database& db, const pm_market_object& mkt, share_type bonus,
                      share_type uncovered = share_type(0));
void settle_market(database& db, const pm_market_object& mkt);
void gc_market(database& db, const pm_market_object& mkt);
void refund_all_bets(database& db, const pm_market_object& mkt);
void return_liquidity(database& db, const pm_market_object& mkt);
void maybe_allocate_lazy(database& db, const pm_market_object& mkt);
bool verify_commit(const pm_commit_object& commit, int8_t side, int16_t outcome_index,
                   share_type amount, share_type min_tokens, const std::string& salt);
const pm_market_object& get_market(const database& db, int64_t market_id);

} } } // namespace graphene::chain::pm_detail
