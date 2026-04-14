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
#include <fc/network/tcp_socket.hpp>
#include <fc/network/ip.hpp>
#include <fc/thread/thread.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>
#include <fc/thread/future.hpp>

#include <boost/filesystem.hpp>
#include <fstream>
#include <set>
#include <map>
#include <algorithm>
#include <atomic>
#include <tuple>

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
    ~plugin_impl() {
        stop_server();
    }

    graphene::chain::database& db;

    std::string snapshot_path;        // --snapshot: load from this path
    std::string create_snapshot_path; // --create-snapshot: create at this path
    uint32_t snapshot_at_block = 0;   // --snapshot-at-block: create when block N is reached
    uint32_t snapshot_every_n_blocks = 0; // --snapshot-every-n-blocks: periodic snapshot
    std::string snapshot_dir;         // --snapshot-dir: directory for auto-generated snapshots
    uint32_t snapshot_max_age_days = 90; // --snapshot-max-age-days: delete snapshots older than N days (0 = disabled)

    // Snapshot P2P sync config
    bool allow_snapshot_serving = false;
    bool allow_snapshot_serving_only_trusted = false;
    std::string snapshot_serve_endpoint_str = "0.0.0.0:8092";
    std::vector<std::string> trusted_snapshot_peers;
    bool sync_snapshot_from_trusted_peer = false;

    // Parsed trusted IPs for server-side trust enforcement
    std::set<uint32_t> trusted_ips;  // numeric IP addresses

    // TCP server
    std::unique_ptr<fc::tcp_server> tcp_srv;
    fc::future<void> accept_loop_future;
    bool server_running = false;

    // Anti-spam: active sessions per IP (max 1 concurrent session per IP)
    std::set<uint32_t> active_sessions;  // IPs with active connections

    // Anti-spam: rate limiting (max N connections per hour per IP)
    static const uint32_t MAX_CONNECTIONS_PER_HOUR = 3;
    static const uint64_t RATE_LIMIT_WINDOW_SEC = 3600; // 1 hour
    std::map<uint32_t, std::vector<fc::time_point>> connection_history; // IP -> timestamps

    // Mutex protecting active_sessions and connection_history.
    // Required because handle_connection runs in separate fc::async fibers
    // while accept_loop also accesses these structures.
    fc::mutex sessions_mutex;

    // Cached snapshot info for serving (avoids re-reading file on each request)
    std::string cached_snap_path;
    uint64_t cached_snap_size = 0;
    fc::sha256 cached_snap_checksum;
    uint32_t cached_snap_block_num = 0;

    // Async connection tracking
    static const uint32_t MAX_CONCURRENT_CONNECTIONS = 5;
    static const uint32_t CONNECTION_TIMEOUT_SEC = 60;
    std::atomic<uint32_t> active_connection_count{0};

    // Max snapshot file size for download (2 GB)
    static const uint64_t MAX_SNAPSHOT_SIZE = 2ULL * 1024 * 1024 * 1024;

    boost::signals2::scoped_connection applied_block_conn;

    void create_snapshot(const fc::path& output_path);
    void load_snapshot(const fc::path& input_path);

    void on_applied_block(const graphene::protocol::signed_block& b);

    // --- Snapshot P2P sync ---
    void start_server();
    void stop_server();
    void accept_loop();
    void handle_connection(fc::tcp_socket& sock, fc::time_point deadline);
    void update_snapshot_cache(const fc::path& snap_path);

    fc::path find_latest_snapshot();
    std::string download_snapshot_from_peers();
    void cleanup_old_snapshots();

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
    if (!out.is_open()) {
        // Try to create parent directory
        auto parent = output_path.parent_path();
        if (!parent.string().empty() && !boost::filesystem::exists(parent)) {
            boost::filesystem::create_directories(parent);
            ilog("Created snapshot directory: ${d}", ("d", parent.string()));
            out.open(output_path.string(), std::ios::binary);
        }
    }
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

    // Update cached info for serving
    update_snapshot_cache(output_path);
}

void snapshot_plugin::plugin_impl::update_snapshot_cache(const fc::path& snap_path) {
    try {
        std::ifstream in(snap_path.string(), std::ios::binary);
        if (!in.is_open()) return;

        // Stream-compute checksum in 1 MB chunks
        fc::sha256::encoder enc;
        uint64_t file_size = 0;
        char buf[1048576];
        while (in.good()) {
            in.read(buf, sizeof(buf));
            auto n = in.gcount();
            if (n > 0) {
                enc.write(buf, static_cast<uint32_t>(n));
                file_size += n;
            }
        }
        in.close();

        // Parse block number from filename
        uint32_t block_num = 0;
        std::string filename = snap_path.filename().string();
        auto pos = filename.find("snapshot-block-");
        if (pos != std::string::npos) {
            try {
                std::string num_str = filename.substr(pos + 15);
                auto dot_pos = num_str.find('.');
                if (dot_pos != std::string::npos) num_str = num_str.substr(0, dot_pos);
                block_num = static_cast<uint32_t>(std::stoul(num_str));
            } catch (...) {}
        }

        cached_snap_path = snap_path.string();
        cached_snap_size = file_size;
        cached_snap_checksum = enc.result();
        cached_snap_block_num = block_num;
        ilog("Snapshot cache updated: block ${b}, size ${s}, checksum ${c}",
             ("b", block_num)("s", file_size)("c", std::string(cached_snap_checksum)));
    } catch (const std::exception& e) {
        wlog("Failed to update snapshot cache: ${e}", ("e", e.what()));
    }
}

