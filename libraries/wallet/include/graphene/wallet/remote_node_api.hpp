#pragma once

#include <graphene/plugins/database_api/plugin.hpp>
#include <graphene/plugins/database_api/forward.hpp>
#include <graphene/plugins/database_api/state.hpp>
#include <graphene/plugins/operation_history/applied_operation.hpp>
#include <fc/api.hpp>
#include <graphene/plugins/network_broadcast_api/network_broadcast_api_plugin.hpp>

#include <graphene/api/account_api_object.hpp>
#include <graphene/plugins/witness_api/plugin.hpp>

namespace graphene { namespace wallet {

using std::vector;
using fc::variant;
using fc::optional;

using namespace chain;
using namespace plugins;
//using namespace plugins::condenser_api;
using namespace plugins::database_api;
using namespace plugins::network_broadcast_api;
using namespace graphene::api;
using namespace plugins::witness_api;

/**
 * This is a dummy class exists only to provide method signature information to fc::api, not to execute calls.
 * Class is used by wallet to send formatted API calls to database_api plugin on remote node.
 */
struct remote_database_api {
    optional< database_api::signed_block > get_block( uint32_t );
    optional< block_header > get_block_header( uint32_t );
    fc::variant_object get_config();
    database_api::dynamic_global_property_object get_dynamic_global_properties();
    chain_properties get_chain_properties();
    hardfork_version get_hardfork_version();
    database_api::scheduled_hardfork get_next_scheduled_hardfork();
    vector< optional< graphene::api::account_api_object > > lookup_account_names( vector< account_name_type > );
    vector< account_name_type > lookup_accounts( account_name_type, uint32_t );

    uint64_t get_account_count();
    vector< database_api::master_authority_history_api_object > get_master_history( account_name_type );
    optional< database_api::account_recovery_request_api_object > get_recovery_request( account_name_type );
    optional< database_api::escrow_api_object > get_escrow( account_name_type, uint32_t );
    vector< database_api::withdraw_vesting_route_api_object > get_withdraw_routes( account_name_type, database_api::withdraw_route_type );
    string get_transaction_hex( signed_transaction );
    set< public_key_type > get_required_signatures( signed_transaction, flat_set< public_key_type > );
    set< public_key_type > get_potential_signatures( signed_transaction );
    bool verify_authority( signed_transaction );
    bool verify_account_authority( string, flat_set< public_key_type > );
    vector< graphene::api::account_api_object > get_accounts( vector< account_name_type > );
    database_api::database_info get_database_info();
    std::vector<proposal_api_object> get_proposed_transactions(account_name_type, uint32_t, uint32_t);
};

/**
 * This is a dummy class exists only to provide method signature information to fc::api, not to execute calls.
 * Class is used by wallet to send formatted API calls to operation_history plugin on remote node.
 */
struct remote_operation_history {
    vector< graphene::plugins::operation_history::applied_operation > get_ops_in_block( uint32_t, bool only_virtual = true );
    annotated_signed_transaction get_transaction( transaction_id_type );
};

/**
 * This is a dummy class exists only to provide method signature information to fc::api, not to execute calls.
 * Class is used by wallet to send formatted API calls to operation_history plugin on remote node.
 */
struct remote_account_history {
    map<uint32_t, graphene::plugins::operation_history::applied_operation> get_account_history( account_name_type, uint64_t, uint32_t );
};

/**
 * This is a dummy class exists only to provide method signature information to fc::api, not to execute calls.
 * Class is used by wallet to send formatted API calls to network_broadcast_api plugin on remote node.
 */
struct remote_network_broadcast_api {
    void broadcast_transaction( signed_transaction );
    broadcast_transaction_synchronous_t broadcast_transaction_synchronous( signed_transaction );
    void broadcast_block( signed_block );
};

/**
 * This is a dummy class exists only to provide method signature information to fc::api, not to execute calls.
 * Class is used by wallet to send formatted API calls to account_by_key plugin on remote node.
 */
 struct remote_account_by_key {
     vector< vector< account_name_type > > get_key_references( vector< public_key_type > );
 };

/**
* This is a dummy class exists only to provide method signature information to fc::api, not to execute calls.
* Class is used by wallet to send formatted API calls to witness_api plugin on remote node.
*/
struct remote_witness_api {
    vector< account_name_type > get_active_witnesses();
    graphene::chain::witness_schedule_object get_witness_schedule();
    vector< optional< witness_api::witness_api_object > > get_witnesses( vector< witness_id_type > );
    vector< witness_api::witness_api_object > get_witnesses_by_vote( account_name_type, uint32_t );
    optional< witness_api::witness_api_object > get_witness_by_account( account_name_type );
    vector< account_name_type > lookup_witness_accounts( string, uint32_t );
    uint64_t get_witness_count();
};

} }

/**
 * Declaration of remote API formatter to database_api plugin on remote node
 */
FC_API( graphene::wallet::remote_database_api,
        (get_block)
        (get_block_header)
        (get_config)
        (get_dynamic_global_properties)
        (get_chain_properties)
        (get_hardfork_version)
        (get_next_scheduled_hardfork)
        (lookup_account_names)
        (lookup_accounts)
        (get_account_count)
        (get_master_history)
        (get_recovery_request)
        (get_escrow)
        (get_withdraw_routes)
        (get_transaction_hex)
        (get_required_signatures)
        (get_potential_signatures)
        (verify_authority)
        (verify_account_authority)
        (get_accounts)
        (get_database_info)
        (get_proposed_transactions)
)

/**
 * Declaration of remote API formatter to operation_history plugin on remote node
 */
FC_API( graphene::wallet::remote_operation_history,
        (get_ops_in_block)
        (get_transaction)
)

/**
 * Declaration of remote API formatter to account_history plugin on remote node
 */
FC_API( graphene::wallet::remote_account_history,
        (get_account_history)
)

/**
 * Declaration of remote API formatter to network_broadcast_api plugin on remote node
 */
FC_API( graphene::wallet::remote_network_broadcast_api,
        (broadcast_transaction)
        (broadcast_transaction_synchronous)
        (broadcast_block)
)

/**
 * Declaration of remote API formatter to account by key plugin on remote node
 */
FC_API( graphene::wallet::remote_account_by_key,
        (get_key_references)
)

/**
 * Declaration of remote API formatter to witness_api plugin on remote node
 */
FC_API( graphene::wallet::remote_witness_api,
        (get_active_witnesses)
        (get_witness_schedule)
        (get_witnesses)
        (get_witnesses_by_vote)
        (get_witness_count)
        (get_witness_by_account)
        (lookup_witness_accounts)
)