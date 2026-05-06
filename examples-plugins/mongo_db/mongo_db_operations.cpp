#include <graphene/plugins/mongo_db/mongo_db_operations.hpp>

#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/stream/value_context.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <appbase/plugin.hpp>


namespace graphene {
namespace plugins {
namespace mongo_db {

    using bsoncxx::builder::stream::array;
    using bsoncxx::builder::stream::document;

    document format_authority(const authority &auth) {
        array account_auths_arr;
        for (const auto &iter : auth.account_auths) {
            document temp;
            temp << "public_key_type" << (std::string) iter.first
                 << "weight_type" << iter.second;
            account_auths_arr << temp;
        }
        array key_auths_arr;
        for (auto &iter : auth.key_auths) {
            document temp;
            temp << "public_key_type" << (std::string) iter.first
                 << "weight_type" << iter.second;
            key_auths_arr << temp;
        }

        document authority_doc;
        authority_doc << "master" << auth.master;
        format_value(authority_doc, "weight_threshold", auth.weight_threshold);
        authority_doc << "account_auths" << account_auths_arr;
        authority_doc << "key_auths" << key_auths_arr;

        return authority_doc;
    }

    void format_authority(document &doc, const std::string &name, const authority &auth) {
        doc << name << format_authority(auth);
    }

    std::string to_string(const signature_type& signature) {
        std::string retVal;

        for (auto& iter : signature) {
            retVal += iter;
        }
        return retVal;
    }

    void format_chain_properties(document& doc, const chain_properties& props) {
        format_value(doc, "account_creation_fee", props.account_creation_fee);
        format_value(doc, "maximum_block_size", props.maximum_block_size);
    }

    /////////////////////////////////////////////////

    operation_writer::operation_writer() {
    }

    auto operation_writer::operator()(const vote_operation& op) -> result_type {
        result_type body;

        format_value(body, "author", op.author);
        format_value(body, "permlink", op.permlink);
        format_value(body, "voter", op.voter);
        format_value(body, "weight", op.weight);

        return body;
    }

    auto operation_writer::operator()(const content_operation& op) -> result_type {
        result_type body;

        format_value(body, "parent_author", op.parent_author);
        format_value(body, "parent_permlink", op.parent_permlink);
        format_value(body, "author", op.author);
        format_value(body, "permlink", op.permlink);
        format_value(body, "title", op.title);
        format_value(body, "body", op.body);
        format_value(body, "json_metadata", op.json_metadata);
        format_value(body, "extensions", op.extensions);
        return body;
    }

    auto operation_writer::operator()(const transfer_operation& op) -> result_type {
        result_type body;

        format_value(body, "from", op.from);
        format_value(body, "to", op.to);
        format_value(body, "amount", op.amount);
        format_value(body, "memo", op.memo);

        return body;
    }

    auto operation_writer::operator()(const transfer_to_vesting_operation& op) -> result_type {
        result_type body;

        format_value(body, "from", op.from);
        format_value(body, "to", op.to);
        format_value(body, "amount", op.amount);

        return body;
    }

    auto operation_writer::operator()(const withdraw_vesting_operation& op) -> result_type {
        result_type body;

        format_value(body, "account", op.account);
        format_value(body, "vesting_shares", op.vesting_shares);

        return body;
    }

    auto operation_writer::operator()(const account_update_operation& op) -> result_type {
        result_type body;

        if (op.master) {
            format_authority(body, "master", *op.master);
        }

        document regular_owner_doc;
        if (op.regular) {
            format_authority(body, "regular", *op.regular);
        }

        document active_owner_doc;
        if (op.active) {
            format_authority(body, "active", *op.active);
        }

        format_value(body, "account", op.account);
        format_value(body, "json_metadata", op.json_metadata);
        format_value(body, "memo_key", (std::string)op.memo_key);

        return body;
    }

    auto operation_writer::operator()(const witness_update_operation& op) -> result_type {
        result_type body;

        format_value(body, "owner", op.owner);
        format_value(body, "fee", op.fee);
        format_value(body, "url", op.url);
        format_value(body, "block_signing_key", (std::string)op.block_signing_key);

        return body;
    }

    auto operation_writer::operator()(const account_witness_vote_operation& op) -> result_type {
        result_type body;

        format_value(body, "account", op.account);
        format_value(body, "witness", op.witness);
        format_value(body, "approve", (op.approve ? std::string("true") : std::string("false")));

        return body;
    }

    auto operation_writer::operator()(const account_witness_proxy_operation& op) -> result_type {
        result_type body;

        format_value(body, "account", op.account);
        format_value(body, "proxy", op.proxy);

        return body;
    }

    auto operation_writer::operator()(const delete_content_operation& op) -> result_type {
        result_type body;

        format_value(body, "author", op.author);
        format_value(body, "permlink", op.permlink);

        return body;
    }

    auto operation_writer::operator()(const custom_operation& op) -> result_type {
        result_type body;

        format_value(body, "id", op.id);
        format_value(body, "json", op.json);

        return body;
    }

    auto operation_writer::operator()(const set_withdraw_vesting_route_operation& op) -> result_type {
        result_type body;

        format_value(body, "from_account", op.from_account);
        format_value(body, "to_account", op.to_account);
        format_value(body, "percent", op.percent);
        format_value(body, "auto_vest", op.auto_vest);

        return body;
    }

