#include <graphene/plugins/snapshot/plugin.hpp>
#include <graphene/plugins/snapshot/snapshot_types.hpp>
#include <graphene/plugins/snapshot/snapshot_serializer.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/witness_objects.hpp>
#include <graphene/chain/content_object.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/committee_objects.hpp>
#include <graphene/chain/invite_objects.hpp>
#include <graphene/chain/paid_subscription_objects.hpp>
#include <graphene/chain/hardfork.hpp>

#include <fc/io/raw.hpp>
#include <fc/io/json.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/compress/zlib.hpp>

#include <fstream>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#endif

namespace graphene { namespace plugins { namespace snapshot {

using namespace graphene::chain;
using graphene::protocol::signed_block;

// ============================================================================
// Import helpers: read individual fields from variant into chainbase objects
// ============================================================================

namespace detail {

/// Copy a string from variant into a shared_string
inline void set_shared_string(shared_string& dst, const fc::variant& v) {
    auto s = v.as_string();
    dst.assign(s.begin(), s.end());
}

/// Copy bytes from variant into a buffer_type
inline void set_buffer(buffer_type& dst, const fc::variant& v) {
    auto vec = v.as<std::vector<char>>();
    dst.resize(vec.size());
    if (!vec.empty()) {
        memcpy(dst.data(), vec.data(), vec.size());
    }
}

/// Copy a shared_authority from variant
inline void set_shared_authority(shared_authority& dst, const fc::variant& v) {
    auto auth = v.as<graphene::protocol::authority>();
    dst.weight_threshold = auth.weight_threshold;
    // Clear and copy account_auths
    dst.account_auths.clear();
    for (auto& p : auth.account_auths) {
        dst.account_auths.insert(p);
    }
    // Clear and copy key_auths
    dst.key_auths.clear();
    for (auto& p : auth.key_auths) {
        dst.key_auths.insert(p);
    }
}

/// Generic import: convert object to variant, then apply fields via from_variant
/// This works for objects without shared_string/buffer_type members.
template<typename ObjectType, typename IndexType>
uint32_t import_objects_generic(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;

    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();

        // Set next_id to match the snapshot ID
        auto& mutable_idx = db.get_mutable_index<IndexType>();
        mutable_idx.set_next_id(typename ObjectType::id_type(id_val));

        db.create<ObjectType>([&](ObjectType& obj) {
            // obj.id is already set by chainbase to id_val
            // Copy remaining fields from variant
            // We use the reflector to iterate fields
            fc::from_variant(v, obj);
        });
        ++count;
    }
    return count;
}

// ============================================================================
// Specialized import functions for objects with shared_string/buffer members
// ============================================================================

inline uint32_t import_dynamic_global_properties(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    // There should be exactly one
    FC_ASSERT(arr.size() == 1, "Expected exactly 1 dynamic_global_property_object");
    const auto& v = arr[0];

    // This is special: the object already exists from init_genesis
    // So we modify it instead of creating it
    const auto& dgpo = db.get<dynamic_global_property_object>();
    db.modify(dgpo, [&](dynamic_global_property_object& obj) {
        obj.head_block_number = v["head_block_number"].as_uint64();
        obj.head_block_id = v["head_block_id"].as<block_id_type>();
        obj.genesis_time = v["genesis_time"].as<fc::time_point_sec>();
        obj.time = v["time"].as<fc::time_point_sec>();
        obj.current_witness = v["current_witness"].as<account_name_type>();
        obj.committee_fund = v["committee_fund"].as<asset>();
        obj.committee_requests = v["committee_requests"].as_uint64();
        obj.current_supply = v["current_supply"].as<asset>();
        obj.total_vesting_fund = v["total_vesting_fund"].as<asset>();
        obj.total_vesting_shares = v["total_vesting_shares"].as<asset>();
        obj.total_reward_fund = v["total_reward_fund"].as<asset>();
        obj.total_reward_shares = v["total_reward_shares"].as<fc::uint128_t>();
        obj.average_block_size = v["average_block_size"].as_uint64();
        obj.maximum_block_size = v["maximum_block_size"].as_uint64();
        obj.current_aslot = v["current_aslot"].as_uint64();
        obj.recent_slots_filled = v["recent_slots_filled"].as<fc::uint128_t>();
        obj.participation_count = static_cast<uint8_t>(v["participation_count"].as_uint64());
        obj.last_irreversible_block_num = v["last_irreversible_block_num"].as_uint64();
        obj.last_irreversible_block_id = v["last_irreversible_block_id"].as<block_id_type>();
        obj.last_irreversible_block_ref_num = static_cast<uint16_t>(v["last_irreversible_block_ref_num"].as_uint64());
        obj.last_irreversible_block_ref_prefix = v["last_irreversible_block_ref_prefix"].as_uint64();
        obj.max_virtual_bandwidth = v["max_virtual_bandwidth"].as_uint64();
        obj.current_reserve_ratio = v["current_reserve_ratio"].as_uint64();
        obj.vote_regeneration_per_day = v["vote_regeneration_per_day"].as_uint64();
        obj.bandwidth_reserve_candidates = v["bandwidth_reserve_candidates"].as_uint64();
        obj.inflation_calc_block_num = v["inflation_calc_block_num"].as_uint64();
        obj.inflation_witness_percent = static_cast<int16_t>(v["inflation_witness_percent"].as_int64());
        obj.inflation_ratio = static_cast<int16_t>(v["inflation_ratio"].as_int64());
    });
    return 1;
}

inline uint32_t import_accounts(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<account_index>();
        mutable_idx.set_next_id(account_id_type(id_val));

        db.create<account_object>([&](account_object& obj) {
            fc::from_variant(v, obj);
        });
        ++count;
    }
    return count;
}

inline uint32_t import_account_authorities(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<account_authority_index>();
        mutable_idx.set_next_id(account_authority_id_type(id_val));

        db.create<account_authority_object>([&](account_authority_object& obj) {
            obj.account = v["account"].as<account_name_type>();
            detail::set_shared_authority(obj.master, v["master"]);
            detail::set_shared_authority(obj.active, v["active"]);
            detail::set_shared_authority(obj.regular, v["regular"]);
            obj.last_master_update = v["last_master_update"].as<fc::time_point_sec>();
        });
        ++count;
    }
    return count;
}

inline uint32_t import_witnesses(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<witness_index>();
        mutable_idx.set_next_id(witness_id_type(id_val));

        db.create<witness_object>([&](witness_object& obj) {
            obj.owner = v["owner"].as<account_name_type>();
            obj.created = v["created"].as<fc::time_point_sec>();
            detail::set_shared_string(obj.url, v["url"]);
            obj.total_missed = v["total_missed"].as_uint64();
            obj.last_aslot = v["last_aslot"].as_uint64();
            obj.last_confirmed_block_num = v["last_confirmed_block_num"].as_uint64();
            obj.current_run = v["current_run"].as_uint64();
            obj.last_supported_block_num = v["last_supported_block_num"].as_uint64();
            obj.signing_key = v["signing_key"].as<graphene::protocol::public_key_type>();
            obj.props = v["props"].as<chain_properties>();
            obj.votes = v["votes"].as<share_type>();
            obj.penalty_percent = v["penalty_percent"].as_uint64();
            obj.counted_votes = v["counted_votes"].as<share_type>();
            obj.schedule = v["schedule"].as<witness_object::witness_schedule_type>();
            obj.virtual_last_update = v["virtual_last_update"].as<fc::uint128_t>();
            obj.virtual_position = v["virtual_position"].as<fc::uint128_t>();
            obj.virtual_scheduled_time = v["virtual_scheduled_time"].as<fc::uint128_t>();
            obj.last_work = v["last_work"].as<graphene::protocol::digest_type>();
            obj.running_version = v["running_version"].as<graphene::protocol::version>();
            obj.hardfork_version_vote = v["hardfork_version_vote"].as<graphene::protocol::hardfork_version>();
            obj.hardfork_time_vote = v["hardfork_time_vote"].as<fc::time_point_sec>();
        });
        ++count;
    }
    return count;
}

