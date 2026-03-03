#include <graphene/plugins/account_history/plugin.hpp>
#include <graphene/plugins/account_history/history_object.hpp>

#include <graphene/plugins/operation_history/history_object.hpp>

#include <graphene/chain/operation_notification.hpp>

#include <boost/algorithm/string.hpp>
#define NAMESPACE_PREFIX "graphene::protocol::"

#define CHECK_ARG_SIZE(s) \
   FC_ASSERT( args.args->size() == s, "Expected #s argument(s), was ${n}", ("n", args.args->size()) );

namespace graphene { namespace plugins { namespace account_history {

    struct operation_visitor_filter;
    void operation_get_impacted_accounts(const operation &op, flat_set<graphene::chain::account_name_type> &result);

using namespace graphene::protocol;
using namespace graphene::chain;
//
template<typename T>
T dejsonify(const string &s) {
    return fc::json::from_string(s).as<T>();
}

#define DEFAULT_VALUE_VECTOR(value) default_value({fc::json::to_string(value)}, fc::json::to_string(value))
#define LOAD_VALUE_SET(options, name, container, type) \
if( options.count(name) ) { \
    const std::vector<std::string>& ops = options[name].as<std::vector<std::string>>(); \
    std::transform(ops.begin(), ops.end(), std::inserter(container, container.end()), &dejsonify<type>); \
}
//

    struct operation_visitor final {
        operation_visitor(
            graphene::chain::database& db,
            const graphene::chain::operation_notification& op_note,
            std::string op_account)
            : database(db),
              note(op_note),
              account(op_account){
        }

        using result_type = void;

        graphene::chain::database& database;
        const graphene::chain::operation_notification& note;
        std::string account;

        template<typename Op>
        void operator()(Op &&) const {
            const auto& idx = database.get_index<account_history_index>().indices().get<by_account>();

            auto itr = idx.lower_bound(std::make_tuple(account, uint32_t(-1)));
            uint32_t sequence = 0;
            if (itr != idx.end() && itr->account == account) {
                sequence = itr->sequence + 1;
            }

            database.create<account_history_object>([&](account_history_object& history) {
                history.account = account;
                history.block = note.block;
                history.sequence = sequence;
                history.op = operation_history::operation_id_type(note.db_id);
            });

            const auto& idx_range = database.get_index<account_range_index>().indices().get<range_by_account>();
            auto itr_range = idx_range.find(account);
            if(itr_range == idx_range.end()){
                database.create<account_range_object>([&](account_range_object& range) {
                    range.account = account;
                    range.start_sequence = sequence;
                    range.end_sequence = sequence;
                });
            }
            else{
                database.modify(*itr_range, [&](account_range_object& range) {
                    range.end_sequence = sequence;
                });
            }
        }
    };

    struct plugin::plugin_impl final {
    public:
        plugin_impl( )
            : database(appbase::app().get_plugin<chain::plugin>().db()) {
        }

        ~plugin_impl() = default;

        void purge_old_history(){
            uint32_t head_block = database.head_block_num();
            //ilog("account_history: purge_old_history START ${c} <= ${h}", ("c",history_count_blocks)("h", head_block));
            if (history_count_blocks <= head_block) {
                uint32_t need_block = head_block - history_count_blocks;
                const auto& idx = database.get_index<account_history_index>().indices().get<by_block>();
                auto it = idx.begin();
                while (it != idx.end() && it->block <= need_block) {
                    //change account range object if needed
                    const auto& idx_range = database.get_index<account_range_index>().indices().get<range_by_account>();
                    auto itr_range = idx_range.find(it->account);
                    if(itr_range != idx_range.end()){
                        if(itr_range->start_sequence <= it->sequence){
                            //remove account range object if it have one sequence in range
                            if(itr_range->start_sequence == itr_range->end_sequence){
                                database.remove(*itr_range);
                            }
                            else{
                                //edit start sequence
                                database.modify(*itr_range, [&](account_range_object& range) {
                                    range.start_sequence++;
                                });
                            }
                        }
                    }

                    auto next_it = it;
                    ++next_it;
                    //ilog("account_history: REMOVE ${c}", ("c",it->block));
                    database.remove(*it);
                    it = next_it;
                }
            }
        }