void snapshot_plugin::plugin_impl::load_snapshot(const fc::path& input_path) {
    ilog("Loading snapshot from ${path}...", ("path", input_path.string()));
    std::cerr << "   Loading snapshot from " << input_path.string() << "...\n";

    auto start = fc::time_point::now();

    // Read file
    std::cerr << "   Reading snapshot file...\n";
    std::ifstream in(input_path.string(), std::ios::binary);
    FC_ASSERT(in.is_open(), "Failed to open snapshot file: ${p}", ("p", input_path.string()));

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::cerr << "   Read " << (content.size() / 1048576) << " MB from file\n";
    ilog("Read ${size} bytes from snapshot file", ("size", content.size()));

    // Decompress zlib
    std::cerr << "   Decompressing...\n";
    auto decompress_start = fc::time_point::now();
    std::string json_content = fc::zlib_decompress(content);
    auto decompress_elapsed = double((fc::time_point::now() - decompress_start).count()) / 1000000.0;

    // Save compressed size before freeing to reduce peak memory usage
    auto compressed_size = content.size();
    content.clear();
    content.shrink_to_fit();

    ilog("Decompressed snapshot: ${comp} -> ${orig} bytes (${time} sec)",
        ("comp", compressed_size)
        ("orig", json_content.size())
        ("time", decompress_elapsed));
    std::cerr << "   Decompressed: " << (compressed_size / 1048576) << " MB -> "
              << (json_content.size() / 1048576) << " MB (" << decompress_elapsed << " sec)\n";

    // Parse JSON
    std::cerr << "   Parsing JSON...\n";
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
    std::cerr << "   Snapshot: block " << header.snapshot_block_num
              << ", LIB " << header.last_irreversible_block_num << "\n";

    // Validate checksum
    std::cerr << "   Validating snapshot checksum...\n";
    std::string state_json = fc::json::to_string(snapshot["state"]);
    auto computed_checksum = fc::sha256::hash(state_json.data(), state_json.size());
    FC_ASSERT(computed_checksum == header.payload_checksum,
        "Snapshot checksum mismatch: computed=${c}, header=${h}",
        ("c", std::string(computed_checksum))("h", std::string(header.payload_checksum)));
    ilog("Snapshot checksum verified");
    std::cerr << "   Snapshot checksum verified OK\n";

    const auto& state = snapshot["state"].get_object();

    // Import objects in dependency order
    std::cerr << "   Importing state into database...\n";
    db.with_strong_write_lock([&]() {
        // Clear genesis-created multi-instance objects before importing.
        // init_genesis() creates initial accounts, authorities, witnesses, and metadata
        // that would conflict with the snapshot's objects. Singletons (dgp, witness_schedule,
        // hardfork_property) and block_summaries are handled separately (modify-in-place).
        {
            ilog("Clearing genesis objects before snapshot import...");
            const auto& acc_idx = db.get_index<account_index>().indices();
            while (!acc_idx.empty()) { db.remove(*acc_idx.begin()); }

            const auto& auth_idx = db.get_index<account_authority_index>().indices();
            while (!auth_idx.empty()) { db.remove(*auth_idx.begin()); }

            const auto& wit_idx = db.get_index<witness_index>().indices();
            while (!wit_idx.empty()) { db.remove(*wit_idx.begin()); }

            const auto& meta_idx = db.get_index<account_metadata_index>().indices();
            while (!meta_idx.empty()) { db.remove(*meta_idx.begin()); }
        }
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
    auto total_elapsed = double((end - start).count()) / 1000000.0;
    std::cerr << "   Snapshot loaded successfully in " << total_elapsed << " sec\n";
    ilog("Snapshot loaded successfully in ${t} sec", ("t", total_elapsed));
}

void snapshot_plugin::plugin_impl::on_applied_block(const graphene::protocol::signed_block& b) {
    uint32_t block_num = b.block_num();

    // Skip snapshot creation while syncing from P2P (block time far behind wall clock).
    // Only create snapshots when the node is caught up and processing live blocks.
    auto block_age = fc::time_point::now() - fc::time_point(b.timestamp);
    bool is_syncing = block_age > fc::seconds(60);

    // Check --snapshot-at-block: one-time snapshot at exact block
    if (snapshot_at_block > 0 && block_num == snapshot_at_block && !is_syncing) {
        fc::path output;
        if (!snapshot_dir.empty()) {
            output = fc::path(snapshot_dir) / ("snapshot-block-" + std::to_string(block_num) + ".vizjson");
        } else if (!create_snapshot_path.empty()) {
            output = fc::path(create_snapshot_path);
        } else {
            output = fc::path("snapshot-block-" + std::to_string(block_num) + ".vizjson");
        }
        ilog("Reached snapshot-at-block ${b}, creating snapshot: ${p}", ("b", block_num)("p", output.string()));
        try {
            // Called from applied_block signal which is already under write lock.
            // Use write_snapshot_to_file directly (no additional lock needed).
            write_snapshot_to_file(output);
            update_snapshot_cache(output);
            cleanup_old_snapshots();
        } catch (const fc::exception& e) {
            elog("Failed to create snapshot at block ${b}: ${e}", ("b", block_num)("e", e.to_detail_string()));
        }
    }

    // Check --snapshot-every-n-blocks: periodic snapshots (only when synced)
    if (snapshot_every_n_blocks > 0 && block_num % snapshot_every_n_blocks == 0 && !is_syncing) {
        std::string dir = snapshot_dir.empty() ? "." : snapshot_dir;
        fc::path output = fc::path(dir) / ("snapshot-block-" + std::to_string(block_num) + ".vizjson");
        ilog("Periodic snapshot at block ${b}: ${p}", ("b", block_num)("p", output.string()));
        try {
            // Called from applied_block signal which is already under write lock.
            // Use write_snapshot_to_file directly (no additional lock needed).
            write_snapshot_to_file(output);
            update_snapshot_cache(output);
            cleanup_old_snapshots();
        } catch (const fc::exception& e) {
            elog("Failed to create periodic snapshot at block ${b}: ${e}", ("b", block_num)("e", e.to_detail_string()));
        }
    }
}

// ============================================================================
// Wire protocol helpers
// ============================================================================

namespace {

/// Timeout for snapshot peer operations (connect, read, write)
const fc::microseconds SNAPSHOT_PEER_TIMEOUT = fc::seconds(30);

/// Read exactly `len` bytes from a tcp_socket with timeout.
/// Returns true on success, false on timeout.
bool read_exact_with_timeout(fc::tcp_socket& sock, char* buf, size_t len, const fc::microseconds& timeout) {
    size_t total = 0;
    auto deadline = fc::time_point::now() + timeout;
    while (total < len) {
        // Use async operation with timeout
        auto read_future = fc::async([&sock, buf, len, total]() -> size_t {
            return sock.readsome(buf + total, len - total);
        });

        auto remaining = deadline - fc::time_point::now();
        if (remaining <= fc::microseconds(0)) {
            return false; // Timeout
        }

        size_t n;
        try {
            n = read_future.wait(remaining);
        } catch (const fc::timeout_exception&) {
            return false; // Timeout
        }

        if (n == 0) {
            FC_THROW("Connection closed while reading");
        }
        total += n;
    }
    return true;
}

/// Read exactly `len` bytes from a tcp_socket.
void read_exact(fc::tcp_socket& sock, char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        size_t n = sock.readsome(buf + total, len - total);
        FC_ASSERT(n > 0, "Connection closed while reading");
        total += n;
    }
}

/// Write exactly `len` bytes to a tcp_socket.
void write_exact(fc::tcp_socket& sock, const char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        size_t n = sock.writesome(buf + total, len - total);
        FC_ASSERT(n > 0, "Connection closed while writing");
        total += n;
    }
}

/// Send a message: [4 bytes payload_size][4 bytes msg_type][payload]
void send_message(fc::tcp_socket& sock, uint32_t msg_type, const std::vector<char>& payload) {
    uint32_t payload_size = static_cast<uint32_t>(payload.size());
    write_exact(sock, reinterpret_cast<const char*>(&payload_size), 4);
    write_exact(sock, reinterpret_cast<const char*>(&msg_type), 4);
    if (!payload.empty()) {
        write_exact(sock, payload.data(), payload.size());
    }
    sock.flush();
}

/// Send a message with no payload (e.g. info_request, not_available).
void send_message_empty(fc::tcp_socket& sock, uint32_t msg_type) {
    std::vector<char> empty;
    send_message(sock, msg_type, empty);
}

/// Read a message: returns (msg_type, payload).
/// max_payload_size limits the accepted payload (default 64 MB for data replies,
/// use 64 KB for control/request messages to prevent memory abuse).
std::pair<uint32_t, std::vector<char>> read_message(fc::tcp_socket& sock, uint32_t max_payload_size = 64 * 1024 * 1024) {
    uint32_t payload_size = 0;
    uint32_t msg_type = 0;
    read_exact(sock, reinterpret_cast<char*>(&payload_size), 4);
    read_exact(sock, reinterpret_cast<char*>(&msg_type), 4);
    FC_ASSERT(payload_size <= max_payload_size, "Message too large: ${s} bytes (limit ${l})",
        ("s", payload_size)("l", max_payload_size));
    std::vector<char> payload(payload_size);
    if (payload_size > 0) {
        read_exact(sock, payload.data(), payload_size);
    }
    return {msg_type, std::move(payload)};
}

/// Read a message with timeout: returns (success, msg_type, payload).
/// Returns success=false on timeout or error.
std::tuple<bool, uint32_t, std::vector<char>> read_message_with_timeout(
    fc::tcp_socket& sock,
    uint32_t max_payload_size = 64 * 1024 * 1024,
    const fc::microseconds& timeout = SNAPSHOT_PEER_TIMEOUT) {

    uint32_t payload_size = 0;
    uint32_t msg_type = 0;

    // Read header (8 bytes) with timeout
    if (!read_exact_with_timeout(sock, reinterpret_cast<char*>(&payload_size), 4, timeout)) {
        return {false, 0, std::vector<char>()};
    }
    if (!read_exact_with_timeout(sock, reinterpret_cast<char*>(&msg_type), 4, timeout)) {
        return {false, 0, std::vector<char>()};
    }

    if (payload_size > max_payload_size) {
        FC_THROW("Message too large: ${s} bytes (limit ${l})",
            ("s", payload_size)("l", max_payload_size));
    }

    std::vector<char> payload(payload_size);
    if (payload_size > 0) {
        if (!read_exact_with_timeout(sock, payload.data(), payload_size, timeout)) {
            return {false, 0, std::vector<char>()};
        }
    }
    return {true, msg_type, std::move(payload)};
}

/// Serialize a struct to vector<char> via fc::raw::pack
template<typename T>
std::vector<char> pack_to_vec(const T& obj) {
    return fc::raw::pack(obj);
}

/// Deserialize a struct from vector<char> via fc::raw::unpack
template<typename T>
T unpack_from_vec(const std::vector<char>& data) {
    return fc::raw::unpack<T>(data);
}

} // anonymous namespace