inline uint32_t import_contents(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<content_index>();
        mutable_idx.set_next_id(content_id_type(id_val));

        db.create<content_object>([&](content_object& obj) {
            obj.parent_author = v["parent_author"].as<account_name_type>();
            detail::set_shared_string(obj.parent_permlink, v["parent_permlink"]);
            obj.author = v["author"].as<account_name_type>();
            detail::set_shared_string(obj.permlink, v["permlink"]);
            obj.last_update = v["last_update"].as<fc::time_point_sec>();
            obj.created = v["created"].as<fc::time_point_sec>();
            obj.active = v["active"].as<fc::time_point_sec>();
            obj.last_payout = v["last_payout"].as<fc::time_point_sec>();
            obj.depth = static_cast<uint16_t>(v["depth"].as_uint64());
            obj.children = v["children"].as_uint64();
            obj.children_rshares = v["children_rshares"].as<fc::uint128_t>();
            obj.net_rshares = v["net_rshares"].as<share_type>();
            obj.abs_rshares = v["abs_rshares"].as<share_type>();
            obj.vote_rshares = v["vote_rshares"].as<share_type>();
            obj.cashout_time = v["cashout_time"].as<fc::time_point_sec>();
            obj.total_vote_weight = v["total_vote_weight"].as_uint64();
            obj.curation_percent = static_cast<int16_t>(v["curation_percent"].as_int64());
            obj.consensus_curation_percent = static_cast<int16_t>(v["consensus_curation_percent"].as_int64());
            obj.payout_value = v["payout_value"].as<asset>();
            obj.shares_payout_value = v["shares_payout_value"].as<asset>();
            obj.curator_payout_value = v["curator_payout_value"].as<asset>();
            obj.beneficiary_payout_value = v["beneficiary_payout_value"].as<asset>();
            obj.author_rewards = v["author_rewards"].as<share_type>();
            obj.net_votes = v["net_votes"].as_int64();
            obj.root_content = v["root_content"].as<content_id_type>();
            auto beneficiaries = v["beneficiaries"].as<std::vector<graphene::protocol::beneficiary_route_type>>();
            obj.beneficiaries.resize(beneficiaries.size());
            for (size_t i = 0; i < beneficiaries.size(); ++i) {
                obj.beneficiaries[i] = beneficiaries[i];
            }
        });
        ++count;
    }
    return count;
}

inline uint32_t import_content_types(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<content_type_index>();
        mutable_idx.set_next_id(content_type_id_type(id_val));

        db.create<content_type_object>([&](content_type_object& obj) {
            obj.content = v["content"].as<content_id_type>();
            detail::set_shared_string(obj.title, v["title"]);
            detail::set_shared_string(obj.body, v["body"]);
            detail::set_shared_string(obj.json_metadata, v["json_metadata"]);
        });
        ++count;
    }
    return count;
}

inline uint32_t import_content_votes(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<content_vote_index>();
        mutable_idx.set_next_id(content_vote_id_type(id_val));

        db.create<content_vote_object>([&](content_vote_object& obj) {
            obj.voter = v["voter"].as<account_id_type>();
            obj.content = v["content"].as<content_id_type>();
            obj.weight = v["weight"].as_uint64();
            obj.rshares = v["rshares"].as_int64();
            obj.vote_percent = static_cast<int16_t>(v["vote_percent"].as_int64());
            obj.last_update = v["last_update"].as<fc::time_point_sec>();
            obj.num_changes = static_cast<int8_t>(v["num_changes"].as_int64());
        });
        ++count;
    }
    return count;
}

inline uint32_t import_witness_votes(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<witness_vote_index>();
        mutable_idx.set_next_id(witness_vote_id_type(id_val));

        db.create<witness_vote_object>([&](witness_vote_object& obj) {
            obj.witness = v["witness"].as<witness_id_type>();
            obj.account = v["account"].as<account_id_type>();
        });
        ++count;
    }
    return count;
}

inline uint32_t import_witness_schedule(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    FC_ASSERT(arr.size() == 1, "Expected exactly 1 witness_schedule_object");
    const auto& v = arr[0];

    const auto& wso = db.get<witness_schedule_object>();
    db.modify(wso, [&](witness_schedule_object& obj) {
        fc::from_variant(v, obj);
    });
    return 1;
}

