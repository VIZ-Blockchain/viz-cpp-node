#pragma once

#define CHAIN_STARTUP_VERSION                 (version(1, 0, 0))
#define CHAIN_HARDFORK_STARTUP_VERSION        (hardfork_version(CHAIN_STARTUP_VERSION))
#define CHAIN_VERSION                         (version(3, 1, 0))
#define CHAIN_HARDFORK_VERSION                (hardfork_version(CHAIN_VERSION))

#define CHAIN_NAME                            "VIZ"
#define CHAIN_ID                              (fc::sha256::hash(CHAIN_NAME))
#define CHAIN_ADDRESS_PREFIX                  "VIZ"

#define SHARES_SYMBOL  (uint64_t(6) | (uint64_t('S') << 8) | (uint64_t('H') << 16) | (uint64_t('A') << 24) | (uint64_t('R') << 32) | (uint64_t('E') << 40) | (uint64_t('S') << 48))
#define TOKEN_SYMBOL  (uint64_t(3) | (uint64_t('V') << 8) | (uint64_t('I') << 16) | (uint64_t('Z') << 24))

#define CHAIN_MAX_PROPOSAL_LIFETIME_SEC       (60*60*24*7*4) /// 4 weeks
#define CHAIN_MAX_PROPOSAL_DEPTH              (2)

#define CHAIN_MIN_ACCOUNT_CREATION_FEE        1000

#define CHAIN_MASTER_AUTH_RECOVERY_PERIOD                 fc::days(30)
#define CHAIN_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD  fc::days(1)
#define CHAIN_MASTER_UPDATE_LIMIT                         fc::minutes(60)
#define CHAIN_ACCOUNT_ON_SALE_DELAY                       fc::days(7)
#define CHAIN_CLEAR_USED_INVITE_DELAY                     fc::days(1)
#define CHAIN_CLEAR_CLOSED_COMMITTEE_REQUEST_DELAY        fc::days(7)

#define CHAIN_BLOCK_INTERVAL                  3
#define CHAIN_BLOCK_WITNESS_REPEAT            1
#define CHAIN_BLOCKS_PER_YEAR                 (365*24*60*60/CHAIN_BLOCK_INTERVAL)
#define CHAIN_BLOCKS_PER_DAY                  (24*60*60/CHAIN_BLOCK_INTERVAL)
#define CHAIN_BLOCKS_PER_HOUR                 (60*60/CHAIN_BLOCK_INTERVAL)
#define CHAIN_MAX_WITNESS_MISSED_BLOCKS       200 // ~10 min after first missed block for top witness

#define CHAIN_INITIATOR_NAME                 "viz"
// Private key: 5JabcrvaLnBTCkCVFX5r4rmeGGfuJuVp4NAKRNLTey6pxhRQmf4
#define CHAIN_INITIATOR_PUBLIC_KEY_STR       "VIZ6MyX5QiXAXRZk7SYCiqpi6Mtm8UbHWDFSV8HPpt7FJyahCnc2T"
#define CHAIN_INITIATOR_PUBLIC_KEY            (graphene::protocol::public_key_type(CHAIN_INITIATOR_PUBLIC_KEY_STR))
#define CHAIN_NUM_INITIATORS                  0
#define CHAIN_MAX_TOP_WITNESSES               11
#define CHAIN_MAX_SUPPORT_WITNESSES           10
#define CHAIN_MAX_WITNESSES                   (CHAIN_MAX_TOP_WITNESSES+CHAIN_MAX_SUPPORT_WITNESSES) /// 21 is more than enough
#define CHAIN_HARDFORK_REQUIRED_WITNESSES     17 // 17 of the 20 dpos witnesses (19 elected and 1 virtual time) required for hardfork. This guarantees 75% participation on all subsequent rounds.
#define CHAIN_MAX_BLOCK_POST_VALIDATION_COUNT 20
#define CHAIN_MAX_TIME_UNTIL_EXPIRATION       (60*60) // seconds,  aka: 1 hour
#define CHAIN_MAX_PROXY_RECURSION_DEPTH       4
#define CHAIN_VESTING_WITHDRAW_INTERVALS      28
#define CHAIN_VESTING_WITHDRAW_INTERVAL_SECONDS (60*60*24) // 1 day per interval
#define CHAIN_MAX_WITHDRAW_ROUTES             10
#define CHAIN_ENERGY_REGENERATION_SECONDS     (5*60*60*24) // 5 days
#define CHAIN_MAX_VOTE_CHANGES                5
#define CHAIN_MAX_COMMENT_BENEFICIARIES       64
#define CHAIN_VOTE_ENERGY_RATE                1

#define CHAIN_MAX_ACCOUNT_WITNESS_VOTES_PRE_HF4 2
#define CHAIN_MAX_ACCOUNT_WITNESS_VOTES       100