// ============================================================================
// Snapshot file discovery
// ============================================================================

fc::path snapshot_plugin::plugin_impl::find_latest_snapshot() {
    std::string dir = snapshot_dir.empty() ? "." : snapshot_dir;
    fc::path dir_path(dir);

    if (!fc::exists(dir_path) || !fc::is_directory(dir_path)) {
        return fc::path();
    }

    fc::path best_path;
    uint32_t best_block = 0;

    boost::filesystem::directory_iterator end_itr;
    for (boost::filesystem::directory_iterator itr(dir); itr != end_itr; ++itr) {
        if (boost::filesystem::is_regular_file(itr->status())) {
            std::string filename = itr->path().filename().string();
            std::string ext = itr->path().extension().string();
            // Accept .vizjson and .json snapshot files
            if (ext == ".vizjson" || ext == ".json") {
                // Try to parse block number from filename: snapshot-block-NNNNN.ext
                auto pos = filename.find("snapshot-block-");
                if (pos != std::string::npos) {
                    try {
                        std::string num_str = filename.substr(pos + 15);
                        // Remove extension
                        auto dot_pos = num_str.find('.');
                        if (dot_pos != std::string::npos) {
                            num_str = num_str.substr(0, dot_pos);
                        }
                        uint32_t block_num = static_cast<uint32_t>(std::stoul(num_str));
                        if (block_num > best_block) {
                            best_block = block_num;
                            best_path = fc::path(itr->path().string());
                        }
                    } catch (...) {
                        // Skip files with unparseable names
                    }
                }
            }
        }
    }

    return best_path;
}