inline uint32_t import_hardfork_property(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    FC_ASSERT(arr.size() == 1, "Expected exactly 1 hardfork_property_object");
    const auto& v = arr[0];

    const auto& hpo = db.get<hardfork_property_object>();
    db.modify(hpo, [&](hardfork_property_object& obj) {
        auto hf_vec = v["processed_hardforks"].as<std::vector<fc::time_point_sec>>();
        obj.processed_hardforks.clear();
        for (const auto& t : hf_vec) {
            obj.processed_hardforks.push_back(t);
        }
        obj.last_hardfork = v["last_hardfork"].as_uint64();
        obj.current_hardfork_version = v["current_hardfork_version"].as<graphene::protocol::hardfork_version>();
        obj.next_hardfork = v["next_hardfork"].as<graphene::protocol::hardfork_version>();
        obj.next_hardfork_time = v["next_hardfork_time"].as<fc::time_point_sec>();
    });
    return 1;
}

inline uint32_t import_block_summaries(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        // block_summary objects are pre-created in init_genesis (65536 of them)
        // So we modify existing ones
        block_summary_id_type bsid(id_val);
        const auto* existing = db.find<block_summary_object>(bsid);
        if (existing) {
            db.modify(*existing, [&](block_summary_object& obj) {
                obj.block_id = v["block_id"].as<block_id_type>();
            });
        } else {
            auto& mutable_idx = db.get_mutable_index<block_summary_index>();
            mutable_idx.set_next_id(block_summary_id_type(id_val));
            db.create<block_summary_object>([&](block_summary_object& obj) {
                obj.block_id = v["block_id"].as<block_id_type>();
            });
        }
        ++count;
    }
    return count;
}

inline uint32_t import_transactions(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<transaction_index>();
        mutable_idx.set_next_id(transaction_object_id_type(id_val));

        db.create<transaction_object>([&](transaction_object& obj) {
            auto packed = v["packed_trx"].as<std::vector<char>>();
            obj.packed_trx.resize(packed.size());
            if (!packed.empty()) {
                memcpy(obj.packed_trx.data(), packed.data(), packed.size());
            }
            obj.trx_id = v["trx_id"].as<transaction_id_type>();
            obj.expiration = v["expiration"].as<fc::time_point_sec>();
        });
        ++count;
    }
    return count;
}

inline uint32_t import_block_post_validations(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<block_post_validation_index>();
        mutable_idx.set_next_id(block_post_validation_object_id_type(id_val));

        db.create<block_post_validation_object>([&](block_post_validation_object& obj) {
            fc::from_variant(v, obj);
        });
        ++count;
    }
    return count;
}

// Simple objects (no shared_string) - use generic import with fc::from_variant
template<typename ObjectType, typename IndexType>
uint32_t import_simple_objects(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<IndexType>();
        mutable_idx.set_next_id(typename ObjectType::id_type(id_val));

        db.create<ObjectType>([&](ObjectType& obj) {
            fc::from_variant(v, obj);
        });
        ++count;
    }
    return count;
}

// Objects with shared_string: committee_request, invite, paid_subscription, proposal
inline uint32_t import_committee_requests(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<committee_request_index>();
        mutable_idx.set_next_id(committee_request_object_id_type(id_val));

        db.create<committee_request_object>([&](committee_request_object& obj) {
            obj.request_id = v["request_id"].as_uint64();
            detail::set_shared_string(obj.url, v["url"]);
            obj.creator = v["creator"].as<account_name_type>();
            obj.worker = v["worker"].as<account_name_type>();
            obj.required_amount_min = v["required_amount_min"].as<asset>();
            obj.required_amount_max = v["required_amount_max"].as<asset>();
            obj.start_time = v["start_time"].as<fc::time_point_sec>();
            obj.duration = v["duration"].as_uint64();
            obj.end_time = v["end_time"].as<fc::time_point_sec>();
            obj.status = static_cast<uint16_t>(v["status"].as_uint64());
            obj.votes_count = v["votes_count"].as_uint64();
            obj.conclusion_time = v["conclusion_time"].as<fc::time_point_sec>();
            obj.conclusion_payout_amount = v["conclusion_payout_amount"].as<asset>();
            obj.payout_amount = v["payout_amount"].as<asset>();
            obj.remain_payout_amount = v["remain_payout_amount"].as<asset>();
            obj.last_payout_time = v["last_payout_time"].as<fc::time_point_sec>();
            obj.payout_time = v["payout_time"].as<fc::time_point_sec>();
        });
        ++count;
    }
    return count;
}

inline uint32_t import_invites(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<invite_index>();
        mutable_idx.set_next_id(invite_object_id_type(id_val));

        db.create<invite_object>([&](invite_object& obj) {
            obj.creator = v["creator"].as<account_name_type>();
            obj.receiver = v["receiver"].as<account_name_type>();
            obj.invite_key = v["invite_key"].as<graphene::protocol::public_key_type>();
            detail::set_shared_string(obj.invite_secret, v["invite_secret"]);
            obj.balance = v["balance"].as<asset>();
            obj.claimed_balance = v["claimed_balance"].as<asset>();
            obj.create_time = v["create_time"].as<fc::time_point_sec>();
            obj.claim_time = v["claim_time"].as<fc::time_point_sec>();
            obj.status = static_cast<uint16_t>(v["status"].as_uint64());
        });
        ++count;
    }
    return count;
}

inline uint32_t import_paid_subscriptions(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<paid_subscription_index>();
        mutable_idx.set_next_id(paid_subscription_object_id_type(id_val));

        db.create<paid_subscription_object>([&](paid_subscription_object& obj) {
            obj.creator = v["creator"].as<account_name_type>();
            detail::set_shared_string(obj.url, v["url"]);
            obj.levels = static_cast<uint16_t>(v["levels"].as_uint64());
            obj.amount = v["amount"].as<share_type>();
            obj.period = static_cast<uint16_t>(v["period"].as_uint64());
            obj.update_time = v["update_time"].as<fc::time_point_sec>();
        });
        ++count;
    }
    return count;
}

