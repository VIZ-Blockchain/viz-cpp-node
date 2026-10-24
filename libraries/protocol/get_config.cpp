#include <graphene/protocol/get_config.hpp>
#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>
#else
#include <graphene/protocol/config.hpp>
#endif
#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/version.hpp>

namespace graphene {
    namespace protocol {

        fc::variant_object get_config(uint32_t live_block_interval) {
            const uint32_t interval = live_block_interval ? live_block_interval : CHAIN_BLOCK_INTERVAL;
            FC_ASSERT(interval == CHAIN_BLOCK_INTERVAL || interval == CHAIN_BLOCK_INTERVAL_HF16);
            const uint32_t scale = CHAIN_BLOCK_INTERVAL / interval;
            fc::mutable_variant_object result;

            result["CHAIN_100_PERCENT"] = CHAIN_100_PERCENT;
            result["CHAIN_1_PERCENT"] = CHAIN_1_PERCENT;
            result["CHAIN_ADDRESS_PREFIX"] = CHAIN_ADDRESS_PREFIX;
            result["CHAIN_BANDWIDTH_AVERAGE_WINDOW_SECONDS"] = CHAIN_BANDWIDTH_AVERAGE_WINDOW_SECONDS;
            result["CHAIN_BANDWIDTH_PRECISION"] = CHAIN_BANDWIDTH_PRECISION;
            result["CONSENSUS_BANDWIDTH_RESERVE_PERCENT"] = CONSENSUS_BANDWIDTH_RESERVE_PERCENT;
            result["CONSENSUS_BANDWIDTH_RESERVE_BELOW"] = CONSENSUS_BANDWIDTH_RESERVE_BELOW;
            result["CHAIN_HARDFORK_VERSION"] = CHAIN_HARDFORK_VERSION;
            result["CHAIN_VERSION"] = CHAIN_VERSION;
            result["CHAIN_BLOCK_INTERVAL"] = interval;
            result["CHAIN_BLOCK_INTERVAL_HF16"] = CHAIN_BLOCK_INTERVAL_HF16;
            result["CHAIN_BLOCKS_PER_HOUR"] = uint32_t(3600 / interval);
            result["CHAIN_BLOCKS_PER_DAY"] = uint32_t(86400 / interval);
            result["CHAIN_BLOCKS_PER_YEAR"] = uint32_t(CHAIN_SECONDS_PER_YEAR / interval);
            result["CHAIN_ID"] = CHAIN_ID;
            result["CHAIN_HARDFORK_REQUIRED_VALIDATORS"] = CHAIN_HARDFORK_REQUIRED_VALIDATORS;
            result["CHAIN_INITIATOR_NAME"] = CHAIN_INITIATOR_NAME;
            result["CHAIN_INITIATOR_PUBLIC_KEY_STR"] = CHAIN_INITIATOR_PUBLIC_KEY_STR;
            result["CHAIN_INIT_SUPPLY"] = CHAIN_INIT_SUPPLY;
            result["CHAIN_COMMITTEE_ACCOUNT"] = CHAIN_COMMITTEE_ACCOUNT;
            result["CHAIN_COMMITTEE_PUBLIC_KEY_STR"] = CHAIN_COMMITTEE_PUBLIC_KEY_STR;
            result["CHAIN_IRREVERSIBLE_THRESHOLD"] = CHAIN_IRREVERSIBLE_THRESHOLD;
            result["CHAIN_IRREVERSIBLE_SUPPORT_MIN_RUN"] = CHAIN_IRREVERSIBLE_SUPPORT_MIN_RUN;
            result["CHAIN_MAX_ACCOUNT_NAME_LENGTH"] = CHAIN_MAX_ACCOUNT_NAME_LENGTH;
            result["CHAIN_MAX_ACCOUNT_VALIDATOR_VOTES"] = CHAIN_MAX_ACCOUNT_VALIDATOR_VOTES;
            result["CHAIN_BLOCK_SIZE"] = CHAIN_BLOCK_SIZE;
            result["CHAIN_MAX_COMMENT_DEPTH"] = CHAIN_MAX_COMMENT_DEPTH;
            result["CHAIN_MAX_MEMO_LENGTH"] = CHAIN_MAX_MEMO_LENGTH;
            result["CHAIN_MAX_VALIDATORS"] = CHAIN_MAX_VALIDATORS;
            result["CHAIN_MAX_PROXY_RECURSION_DEPTH"] = CHAIN_MAX_PROXY_RECURSION_DEPTH;
            result["CHAIN_MAX_RESERVE_RATIO"] = CHAIN_MAX_RESERVE_RATIO;
            result["CHAIN_MAX_SUPPORT_VALIDATORS"] = CHAIN_MAX_SUPPORT_VALIDATORS;
            result["CHAIN_MAX_SHARE_SUPPLY"] = CHAIN_MAX_SHARE_SUPPLY;
            result["CHAIN_MAX_SIG_CHECK_DEPTH"] = CHAIN_MAX_SIG_CHECK_DEPTH;
            result["CHAIN_MAX_TIME_UNTIL_EXPIRATION"] = CHAIN_MAX_TIME_UNTIL_EXPIRATION;
            result["CHAIN_MAX_TRANSACTION_SIZE"] = CHAIN_MAX_TRANSACTION_SIZE;
            result["CHAIN_MAX_UNDO_HISTORY"] = uint32_t(CHAIN_MAX_UNDO_HISTORY * scale);
            result["CHAIN_MAX_VOTE_CHANGES"] = CHAIN_MAX_VOTE_CHANGES;
            result["CHAIN_MAX_TOP_VALIDATORS"] = CHAIN_MAX_TOP_VALIDATORS;
            result["CHAIN_MAX_WITHDRAW_ROUTES"] = CHAIN_MAX_WITHDRAW_ROUTES;
            result["CHAIN_MAX_VALIDATOR_URL_LENGTH"] = CHAIN_MAX_VALIDATOR_URL_LENGTH;
            result["CHAIN_MIN_ACCOUNT_CREATION_FEE"] = CHAIN_MIN_ACCOUNT_CREATION_FEE;
            result["CHAIN_MIN_ACCOUNT_NAME_LENGTH"] = CHAIN_MIN_ACCOUNT_NAME_LENGTH;
            result["CHAIN_MIN_BLOCK_SIZE_LIMIT"] = CHAIN_MIN_BLOCK_SIZE_LIMIT;
            result["CHAIN_MAX_BLOCK_SIZE_LIMIT"] = CHAIN_MAX_BLOCK_SIZE_LIMIT;
            result["CHAIN_NULL_ACCOUNT"] = CHAIN_NULL_ACCOUNT;
            result["CHAIN_NUM_INITIATORS"] = CHAIN_NUM_INITIATORS;
            result["CHAIN_PROXY_TO_SELF_ACCOUNT"] = CHAIN_PROXY_TO_SELF_ACCOUNT;
            result["CHAIN_SECONDS_PER_YEAR"] = CHAIN_SECONDS_PER_YEAR;
            result["CHAIN_VESTING_WITHDRAW_INTERVALS"] = CHAIN_VESTING_WITHDRAW_INTERVALS;
            result["CHAIN_VESTING_WITHDRAW_INTERVAL_SECONDS"] = CHAIN_VESTING_WITHDRAW_INTERVAL_SECONDS;
            result["CHAIN_ENERGY_REGENERATION_SECONDS"] = CHAIN_ENERGY_REGENERATION_SECONDS;
            result["TOKEN_SYMBOL"] = TOKEN_SYMBOL;
            result["SHARES_SYMBOL"] = SHARES_SYMBOL;
            result["CHAIN_NAME"] = CHAIN_NAME;
            result["CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT"] = CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT;
            result["CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT"] = CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT;
            result["CHAIN_DIGITAL_ASSET_ISSUED_PER_BLOCK"] = CHAIN_DIGITAL_ASSET_ISSUED_PER_BLOCK;

            // Stored/voted properties are native block counts (owner Q3108=A), not
            // hidden 3-second units. Expose live defaults so clients need not re-vote
            // stale compile-time values after HF16.
            chain_properties_pm defaults;
            defaults.inflation_recalc_period *= scale;
            defaults.distribution_epoch_length *= scale;
            defaults.pm_batch_epoch_blocks *= scale;
            defaults.pm_reveal_window_blocks *= scale;
            result["CHAIN_DEFAULT_CHAIN_PROPERTIES"] = fc::variant(defaults);
            result["CHAIN_PROPERTY_BLOCK_UNITS"] = "native-blocks";
            result["CHAIN_MIN_DISTRIBUTION_EPOCH_LENGTH"] = uint32_t(CHAIN_MIN_DISTRIBUTION_EPOCH_LENGTH * scale);
            result["CHAIN_MAX_VALIDATOR_MISSED_BLOCKS"] = uint32_t(CHAIN_MAX_VALIDATOR_MISSED_BLOCKS * scale);
            result["CHAIN_EMERGENCY_MAX_VALIDATOR_MISSED_BLOCKS"] = uint32_t(CHAIN_EMERGENCY_MAX_VALIDATOR_MISSED_BLOCKS * scale);
            return result;
        }

    }
} // graphene::protocol