void snapshot_plugin::plugin_impl::cleanup_old_snapshots() {
    if (snapshot_max_age_days == 0 || snapshot_dir.empty()) return;

    std::string dir = snapshot_dir;
    if (!fc::exists(fc::path(dir)) || !fc::is_directory(fc::path(dir))) return;

    auto now = std::time(nullptr);
    uint64_t max_age_sec = static_cast<uint64_t>(snapshot_max_age_days) * 86400;
    uint32_t removed = 0;

    try {
        boost::filesystem::directory_iterator end_itr;
        for (boost::filesystem::directory_iterator itr(dir); itr != end_itr; ++itr) {
            if (!boost::filesystem::is_regular_file(itr->status())) continue;

            std::string filename = itr->path().filename().string();
            std::string ext = itr->path().extension().string();

            if (ext != ".vizjson" && ext != ".json") continue;
            if (filename.find("snapshot-block-") == std::string::npos) continue;

            auto mtime = boost::filesystem::last_write_time(itr->path());
            uint64_t age_sec = static_cast<uint64_t>(now - mtime);
            if (age_sec > max_age_sec) {
                ilog("Removing old snapshot (${days}d old): ${f}",
                     ("days", age_sec / 86400)("f", filename));
                boost::filesystem::remove(itr->path());
                ++removed;
            }
        }
    } catch (const std::exception& e) {
        wlog("Error during snapshot cleanup: ${e}", ("e", e.what()));
    }

    if (removed > 0) {
        ilog("Snapshot cleanup: removed ${n} old snapshot(s)", ("n", removed));
    }
}

// ============================================================================
// Snapshot TCP server
// ============================================================================

void snapshot_plugin::plugin_impl::start_server() {
    if (!allow_snapshot_serving) return;

    auto ep = fc::ip::endpoint::from_string(snapshot_serve_endpoint_str);

    tcp_srv = std::make_unique<fc::tcp_server>();
    tcp_srv->set_reuse_address();
    tcp_srv->listen(ep);
    server_running = true;

    ilog("Snapshot TCP server listening on ${ep}", ("ep", snapshot_serve_endpoint_str));

    accept_loop_future = fc::async([this]() {
        accept_loop();
    }, "snapshot_accept_loop");
}

void snapshot_plugin::plugin_impl::stop_server() {
    server_running = false;
    if (tcp_srv) {
        tcp_srv->close();
    }
    if (accept_loop_future.valid() && !accept_loop_future.ready()) {
        accept_loop_future.cancel("shutdown");
        try { accept_loop_future.wait(); } catch (...) {}
    }
}

