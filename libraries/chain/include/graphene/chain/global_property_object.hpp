#pragma once

#include <fc/uint128_t.hpp>

#include <graphene/chain/chain_object_types.hpp>

#include <graphene/protocol/asset.hpp>

namespace graphene {
    namespace chain {

        using graphene::protocol::asset;
        using graphene::protocol::price;

        /**
         * @class dynamic_global_property_object
         * @brief Maintains global state information
         * @ingroup object
         * @ingroup implementation
         *
         * This is an implementation detail. The values here are calculated during normal chain operations and reflect the
         * current values of global blockchain properties.
         */
        class dynamic_global_property_object
                : public object<dynamic_global_property_object_type, dynamic_global_property_object> {
        public:
            template<typename Constructor, typename Allocator>
            dynamic_global_property_object(Constructor &&c, allocator <Allocator> a) {
                c(*this);
            }

            dynamic_global_property_object() {
            }

            id_type id;

            uint32_t head_block_number = 0;
            block_id_type head_block_id;
            time_point_sec genesis_time;
            time_point_sec time;
            account_name_type current_witness;

            asset committee_fund = asset(0, TOKEN_SYMBOL);
            uint32_t committee_requests = 0;
            asset current_supply = asset(0, TOKEN_SYMBOL);
            asset total_vesting_fund = asset(0, TOKEN_SYMBOL);
            asset total_vesting_shares = asset(0, SHARES_SYMBOL);
            asset total_reward_fund = asset(0, TOKEN_SYMBOL);
            fc::uint128_t total_reward_shares;

			uint32_t inflation_calc_block_num = 0;
			int16_t inflation_witness_percent = 0;
			int16_t inflation_ratio = 0;

            price get_vesting_share_price() const {
                if (total_vesting_fund.amount == 0 ||
                    total_vesting_shares.amount == 0) {
                        return price(asset(1000, TOKEN_SYMBOL), asset(1000000, SHARES_SYMBOL));
                }

                return price(total_vesting_shares, total_vesting_fund);
            }

            /**
             *  Average block size is updated every block to be:
             *
             *     average_block_size = (99 * average_block_size + new_block_size) / 100
             *
             *  This property is used to update the current_reserve_ratio to maintain approximately
             *  50% or less utilization of network capacity.
             */
            uint32_t average_block_size = 0;

            /**
             *  Maximum block size is decided by the set of active witnesses which change every round.
             *  Each witness posts what they think the maximum size should be as part of their witness
             *  properties, the median size is chosen to be the maximum block size for the round.
             *
             *  @note the minimum value for maximum_block_size is defined by the protocol to prevent the
             *  network from getting stuck by witnesses attempting to set this too low.
             */
            uint32_t maximum_block_size = 0;

            /**
             * The current absolute slot number.  Equal to the total
             * number of slots since genesis.  Also equal to the total
             * number of missed slots plus head_block_number.
             */
            uint64_t current_aslot = 0;

            /**
             * used to compute witness participation.
             */
            fc::uint128_t recent_slots_filled;
            uint8_t participation_count = 0; ///< Divide by 128 to compute participation percentage

            uint32_t last_irreversible_block_num = 0;
            block_id_type last_irreversible_block_id;

            uint16_t last_irreversible_block_ref_num = 0;
            uint32_t last_irreversible_block_ref_prefix = 0;

            /**
             * The maximum bandwidth the blockchain can support is:
             *
             *    max_bandwidth = maximum_block_size * CHAIN_BANDWIDTH_AVERAGE_WINDOW_SECONDS / CHAIN_BLOCK_INTERVAL
             *
             * The maximum virtual bandwidth is:
             *
             *    max_bandwidth * current_reserve_ratio
             */
            uint64_t max_virtual_bandwidth = 0;

            /**
             *   Any time average_block_size <= 50% maximum_block_size this value grows by 1 until it
             *   reaches CHAIN_MAX_RESERVE_RATIO.  Any time average_block_size is greater than
             *   50% it falls by 1%.  Upward adjustments happen once per round, downward adjustments
             *   happen every block.
             */
            uint64_t current_reserve_ratio = 1;

            /**
             * The number of votes regenerated per day.  Any user voting slower than this rate will be
             * "wasting" voting power through spillover; any user voting faster than this rate will have
             * their votes reduced.
             */
            uint32_t vote_regeneration_per_day = CHAIN_VOTE_ENERGY_RATE;

            /**
             * The number of accounts who can use bandwidth reserve assigned by witnesses consensus
             */
            uint32_t bandwidth_reserve_candidates = 1;

            /**
             * Emergency consensus mode state.
             * When active, the committee account produces blocks to maintain
             * chain continuity during prolonged network stalls.
             */
            bool emergency_consensus_active = false;

            /**
             * Block number at which emergency consensus mode was activated.
             * Zero if never activated. Used to compute duration and for exit condition.
             */
            uint32_t emergency_consensus_start_block = 0;
        };

        typedef multi_index_container <
        dynamic_global_property_object,
        indexed_by<
                ordered_unique < tag < by_id>,
        member<dynamic_global_property_object, dynamic_global_property_object::id_type, &dynamic_global_property_object::id>>
        >,
        allocator <dynamic_global_property_object>
        >
        dynamic_global_property_index;

    }
} // graphene::chain

FC_REFLECT((graphene::chain::dynamic_global_property_object),
        (id)
                (head_block_number)
                (head_block_id)
                (genesis_time)
                (time)
                (current_witness)
                (committee_fund)
                (committee_requests)
                (current_supply)
                (total_vesting_fund)
                (total_vesting_shares)
                (total_reward_fund)
                (total_reward_shares)
                (average_block_size)
                (maximum_block_size)
                (current_aslot)
                (recent_slots_filled)
                (participation_count)
                (last_irreversible_block_num)
                (last_irreversible_block_id)
                (last_irreversible_block_ref_num)
                (last_irreversible_block_ref_prefix)
                (max_virtual_bandwidth)
                (current_reserve_ratio)
                (vote_regeneration_per_day)
                (bandwidth_reserve_candidates)
                (inflation_calc_block_num)
                (inflation_witness_percent)
                (inflation_ratio)
                (emergency_consensus_active)
                (emergency_consensus_start_block)
)
CHAINBASE_SET_INDEX_TYPE(graphene::chain::dynamic_global_property_object, graphene::chain::dynamic_global_property_index)