        void on_operation(const graphene::chain::operation_notification& note) {
            if (!note.stored_in_db) {
                return;
            }

            fc::flat_set<graphene::chain::account_name_type> impacted;
            operation_get_impacted_accounts(note.op, impacted);

            for (const auto& item : impacted) {
                if (0 == tracked_accounts.size()) {
                    note.op.visit(operation_visitor(database, note, item));
                }
                else{
                    auto itr = tracked_accounts.lower_bound(item);
                    if (itr != tracked_accounts.end() && itr->first <= item && item <= itr->second) {
                        note.op.visit(operation_visitor(database, note, item));
                    }
                }
            }
        }

        std::map<uint32_t, applied_operation> get_account_history(
            std::string account,
            uint32_t from,
            uint32_t limit
        ) {
            //new account range index to prevent search unavailable "FROM" in account_history_index
            const auto& idx_range = database.get_index<account_range_index>().indices().get<range_by_account>();
            auto itr_range = idx_range.find(account);
            FC_ASSERT(itr_range != idx_range.end(), "Account not found in history index, it may have been purged since the last  ${b} blocks are stored in the history", ("b", history_count_blocks));
            if(from != UINT32_MAX){
                FC_ASSERT(from >= itr_range->start_sequence, "From is less than account history start sequence ${s}", ("s", itr_range->start_sequence));
                FC_ASSERT(from <= itr_range->end_sequence, "From is greater than account history end sequence ${s}", ("s", itr_range->end_sequence));
            }

            //changed to 1000 after long jsonrpc requests, that use read lock
            FC_ASSERT(limit <= 1000, "Limit of ${l} is greater than maxmimum allowed (1000)", ("l", limit));
            FC_ASSERT(limit > 0, "Limit must be greater than 0");
            //   idump((account)(from)(limit));
            const auto& idx = database.get_index<account_history_index>().indices().get<by_account>();

            // Handle from = -1 (UINT32_MAX) case: start from the latest entry
            uint32_t actual_from = from;
            if (from == UINT32_MAX) {
                actual_from = itr_range->end_sequence;
            }

            auto itr = idx.lower_bound(std::make_tuple(account, actual_from));
            if (itr == idx.end() || itr->account != account) {
                return std::map<uint32_t, applied_operation>();
            }
            //   if( itr != idx.end() ) idump((*itr));
            // Calculate end sequence, clamping to start_sequence to avoid underflow
            uint32_t end_sequence = 0;
            if (itr->sequence >= limit) {
                end_sequence = itr->sequence - limit;
            }
            // Ensure we don't go below start_sequence
            if (end_sequence < itr_range->start_sequence && itr_range->start_sequence > 0) {
                end_sequence = itr_range->start_sequence - 1;
            }
            auto end = idx.upper_bound(std::make_tuple(account, end_sequence));
            //   if( end != idx.end() ) idump((*end));

            std::map<uint32_t, applied_operation> result;
            for (; itr != end; ++itr) {
                result[itr->sequence] = database.get(itr->op);
            }
            return result;
        }

        fc::flat_map<std::string, std::string> tracked_accounts;
        graphene::chain::database& database;
        uint32_t history_count_blocks = UINT32_MAX;
    };

    DEFINE_API(plugin, get_account_history) {
        CHECK_ARG_SIZE(3)
        auto account = args.args->at(0).as<std::string>();
        auto from = args.args->at(1).as<uint32_t>();
        auto limit = args.args->at(2).as<uint32_t>();

        return pimpl->database.with_weak_read_lock([&]() {
            return pimpl->get_account_history(account, from, limit);
        });
    }

    struct get_impacted_account_visitor final {
        fc::flat_set<graphene::chain::account_name_type>& impacted;

        get_impacted_account_visitor(fc::flat_set<graphene::chain::account_name_type>& impact)
            : impacted(impact) {
        }

        using result_type = void;

        template<typename T>
        void operator()(const T& op) {
            op.get_required_regular_authorities(impacted);
            op.get_required_active_authorities(impacted);
            op.get_required_master_authorities(impacted);
        }

        void operator()(const account_create_operation& op) {
            impacted.insert(op.new_account_name);
            impacted.insert(op.creator);
        }

        void operator()(const account_update_operation& op) {
            impacted.insert(op.account);
        }

        void operator()(const account_metadata_operation& op) {
            impacted.insert(op.account);
        }

        void operator()(const content_operation& op) {
            impacted.insert(op.author);
            if (op.parent_author.size()) {
                impacted.insert(op.parent_author);
            }
        }