inline uint32_t import_proposals(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<proposal_index>();
        mutable_idx.set_next_id(proposal_object_id_type(id_val));

        db.create<proposal_object>([&](proposal_object& obj) {
            obj.author = v["author"].as<account_name_type>();
            detail::set_shared_string(obj.title, v["title"]);
            detail::set_shared_string(obj.memo, v["memo"]);
            obj.expiration_time = v["expiration_time"].as<fc::time_point_sec>();
            if (v.get_object().contains("review_period_time") && !v["review_period_time"].is_null()) {
                obj.review_period_time = v["review_period_time"].as<fc::time_point_sec>();
            }
            // proposed_operations is buffer_type
            auto ops_data = v["proposed_operations"].as<std::vector<char>>();
            obj.proposed_operations.resize(ops_data.size());
            if (!ops_data.empty()) {
                memcpy(obj.proposed_operations.data(), ops_data.data(), ops_data.size());
            }
            // Approval sets
            auto ra = v["required_active_approvals"].as<fc::flat_set<account_name_type>>();
            for (const auto& a : ra) obj.required_active_approvals.insert(a);
            auto aa = v["available_active_approvals"].as<fc::flat_set<account_name_type>>();
            for (const auto& a : aa) obj.available_active_approvals.insert(a);
            auto rm = v["required_master_approvals"].as<fc::flat_set<account_name_type>>();
            for (const auto& a : rm) obj.required_master_approvals.insert(a);
            auto am = v["available_master_approvals"].as<fc::flat_set<account_name_type>>();
            for (const auto& a : am) obj.available_master_approvals.insert(a);
            auto rr = v["required_regular_approvals"].as<fc::flat_set<account_name_type>>();
            for (const auto& a : rr) obj.required_regular_approvals.insert(a);
            auto ar = v["available_regular_approvals"].as<fc::flat_set<account_name_type>>();
            for (const auto& a : ar) obj.available_regular_approvals.insert(a);
            auto ak = v["available_key_approvals"].as<fc::flat_set<graphene::protocol::public_key_type>>();
            for (const auto& k : ak) obj.available_key_approvals.insert(k);
        });
        ++count;
    }
    return count;
}

inline uint32_t import_account_metadata(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<account_metadata_index>();
        mutable_idx.set_next_id(account_metadata_id_type(id_val));

        db.create<account_metadata_object>([&](account_metadata_object& obj) {
            obj.account = v["account"].as<account_name_type>();
            detail::set_shared_string(obj.json_metadata, v["json_metadata"]);
        });
        ++count;
    }
    return count;
}

inline uint32_t import_master_authority_history(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<master_authority_history_index>();
        mutable_idx.set_next_id(master_authority_history_id_type(id_val));

        db.create<master_authority_history_object>([&](master_authority_history_object& obj) {
            obj.account = v["account"].as<account_name_type>();
            detail::set_shared_authority(obj.previous_master_authority, v["previous_master_authority"]);
            obj.last_valid_time = v["last_valid_time"].as<fc::time_point_sec>();
        });
        ++count;
    }
    return count;
}

inline uint32_t import_account_recovery_requests(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<account_recovery_request_index>();
        mutable_idx.set_next_id(account_recovery_request_id_type(id_val));

        db.create<account_recovery_request_object>([&](account_recovery_request_object& obj) {
            obj.account_to_recover = v["account_to_recover"].as<account_name_type>();
            detail::set_shared_authority(obj.new_master_authority, v["new_master_authority"]);
            obj.expires = v["expires"].as<fc::time_point_sec>();
        });
        ++count;
    }
    return count;
}

} // namespace detail

// ============================================================================
// Plugin implementation
// ============================================================================

class snapshot_plugin::plugin_impl {
public:
    plugin_impl() : db(appbase::app().get_plugin<chain::plugin>().db()) {}
    ~plugin_impl() = default;

    graphene::chain::database& db;

    std::string snapshot_path;        // --snapshot: load from this path
    std::string create_snapshot_path; // --create-snapshot: create at this path
    uint32_t snapshot_at_block = 0;   // --snapshot-at-block: create when block N is reached
    uint32_t snapshot_every_n_blocks = 0; // --snapshot-every-n-blocks: periodic snapshot
    std::string snapshot_dir;         // --snapshot-dir: directory for auto-generated snapshots

    boost::signals2::scoped_connection applied_block_conn;

    void create_snapshot(const fc::path& output_path);
    void load_snapshot(const fc::path& input_path);

    void on_applied_block(const graphene::protocol::signed_block& b);

private:
    // Core snapshot serialization (caller must hold appropriate lock)
    void write_snapshot_to_file(const fc::path& output_path);

    // Export: convert all objects to variants and write to file
    fc::variant export_index_to_variant(const std::string& type_name);

    // Serialize all object sections to JSON
    fc::mutable_variant_object serialize_state();
};