    auto operation_writer::operator()(const request_account_recovery_operation& op) -> result_type {
        result_type body;

        format_value(body, "recovery_account", op.recovery_account);
        format_value(body, "account_to_recover", op.account_to_recover);
        format_authority(body, "new_master_authority", op.new_master_authority);

        return body;
    }

    auto operation_writer::operator()(const recover_account_operation& op) -> result_type {
        result_type body;

        format_value(body, "account_to_recover", op.account_to_recover);
        format_authority(body, "new_master_authority", op.new_master_authority);
        format_authority(body, "recent_master_authority", op.recent_master_authority);

        return body;
    }

    auto operation_writer::operator()(const change_recovery_account_operation& op) -> result_type {
        result_type body;

        format_value(body, "account_to_recover", op.account_to_recover);
        format_value(body, "new_recovery_account", op.new_recovery_account);

        return body;
    }

    auto operation_writer::operator()(const escrow_transfer_operation& op) -> result_type {
        result_type body;

        format_value(body, "from", op.from);
        format_value(body, "to", op.to);
        format_value(body, "agent", op.agent);
        format_value(body, "escrow_id", op.escrow_id);

        format_value(body, "token_amount ", op.token_amount);
        format_value(body, "fee", op.fee);
        format_value(body, "json_metadata", op.json_metadata);

        return body;
    }

    auto operation_writer::operator()(const escrow_dispute_operation& op) -> result_type {
        result_type body;

        format_value(body, "from", op.from);
        format_value(body, "to", op.to);
        format_value(body, "agent", op.agent);
        format_value(body, "who", op.who);
        format_value(body, "escrow_id", op.escrow_id);

        return body;
    }

    auto operation_writer::operator()(const escrow_release_operation& op) -> result_type {
        result_type body;

        format_value(body, "from", op.from);
        format_value(body, "to", op.to);
        format_value(body, "agent", op.agent);
        format_value(body, "who", op.who);
        format_value(body, "receiver", op.receiver);
        format_value(body, "escrow_id", op.escrow_id);

        format_value(body, "token_amount", op.token_amount);

        return body;
    }

    auto operation_writer::operator()(const escrow_approve_operation& op) -> result_type {
        result_type body;

        format_value(body, "from", op.from);
        format_value(body, "to", op.to);
        format_value(body, "agent", op.agent);
        format_value(body, "who", op.who);
        format_value(body, "escrow_id", op.escrow_id);
        format_value(body, "approve", op.approve);

        return body;
    }

//
    auto operation_writer::operator()(const delegate_vesting_shares_operation& op) -> result_type {
        result_type body;

        return body;
    }
    auto operation_writer::operator()(const account_create_operation& op) -> result_type {
        result_type body;

        return body;
    }
    auto operation_writer::operator()(const account_metadata_operation& op) -> result_type {
        result_type body;

        return body;
    }
    auto operation_writer::operator()(const proposal_create_operation& op) -> result_type {
        result_type body;

        return body;
    }
    auto operation_writer::operator()(const proposal_update_operation& op) -> result_type {
        result_type body;

        return body;
    }
    auto operation_writer::operator()(const proposal_delete_operation& op) -> result_type {
        result_type body;

        return body;
    }

    auto operation_writer::operator()(const author_reward_operation& op) -> result_type {
        result_type body;

        format_value(body, "author", op.author);
        format_value(body, "permlink", op.permlink);
        format_value(body, "token_payout", op.token_payout);
        format_value(body, "vesting_payout", op.vesting_payout);

        return body;
    }

    auto operation_writer::operator()(const curation_reward_operation& op) -> result_type {
        result_type body;

        format_value(body, "curator", op.curator);
        format_value(body, "reward", op.reward);
        format_value(body, "content_author", op.content_author);
        format_value(body, "content_permlink", op.content_permlink);

        return body;
    }

    auto operation_writer::operator()(const content_reward_operation& op) -> result_type {
        result_type body;

        format_value(body, "author", op.author);
        format_value(body, "permlink", op.permlink);
        format_value(body, "payout", op.payout);

        return body;
    }

    auto operation_writer::operator()(const fill_vesting_withdraw_operation& op) -> result_type {
        result_type body;

        format_value(body, "from_account", op.from_account);
        format_value(body, "to_account", op.to_account);
        format_value(body, "withdrawn", op.withdrawn);
        format_value(body, "deposited", op.deposited);

        return body;
    }

    auto operation_writer::operator()(const shutdown_witness_operation& op) -> result_type {
        result_type body;

        format_value(body, "owner", op.owner);

        return body;
    }

    auto operation_writer::operator()(const hardfork_operation& op) -> result_type {
        result_type body;

        format_value(body, "hardfork_id", op.hardfork_id);

        return body;
    }

    auto operation_writer::operator()(const content_payout_update_operation& op) -> result_type {
        result_type body;

        format_value(body, "author", op.author);
        format_value(body, "permlink", op.permlink);

        return body;
    }

    auto operation_writer::operator()(const content_benefactor_reward_operation& op) -> result_type {
        result_type body;

        format_value(body, "benefactor", op.benefactor);
        format_value(body, "author", op.author);
        format_value(body, "permlink", op.permlink);
        format_value(body, "reward", op.reward);

        return body;
    }

    auto operation_writer::operator()(const return_vesting_delegation_operation& op) -> result_type {
        result_type body;

        return body;
    }

    auto operation_writer::operator()(const chain_properties_update_operation& op) -> result_type {
        result_type body;
        format_value(body, "owner", op.owner);
        format_chain_properties(body, op.props);
        return body;
    }

}}}
