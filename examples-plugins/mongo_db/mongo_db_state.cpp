#include <graphene/plugins/mongo_db/mongo_db_state.hpp>
#include <graphene/plugins/follow/follow_objects.hpp>
#include <graphene/plugins/follow/plugin.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/chain/content_object.hpp>
#include <graphene/chain/account_object.hpp>

#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/stream/value_context.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <appbase/plugin.hpp>

#include <boost/algorithm/string.hpp>

namespace graphene {
namespace plugins {
namespace mongo_db {

    using bsoncxx::builder::stream::array;
    using bsoncxx::builder::stream::document;
    using bsoncxx::builder::stream::open_document;
    using bsoncxx::builder::stream::close_document;
    using namespace graphene::plugins::follow;

    state_writer::state_writer(db_map& bmi_to_add, const signed_block& block) :
        db_(appbase::app().get_plugin<graphene::plugins::chain::plugin>().db()),
        state_block(block),
        all_docs(bmi_to_add) {
    }

    named_document state_writer::create_document(const std::string& name,
            const std::string& key, const std::string& keyval) {
        named_document doc;
        doc.collection_name = name;
        doc.key = key;
        doc.keyval = keyval;
        doc.is_removal = false;
        return doc;
    }

    named_document state_writer::create_removal_document(const std::string& name,
            const std::string& key, const std::string& keyval) {
        named_document doc;
        doc.collection_name = name;
        doc.key = key;
        doc.keyval = keyval;
        doc.is_removal = true;
        return doc;
    }

    bool state_writer::format_content(const std::string& auth, const std::string& perm) {
        try {
            auto& content = db_.get_content(auth, perm);
            auto oid = std::string(auth).append("/").append(perm);
            auto oid_hash = hash_oid(oid);

            auto doc = create_document("content_object", "_id", oid_hash);
            auto& body = doc.doc;

            body << "$set" << open_document;

            format_oid(body, oid);

            format_value(body, "removed", false);

            format_value(body, "author", auth);
            format_value(body, "permlink", perm);
            format_value(body, "abs_rshares", content.abs_rshares);
            format_value(body, "active", content.active);

            format_value(body, "author_rewards", content.author_rewards);
            format_value(body, "beneficiary_payout", content.beneficiary_payout_value);
            format_value(body, "cashout_time", content.cashout_time);
            format_value(body, "children", content.children);
            format_value(body, "children_rshares", content.children_rshares);
            format_value(body, "created", content.created);
            format_value(body, "curator_payout", content.curator_payout_value);
            format_value(body, "depth", content.depth);
            format_value(body, "last_payout", content.last_payout);
            format_value(body, "last_update", content.last_update);
            format_value(body, "net_rshares", content.net_rshares);
            format_value(body, "net_votes", content.net_votes);
            format_value(body, "parent_author", content.parent_author);
            format_value(body, "parent_permlink", content.parent_permlink);
            format_value(body, "total_payout", content.payout_value);
            format_value(body, "total_vote_weight", content.total_vote_weight);
            format_value(body, "vote_rshares", content.vote_rshares);

            if (!content.beneficiaries.empty()) {
                array ben_array;
                for (auto& b: content.beneficiaries) {
                    document tmp;
                    format_value(tmp, "account", b.account);
                    format_value(tmp, "weight", b.weight);
                    ben_array << tmp;
                }
                body << "beneficiaries" << ben_array;
            }

            auto& content = db_.get_content_type(content_id_type(content.id));

            format_value(body, "title", content.title);
            format_value(body, "body", content.body);
            format_value(body, "json_metadata", content.json_metadata);

            std::string root_oid;
            if (content.parent_author == CHAIN_ROOT_POST_PARENT) {
                root_oid = oid;
            } else {
                auto& root_content = db_.get<content_object, by_id>(content.root_content);
                root_oid = std::string(root_content.author).append("/").append(root_content.permlink.c_str());
            }
            format_oid(body, "root_content", root_oid);
            document root_content_index;
            root_content_index << "root_content" << 1;
            doc.indexes_to_create.push_back(std::move(root_content_index));

            body << close_document;

            bmi_insert_or_replace(all_docs, std::move(doc));

            return true;
        }
//        catch (fc::exception& ex) {
//            ilog("MongoDB operations fc::exception during formatting content. ${e}", ("e", ex.what()));
//        }
        catch (...) {
            // ilog("Unknown exception during formatting content.");
            return false;
        }
    }