#define CHAIN_100_PERCENT                     10000
#define CHAIN_1_PERCENT                       (CHAIN_100_PERCENT/100)
#define CHAIN_1_TENTH_PERCENT                 (CHAIN_100_PERCENT/1000)

#define CHAIN_FIXED_INFLATION                 (1000) //10%
#define CHAIN_VESTING_FUND_PERCENT            (10*CHAIN_1_PERCENT) //10% of inflation
#define CHAIN_REWARD_FUND_PERCENT             (35*CHAIN_1_PERCENT) //35% of inflation
#define CHAIN_COMMITTEE_FUND_PERCENT          (35*CHAIN_1_PERCENT) //35% of inflation
#define CHAIN_DIGITAL_ASSET_ISSUED_PER_BLOCK  int64_t(1000) //1.000 viz (new emission of digital asset per block)

#define CHAIN_CONSENSUS_INFLATION_WITNESS_PERCENT (20*CHAIN_1_PERCENT) //20% of inflation
#define CHAIN_CONSENSUS_INFLATION_RATIO       (50*CHAIN_1_PERCENT) //default: 50% of inflation minus witness percent (80% split between committee and reward fund)
#define CHAIN_CONSENSUS_INFLATION_RECALC_PERIOD (28*CHAIN_BLOCKS_PER_DAY) //default: period for recalc inflation medians to global properties

#define CHAIN_REWARD_FUND_CURATOR_PERCENT     (5*100/30*CHAIN_1_PERCENT) //5% of inflation from reward fund

#define CHAIN_BANDWIDTH_AVERAGE_WINDOW_SECONDS (60*60*24*7) ///< 1 week
#define CHAIN_BANDWIDTH_PRECISION             1000000ll ///< 1 million
#define CONSENSUS_BANDWIDTH_RESERVE_PERCENT   (CHAIN_100_PERCENT/10) // 10%
#define CONSENSUS_BANDWIDTH_RESERVE_BELOW     int64_t(500000000) // 500 SHARES AMOUNT
#define CHAIN_BANDWIDTH_RESERVE_ACTIVE_TIME   fc::days(30)
#define CHAIN_MAX_COMMENT_DEPTH               0xfff0 // 64k - 16

#define CHAIN_MAX_RESERVE_RATIO               (20000)

#define CHAIN_CREATE_ACCOUNT_DELEGATION_RATIO 10
#define CHAIN_CREATE_ACCOUNT_DELEGATION_TIME  (fc::days(30))
#define CHAIN_MIN_DELEGATION                  1

#define CHAIN_EQUIHASH_N                      140
#define CHAIN_EQUIHASH_K                      6

#define CHAIN_MIN_ACCOUNT_NAME_LENGTH         2
#define CHAIN_CREATE_MIN_ACCOUNT_NAME_LENGTH  3
#define CHAIN_MAX_ACCOUNT_NAME_LENGTH         32

#define CHAIN_MAX_URL_LENGTH                  256
#define CHAIN_MAX_MEMO_LENGTH                 2048
#define CHAIN_MAX_WITNESS_URL_LENGTH          2048

#define CHAIN_INIT_SUPPLY                     int64_t(50000000000)
#define CHAIN_MAX_SHARE_SUPPLY                int64_t(1000000000000000ll)
#define CHAIN_MAX_SIG_CHECK_DEPTH             2

#define CHAIN_SECONDS_PER_YEAR                (uint64_t(60*60*24*365ll))

#define CHAIN_MAX_TRANSACTION_SIZE            (1024*64)
#define CHAIN_MIN_BLOCK_SIZE_LIMIT            (CHAIN_MAX_TRANSACTION_SIZE)
#define CHAIN_MAX_BLOCK_SIZE_LIMIT            (2*1024*1024) // 2Mb p2p graphene limit : /libraries/net/include/graphene/net/config.hpp#L39
#define CHAIN_BLOCK_SIZE                      (CHAIN_MAX_BLOCK_SIZE_LIMIT*CHAIN_BLOCK_INTERVAL) // initial block size, preset to max block size limit

#define CHAIN_MAX_UNDO_HISTORY                10000

/// Emergency consensus mode: activates when no block has been produced for
/// this many seconds since the last irreversible block.
#define CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC    3600  // 1 hour

/// The witness account name that produces blocks during emergency mode
#define CHAIN_EMERGENCY_WITNESS_ACCOUNT          CHAIN_COMMITTEE_ACCOUNT  // "committee"

/// The public key used to sign blocks during emergency mode
#define CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY_STR   "VIZ75CRHVHPwYiUESy1bgN3KhVFbZCQQRA9jT6TnpzKAmpxMPD6Xv"
#define CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY       (graphene::protocol::public_key_type(CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY_STR))