void snapshot_plugin::plugin_impl::accept_loop() {
    while (server_running) {
        try {
            auto sock_ptr = std::make_shared<fc::tcp_socket>();
            tcp_srv->accept(*sock_ptr);

            if (!server_running) break;

            auto remote = sock_ptr->remote_endpoint();
            uint32_t remote_ip = static_cast<uint32_t>(remote.get_address());

            // Trust enforcement
            if (allow_snapshot_serving_only_trusted) {
                if (trusted_ips.find(remote_ip) == trusted_ips.end()) {
                    wlog("Snapshot server: rejected untrusted connection from ${ip}",
                         ("ip", std::string(remote.get_address())));
                    sock_ptr->close();
                    continue;
                }
            }

            // Reject if max concurrent connections reached
            if (active_connection_count.load() >= MAX_CONCURRENT_CONNECTIONS) {
                wlog("Snapshot server: max concurrent connections reached, rejecting ${ip}",
                     ("ip", std::string(remote.get_address())));
                sock_ptr->close();
                continue;
            }

            // All anti-spam checks under mutex (active_sessions + connection_history)
            {
                fc::scoped_lock<fc::mutex> lock(sessions_mutex);

                // Anti-spam: reject if this IP already has an active session
                if (active_sessions.count(remote_ip)) {
                    wlog("Snapshot server: rejected duplicate session from ${ip} (already connected)",
                         ("ip", std::string(remote.get_address())));
                    sock_ptr->close();
                    continue;
                }

                // Anti-spam: rate limiting (max 3 connections per hour per IP)
                {
                    auto now = fc::time_point::now();
                    auto cutoff = now - fc::seconds(RATE_LIMIT_WINDOW_SEC);
                    auto& history = connection_history[remote_ip];

                    // Prune old entries outside the window
                    history.erase(
                        std::remove_if(history.begin(), history.end(),
                            [&cutoff](const fc::time_point& t) { return t < cutoff; }),
                        history.end());

                    // Clean up empty entries to prevent unbounded map growth
                    if (history.empty()) {
                        connection_history.erase(remote_ip);
                        // Re-check rate limit with fresh entry
                        connection_history[remote_ip].push_back(now);
                    } else if (history.size() >= MAX_CONNECTIONS_PER_HOUR) {
                        wlog("Snapshot server: rate limit exceeded for ${ip} (${n} connections in last hour)",
                             ("ip", std::string(remote.get_address()))("n", history.size()));
                        sock_ptr->close();
                        continue;
                    } else {
                        history.push_back(now);
                    }
                }

                // Register active session
                active_sessions.insert(remote_ip);
            }
            active_connection_count.fetch_add(1);

            ilog("Snapshot server: accepted connection from ${ip}:${port}",
                 ("ip", std::string(remote.get_address()))("port", remote.port()));

            // Handle connection asynchronously in a separate fiber.
            // The socket is heap-allocated (shared_ptr) so it survives the accept_loop
            // iteration and lives until the async fiber completes.
            auto deadline = fc::time_point::now() + fc::seconds(CONNECTION_TIMEOUT_SEC);
            fc::async([this, sock_ptr, remote_ip, deadline]() {
                try {
                    handle_connection(*sock_ptr, deadline);
                } catch (const fc::exception& e) {
                    wlog("Snapshot server: error handling connection: ${e}", ("e", e.to_detail_string()));
                } catch (const std::exception& e) {
                    wlog("Snapshot server: error handling connection: ${e}", ("e", e.what()));
                }

                // Cleanup under mutex
                {
                    fc::scoped_lock<fc::mutex> lock(sessions_mutex);
                    active_sessions.erase(remote_ip);
                }
                active_connection_count.fetch_sub(1);
                try { sock_ptr->close(); } catch (...) {}
            }, "snapshot_handle_connection");

        } catch (const fc::canceled_exception&) {
            break;
        } catch (const fc::exception& e) {
            if (server_running) {
                wlog("Snapshot server: accept error: ${e}", ("e", e.to_detail_string()));
            }
        }
    }
}

void snapshot_plugin::plugin_impl::handle_connection(fc::tcp_socket& sock, fc::time_point deadline) {
    auto remote = sock.remote_endpoint();
    std::string remote_str = std::string(remote.get_address()) + ":" + std::to_string(remote.port());

    ilog("Snapshot server: handling connection from ${remote}", ("remote", remote_str));

    // Check deadline before initial read
    if (fc::time_point::now() > deadline) {
        wlog("Snapshot server: connection timeout before processing request from ${remote}", ("remote", remote_str));
        return;
    }

    // Read initial request (server-side: small payload limit) with timeout.
    // Use 256 KB to tolerate slightly oversized messages from future protocol versions,
    // while still rejecting non-protocol traffic (P2P nodes, scanners, browsers).
    auto msg_result = read_message_with_timeout(sock, 256 * 1024, fc::seconds(10));
    if (!std::get<0>(msg_result)) {
        wlog("Snapshot server: timeout reading request from ${remote}", ("remote", remote_str));
        return;
    }
    uint32_t msg_type = std::get<1>(msg_result);

    ilog("Snapshot server: received message type ${type} from ${remote}",
         ("type", msg_type)("remote", remote_str));

    if (msg_type == snapshot_info_request) {
        // Use cached snapshot info if available, otherwise find and cache
        if (cached_snap_path.empty() || !fc::exists(fc::path(cached_snap_path))) {
            fc::path snap_path = find_latest_snapshot();
            if (snap_path.string().empty() || !fc::exists(snap_path)) {
                ilog("Snapshot server: no snapshot available, sending NOT_AVAILABLE to ${remote}",
                     ("remote", remote_str));
                send_message_empty(sock, snapshot_not_available);
                ilog("Snapshot server: sent snapshot_not_available to ${remote}", ("remote", remote_str));
                return;
            }
            update_snapshot_cache(snap_path);
        }

        if (cached_snap_size == 0) {
            ilog("Snapshot server: snapshot has zero size, sending NOT_AVAILABLE to ${remote}",
                 ("remote", remote_str));
            send_message_empty(sock, snapshot_not_available);
            return;
        }

        snapshot_info_reply_data reply;
        reply.block_num = cached_snap_block_num;
        reply.checksum = cached_snap_checksum;
        reply.compressed_size = cached_snap_size;

        ilog("Snapshot server: offering snapshot at block ${b}, size ${s} bytes to ${remote}",
             ("b", cached_snap_block_num)("s", cached_snap_size)("remote", remote_str));

        send_message(sock, snapshot_info_reply, pack_to_vec(reply));
        ilog("Snapshot server: sent snapshot_info_reply to ${remote}", ("remote", remote_str));

        std::string serve_path = cached_snap_path;
        uint64_t file_size = cached_snap_size;

        // Now wait for data requests in a loop.
        // The client may close after receiving just the info (Phase 1 query).
        // This is normal — treat EOF here as a clean disconnect.
        auto serve_start = fc::time_point::now();
        uint64_t bytes_sent = 0;
        while (true) {
            try {
                // Check connection deadline before each chunk operation
                if (fc::time_point::now() > deadline) {
                    wlog("Snapshot server: connection timeout during transfer to ${remote} "
                         "(sent ${b}/${t} bytes)",
                         ("remote", remote_str)("b", bytes_sent)("t", file_size));
                    return;
                }

                // Use longer per-chunk timeout for data requests to support slow clients.
                // Client has 5 min to request next chunk (includes their processing time).
                // But overall connection is bounded by the deadline above.
                auto req_result = read_message_with_timeout(sock, 256 * 1024, fc::minutes(5));
                if (!std::get<0>(req_result)) {
                    wlog("Snapshot server: timeout waiting for data request from ${remote}", ("remote", remote_str));
                    return;
                }
                if (std::get<1>(req_result) != snapshot_data_request) break;

                auto req = unpack_from_vec<snapshot_data_request_data>(std::get<2>(req_result));

                // Read chunk from file
                std::ifstream chunk_in(serve_path, std::ios::binary);
                FC_ASSERT(chunk_in.is_open(), "Failed to open snapshot for serving");

                chunk_in.seekg(req.offset, std::ios::beg);
                uint32_t to_read = req.chunk_size;
                if (req.offset + to_read > file_size) {
                    to_read = static_cast<uint32_t>(file_size - req.offset);
                }

                snapshot_data_reply_data reply_data;
                reply_data.offset = req.offset;
                reply_data.data.resize(to_read);
                if (to_read > 0) {
                    chunk_in.read(reply_data.data.data(), to_read);
                }
                reply_data.is_last = (req.offset + to_read >= file_size);
                chunk_in.close();

                send_message(sock, snapshot_data_reply, pack_to_vec(reply_data));
                bytes_sent += to_read;

                if (reply_data.is_last) break;
            } catch (const fc::eof_exception&) {
                // Client disconnected — normal for info-only queries (Phase 1)
                if (bytes_sent == 0) {
                    ilog("Snapshot server: client disconnected after info query (normal)");
                } else {
                    wlog("Snapshot server: client disconnected during transfer at ${b}/${t} bytes",
                         ("b", bytes_sent)("t", file_size));
                }
                return;
            }
        }

        auto serve_elapsed = double((fc::time_point::now() - serve_start).count()) / 1000000.0;
        ilog("Snapshot server: served ${s} bytes in ${t} sec", ("s", bytes_sent)("t", serve_elapsed));
    }
}