    void state_writer::format_account(const std::string& name) {
        try {
            auto& account = db_.get_account(name);
            auto oid = name;
            auto oid_hash = hash_oid(oid);

            auto doc = create_document("account_object", "_id", oid_hash);
            auto& body = doc.doc;

            body << "$set" << open_document;

            format_oid(body, oid);

            format_value(body, "name", account.name);
            format_value(body, "memo_key", std::string(account.memo_key));
            format_value(body, "proxy", account.proxy);

            format_value(body, "last_account_update", account.last_account_update);

            format_value(body, "created", account.created);
            format_value(body, "recovery_account", account.recovery_account);
            format_value(body, "last_account_recovery", account.last_account_recovery);
            format_value(body, "subcontent_count", account.subcontent_count);
            format_value(body, "vote_count", account.vote_count);
            format_value(body, "content_count", account.content_count);

            format_value(body, "energy", account.energy);
            format_value(body, "last_vote_time", account.last_vote_time);

            format_value(body, "balance", account.balance);

            format_value(body, "curation_rewards", account.curation_rewards);
            format_value(body, "posting_rewards", account.posting_rewards);

            format_value(body, "vesting_shares", account.vesting_shares);
            format_value(body, "delegated_vesting_shares", account.delegated_vesting_shares);
            format_value(body, "received_vesting_shares", account.received_vesting_shares);

            format_value(body, "vesting_withdraw_rate", account.vesting_withdraw_rate);
            format_value(body, "next_vesting_withdrawal", account.next_vesting_withdrawal);
            format_value(body, "withdrawn", account.withdrawn);
            format_value(body, "to_withdraw", account.to_withdraw);
            format_value(body, "withdraw_routes", account.withdraw_routes);

            if (account.proxied_vsf_votes.size() != 0) {
                array ben_array;
                for (auto& b: account.proxied_vsf_votes) {
                    ben_array << b;
                }
                body << "proxied_vsf_votes" << ben_array;
            }

            format_value(body, "witnesses_voted_for", account.witnesses_voted_for);

            format_value(body, "last_root_post", account.last_root_post);
            format_value(body, "last_post", account.last_post);

            body << close_document;

            bmi_insert_or_replace(all_docs, std::move(doc));

        }
//        catch (fc::exception& ex) {
//            ilog("MongoDB operations fc::exception during formatting content. ${e}", ("e", ex.what()));
//        }
        catch (...) {
            // ilog("Unknown exception during formatting content.");
        }
    }

    auto state_writer::operator()(const vote_operation& op) -> result_type {
        format_content(op.author, op.permlink);

        try {
            auto& vote_idx = db_.get_index<content_vote_index>().indices().get<by_content_voter>();
            auto& content = db_.get_content(op.author, op.permlink);
            auto& voter = db_.get_account(op.voter);
            auto itr = vote_idx.find(std::make_tuple(content.id, voter.id));
            if (vote_idx.end() != itr) {
                auto content_oid = std::string(op.author).append("/").append(op.permlink);
                auto oid = content_oid + "/" + op.voter;
                auto oid_hash = hash_oid(oid);

                auto doc = create_document("content_vote_object", "_id", oid_hash);
                document content_index;
                content_index << "content" << 1;
                doc.indexes_to_create.push_back(std::move(content_index));
                auto &body = doc.doc;

                body << "$set" << open_document;

                format_oid(body, oid);
                format_oid(body, "content", content_oid);

                format_value(body, "author", op.author);
                format_value(body, "permlink", op.permlink);
                format_value(body, "voter", op.voter);

                format_value(body, "weight", itr->weight);
                format_value(body, "rshares", itr->rshares);
                format_value(body, "vote_percent", itr->vote_percent);
                format_value(body, "last_update", itr->last_update);
                format_value(body, "num_changes", itr->num_changes);

                body << close_document;

                bmi_insert_or_replace(all_docs, std::move(doc));
            }
        }
//        catch (fc::exception& ex) {
//            ilog("MongoDB operations fc::exception during formatting vote. ${e}", ("e", ex.what()));
//        }
        catch (...) {
            // ilog("Unknown exception during formatting vote.");
        }
    }