fc::mutable_variant_object snapshot_plugin::plugin_impl::serialize_state() {
    fc::mutable_variant_object state;

    // Helper macro to export an index
    #define EXPORT_INDEX(index_type, obj_type, name) \
    { \
        fc::variants arr; \
        const auto& idx = db.get_index<index_type>().indices(); \
        for (auto itr = idx.begin(); itr != idx.end(); ++itr) { \
            fc::variant v; \
            fc::to_variant(*itr, v); \
            arr.push_back(std::move(v)); \
        } \
        state[name] = std::move(arr); \
        ilog("Exported ${n} ${type} objects", ("n", state[name].get_array().size())("type", name)); \
    }

    // CRITICAL objects
    EXPORT_INDEX(dynamic_global_property_index, dynamic_global_property_object, "dynamic_global_property")
    EXPORT_INDEX(witness_schedule_index, witness_schedule_object, "witness_schedule")
    EXPORT_INDEX(hardfork_property_index, hardfork_property_object, "hardfork_property")
    EXPORT_INDEX(account_index, account_object, "account")
    EXPORT_INDEX(account_authority_index, account_authority_object, "account_authority")
    EXPORT_INDEX(witness_index, witness_object, "witness")
    EXPORT_INDEX(witness_vote_index, witness_vote_object, "witness_vote")
    EXPORT_INDEX(block_summary_index, block_summary_object, "block_summary")
    EXPORT_INDEX(content_index, content_object, "content")
    EXPORT_INDEX(content_vote_index, content_vote_object, "content_vote")
    EXPORT_INDEX(block_post_validation_index, block_post_validation_object, "block_post_validation")

    // IMPORTANT objects
    EXPORT_INDEX(transaction_index, transaction_object, "transaction")
    EXPORT_INDEX(vesting_delegation_index, vesting_delegation_object, "vesting_delegation")
    EXPORT_INDEX(vesting_delegation_expiration_index, vesting_delegation_expiration_object, "vesting_delegation_expiration")
    EXPORT_INDEX(fix_vesting_delegation_index, fix_vesting_delegation_object, "fix_vesting_delegation")
    EXPORT_INDEX(withdraw_vesting_route_index, withdraw_vesting_route_object, "withdraw_vesting_route")
    EXPORT_INDEX(escrow_index, escrow_object, "escrow")
    // proposal_object has bip::flat_set with shared allocators — custom export
    {
        fc::variants arr;
        const auto& idx = db.get_index<proposal_index>().indices();
        for (auto itr = idx.begin(); itr != idx.end(); ++itr) {
            fc::mutable_variant_object o;
            o["id"] = static_cast<int64_t>(itr->id._id);
            o["author"] = itr->author;
            o["title"] = std::string(itr->title.begin(), itr->title.end());
            o["memo"] = std::string(itr->memo.begin(), itr->memo.end());
            o["expiration_time"] = itr->expiration_time;
            if (itr->review_period_time.valid()) {
                o["review_period_time"] = *(itr->review_period_time);
            } else {
                o["review_period_time"] = fc::variant();
            }
            // buffer_type → vector<char>
            std::vector<char> ops_data(itr->proposed_operations.begin(), itr->proposed_operations.end());
            o["proposed_operations"] = ops_data;
            // bip::flat_set → fc::flat_set
            fc::flat_set<account_name_type> ra(itr->required_active_approvals.begin(), itr->required_active_approvals.end());
            o["required_active_approvals"] = ra;
            fc::flat_set<account_name_type> aa(itr->available_active_approvals.begin(), itr->available_active_approvals.end());
            o["available_active_approvals"] = aa;
            fc::flat_set<account_name_type> rm(itr->required_master_approvals.begin(), itr->required_master_approvals.end());
            o["required_master_approvals"] = rm;
            fc::flat_set<account_name_type> am(itr->available_master_approvals.begin(), itr->available_master_approvals.end());
            o["available_master_approvals"] = am;
            fc::flat_set<account_name_type> rr(itr->required_regular_approvals.begin(), itr->required_regular_approvals.end());
            o["required_regular_approvals"] = rr;
            fc::flat_set<account_name_type> ar(itr->available_regular_approvals.begin(), itr->available_regular_approvals.end());
            o["available_regular_approvals"] = ar;
            fc::flat_set<graphene::protocol::public_key_type> ak(itr->available_key_approvals.begin(), itr->available_key_approvals.end());
            o["available_key_approvals"] = ak;
            arr.push_back(fc::variant(o));
        }
        state["proposal"] = std::move(arr);
    }
    EXPORT_INDEX(required_approval_index, required_approval_object, "required_approval")
    EXPORT_INDEX(committee_request_index, committee_request_object, "committee_request")
    EXPORT_INDEX(committee_vote_index, committee_vote_object, "committee_vote")
    EXPORT_INDEX(invite_index, invite_object, "invite")
    EXPORT_INDEX(award_shares_expire_index, award_shares_expire_object, "award_shares_expire")
    EXPORT_INDEX(paid_subscription_index, paid_subscription_object, "paid_subscription")
    EXPORT_INDEX(paid_subscribe_index, paid_subscribe_object, "paid_subscribe")
    EXPORT_INDEX(witness_penalty_expire_index, witness_penalty_expire_object, "witness_penalty_expire")

    // OPTIONAL objects
    EXPORT_INDEX(content_type_index, content_type_object, "content_type")
    EXPORT_INDEX(account_metadata_index, account_metadata_object, "account_metadata")
    EXPORT_INDEX(master_authority_history_index, master_authority_history_object, "master_authority_history")
    EXPORT_INDEX(account_recovery_request_index, account_recovery_request_object, "account_recovery_request")
    EXPORT_INDEX(change_recovery_account_request_index, change_recovery_account_request_object, "change_recovery_account_request")

    #undef EXPORT_INDEX

    return state;
}

void snapshot_plugin::plugin_impl::create_snapshot(const fc::path& output_path) {
    ilog("Creating snapshot at ${path}...", ("path", output_path.string()));

    db.with_strong_read_lock([&]() {
        write_snapshot_to_file(output_path);
    });
}

void snapshot_plugin::plugin_impl::write_snapshot_to_file(const fc::path& output_path) {
    auto start = fc::time_point::now();

    // Get LIB info
    uint32_t lib = db.last_non_undoable_block_num();
    uint32_t head = db.head_block_num();

    // Build header
    snapshot_header header;
    header.version = SNAPSHOT_FORMAT_VERSION;
    header.chain_id = db.get_chain_id();
    header.snapshot_block_num = head;
    header.snapshot_block_id = db.head_block_id();
    header.snapshot_block_time = db.head_block_time();
    header.last_irreversible_block_num = lib;
    header.snapshot_creation_time = fc::time_point_sec(fc::time_point::now());

    ilog("Snapshot at block ${b} (LIB: ${lib})", ("b", head)("lib", lib));

    // Serialize all state
    auto state = serialize_state();

    // Build object counts
    for (auto itr = state.begin(); itr != state.end(); ++itr) {
        if (itr->value().is_array()) {
            header.object_counts[itr->key()] = static_cast<uint32_t>(itr->value().get_array().size());
        }
    }

    // Add the head block for fork_db seeding
    auto head_block = db.fetch_block_by_number(head);
    if (head_block.valid()) {
        fc::variant block_var;
        fc::to_variant(*head_block, block_var);
        state["fork_db_head_block"] = std::move(block_var);
    }

    // Compute checksum of serialized state
    std::string state_json = fc::json::to_string(state);
    header.payload_checksum = fc::sha256::hash(state_json.data(), state_json.size());

    // Build final snapshot object
    fc::mutable_variant_object snapshot;
    fc::variant header_var;
    fc::to_variant(header, header_var);
    snapshot["header"] = std::move(header_var);
    snapshot["state"] = std::move(state);

    // Write to file with zlib compression
    std::string snapshot_json = fc::json::to_string(fc::variant(snapshot));

    auto compress_start = fc::time_point::now();
    std::string compressed = fc::zlib_compress(snapshot_json);
    auto compress_elapsed = double((fc::time_point::now() - compress_start).count()) / 1000000.0;

    ilog("Compressed snapshot: ${orig} -> ${comp} bytes (${ratio}%, ${time} sec)",
        ("orig", snapshot_json.size())
        ("comp", compressed.size())
        ("ratio", 100 - (compressed.size() * 100 / snapshot_json.size()))
        ("time", compress_elapsed));

    std::ofstream out(output_path.string(), std::ios::binary);
    FC_ASSERT(out.is_open(), "Failed to open snapshot file for writing: ${p}", ("p", output_path.string()));
    out.write(compressed.data(), compressed.size());
    out.flush();
    out.close();

#ifndef _WIN32
    // fsync to ensure data is persisted to disk before we report success
    int fd = ::open(output_path.string().c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fdatasync(fd);
        ::close(fd);
    }
#endif

    auto end = fc::time_point::now();
    ilog("Snapshot created successfully: ${path} (${size} bytes compressed, ${time} sec)",
        ("path", output_path.string())
        ("size", compressed.size())
        ("time", double((end - start).count()) / 1000000.0));
}

