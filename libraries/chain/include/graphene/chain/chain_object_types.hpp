#pragma once

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

#include <chainbase/chainbase.hpp>

#include <graphene/protocol/types.hpp>
#include <graphene/protocol/authority.hpp>


namespace graphene { namespace chain {

        using namespace boost::multi_index;

        using boost::multi_index_container;

        using chainbase::object;
        using chainbase::object_id;
        using chainbase::allocator;

        using graphene::protocol::block_id_type;
        using graphene::protocol::transaction_id_type;
        using graphene::protocol::chain_id_type;
        using graphene::protocol::account_name_type;
        using graphene::protocol::share_type;

        typedef boost::interprocess::basic_string<char, std::char_traits<char>, allocator<char>> shared_string;

        inline std::string to_string(const shared_string &str) {
            return std::string(str.begin(), str.end());
        }

        inline void from_string(shared_string &out, const string &in) {
            out.assign(in.begin(), in.end());
        }

        typedef boost::interprocess::vector<char, allocator<char>> buffer_type;

        struct by_id;

        enum object_type {
            dynamic_global_property_object_type,
            account_object_type,
            account_authority_object_type,
            validator_object_type,
            transaction_object_type,
            block_summary_object_type,
            validator_schedule_object_type,
            content_object_type,
            content_type_object_type,
            content_vote_object_type,
            witness_vote_object_type,
            hardfork_property_object_type,
            withdraw_vesting_route_object_type,
            master_authority_history_object_type,
            account_recovery_request_object_type,
            change_recovery_account_request_object_type,
            escrow_object_type,
            block_stats_object_type,
            vesting_delegation_object_type,
            fix_vesting_delegation_object_type,
            vesting_delegation_expiration_object_type,
            account_metadata_object_type,
            proposal_object_type,
            required_approval_object_type,
            committee_request_object_type,
            committee_vote_object_type,
            invite_object_type,
            award_shares_expire_object_type,
            paid_subscription_object_type,
            paid_subscribe_object_type,
            witness_penalty_expire_object_type,
            validator_confirmation_object_type
        };

        class dynamic_global_property_object;
        class account_object;
        class account_authority_object;
        class validator_object;
        class transaction_object;
        class block_summary_object;
        class validator_schedule_object;
        class proposal_object;
        class required_approval_object;
        class content_object;
        class content_type_object;
        class content_vote_object;
        class witness_vote_object;
        class hardfork_property_object;
        class withdraw_vesting_route_object;
        class master_authority_history_object;
        class account_recovery_request_object;
        class change_recovery_account_request_object;
        class escrow_object;
        class block_stats_object;
        class vesting_delegation_object;
        class fix_vesting_delegation_object;
        class vesting_delegation_expiration_object;
        class account_metadata_object;
        class proposal_object;
        class committee_request_object;
        class committee_vote_object;
        class invite_object;
        class award_shares_expire_object;
        class paid_subscription_object;
        class paid_subscribe_object;
        class witness_penalty_expire_object;
        class validator_confirmation_object;

        typedef object_id<dynamic_global_property_object> dynamic_global_property_id_type;
        typedef object_id<account_object> account_id_type;
        typedef object_id<account_authority_object> account_authority_id_type;
        typedef object_id<validator_object> validator_id_type;
        typedef object_id<transaction_object> transaction_object_id_type;
        typedef object_id<block_summary_object> block_summary_id_type;
        typedef object_id<validator_schedule_object> validator_schedule_id_type;
        typedef object_id<content_object> content_id_type;
        typedef object_id<content_type_object> content_type_id_type;
        typedef object_id<content_vote_object> content_vote_id_type;
        typedef object_id<witness_vote_object> witness_vote_id_type;
        typedef object_id<hardfork_property_object> hardfork_property_id_type;
        typedef object_id<withdraw_vesting_route_object> withdraw_vesting_route_id_type;
        typedef object_id<master_authority_history_object> master_authority_history_id_type;
        typedef object_id<account_recovery_request_object> account_recovery_request_id_type;
        typedef object_id<change_recovery_account_request_object> change_recovery_account_request_id_type;
        typedef object_id<escrow_object> escrow_id_type;
        typedef object_id<block_stats_object> block_stats_id_type;
        typedef object_id<vesting_delegation_object> vesting_delegation_id_type;
        typedef object_id<fix_vesting_delegation_object> fix_vesting_delegation_id_type;
        typedef object_id<vesting_delegation_expiration_object> vesting_delegation_expiration_id_type;
        typedef object_id<account_metadata_object> account_metadata_id_type;
        typedef object_id<proposal_object> proposal_object_id_type;
        typedef object_id<required_approval_object> required_approval_object_id_type;
        typedef object_id<committee_request_object> committee_request_object_id_type;
        typedef object_id<committee_vote_object> committee_vote_object_id_type;
        typedef object_id<invite_object> invite_object_id_type;
        typedef object_id<award_shares_expire_object> award_shares_expire_object_id_type;
        typedef object_id<paid_subscription_object> paid_subscription_object_id_type;
        typedef object_id<paid_subscribe_object> paid_subscribe_object_id_type;
        typedef object_id<witness_penalty_expire_object> witness_penalty_expire_object_id_type;
        typedef object_id<validator_confirmation_object> validator_confirmation_object_id_type;

} } //graphene::chain