        void operator()(const delete_content_operation& op) {
            impacted.insert(op.author);
        }

        void operator()(const vote_operation& op) {
            impacted.insert(op.voter);
            impacted.insert(op.author);
        }

        void operator()(const author_reward_operation& op) {
            impacted.insert(op.author);
        }

        void operator()(const curation_reward_operation& op) {
            impacted.insert(op.curator);
        }

        void operator()(const receive_award_operation& op) {
            impacted.insert(op.receiver);
        }

        void operator()(const benefactor_award_operation& op) {
            impacted.insert(op.benefactor);
        }

        void operator()(const transfer_operation& op) {
            impacted.insert(op.from);
            impacted.insert(op.to);
        }

        void operator()(const transfer_to_vesting_operation& op) {
            impacted.insert(op.from);

            if (op.to != graphene::chain::account_name_type() && op.to != op.from) {
                impacted.insert(op.to);
            }
        }

        void operator()(const withdraw_vesting_operation& op) {
            impacted.insert(op.account);
        }

        void operator()(const witness_update_operation& op) {
            impacted.insert(op.owner);
        }

        void operator()(const account_witness_vote_operation& op) {
            impacted.insert(op.account);
            impacted.insert(op.witness);
        }

        void operator()(const account_witness_proxy_operation& op) {
            impacted.insert(op.account);
            impacted.insert(op.proxy);
        }

        void operator()(const fill_vesting_withdraw_operation& op) {
            impacted.insert(op.from_account);
            impacted.insert(op.to_account);
        }

        void operator()(const shutdown_witness_operation& op) {
            impacted.insert(op.owner);
        }

        void operator()(const request_account_recovery_operation& op) {
            impacted.insert(op.account_to_recover);
        }

        void operator()(const recover_account_operation& op) {
            impacted.insert(op.account_to_recover);
        }

        void operator()(const change_recovery_account_operation& op) {
            impacted.insert(op.account_to_recover);
        }

        void operator()(const escrow_transfer_operation& op) {
            impacted.insert(op.from);
            impacted.insert(op.to);
            impacted.insert(op.agent);
        }

        void operator()(const escrow_approve_operation& op) {
            impacted.insert(op.from);
            impacted.insert(op.to);
            impacted.insert(op.agent);
        }

        void operator()(const escrow_dispute_operation& op) {
            impacted.insert(op.from);
            impacted.insert(op.to);
            impacted.insert(op.agent);
        }

        void operator()(const escrow_release_operation& op) {
            impacted.insert(op.from);
            impacted.insert(op.to);
            impacted.insert(op.agent);
        }

        void operator()(const content_benefactor_reward_operation& op) {
            impacted.insert(op.benefactor);
            impacted.insert(op.author);
        }

        void operator()(const delegate_vesting_shares_operation& op) {
            impacted.insert(op.delegator);
            impacted.insert(op.delegatee);
        }

        void operator()(const return_vesting_delegation_operation& op) {
            impacted.insert(op.account);
        }

        void operator()(const proposal_create_operation& op) {
            impacted.insert(op.author);
        }

        void operator()(const proposal_update_operation& op) {
            impacted.insert(op.active_approvals_to_add.begin(), op.active_approvals_to_add.end());
            impacted.insert(op.master_approvals_to_add.begin(), op.master_approvals_to_add.end());
            impacted.insert(op.regular_approvals_to_add.begin(), op.regular_approvals_to_add.end());
            impacted.insert(op.active_approvals_to_remove.begin(), op.active_approvals_to_remove.end());
            impacted.insert(op.master_approvals_to_remove.begin(), op.master_approvals_to_remove.end());
            impacted.insert(op.regular_approvals_to_remove.begin(), op.regular_approvals_to_remove.end());
        }

        void operator()(const proposal_delete_operation& op) {
            impacted.insert(op.requester);
        }

        void operator()(const committee_pay_request_operation& op) {
            impacted.insert(op.worker);
        }

        void operator()(const witness_reward_operation& op) {
            impacted.insert(op.witness);
        }

        void operator()(const set_paid_subscription_operation& op) {
            impacted.insert(op.account);
        }

        void operator()(const paid_subscribe_operation& op) {
            impacted.insert(op.subscriber);
        }

        void operator()(const paid_subscription_action_operation& op) {
            impacted.insert(op.subscriber);
            impacted.insert(op.account);
        }