void snapshot_plugin::plugin_impl::load_snapshot(const fc::path& input_path) {
    ilog("Loading snapshot from ${path}...", ("path", input_path.string()));

    auto start = fc::time_point::now();

    // Read file
    std::ifstream in(input_path.string(), std::ios::binary);
    FC_ASSERT(in.is_open(), "Failed to open snapshot file: ${p}", ("p", input_path.string()));

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    ilog("Read ${size} bytes from snapshot file", ("size", content.size()));

    // Decompress zlib
    auto decompress_start = fc::time_point::now();
    std::string json_content = fc::zlib_decompress(content);
    auto decompress_elapsed = double((fc::time_point::now() - decompress_start).count()) / 1000000.0;

    ilog("Decompressed snapshot: ${comp} -> ${orig} bytes (${time} sec)",
        ("comp", content.size())
        ("orig", json_content.size())
        ("time", decompress_elapsed));

    // Parse JSON
    fc::variant snapshot_var = fc::json::from_string(json_content);
    FC_ASSERT(snapshot_var.is_object(), "Snapshot file is not a valid JSON object");

    auto snapshot = snapshot_var.get_object();

    // Parse and validate header
    snapshot_header header;
    fc::from_variant(snapshot["header"], header);

    FC_ASSERT(header.version == SNAPSHOT_FORMAT_VERSION,
        "Unsupported snapshot version: ${v} (expected ${e})",
        ("v", header.version)("e", SNAPSHOT_FORMAT_VERSION));

    FC_ASSERT(header.chain_id == db.get_chain_id(),
        "Chain ID mismatch: snapshot=${s}, node=${n}",
        ("s", std::string(header.chain_id))("n", std::string(db.get_chain_id())));

    ilog("Snapshot header validated: block=${b}, LIB=${lib}, chain_id=${cid}",
        ("b", header.snapshot_block_num)("lib", header.last_irreversible_block_num)
        ("cid", std::string(header.chain_id)));

    // Validate checksum
    std::string state_json = fc::json::to_string(snapshot["state"]);
    auto computed_checksum = fc::sha256::hash(state_json.data(), state_json.size());
    FC_ASSERT(computed_checksum == header.payload_checksum,
        "Snapshot checksum mismatch: computed=${c}, header=${h}",
        ("c", std::string(computed_checksum))("h", std::string(header.payload_checksum)));
    ilog("Snapshot checksum verified");

    const auto& state = snapshot["state"].get_object();

    // Import objects in dependency order
    db.with_strong_write_lock([&]() {
        // CRITICAL - singleton objects (modify existing)
        if (state.contains("dynamic_global_property")) {
            detail::import_dynamic_global_properties(db, state["dynamic_global_property"].get_array());
            ilog("Imported dynamic_global_property");
        }
        if (state.contains("witness_schedule")) {
            detail::import_witness_schedule(db, state["witness_schedule"].get_array());
            ilog("Imported witness_schedule");
        }
        if (state.contains("hardfork_property")) {
            detail::import_hardfork_property(db, state["hardfork_property"].get_array());
            ilog("Imported hardfork_property");
        }

        // CRITICAL - multi-instance objects
        if (state.contains("account")) {
            auto n = detail::import_accounts(db, state["account"].get_array());
            ilog("Imported ${n} accounts", ("n", n));
        }
        if (state.contains("account_authority")) {
            auto n = detail::import_account_authorities(db, state["account_authority"].get_array());
            ilog("Imported ${n} account authorities", ("n", n));
        }
        if (state.contains("witness")) {
            auto n = detail::import_witnesses(db, state["witness"].get_array());
            ilog("Imported ${n} witnesses", ("n", n));
        }
        if (state.contains("witness_vote")) {
            auto n = detail::import_witness_votes(db, state["witness_vote"].get_array());
            ilog("Imported ${n} witness votes", ("n", n));
        }
        if (state.contains("block_summary")) {
            auto n = detail::import_block_summaries(db, state["block_summary"].get_array());
            ilog("Imported ${n} block summaries", ("n", n));
        }
        if (state.contains("content")) {
            auto n = detail::import_contents(db, state["content"].get_array());
            ilog("Imported ${n} contents", ("n", n));
        }
        if (state.contains("content_vote")) {
            auto n = detail::import_content_votes(db, state["content_vote"].get_array());
            ilog("Imported ${n} content votes", ("n", n));
        }
        if (state.contains("block_post_validation")) {
            auto n = detail::import_block_post_validations(db, state["block_post_validation"].get_array());
            ilog("Imported ${n} block post validations", ("n", n));
        }

        // IMPORTANT objects
        if (state.contains("transaction")) {
            auto n = detail::import_transactions(db, state["transaction"].get_array());
            ilog("Imported ${n} transactions", ("n", n));
        }
        if (state.contains("vesting_delegation")) {
            auto n = detail::import_simple_objects<vesting_delegation_object, vesting_delegation_index>(db, state["vesting_delegation"].get_array());
            ilog("Imported ${n} vesting delegations", ("n", n));
        }
        if (state.contains("vesting_delegation_expiration")) {
            auto n = detail::import_simple_objects<vesting_delegation_expiration_object, vesting_delegation_expiration_index>(db, state["vesting_delegation_expiration"].get_array());
            ilog("Imported ${n} vesting delegation expirations", ("n", n));
        }
        if (state.contains("fix_vesting_delegation")) {
            auto n = detail::import_simple_objects<fix_vesting_delegation_object, fix_vesting_delegation_index>(db, state["fix_vesting_delegation"].get_array());
            ilog("Imported ${n} fix vesting delegations", ("n", n));
        }
        if (state.contains("withdraw_vesting_route")) {
            auto n = detail::import_simple_objects<withdraw_vesting_route_object, withdraw_vesting_route_index>(db, state["withdraw_vesting_route"].get_array());
            ilog("Imported ${n} withdraw vesting routes", ("n", n));
        }
        if (state.contains("escrow")) {
            auto n = detail::import_simple_objects<escrow_object, escrow_index>(db, state["escrow"].get_array());
            ilog("Imported ${n} escrows", ("n", n));
        }
        if (state.contains("proposal")) {
            auto n = detail::import_proposals(db, state["proposal"].get_array());
            ilog("Imported ${n} proposals", ("n", n));
        }
        if (state.contains("required_approval")) {
            auto n = detail::import_simple_objects<required_approval_object, required_approval_index>(db, state["required_approval"].get_array());
            ilog("Imported ${n} required approvals", ("n", n));
        }
        if (state.contains("committee_request")) {
            auto n = detail::import_committee_requests(db, state["committee_request"].get_array());
            ilog("Imported ${n} committee requests", ("n", n));
        }
        if (state.contains("committee_vote")) {
            auto n = detail::import_simple_objects<committee_vote_object, committee_vote_index>(db, state["committee_vote"].get_array());
            ilog("Imported ${n} committee votes", ("n", n));
        }
        if (state.contains("invite")) {
            auto n = detail::import_invites(db, state["invite"].get_array());
            ilog("Imported ${n} invites", ("n", n));
        }
        if (state.contains("award_shares_expire")) {
            auto n = detail::import_simple_objects<award_shares_expire_object, award_shares_expire_index>(db, state["award_shares_expire"].get_array());
            ilog("Imported ${n} award shares expire objects", ("n", n));
        }
        if (state.contains("paid_subscription")) {
            auto n = detail::import_paid_subscriptions(db, state["paid_subscription"].get_array());
            ilog("Imported ${n} paid subscriptions", ("n", n));
        }
        if (state.contains("paid_subscribe")) {
            auto n = detail::import_simple_objects<paid_subscribe_object, paid_subscribe_index>(db, state["paid_subscribe"].get_array());
            ilog("Imported ${n} paid subscribes", ("n", n));
        }
        if (state.contains("witness_penalty_expire")) {
            auto n = detail::import_simple_objects<witness_penalty_expire_object, witness_penalty_expire_index>(db, state["witness_penalty_expire"].get_array());
            ilog("Imported ${n} witness penalty expire objects", ("n", n));
        }

        // OPTIONAL objects
        if (state.contains("content_type")) {
            auto n = detail::import_content_types(db, state["content_type"].get_array());
            ilog("Imported ${n} content types", ("n", n));
        }
        if (state.contains("account_metadata")) {
            auto n = detail::import_account_metadata(db, state["account_metadata"].get_array());
            ilog("Imported ${n} account metadata objects", ("n", n));
        }
        if (state.contains("master_authority_history")) {
            auto n = detail::import_master_authority_history(db, state["master_authority_history"].get_array());
            ilog("Imported ${n} master authority history objects", ("n", n));
        }
        if (state.contains("account_recovery_request")) {
            auto n = detail::import_account_recovery_requests(db, state["account_recovery_request"].get_array());
            ilog("Imported ${n} account recovery requests", ("n", n));
        }
        if (state.contains("change_recovery_account_request")) {
            auto n = detail::import_simple_objects<change_recovery_account_request_object, change_recovery_account_request_index>(db, state["change_recovery_account_request"].get_array());
            ilog("Imported ${n} change recovery account requests", ("n", n));
        }

        // Set the chainbase revision to match the snapshot head block
        db.set_revision(header.snapshot_block_num);

        ilog("All objects imported successfully");
    });

    // Seed fork_db with head block from snapshot
    if (state.contains("fork_db_head_block")) {
        auto head_block = state["fork_db_head_block"].as<signed_block>();
        db.get_fork_db().start_block(head_block);
        ilog("Fork database seeded with head block ${n}", ("n", header.snapshot_block_num));
    }

    auto end = fc::time_point::now();
    ilog("Snapshot loaded successfully in ${t} sec", ("t", double((end - start).count()) / 1000000.0));
}