namespace fc {
    class variant;

    inline void to_variant(const graphene::chain::shared_string &s, variant &var) {
        var = std::string(graphene::chain::to_string(s));
    }

    inline void from_variant(const variant &var, graphene::chain::shared_string &s) {
        auto str = var.as_string();
        s.assign(str.begin(), str.end());
    }

    template<typename T>
    void to_variant(const chainbase::object_id<T> &var, variant &vo) {
        vo = var._id;
    }

    template<typename T>
    void from_variant(const variant &vo, chainbase::object_id<T> &var) {
        var._id = vo.as_int64();
    }

    namespace raw {
        template<typename Stream, typename T>
        inline void pack(Stream &s, const chainbase::object_id<T> &id) {
            s.write((const char *)&id._id, sizeof(id._id));
        }

        template<typename Stream, typename T>
        inline void unpack(Stream &s, chainbase::object_id<T> &id, uint32_t = 0) {
            s.read((char *)&id._id, sizeof(id._id));
        }
    }

    namespace raw {
        using chainbase::allocator;

        template<typename T>
        inline void pack(graphene::chain::buffer_type &raw, const T &v) {
            auto size = pack_size(v);
            raw.resize(size);
            datastream<char *> ds(raw.data(), size);
            pack(ds, v);
        }

        template<typename T>
        inline void unpack(const graphene::chain::buffer_type &raw, T &v, uint32_t depth = 0) {
            datastream<const char *> ds(raw.data(), raw.size());
            unpack(ds, v, depth);
        }

        template<typename T>
        inline T unpack(const graphene::chain::buffer_type &raw, uint32_t depth = 0) {
            T v;
            datastream<const char *> ds(raw.data(), raw.size());
            unpack(ds, v, depth);
            return v;
        }
    }
}

FC_REFLECT_ENUM(graphene::chain::object_type,
        (dynamic_global_property_object_type)
                (account_object_type)
                (account_authority_object_type)
                (validator_object_type)
                (transaction_object_type)
                (block_summary_object_type)
                (validator_schedule_object_type)
                (content_object_type)
                (content_type_object_type)
                (content_vote_object_type)
                (witness_vote_object_type)
                (hardfork_property_object_type)
                (withdraw_vesting_route_object_type)
                (master_authority_history_object_type)
                (account_recovery_request_object_type)
                (change_recovery_account_request_object_type)
                (escrow_object_type)
                (block_stats_object_type)
                (vesting_delegation_object_type)
                (fix_vesting_delegation_object_type)
                (vesting_delegation_expiration_object_type)
                (account_metadata_object_type)
                (proposal_object_type)
                (required_approval_object_type)
                (committee_request_object_type)
                (committee_vote_object_type)
                (invite_object_type)
                (award_shares_expire_object_type)
                (paid_subscription_object_type)
                (paid_subscribe_object_type)
                (witness_penalty_expire_object_type)
                (validator_confirmation_object_type)
)

FC_REFLECT_TYPENAME((graphene::chain::shared_string))
FC_REFLECT_TYPENAME((graphene::chain::buffer_type))