    auto state_writer::operator()(const content_operation& op) -> result_type {
        format_content(op.author, op.permlink);
    }

    auto state_writer::operator()(const delete_content_operation& op) -> result_type {

	std::string author = op.author;

        auto content_oid = std::string(op.author).append("/").append(op.permlink);
        auto content_oid_hash = hash_oid(content_oid);

        // Will be updated with the following fields. If no one - created with these fields.
	auto content = create_document("content_object", "_id", content_oid_hash);

        auto& body = content.doc;

        body << "$set" << open_document;

        format_oid(body, content_oid);

        format_value(body, "removed", true);

        format_value(body, "author", op.author);
        format_value(body, "permlink", op.permlink);

        body << close_document;

        bmi_insert_or_replace(all_docs, std::move(content));

        // Will be updated with removed = true. If no one - nothing to do.
	auto content_vote = create_removal_document("content_vote_object", "content", content_oid_hash);

        bmi_insert_or_replace(all_docs, std::move(content_vote));
    }

    auto state_writer::operator()(const transfer_operation& op) -> result_type {
        auto doc = create_document("transfer", "", "");
        auto& body = doc.doc;

        format_value(body, "from", op.from);
        format_value(body, "to", op.to);
        format_value(body, "amount", op.amount);
        format_value(body, "memo", op.memo);

        std::vector<std::string> part;
        auto path = op.memo;
        boost::split(part, path, boost::is_any_of("/"));
        if (part.size() >= 2 && part[0][0] == '@') {
            auto acnt = part[0].substr(1);
            auto perm = part[1];

            if (format_content(acnt, perm)) {
                auto content_oid = acnt.append("/").append(perm);
                format_oid(body, "content", content_oid);
            } else {
                ilog("unable to find body");
            }
        }

        format_account(op.from);
        format_account(op.to);

        all_docs.push_back(std::move(doc));
    }

    auto state_writer::operator()(const transfer_to_vesting_operation& op) -> result_type {

    }

    auto state_writer::operator()(const withdraw_vesting_operation& op) -> result_type {

    }

    auto state_writer::operator()(const account_create_operation& op) -> result_type {
        format_account(op.new_account_name);
    }

    auto state_writer::operator()(const account_update_operation& op) -> result_type {
        format_account(op.account);
    }

    auto state_writer::operator()(const account_metadata_operation& op) -> result_type {
        format_account(op.account);
    }

    auto state_writer::operator()(const witness_update_operation& op) -> result_type {

    }

    auto state_writer::operator()(const account_witness_vote_operation& op) -> result_type {

    }

    auto state_writer::operator()(const account_witness_proxy_operation& op) -> result_type {

    }

    auto state_writer::operator()(const custom_operation& op) -> result_type {

    }

    auto state_writer::operator()(const set_withdraw_vesting_route_operation& op) -> result_type {

    }

    auto state_writer::operator()(const request_account_recovery_operation& op) -> result_type {

    }

    auto state_writer::operator()(const recover_account_operation& op) -> result_type {

    }

    auto state_writer::operator()(const change_recovery_account_operation& op) -> result_type {

    }

    auto state_writer::operator()(const escrow_transfer_operation& op) -> result_type {

    }

    auto state_writer::operator()(const escrow_dispute_operation& op) -> result_type {

    }

    auto state_writer::operator()(const escrow_release_operation& op) -> result_type {

    }

    auto state_writer::operator()(const escrow_approve_operation& op) -> result_type {

    }

    auto state_writer::operator()(const delegate_vesting_shares_operation& op) -> result_type {

    }

    auto state_writer::operator()(const proposal_create_operation& op) -> result_type {

    }

    auto state_writer::operator()(const proposal_update_operation& op) -> result_type {

    }

    auto state_writer::operator()(const proposal_delete_operation& op) -> result_type {

    }

    auto state_writer::operator()(const fill_vesting_withdraw_operation& op) -> result_type {

    }

    auto state_writer::operator()(const shutdown_witness_operation& op) -> result_type {

    }