void snapshot_plugin::plugin_impl::on_applied_block(const graphene::protocol::signed_block& b) {
    uint32_t block_num = b.block_num();

    // Check --snapshot-at-block: one-time snapshot at exact block
    if (snapshot_at_block > 0 && block_num == snapshot_at_block) {
        fc::path output;
        if (!snapshot_dir.empty()) {
            output = fc::path(snapshot_dir) / ("snapshot-block-" + std::to_string(block_num) + ".json");
        } else if (!create_snapshot_path.empty()) {
            output = fc::path(create_snapshot_path);
        } else {
            output = fc::path("snapshot-block-" + std::to_string(block_num) + ".json");
        }
        ilog("Reached snapshot-at-block ${b}, creating snapshot: ${p}", ("b", block_num)("p", output.string()));
        try {
            // Called from applied_block signal which is already under write lock.
            // Use write_snapshot_to_file directly (no additional lock needed).
            write_snapshot_to_file(output);
        } catch (const fc::exception& e) {
            elog("Failed to create snapshot at block ${b}: ${e}", ("b", block_num)("e", e.to_detail_string()));
        }
    }

    // Check --snapshot-every-n-blocks: periodic snapshots
    if (snapshot_every_n_blocks > 0 && block_num % snapshot_every_n_blocks == 0) {
        std::string dir = snapshot_dir.empty() ? "." : snapshot_dir;
        fc::path output = fc::path(dir) / ("snapshot-block-" + std::to_string(block_num) + ".json");
        ilog("Periodic snapshot at block ${b}: ${p}", ("b", block_num)("p", output.string()));
        try {
            // Called from applied_block signal which is already under write lock.
            // Use write_snapshot_to_file directly (no additional lock needed).
            write_snapshot_to_file(output);
        } catch (const fc::exception& e) {
            elog("Failed to create periodic snapshot at block ${b}: ${e}", ("b", block_num)("e", e.to_detail_string()));
        }
    }
}

