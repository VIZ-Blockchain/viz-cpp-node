#pragma once

#include <graphene/chain/chain_object_types.hpp>
#include <graphene/protocol/authority.hpp>

#include <boost/multi_index/composite_key.hpp>

// HF15 key history — a permanent record of every key an account ever stood behind.
//
// A DLT node starts from a snapshot and has no past blocks, so it cannot tell which key an account
// held at some point in the past: an account could sign something, change its keys and claim it
// was never theirs. Consensus therefore keeps one row per member of an authority that stopped
// being valid: account A held key B (or account C) in role R with weight D out of threshold E until
// block F at time G. The current state lives in account_authority_object / account_object; together
// they give the full timeline. "From when" is the valid_until of the previous row of the same role.
//
// An empty old authority produces one marker row (null key, empty auth_account, zero weight),
// preserving the change time for the cooldown without claiming a former signer. Rows are never
// removed. master_authority_history_object (recovery, 30 days) is a separate thing.

namespace graphene { namespace chain {

        enum key_history_role : uint8_t {
            key_role_master  = 0,
            key_role_active  = 1,
            key_role_regular = 2,
            key_role_memo    = 3
        };

        class key_history_object
                : public object<key_history_object_type, key_history_object> {
        public:
            key_history_object() = delete;
            template<typename Constructor, typename Allocator>
            key_history_object(Constructor&& c, allocator<Allocator>) { c(*this); }

            id_type           id;
            account_name_type account;             ///< whose key it was
            uint8_t           role = 0;            ///< key_history_role
            public_key_type   key;                 ///< the key; null key when the member is an account
            account_name_type auth_account;        ///< member account (account_auths); empty for a key
            uint16_t          weight = 0;          ///< member weight; 0 for memo
            uint32_t          weight_threshold = 0;///< role threshold; 0 for memo
            uint32_t          valid_until_block = 0; ///< last block in which it still held (inclusive)
            time_point_sec    valid_until_time;      ///< timestamp of that block
        };

        struct by_account_role;
        struct by_key;
        typedef multi_index_container<
            key_history_object,
            indexed_by<
                ordered_unique<tag<by_id>,
                    member<key_history_object, key_history_id_type, &key_history_object::id>>,
                ordered_unique<tag<by_account_role>,
                    composite_key<key_history_object,
                        member<key_history_object, account_name_type, &key_history_object::account>,
                        member<key_history_object, uint8_t, &key_history_object::role>,
                        member<key_history_object, key_history_id_type, &key_history_object::id>
                    >,
                    composite_key_compare<protocol::string_less, std::less<uint8_t>, std::less<key_history_id_type>>
                >,
                ordered_unique<tag<by_key>,
                    composite_key<key_history_object,
                        member<key_history_object, public_key_type, &key_history_object::key>,
                        member<key_history_object, key_history_id_type, &key_history_object::id>
                    >
                >
            >,
            allocator<key_history_object>
        > key_history_index;

        /// Keys of one account at a point in time — taken before a change, compared after it.
        struct account_keys_snapshot {
            protocol::authority master, active, regular;
            public_key_type     memo;
        };

} } // graphene::chain

FC_REFLECT((graphene::chain::key_history_object),
        (id)(account)(role)(key)(auth_account)(weight)(weight_threshold)(valid_until_block)(valid_until_time))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::key_history_object, graphene::chain::key_history_index)