    auto state_writer::operator()(const hardfork_operation& op) -> result_type {

    }

    auto state_writer::operator()(const content_payout_update_operation& op) -> result_type {
        format_content(op.author, op.permlink);
    }

    auto state_writer::operator()(const author_reward_operation& op) -> result_type {
        try {
            auto content_oid = std::string(op.author).append("/").append(op.permlink);
            auto content_oid_hash = hash_oid(content_oid);

            auto doc = create_document("author_reward", "_id", content_oid_hash);
            auto &body = doc.doc;

            body << "$set" << open_document;

            format_value(body, "removed", false);
            format_oid(body, content_oid);
            format_oid(body, "content", content_oid);
            format_value(body, "author", op.author);
            format_value(body, "permlink", op.permlink);
            format_value(body, "timestamp", state_block.timestamp);
            format_value(body, "token_payout", op.token_payout);
            format_value(body, "vesting_payout", op.vesting_payout);

            body << close_document;

            bmi_insert_or_replace(all_docs, std::move(doc));

        } catch (...) {
            //
        }
    }

    auto state_writer::operator()(const curation_reward_operation& op) -> result_type {
        try {
            auto content_oid = std::string(op.content_author).append("/").append(op.content_permlink);
            auto vote_oid = content_oid + "/" + op.curator;
            auto vote_oid_hash = hash_oid(vote_oid);

            auto doc = create_document("curation_reward", "_id", vote_oid_hash);
            document content_index;
            content_index << "content" << 1;
            doc.indexes_to_create.push_back(std::move(content_index));
            auto &body = doc.doc;

            body << "$set" << open_document;

            format_value(body, "removed", false);
            format_oid(body, vote_oid);
            format_oid(body, "content", content_oid);
            format_oid(body, "vote", vote_oid);
            format_value(body, "author", op.content_author);
            format_value(body, "permlink", op.content_permlink);
            format_value(body, "timestamp", state_block.timestamp);
            format_value(body, "reward", op.reward);
            format_value(body, "curator", op.curator);

            body << close_document;

            bmi_insert_or_replace(all_docs, std::move(doc));
        } catch (...) {
            //
        }
    }

    auto state_writer::operator()(const content_reward_operation& op) -> result_type {
        try {
            auto content_oid = std::string(op.author).append("/").append(op.permlink);
            auto content_oid_hash = hash_oid(content_oid);

            auto doc = create_document("content_reward", "_id", content_oid_hash);
            auto &body = doc.doc;

            body << "$set" << open_document;

            format_value(body, "removed", false);
            format_oid(body, content_oid);
            format_oid(body, "content", content_oid);
            format_value(body, "author", op.author);
            format_value(body, "permlink", op.permlink);
            format_value(body, "timestamp", state_block.timestamp);
            format_value(body, "payout", op.payout);

            body << close_document;

            bmi_insert_or_replace(all_docs, std::move(doc));
        } catch (...) {
            //
        }
    }

    auto state_writer::operator()(const content_benefactor_reward_operation& op) -> result_type {
        try {
            auto content_oid = std::string(op.author).append("/").append(op.permlink);
            auto benefactor_oid = content_oid + "/" + op.benefactor;
            auto benefactor_oid_hash = hash_oid(benefactor_oid);

            auto doc = create_document("benefactor_reward", "_id", benefactor_oid_hash);
            document content_index;
            content_index << "content" << 1;
            doc.indexes_to_create.push_back(std::move(content_index));
            auto &body = doc.doc;

            body << "$set" << open_document;

            format_value(body, "removed", false);
            format_oid(body, benefactor_oid);
            format_oid(body, "content", content_oid);
            format_value(body, "author", op.author);
            format_value(body, "permlink", op.permlink);
            format_value(body, "timestamp", state_block.timestamp);
            format_value(body, "reward", op.reward);
            format_value(body, "benefactor", op.benefactor);

            body << close_document;

            bmi_insert_or_replace(all_docs, std::move(doc));
        } catch (...) {
            //
        }
    }

    auto state_writer::operator()(const return_vesting_delegation_operation& op) -> result_type {

    }

    auto state_writer::operator()(const chain_properties_update_operation& op) -> result_type {

    }

}}}