/// Number of consecutive blocks produced by the emergency witness that
/// triggers automatic exit from emergency mode (witnesses have rejoined).
#define CHAIN_EMERGENCY_EXIT_NORMAL_BLOCKS       21  // 1 full round of 21 witnesses

#define CHAIN_IRREVERSIBLE_THRESHOLD          (75 * CHAIN_1_PERCENT)
/** Irreversibility only counts blocks produced if wit.current_run >= CHAIN_IRREVERSIBLE_SUPPORT_MIN_RUN */
#define CHAIN_IRREVERSIBLE_SUPPORT_MIN_RUN    2

#define CHAIN_ACCOUNT_AUCTION_MIN_STEP        (CHAIN_100_PERCENT/10) // 10%
#define CHAIN_ACCOUNT_AUCTION_EXTENSION_TIME  fc::minutes(5)

#define COMMITTEE_MIN_DURATION                (60*60*24*5)
#define COMMITTEE_MAX_DURATION                (60*60*24*30)
#define COMMITTEE_MAX_REQUIRED_AMOUNT         int64_t(CHAIN_INIT_SUPPLY/100)
#define COMMITTEE_REQUEST_PER_TIME            (60*60*24)
#define COMMITTEE_REQUEST_PROCESSING          (CHAIN_BLOCKS_PER_HOUR/6) //every 10 minutes

#define CONSENSUS_FLAG_ENERGY_ADDITIONAL_COST 0
#define CONSENSUS_VOTE_ACCOUNTING_MIN_RSHARES uint32_t(5000000) // default 0.5 SHARES equivalent
#define CONSENSUS_COMMITTEE_REQUEST_APPROVE_MIN_PERCENT 1000 // default: 10.00%
#define CONSENSUS_DATA_OPERATIONS_COST_ADDITIONAL_BANDWIDTH 0
#define CONSENSUS_WITNESS_MISS_PENALTY_PERCENT 100 // default: 1.00%
#define CONSENSUS_WITNESS_MISS_PENALTY_DURATION (CHAIN_BLOCKS_PER_DAY*CHAIN_BLOCK_INTERVAL) // default: 1 day

#define CONSENSUS_CREATE_INVITE_MIN_BALANCE   10000
#define CONSENSUS_COMMITTEE_CREATE_REQUEST_FEE 100000
#define CONSENSUS_WITNESS_DECLARATION_FEE     10000
#define CONSENSUS_CREATE_PAID_SUBSCRIPTION_FEE 100000
#define CONSENSUS_ACCOUNT_ON_SALE_FEE         10000
#define CONSENSUS_SUBACCOUNT_ON_SALE_FEE      100000

#define PAID_SUBSCRIPTION_MAX_LEVEL           1000
#define PAID_SUBSCRIPTION_MAX_PERIOD          1000 // days

#define CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT 5
#define CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT fc::milliseconds(200)

/**
 *  Reserved Account IDs with special meaning
 */
///@{
/// Represents the canonical account with NO authority (nobody can access funds in null account)
#define CHAIN_NULL_ACCOUNT                    "null"
/// Represents the canonical account with NO authority (nobody can access funds in committee account, all income transfers going to committee fund)
#define CHAIN_COMMITTEE_ACCOUNT               "committee"
/// Private key 5Hw9YPABaFxa2LooiANLrhUK5TPryy8f7v9Y1rk923PuYqbYdfC (committee viz sign)
#define CHAIN_COMMITTEE_PUBLIC_KEY_STR        "VIZ6Yt7d6LsngBoXQr47aLv97bJVs7jyr7esZTM4UUSpLUf3nbRKS"
#define CHAIN_COMMITTEE_PUBLIC_KEY            (graphene::protocol::public_key_type(CHAIN_COMMITTEE_PUBLIC_KEY_STR))
/// Represents the canonical account with NO authority (nobody can access funds in committee account, all income transfers going to new anonymous sub-account)
#define CHAIN_ANONYMOUS_ACCOUNT               "anonymous"
/// Represents the canonical account for specifying you will vote for directly (as opposed to a proxy)
#define CHAIN_PROXY_TO_SELF_ACCOUNT           ""
/// Represents the canonical account with ACTIVE authority for invite activity
#define CHAIN_INVITE_ACCOUNT                  "invite"
/// Private key 5KcfoRuDfkhrLCxVcE9x51J6KN9aM9fpb78tLrvvFckxVV6FyFW (invite viz active)
#define CHAIN_INVITE_PUBLIC_KEY              "VIZ6LiKWipnNoBSDafStzSUgo9ihGawo25WW9kMvfTpGMwKp2qfG9"

/// Represents the canonical root post parent account
#define CHAIN_ROOT_POST_PARENT                (account_name_type())
///@}

// Deprecated defines
#define CHAIN_CASHOUT_WINDOW_SECONDS          (60*60*24)  // 1 day