// ============================================================================
// Snapshot TCP client
// ============================================================================

std::string snapshot_plugin::plugin_impl::download_snapshot_from_peers() {
    FC_ASSERT(!trusted_snapshot_peers.empty(), "No trusted snapshot peers configured");

    std::cerr << "   Querying " << trusted_snapshot_peers.size() << " trusted peer(s) for snapshot info...\n";

    // Phase 1: Query all peers for snapshot info
    struct peer_info {
        std::string endpoint_str;
        uint32_t block_num = 0;
        fc::sha256 checksum;
        uint64_t compressed_size = 0;
    };
    std::vector<peer_info> available_peers;

    for (const auto& peer_str : trusted_snapshot_peers) {
        try {
            std::cerr << "   Connecting to peer " << peer_str << "...\n";
            ilog("Querying snapshot info from peer ${p}...", ("p", peer_str));
            fc::tcp_socket sock;
            auto ep = fc::ip::endpoint::from_string(peer_str);

            // Connect with timeout
            auto connect_future = fc::async([&sock, &ep]() {
                sock.connect_to(ep);
            });
            try {
                connect_future.wait(SNAPSHOT_PEER_TIMEOUT);
            } catch (const fc::timeout_exception&) {
                std::cerr << "   Peer " << peer_str << ": connection timeout\n";
                wlog("Connection timeout to peer ${p}", ("p", peer_str));
                sock.close();
                continue;
            }

            send_message_empty(sock, snapshot_info_request);

            // Read response with timeout
            auto resp_result = read_message_with_timeout(sock, 256 * 1024, SNAPSHOT_PEER_TIMEOUT);
            if (!std::get<0>(resp_result)) {
                std::cerr << "   Peer " << peer_str << ": response timeout\n";
                wlog("Response timeout from peer ${p}", ("p", peer_str));
                sock.close();
                continue;
            }

            uint32_t resp_msg_type = std::get<1>(resp_result);
            const auto& resp_payload = std::get<2>(resp_result);

            if (resp_msg_type == snapshot_info_reply) {
                auto info = unpack_from_vec<snapshot_info_reply_data>(resp_payload);
                std::cerr << "   Peer " << peer_str << ": snapshot at block "
                          << info.block_num << " (" << (info.compressed_size / 1048576) << " MB)\n";
                ilog("Peer ${p}: snapshot at block ${b}, size ${s} bytes",
                     ("p", peer_str)("b", info.block_num)("s", info.compressed_size));
                available_peers.push_back({peer_str, info.block_num, info.checksum, info.compressed_size});
            } else if (resp_msg_type == snapshot_not_available) {
                std::cerr << "   Peer " << peer_str << ": no snapshot available\n";
                ilog("Peer ${p}: no snapshot available", ("p", peer_str));
            }

            sock.close();
        } catch (const fc::exception& e) {
            std::cerr << "   Peer " << peer_str << ": connection failed\n";
            wlog("Failed to query peer ${p}: ${e}", ("p", peer_str)("e", e.to_detail_string()));
        } catch (const std::exception& e) {
            std::cerr << "   Peer " << peer_str << ": connection failed\n";
            wlog("Failed to query peer ${p}: ${e}", ("p", peer_str)("e", e.what()));
        }
    }

    FC_ASSERT(!available_peers.empty(), "No peers have snapshots available");

    // Pick the peer with the highest block_num
    auto best = std::max_element(available_peers.begin(), available_peers.end(),
        [](const peer_info& a, const peer_info& b) { return a.block_num < b.block_num; });

    ilog("Selected peer ${p} with snapshot at block ${b} (${s} bytes)",
         ("p", best->endpoint_str)("b", best->block_num)("s", best->compressed_size));
    std::cerr << "   Selected peer " << best->endpoint_str
              << " (block " << best->block_num
              << ", " << (best->compressed_size / 1048576) << " MB)\n";

    // Phase 2: Download snapshot in chunks
    std::cerr << "   Downloading snapshot...\n";
    fc::tcp_socket sock;
    auto ep = fc::ip::endpoint::from_string(best->endpoint_str);

    // Connect with timeout
    auto connect_future = fc::async([&sock, &ep]() {
        sock.connect_to(ep);
    });
    connect_future.wait(SNAPSHOT_PEER_TIMEOUT);

    // Request info again to establish session
    send_message_empty(sock, snapshot_info_request);
    auto info_result = read_message_with_timeout(sock, 256 * 1024, SNAPSHOT_PEER_TIMEOUT);
    FC_ASSERT(std::get<0>(info_result), "Timeout waiting for peer response during download");
    FC_ASSERT(std::get<1>(info_result) == snapshot_info_reply, "Unexpected response from peer during download");

    // Validate snapshot size against maximum
    FC_ASSERT(best->compressed_size <= MAX_SNAPSHOT_SIZE,
        "Snapshot too large: ${s} bytes exceeds limit of ${l} bytes",
        ("s", best->compressed_size)("l", MAX_SNAPSHOT_SIZE));

    // Create temp file for download
    std::string dir = snapshot_dir.empty() ? "." : snapshot_dir;
    if (!boost::filesystem::exists(dir)) {
        boost::filesystem::create_directories(dir);
        ilog("Created snapshot directory: ${d}", ("d", dir));
    }
    std::string temp_path = dir + "/snapshot-download-temp.vizjson";
    std::ofstream out(temp_path, std::ios::binary);
    FC_ASSERT(out.is_open(), "Failed to create temp file for snapshot download: ${p}", ("p", temp_path));

    uint64_t total_size = best->compressed_size;
    uint64_t offset = 0;
    const uint32_t chunk_size = 1048576; // 1 MB chunks
    int last_printed_percent = -1;

    auto download_start = fc::time_point::now();

    while (offset < total_size) {
        snapshot_data_request_data req;
        req.block_num = best->block_num;
        req.offset = offset;
        req.chunk_size = chunk_size;

        send_message(sock, snapshot_data_request, pack_to_vec(req));
        // Use longer timeout for chunk download to support slow connections.
        // 1 MB chunk with 5 min timeout = min 3.4 KB/s required (very slow connections OK).
        auto data_result = read_message_with_timeout(sock, 64 * 1024 * 1024, fc::minutes(5));
        FC_ASSERT(std::get<0>(data_result), "Timeout waiting for chunk data from peer");
        FC_ASSERT(std::get<1>(data_result) == snapshot_data_reply, "Unexpected response during chunk download");

        auto reply = unpack_from_vec<snapshot_data_reply_data>(std::get<2>(data_result));

        if (!reply.data.empty()) {
            out.write(reply.data.data(), reply.data.size());
            offset += reply.data.size();
        }

        uint32_t percent = total_size > 0 ? static_cast<uint32_t>(offset * 100 / total_size) : 100;
        if (static_cast<int>(percent) != last_printed_percent && (percent % 5 == 0 || reply.is_last)) {
            std::cerr << "   Downloaded " << (offset / 1048576) << "/" << (total_size / 1048576) << " MB (" << percent << "%)\n";
            last_printed_percent = static_cast<int>(percent);
        }
        ilog("Downloaded ${offset}/${total} bytes (${pct}%)",
             ("offset", offset)("total", total_size)("pct", percent));

        if (reply.is_last) break;
    }

    out.flush();
    out.close();
    sock.close();

    auto download_elapsed = double((fc::time_point::now() - download_start).count()) / 1000000.0;
    std::cerr << "   Download complete: " << (offset / 1048576) << " MB in " << download_elapsed << " sec\n";
    ilog("Download complete: ${s} bytes in ${t} sec", ("s", offset)("t", download_elapsed));

    // Verify checksum by streaming file in chunks (avoids loading entire file into memory)
    std::cerr << "   Verifying checksum...\n";
    {
        std::ifstream verify_in(temp_path, std::ios::binary);
        FC_ASSERT(verify_in.is_open(), "Failed to open downloaded snapshot for verification");

        fc::sha256::encoder enc;
        char buf[1048576]; // 1 MB chunks
        while (verify_in.good()) {
            verify_in.read(buf, sizeof(buf));
            auto n = verify_in.gcount();
            if (n > 0) {
                enc.write(buf, static_cast<uint32_t>(n));
            }
        }
        verify_in.close();

        fc::sha256 computed = enc.result();
        FC_ASSERT(computed == best->checksum,
            "Snapshot checksum mismatch after download: computed=${c}, expected=${e}",
            ("c", std::string(computed))("e", std::string(best->checksum)));
    }
    ilog("Snapshot checksum verified");
    std::cerr << "   Checksum verified OK\n";

    // Rename to final path
    std::string final_path = dir + "/snapshot-block-" + std::to_string(best->block_num) + ".vizjson";
    boost::filesystem::rename(temp_path, final_path);

    std::cerr << "   Snapshot saved to " << final_path << "\n";
    ilog("Snapshot saved to ${p}", ("p", final_path));
    return final_path;
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
        ("snapshot-max-age-days", bpo::value<uint32_t>()->default_value(90),
            "Delete snapshots older than N days after creating a new one (0 = disabled)")
        ("allow-snapshot-serving", bpo::value<bool>()->default_value(false),
            "Enable serving snapshots over TCP to other nodes")
        ("allow-snapshot-serving-only-trusted", bpo::value<bool>()->default_value(false),
            "Restrict snapshot serving to trusted peers only (from trusted-snapshot-peer list)")
        ("snapshot-serve-endpoint", bpo::value<std::string>()->default_value("0.0.0.0:8092"),
            "TCP endpoint for the snapshot serving listener")
        ("trusted-snapshot-peer", bpo::value<std::vector<std::string>>()->composing(),
            "Trusted peer endpoint for snapshot sync (IP:port). Can be specified multiple times.")
    ;
    cli.add_options()
        ("snapshot", bpo::value<std::string>(),
            "Load state from snapshot file instead of replaying blockchain")
        ("create-snapshot", bpo::value<std::string>(),
            "Create a snapshot file at the specified path and exit")
        ("sync-snapshot-from-trusted-peer", bpo::value<bool>()->default_value(false),
            "Download and load snapshot from trusted peers on empty state (requires trusted-snapshot-peer)")
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

    if (options.count("snapshot-max-age-days")) {
        my->snapshot_max_age_days = options.at("snapshot-max-age-days").as<uint32_t>();
        if (my->snapshot_max_age_days > 0) {
            ilog("Snapshot rotation enabled: delete snapshots older than ${d} days", ("d", my->snapshot_max_age_days));
        }
    }

    // Snapshot P2P sync config
    my->allow_snapshot_serving = options.at("allow-snapshot-serving").as<bool>();
    my->allow_snapshot_serving_only_trusted = options.at("allow-snapshot-serving-only-trusted").as<bool>();
    my->snapshot_serve_endpoint_str = options.at("snapshot-serve-endpoint").as<std::string>();
    my->sync_snapshot_from_trusted_peer = options.at("sync-snapshot-from-trusted-peer").as<bool>();

    if (options.count("trusted-snapshot-peer")) {
        my->trusted_snapshot_peers = options.at("trusted-snapshot-peer").as<std::vector<std::string>>();
        // Parse trusted IPs for server-side trust enforcement
        for (const auto& peer_str : my->trusted_snapshot_peers) {
            try {
                auto ep = fc::ip::endpoint::from_string(peer_str);
                my->trusted_ips.insert(static_cast<uint32_t>(ep.get_address()));
            } catch (const fc::exception& e) {
                wlog("Failed to parse trusted-snapshot-peer '${p}': ${e}",
                     ("p", peer_str)("e", e.to_detail_string()));
            }
        }
        if (!my->trusted_snapshot_peers.empty()) {
            ilog("Trusted snapshot peers: ${n} configured", ("n", my->trusted_snapshot_peers.size()));
        }
    }

    if (my->allow_snapshot_serving) {
        ilog("Snapshot serving enabled on ${ep}", ("ep", my->snapshot_serve_endpoint_str));
        if (my->allow_snapshot_serving_only_trusted) {
            ilog("Snapshot serving restricted to trusted peers only (${n} IPs)",
                 ("n", my->trusted_ips.size()));
        } else {
            ilog("Snapshot serving open to anyone (public gate)");
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

    // Register P2P snapshot sync callback on the chain plugin.
    // When state is empty (head_block_num == 0), download and load snapshot from trusted peers.
    if (my->sync_snapshot_from_trusted_peer && !my->trusted_snapshot_peers.empty()) {
        ilog("P2P snapshot sync enabled: will download from trusted peers on empty state");
        auto& chain_plug = appbase::app().get_plugin<chain::plugin>();
        chain_plug.snapshot_p2p_sync_callback = [this]() {
            auto start = fc::time_point::now();
            std::cerr << "   === P2P Snapshot Sync ===\n";
            ilog("Requesting snapshot from trusted peers...");
            auto snapshot_path = my->download_snapshot_from_peers();
            std::cerr << "   Clearing state and importing snapshot...\n";
            ilog("Download complete, loading snapshot...");
            my->load_snapshot(fc::path(snapshot_path));
            my->db.set_dlt_mode(true);  // Mark DLT mode — block_log stays empty
            my->db.initialize_hardforks();
            auto elapsed = (fc::time_point::now() - start).count() / 1000000.0;
            std::cerr << "   === P2P Snapshot Sync complete (block "
                      << my->db.head_block_num() << ", " << elapsed << " sec) ===\n";
            ilog("P2P snapshot sync complete at block ${n}, elapsed ${t} sec",
                ("n", my->db.head_block_num())("t", elapsed));
        };
    } else if (!my->trusted_snapshot_peers.empty()) {
        ilog("P2P snapshot sync disabled (sync-snapshot-from-trusted-peer = false)");
    } else {
        ilog("No trusted-snapshot-peer configured. P2P snapshot sync not available.");
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

    // Note: --sync-snapshot-from-trusted-peer is handled via snapshot_p2p_sync_callback
    // registered in plugin_initialize(). It runs during chain plugin's startup when
    // state is empty (head_block_num == 0), before on_sync().

    // Start snapshot TCP server if enabled
    if (my->allow_snapshot_serving) {
        my->start_server();
    }

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
    my->stop_server();
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