        void operator()(const cancel_paid_subscription_operation& op) {
            impacted.insert(op.subscriber);
            impacted.insert(op.account);
        }

        void operator()(const set_account_price_operation& op) {
            impacted.insert(op.account);
            impacted.insert(op.account_seller);
        }

        void operator()(const target_account_sale_operation& op) {
            impacted.insert(op.account);
            impacted.insert(op.account_seller);
            impacted.insert(op.target_buyer);
        }

        void operator()(const set_subaccount_price_operation& op) {
            impacted.insert(op.account);
            impacted.insert(op.subaccount_seller);
        }

        void operator()(const buy_account_operation& op) {
            impacted.insert(op.account);
            impacted.insert(op.buyer);
        }

        void operator()(const account_sale_operation& op) {
            impacted.insert(op.buyer);
            impacted.insert(op.seller);
            impacted.insert(op.account);
        }

        void operator()(const bid_operation& op) {
            impacted.insert(op.account);
            impacted.insert(op.bidder);
        }

        void operator()(const outbid_operation& op) {
            impacted.insert(op.account);
            impacted.insert(op.bidder);
        }

        void operator()(const create_invite_operation& op) {
            impacted.insert(op.creator);
        }

        void operator()(const claim_invite_balance_operation& op) {
            impacted.insert(op.receiver);
        }

        void operator()(const invite_registration_operation& op) {
            impacted.insert(op.new_account_name);
        }

        void operator()(const use_invite_balance_operation& op) {
            impacted.insert(op.receiver);
        }

        void operator()(const expire_escrow_ratification_operation& op) {
            impacted.insert(op.from);
            impacted.insert(op.to);
            impacted.insert(op.agent);
        }
        //void operator()( const operation& op ){}
    };

    void operation_get_impacted_accounts(
        const operation& op, fc::flat_set<graphene::chain::account_name_type>& result
    ) {
        get_impacted_account_visitor vtor = get_impacted_account_visitor(result);
        op.visit(vtor);
    }

    void plugin::set_program_options(
        boost::program_options::options_description& cli,
        boost::program_options::options_description& cfg
    ) {
        cli.add_options()(
            "track-account-range",
            boost::program_options::value<std::vector<std::string>>()->composing()->multitoken(),
            "Defines a range of accounts to track as a json pair [\"from\",\"to\"] [from,to]. "
            "Can be specified multiple times"
        );
        cfg.add(cli);
    }

    void plugin::plugin_initialize(const boost::program_options::variables_map& options) {
        ilog("account_history plugin: plugin_initialize() begin");
        pimpl = std::make_unique<plugin_impl>();

        if (options.count("history-count-blocks")) {
            uint32_t history_count_blocks = options.at("history-count-blocks").as<uint32_t>();
            pimpl->history_count_blocks = history_count_blocks;
            pimpl->database.applied_block.connect([&](const signed_block& block){
                pimpl->purge_old_history();
            });
        } else {
            pimpl->history_count_blocks = UINT32_MAX;
        }
        ilog("account_history: history-count-blocks ${s}", ("s", pimpl->history_count_blocks));

        // this is worked, because the appbase initialize required plugins at first
        pimpl->database.pre_apply_operation.connect([&](graphene::chain::operation_notification& note){
            pimpl->on_operation(note);
        });

        graphene::chain::add_plugin_index<account_history_index>(pimpl->database);
        graphene::chain::add_plugin_index<account_range_index>(pimpl->database);

        using pairstring = std::pair<std::string, std::string>;
        LOAD_VALUE_SET(options, "track-account-range", pimpl->tracked_accounts, pairstring);

        ilog("account_history: tracked_accounts ${s}", ("s", pimpl->tracked_accounts));

        JSON_RPC_REGISTER_API(name());
        ilog("account_history plugin: plugin_initialize() end");
    }

    plugin::plugin() = default;

    plugin::~plugin() = default;

    const std::string& plugin::name() {
        static std::string name = "account_history";
        return name;
    }

    void plugin::plugin_startup() {
        ilog("account_history plugin: plugin_startup() begin");
        ilog("account_history plugin: plugin_startup() end");
    }

    void plugin::plugin_shutdown() {
    }

    fc::flat_map<std::string, std::string> plugin::tracked_accounts() const {
        return pimpl->tracked_accounts;
    }

} } } // graphene::plugins::account_history