// ============================================================================
// Plugin interface
// ============================================================================

snapshot_plugin::snapshot_plugin() = default;
snapshot_plugin::~snapshot_plugin() = default;

void snapshot_plugin::set_program_options(
    bpo::options_description& cli,
    bpo::options_description& cfg
) {
    cfg.add_options()
        ("snapshot-at-block", bpo::value<uint32_t>()->default_value(0),
            "Create snapshot when the specified block number is reached")
        ("snapshot-every-n-blocks", bpo::value<uint32_t>()->default_value(0),
            "Automatically create a snapshot every N blocks (0 = disabled)")
        ("snapshot-dir", bpo::value<std::string>()->default_value(""),
            "Directory for auto-generated snapshot files")
    ;
    cli.add_options()
        ("snapshot", bpo::value<std::string>(),
            "Load state from snapshot file instead of replaying blockchain")
        ("create-snapshot", bpo::value<std::string>(),
            "Create a snapshot file at the specified path and exit")
    ;
}

void snapshot_plugin::plugin_initialize(const bpo::variables_map& options) {
    ilog("snapshot plugin: initializing");

    my = std::make_unique<plugin_impl>();

    if (options.count("snapshot")) {
        my->snapshot_path = options.at("snapshot").as<std::string>();
        ilog("Snapshot load path: ${p}", ("p", my->snapshot_path));
    }

    if (options.count("create-snapshot")) {
        my->create_snapshot_path = options.at("create-snapshot").as<std::string>();
        ilog("Snapshot creation path: ${p}", ("p", my->create_snapshot_path));
    }

    if (options.count("snapshot-at-block")) {
        my->snapshot_at_block = options.at("snapshot-at-block").as<uint32_t>();
    }

    if (options.count("snapshot-every-n-blocks")) {
        my->snapshot_every_n_blocks = options.at("snapshot-every-n-blocks").as<uint32_t>();
        if (my->snapshot_every_n_blocks > 0) {
            ilog("Periodic snapshots enabled: every ${n} blocks", ("n", my->snapshot_every_n_blocks));
        }
    }

    if (options.count("snapshot-dir")) {
        my->snapshot_dir = options.at("snapshot-dir").as<std::string>();
        if (!my->snapshot_dir.empty()) {
            ilog("Snapshot directory: ${d}", ("d", my->snapshot_dir));
        }
    }

    // Register snapshot loading callback on the chain plugin.
    // This ensures the snapshot is loaded DURING chain plugin startup,
    // BEFORE on_sync() fires and P2P starts syncing.
    if (!my->snapshot_path.empty()) {
        auto& chain_plug = appbase::app().get_plugin<chain::plugin>();
        chain_plug.snapshot_load_callback = [this]() {
            ilog("Loading state from snapshot: ${p}", ("p", my->snapshot_path));
            auto start = fc::time_point::now();
            my->load_snapshot(fc::path(my->snapshot_path));
            my->db.initialize_hardforks();
            auto elapsed = (fc::time_point::now() - start).count() / 1000000.0;
            ilog("Snapshot loaded successfully at block ${n}, elapsed time ${t} sec",
                ("n", my->db.head_block_num())("t", elapsed));
        };
    }

    // Register snapshot creation callback on the chain plugin.
    // This ensures the snapshot is created AFTER full DB load (including replay),
    // but BEFORE on_sync() — so P2P/witness never start.
    if (!my->create_snapshot_path.empty()) {
        auto& chain_plug = appbase::app().get_plugin<chain::plugin>();
        chain_plug.snapshot_create_callback = [this]() {
            ilog("Creating snapshot at: ${p}", ("p", my->create_snapshot_path));
            auto start = fc::time_point::now();
            my->create_snapshot(fc::path(my->create_snapshot_path));
            auto elapsed = (fc::time_point::now() - start).count() / 1000000.0;
            ilog("Snapshot created successfully, elapsed time ${t} sec. Shutting down...", ("t", elapsed));
            appbase::app().quit();
        };
    }
}

void snapshot_plugin::plugin_startup() {
    ilog("snapshot plugin: starting");

    // Note: --snapshot loading is handled via snapshot_load_callback registered
    // in plugin_initialize(). It runs during chain plugin's startup, before on_sync(),
    // so that P2P syncs from the snapshot head block, not from genesis.

    // Note: --create-snapshot is handled via snapshot_create_callback registered
    // in plugin_initialize(). It runs during chain plugin's startup, after full DB load
    // (including replay), but before on_sync() — so P2P/witness never start.

    // If --snapshot-at-block or --snapshot-every-n-blocks is set, connect to applied_block signal
    if (my->snapshot_at_block > 0 || my->snapshot_every_n_blocks > 0) {
        my->applied_block_conn = my->db.applied_block.connect(
            [this](const graphene::protocol::signed_block& b) {
                my->on_applied_block(b);
            });
        if (my->snapshot_at_block > 0) {
            ilog("Will create snapshot at block ${b}", ("b", my->snapshot_at_block));
        }
        if (my->snapshot_every_n_blocks > 0) {
            ilog("Will create periodic snapshots every ${n} blocks", ("n", my->snapshot_every_n_blocks));
        }
    }
}

void snapshot_plugin::plugin_shutdown() {
    ilog("snapshot plugin: shutdown");
}

std::string snapshot_plugin::get_snapshot_path() const {
    return my ? my->snapshot_path : std::string();
}

void snapshot_plugin::load_snapshot_from(const std::string& path) {
    FC_ASSERT(my, "Snapshot plugin not initialized");
    my->load_snapshot(fc::path(path));
}

void snapshot_plugin::create_snapshot_at(const std::string& path) {
    FC_ASSERT(my, "Snapshot plugin not initialized");
    my->create_snapshot(fc::path(path));
}

} } } // graphene::plugins::snapshot
