#pragma once

#include <graphene/wallet/remote_node_api.hpp>
#include <graphene/plugins/private_message/private_message_plugin.hpp>
#include <graphene/plugins/account_history/history_object.hpp>

#include <graphene/utilities/key_conversion.hpp>

#include <fc/macros.hpp>
#include <fc/real128.hpp>
#include <fc/crypto/base58.hpp>
#include <fc/api.hpp>

namespace graphene { namespace wallet {

        using namespace std;

        using namespace graphene::utilities;
        using namespace graphene::protocol;
        using namespace graphene::plugins::private_message;

        typedef uint16_t transaction_handle_type;

        /// NS DNS helper constants
        const uint32_t NS_DEFAULT_TTL = 28800;           // 8 hours in seconds
        const uint32_t NS_MAX_TXT_LENGTH = 256;          // Max TXT record length per NS standard
        const uint32_t NS_SHA256_HEX_LENGTH = 64;        // SHA256 hash length in hex characters

        /**
         * NS record - represents a single DNS record tuple [type, value]
         */
        struct ns_record {
            string type;   // "A" or "TXT"
            string value;  // IPv4 address or TXT content like "ssl=<hash>"
        };

        /**
         * Options for creating NS metadata
         */
        struct ns_metadata_options {
            vector<string> a_records;     // List of IPv4 addresses
            optional<string> ssl_hash;    // SHA256 hash for SSL TXT record
            uint32_t ttl = NS_DEFAULT_TTL; // TTL in seconds
        };

        /**
         * Summary of NS data extracted from account metadata
         */
        struct ns_summary {
            vector<string> a_records;     // List of IPv4 addresses
            optional<string> ssl_hash;    // SSL certificate hash if present
            uint32_t ttl = 0;             // TTL value (0 if not set)
            bool has_ns_data = false;     // Whether any NS data was found
        };

        /**
         * Result of NS metadata validation
         */
        struct ns_validation_result {
            bool is_valid = true;
            vector<string> errors;
        };

        struct approval_delta {
            vector<string> active_approvals_to_add;
            vector<string> active_approvals_to_remove;
            vector<string> master_approvals_to_add;
            vector<string> master_approvals_to_remove;
            vector<string> regular_approvals_to_add;
            vector<string> regular_approvals_to_remove;
            vector<string> key_approvals_to_add;
            vector<string> key_approvals_to_remove;
        };

        struct memo_data {

            static optional<memo_data> from_string( string str ) {
                try {
                    if( str.size() > sizeof(memo_data) && str[0] == '#') {
                        auto data = fc::from_base58( str.substr(1) );
                        auto m = fc::raw::unpack<memo_data>( data );
                        FC_ASSERT( string(m) == str );
                        return m;
                    }
                } catch ( ... ) {}
                return optional<memo_data>();
            }

            public_key_type from;
            public_key_type to;
            uint64_t        nonce = 0;
            uint32_t        check = 0;
            vector<char>    encrypted;

            operator string()const {
                auto data = fc::raw::pack(*this);
                auto base58 = fc::to_base58( data );
                return '#'+base58;
            }
        };

        struct brain_key_info {
            string               brain_priv_key;
            public_key_type      pub_key;
            string               wif_priv_key;
        };

        struct wallet_data {
            vector<char>              cipher_keys; /** encrypted keys */
            string                    ws_server = "ws://localhost:8090";
        };

        struct signed_block_with_info: public signed_block {
            signed_block_with_info(const signed_block& block);
            signed_block_with_info(const signed_block_with_info& block) = default;

            block_id_type block_id;
            public_key_type signing_key;
            vector<transaction_id_type> transaction_ids;
        };

        enum authority_type {
            master,
            active,
            regular
        };

        namespace detail {
            class wallet_api_impl;
        }

/**
 * This wallet assumes it is connected to the database server with a high-bandwidth, low-latency connection and
 * performs minimal caching. This API could be provided locally to be used by a web interface.
 */
        class wallet_api {
        public:
            wallet_api( const wallet_data& initial_data, const graphene::protocol::chain_id_type& _chain_id, fc::api_connection& con );
            virtual ~wallet_api();

            bool copy_wallet_file( string destination_filename );


            /** Returns a list of all commands supported by the wallet API.
             *
             * This lists each command, along with its arguments and return types.
             * For more detailed help on a single command, use \c get_help()
             *
             * @returns a multi-line string suitable for displaying on a terminal
             */
            string                              help()const;

            /**
             * Returns info about the current state of the blockchain
             */
            variant                             info() const;

            /**
             * Returns info about database objects
             */
            variant_object                      database_info() const;

            /** Returns info such as client version, git version of <graphene/fc, version of boost, openssl.
             * @returns compile time info and client and dependencies versions
             */
            variant_object                      about() const;


            /**
             * @ingroup Transaction Builder API
             */
            transaction_handle_type begin_builder_transaction();

            /**
             * @ingroup Transaction Builder API
             */
            void add_operation_to_builder_transaction(transaction_handle_type handle, const operation& op);

            /**
             * @ingroup Transaction Builder API
             */
            void add_operation_copy_to_builder_transaction(
                transaction_handle_type src_handle, transaction_handle_type dst_handle, uint32_t op_index
            );

            /**
             * @ingroup Transaction Builder API
             */
            void replace_operation_in_builder_transaction(
                transaction_handle_type handle, unsigned op_index, const operation& op
            );

            /**
             * @ingroup Transaction Builder API
             */
            transaction preview_builder_transaction(transaction_handle_type handle);

            /**
             * @ingroup Transaction Builder API
             */
            signed_transaction sign_builder_transaction(transaction_handle_type handle, bool broadcast = true);

            /**
             * @ingroup Transaction Builder API
             */
            signed_transaction propose_builder_transaction(
                transaction_handle_type handle,
                std::string author,
                std::string title,
                std::string memo,
                time_point_sec expiration = time_point::now() + fc::minutes(1),
                time_point_sec review_period_time = time_point::min(),
                bool broadcast = true
            );

            /**
             * @ingroup Transaction Builder API
             */
            void remove_builder_transaction(transaction_handle_type handle);

            /**
             * Approve or disapprove a proposal.
             *
             * @param author The author of the proposal
             * @param title The proposal to modify.
             * @param delta Members contain approvals to create or remove. In JSON you can leave empty members undefined.
             * @param broadcast true if you wish to broadcast the transaction
             * @return the signed version of the transaction
             */
            signed_transaction approve_proposal(
                const std::string& author,
                const std::string& title,
                const approval_delta& delta,
                bool broadcast /* = false */
            );

            /**
             * Returns proposals for the account
             */
             std::vector<database_api::proposal_api_object> get_proposed_transactions(
                 std::string account, uint32_t from, uint32_t limit
            );

            /** Returns the information about a block
             *
             * @param num Block num
             *
             * @returns Public block data on the blockchain
             */
            optional<signed_block_with_info> get_block(uint32_t num);

            /** Returns sequence of operations included/generated in a specified block
             *
             * @param block_num Block height of specified block
             * @param only_virtual Whether to only return virtual operations
             */
            vector<graphene::plugins::operation_history::applied_operation> get_ops_in_block( uint32_t block_num, bool only_virtual = true );

            /**
             * Returns the list of witnesses producing blocks in the current round (21 Blocks)
             */
            vector< account_name_type > get_active_witnesses()const;

            /**
             * Returns vesting withdraw routes for an account.
             *
             * @param account Account to query routes
             * @param type Withdraw type type [incoming, outgoing, all]
             */
            vector< database_api::withdraw_vesting_route_api_object > get_withdraw_routes( string account, database_api::withdraw_route_type type = database_api::all )const;

            /**
             *  Gets the account information for all accounts for which this wallet has a private key
             */
            vector< graphene::api::account_api_object > list_my_accounts();

            /** Lists all accounts registered in the blockchain.
             * This returns a list of all account names and their account ids, sorted by account name.
             *
             * Use the \c lowerbound and limit parameters to page through the list. To retrieve all accounts,
             * start by setting \c lowerbound to the empty string \c "", and then each iteration, pass
             * the last account name returned as the \c lowerbound for the next \c list_accounts() call.
             *
             * @param lowerbound the name of the first account to return. If the named account does not exist,
             *                   the list will start at the account that comes after \c lowerbound
             * @param limit the maximum number of accounts to return (max: 1000)
             * @returns a list of accounts mapping account names to account ids
             */
            vector< account_name_type > list_accounts(const string& lowerbound, uint32_t limit);

            /** Returns the block chain's rapidly-changing properties.
             * The returned object contains information that changes every block interval
             * such as the head block number, the next witness, etc.
             * @see \c get_global_properties() for less-frequently changing properties
             * @returns the dynamic global properties
             */
            database_api::dynamic_global_property_object get_dynamic_global_properties() const;

            /** Returns information about the given account.
             *
             * @param account_name the name of the account to provide information about
             * @returns the public account data stored in the blockchain
             */
            graphene::api::account_api_object get_account( string account_name ) const;

            /** Returns the current wallet filename.
             *
             * This is the filename that will be used when automatically saving the wallet.
             *
             * @see set_wallet_filename()
             * @return the wallet filename
             */
            string                            get_wallet_filename() const;

            /**
             * Get the WIF private key corresponding to a public key. The
             * private key must already be in the wallet.
             */
            string                            get_private_key( public_key_type pubkey )const;

            /**
             *  @param account  - the name of the account to retrieve key for
             *  @param role     - active | master | regular | memo
             *  @param password - the password to be used at key generation
             *  @return public key corresponding to generated private key, and private key in WIF format.
             */
            pair<public_key_type,string> get_private_key_from_password(string account, string role, string password) const;


            /**
             * Returns transaction by ID.
             */
            annotated_signed_transaction get_transaction( transaction_id_type trx_id )const;

            /** Checks whether the wallet has just been created and has not yet had a password set.
             *
             * Calling \c set_password will transition the wallet to the locked state.
             * @return true if the wallet is new
             * @ingroup Wallet Management
             */
            bool    is_new()const;

            /** Checks whether the wallet is locked (is unable to use its private keys).
             *
             * This state can be changed by calling \c lock() or \c unlock().
             * @return true if the wallet is locked
             * @ingroup Wallet Management
             */
            bool    is_locked()const;

            /** Locks the wallet immediately.
             * @ingroup Wallet Management
             */
            void    lock();

            /** Unlocks the wallet.
             *
             * The wallet remain unlocked until the \c lock is called
             * or the program exits.
             * @param password the password previously set with \c set_password()
             * @ingroup Wallet Management
             */
            void    unlock(string password);

            /** Sets a new password on the wallet.
             *
             * The wallet must be either 'new' or 'unlocked' to
             * execute this command.
             * @ingroup Wallet Management
             */
            void    set_password(string password);

            /** Dumps all private keys owned by the wallet.
             *
             * The keys are printed in WIF format. You can import these keys into another wallet
             * using \c import_key()
             * @returns a map containing the private keys, indexed by their public key
             */
            map<public_key_type, string> list_keys();

            /** Returns detailed help on a single API command.
             * @param method the name of the API command you want help with
             * @returns a multi-line string suitable for displaying on a terminal
             */
            string gethelp(const string& method)const;

            /** Loads a specified Graphene wallet.
             *
             * The current wallet is closed before the new wallet is loaded.
             *
             * @warning This does not change the filename that will be used for future
             * wallet writes, so this may cause you to overwrite your original
             * wallet unless you also call \c set_wallet_filename()
             *
             * @param wallet_filename the filename of the wallet JSON file to load.
             *                        If \c wallet_filename is empty, it reloads the
             *                        existing wallet file
             * @returns true if the specified wallet is loaded
             */
            bool    load_wallet_file(string wallet_filename = "");

            /** Saves the current wallet to the given filename.
             *
             * @warning This does not change the wallet filename that will be used for future
             * writes, so think of this function as 'Save a Copy As...' instead of
             * 'Save As...'. Use \c set_wallet_filename() to make the filename
             * persist.
             * @param wallet_filename the filename of the new wallet JSON file to create
             *                        or overwrite. If \c wallet_filename is empty,
             *                        save to the current filename.
             */
            void    save_wallet_file(string wallet_filename = "");

            /**
             * Quits wallet.
             */
            void    quit();

            /** Sets the wallet filename used for future writes.
             *
             * This does not trigger a save, it only changes the default filename
             * that will be used the next time a save is triggered.
             *
             * @param wallet_filename the new filename to use for future saves
             */
            void    set_wallet_filename(string wallet_filename);

            /** Suggests a safe brain key to use for creating your account.
             * \c create_account_with_brain_key() requires you to specify a 'brain key',
             * a long passphrase that provides enough entropy to generate cyrptographic
             * keys. This function will suggest a suitably random string that should
             * be easy to write down (and, with effort, memorize).
             * @returns a suggested brain_key
             */
            brain_key_info suggest_brain_key()const;

            /** Converts a signed_transaction in JSON form to its binary representation.
             *
             * TODO: I don't see a broadcast_transaction() function, do we need one?
             *
             * @param tx the transaction to serialize
             * @returns the binary form of the transaction. It will not be hex encoded,
             *          this returns a raw string that may have null characters embedded
             *          in it
             */
            string serialize_transaction(signed_transaction tx) const;

            /** Imports a WIF Private Key into the wallet to be used to sign transactions by an account.
             *
             * example: import_key 5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3
             *
             * @param wif_key the WIF Private Key to import
             */
            bool import_key( string wif_key );

            /** Transforms a brain key to reduce the chance of errors when re-entering the key from memory.
             *
             * This takes a user-supplied brain key and normalizes it into the form used
             * for generating private keys. In particular, this upper-cases all ASCII characters
             * and collapses multiple spaces into one.
             * @param s the brain key as supplied by the user
             * @returns the brain key in its normalized form
             */
            string normalize_brain_key(string s) const;

            /**
             *  This method will genrate new master, active, regular and memo keys for the new account which
             *  will be controlable by this wallet. There is a fee associated with account creation
             *  that is paid by the creator. The current account creation fee can be found with the
             *  'info' wallet command.
             *
             *  These accounts are created with combination of TOKEN and delegated SHARES
             *
             *  @param creator The account creating the new account
             *  @param tokens_fee The amount of the fee to be paid with TOKEN
             *  @param delegated_vests The amount of the fee to be paid with delegation
             *  @param new_account_name The name of the new account
             *  @param json_metadata JSON Metadata associated with the new account
             *  @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction create_account(
                string creator, asset tokens_fee, asset delegated_vests, string new_account_name, string json_metadata, bool broadcast);

            /**
             * This method is used by faucets to create new accounts for other users which must
             * provide their desired keys. The resulting account may not be controllable by this
             * wallet. There is a fee associated with account creation that is paid by the creator.
             * The current account creation fee can be found with the 'info' wallet command.
             *
             * These accounts are created with combination of TOKEN and delegated SHARES
             *
             * @param creator The account creating the new account
             * @param tokens_fee The amount of the fee to be paid with TOKEN
             * @param delegated_vests The amount of the fee to be paid with delegation
             * @param newname The name of the new account
             * @param json_metadata JSON Metadata associated with the new account
             * @param master public master key of the new account
             * @param active public active key of the new account
             * @param regular public regular key of the new account
             * @param memo public memo key of the new account
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction create_account_with_keys(
                string creator,
                asset tokens_fee,
                asset delegated_vests,
                string newname,
                string json_metadata,
                public_key_type master,
                public_key_type active,
                public_key_type regular,
                public_key_type memo,
                bool broadcast) const;

            /**
             * This method updates the keys of an existing account.
             *
             * @param accountname The name of the account
             * @param json_metadata New JSON Metadata to be associated with the account
             * @param master New public master key for the account
             * @param active New public active key for the account
             * @param regular New public regular key for the account
             * @param memo New public memo key for the account
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction update_account( string accountname,
                                                         string json_metadata,
                                                         public_key_type master,
                                                         public_key_type active,
                                                         public_key_type regular,
                                                         public_key_type memo,
                                                         bool broadcast )const;

            /**
             * This method updates the key of an authority for an exisiting account.
             * Warning: You can create impossible authorities using this method. The method
             * will fail if you create an impossible master authority, but will allow impossible
             * active and regular authorities.
             *
             * @param account_name The name of the account whose authority you wish to update
             * @param type The authority type. e.g. master, active, or regular
             * @param key The public key to add to the authority
             * @param weight The weight the key should have in the authority. A weight of 0 indicates the removal of the key.
             * @param broadcast true if you wish to broadcast the transaction.
             */
            annotated_signed_transaction update_account_auth_key( string account_name, authority_type type, public_key_type key, weight_type weight, bool broadcast );

            /**
             * This method updates the account of an authority for an exisiting account.
             * Warning: You can create impossible authorities using this method. The method
             * will fail if you create an impossible master authority, but will allow impossible
             * active and regular authorities.
             *
             * @param account_name The name of the account whose authority you wish to update
             * @param type The authority type. e.g. master, active, or regular
             * @param auth_account The account to add the the authority
             * @param weight The weight the account should have in the authority. A weight of 0 indicates the removal of the account.
             * @param broadcast true if you wish to broadcast the transaction.
             */
            annotated_signed_transaction update_account_auth_account( string account_name, authority_type type, string auth_account, weight_type weight, bool broadcast );

            /**
             * This method updates the weight threshold of an authority for an account.
             * Warning: You can create impossible authorities using this method as well
             * as implicitly met authorities. The method will fail if you create an implicitly
             * true authority and if you create an impossible master authoroty, but will allow
             * impossible active and regular authorities.
             *
             * @param account_name The name of the account whose authority you wish to update
             * @param type The authority type. e.g. master, active, or regular
             * @param threshold The weight threshold required for the authority to be met
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction update_account_auth_threshold( string account_name, authority_type type, uint32_t threshold, bool broadcast );

            /**
             * This method updates the account JSON metadata
             *
             * @param account_name The name of the account you wish to update
             * @param json_metadata The new JSON metadata for the account. This overrides existing metadata
             * @param broadcast ture if you wish to broadcast the transaction
             */
            annotated_signed_transaction update_account_meta(string account_name, string json_metadata, bool broadcast);

            /**
             * This method updates the memo key of an account
             *
             * @param account_name The name of the account you wish to update
             * @param key The new memo public key
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction update_account_memo_key( string account_name, public_key_type key, bool broadcast );

            /**
             * This method delegates SHARES from one account to another.
             *
             * @param delegator The name of the account delegating SHARES
             * @param delegatee The name of the account receiving SHARES
             * @param vesting_shares The amount of SHARES to delegate
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction delegate_vesting_shares(string delegator, string delegatee, asset vesting_shares, bool broadcast);


            /**
             *  This method is used to convert a JSON transaction to its transaction ID.
             */
            transaction_id_type get_transaction_id( const signed_transaction& trx )const { return trx.id(); }

            /** Lists all witnesses registered in the blockchain.
             * This returns a list of all account names that own witnesses, and the associated witness id,
             * sorted by name. This lists witnesses whether they are currently voted in or not.
             *
             * Use the \c lowerbound and limit parameters to page through the list. To retrieve all witnesss,
             * start by setting \c lowerbound to the empty string \c "", and then each iteration, pass
             * the last witness name returned as the \c lowerbound for the next \c list_witnesss() call.
             *
             * @param lowerbound the name of the first witness to return. If the named witness does not exist,
             *                   the list will start at the witness that comes after \c lowerbound
             * @param limit the maximum number of witnesss to return (max: 1000)
             * @returns a list of witnesss mapping witness names to witness ids
             */
            vector< account_name_type > list_witnesses(const string& lowerbound, uint32_t limit);

            /** Returns information about the given witness.
             * @param owner_account the name or id of the witness account owner, or the id of the witness
             * @returns the information about the witness stored in the block chain
             */
            optional< witness_api::witness_api_object > get_witness(string owner_account);

            /**
             * Update a witness object owned by the given account.
             *
             * @param witness_name The name of the witness account.
             * @param url A URL containing some information about the witness. The empty string makes it remain the same.
             * @param block_signing_key The new block signing public key. The empty string disables block production.
             * @param broadcast true if you wish to broadcast the transaction.
             */
            annotated_signed_transaction update_witness(
                string witness_name,
                string url,
                public_key_type block_signing_key,
                bool broadcast = false
            );

            /**
             * Vote for the chain properties
             *
             * @param witness_name The name of the witness account.
             * @param props The chain properties the witness is voting on.
             * @param broadcast true if you wish to broadcast the transaction.
             */
            annotated_signed_transaction update_chain_properties(
                string witness_name,
                const chain_properties& props,
                bool broadcast = false
            );

            /**
             * Vote for the versioned chain properties
             *
             * @param witness_name The name of the witness account.
             * @param props The chain properties the witness is voting on.
             * @param broadcast true if you wish to broadcast the transaction.
             */
            annotated_signed_transaction versioned_update_chain_properties(
                string witness_name,
                const chain_properties& props,
                bool broadcast = false
            );

            /** Set the voting proxy for an account.
             *
             * If a user does not wish to take an active part in voting, they can choose
             * to allow another account to vote their stake.
             *
             * Setting a vote proxy does not remove your previous votes from the blockchain,
             * they remain there but are ignored. If you later null out your vote proxy,
             * your previous votes will take effect again.
             *
             * This setting can be changed at any time.
             *
             * @param account_to_modify the name or id of the account to update
             * @param proxy the name of account that should proxy to, or empty string to have no proxy
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction set_voting_proxy(string account_to_modify,
                                                          string proxy,
                                                          bool broadcast = false);

            /**
             * Vote for a witness to become a block producer. By default an account has not voted
             * positively or negatively for a witness. The account can either vote for with positively
             * votes or against with negative votes. The vote will remain until updated with another
             * vote. Vote strength is determined by the accounts vesting shares.
             *
             * @param account_to_vote_with The account voting for a witness
             * @param witness_to_vote_for The witness that is being voted for
             * @param approve true if the account is voting for the account to be able to be a block produce
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction vote_for_witness(string account_to_vote_with,
                                                          string witness_to_vote_for,
                                                          bool approve = true,
                                                          bool broadcast = false);

            /**
             * Transfer funds from one account to another.
             *
             * @param from The account the funds are coming from
             * @param to The account the funds are going to
             * @param amount The funds being transferred. i.e. "100.000 TOKEN"
             * @param memo A memo for the transactionm, encrypted with the to account's public memo key
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction transfer(string from, string to, asset amount, string memo, bool broadcast = false);

            /**
             * Transfer funds from one account to another using escrow.
             *
             * @param from The account the funds are coming from
             * @param to The account the funds are going to
             * @param agent The account acting as the agent in case of dispute
             * @param escrow_id A unique id for the escrow transfer. (from, escrow_id) must be a unique pair
             * @param token_amount The amount of TOKEN to transfer
             * @param fee The fee paid to the agent
             * @param ratification_deadline The deadline for 'to' and 'agent' to approve the escrow transfer
             * @param escrow_expiration The expiration of the escrow transfer, after which either party can claim the funds
             * @param json_metadata JSON encoded meta data
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction escrow_transfer(
                    string from,
                    string to,
                    string agent,
                    uint32_t escrow_id,
                    asset token_amount,
                    asset fee,
                    time_point_sec ratification_deadline,
                    time_point_sec escrow_expiration,
                    string json_metadata,
                    bool broadcast = false
            );

            /**
             * Approve a proposed escrow transfer. Funds cannot be released until after approval. This is in lieu of requiring
             * multi-sig on escrow_transfer
             *
             * @param from The account that funded the escrow
             * @param to The destination of the escrow
             * @param agent The account acting as the agent in case of dispute
             * @param who The account approving the escrow transfer (either 'to' or 'agent)
             * @param escrow_id A unique id for the escrow transfer
             * @param approve true to approve the escrow transfer, otherwise cancels it and refunds 'from'
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction escrow_approve(
                    string from,
                    string to,
                    string agent,
                    string who,
                    uint32_t escrow_id,
                    bool approve,
                    bool broadcast = false
            );

            /**
             * Raise a dispute on the escrow transfer before it expires
             *
             * @param from The account that funded the escrow
             * @param to The destination of the escrow
             * @param agent The account acting as the agent in case of dispute
             * @param who The account raising the dispute (either 'from' or 'to')
             * @param escrow_id A unique id for the escrow transfer
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction escrow_dispute(
                    string from,
                    string to,
                    string agent,
                    string who,
                    uint32_t escrow_id,
                    bool broadcast = false
            );

            /**
             * Release funds help in escrow
             *
             * @param from The account that funded the escrow
             * @param to The account the funds are originally going to
             * @param agent The account acting as the agent in case of dispute
             * @param who The account authorizing the release
             * @param receiver The account that will receive funds being released
             * @param escrow_id A unique id for the escrow transfer
             * @param token_amount The amount of TOKEN that will be released
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction escrow_release(
                    string from,
                    string to,
                    string agent,
                    string who,
                    string receiver,
                    uint32_t escrow_id,
                    asset token_amount,
                    bool broadcast = false
            );

            /**
             * Transfer TOKEN into a vesting fund represented by vesting shares (SHARES). SHARES are required to vesting
             * for a minimum of one coin year and can be withdrawn once a week over a two year withdraw period.
             * SHARES are protected against dilution up until 90% of TOKEN is vesting.
             *
             * @param from The account the TOKEN is coming from
             * @param to The account getting the SHARES
             * @param amount The amount of TOKEN to vest i.e. "100.00 TOKEN"
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction transfer_to_vesting(string from, string to, asset amount, bool broadcast = false);

            /**
             * Set up a vesting withdraw request. The request is fulfilled once a week over the next two year (104 weeks).
             *
             * @param from The account the SHARES are withdrawn from
             * @param vesting_shares The amount of SHARES to withdraw over the next two years. Each week (amount/104) shares are
             *    withdrawn and deposited back as TOKEN. i.e. "10.000000 SHARES"
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction withdraw_vesting( string from, asset vesting_shares, bool broadcast = false );

            /**
             * Set up a vesting withdraw route. When vesting shares are withdrawn, they will be routed to these accounts
             * based on the specified weights.
             *
             * @param from The account the SHARES are withdrawn from.
             * @param to   The account receiving either SHARES or TOKEN.
             * @param percent The percent of the withdraw to go to the 'to' account. This is denoted in hundreths of a percent.
             *    i.e. 100 is 1% and 10000 is 100%. This value must be between 1 and 100000
             * @param auto_vest Set to true if the from account should receive the SHARES as SHARES, or false if it should receive
             *    them as TOKEN.
             * @param broadcast true if you wish to broadcast the transaction.
             */
            annotated_signed_transaction set_withdraw_vesting_route( string from, string to, uint16_t percent, bool auto_vest, bool broadcast = false );

            /** Signs a transaction.
             *
             * Given a fully-formed transaction that is only lacking signatures, this signs
             * the transaction with the necessary keys and optionally broadcasts the transaction
             * @param tx the unsigned transaction
             * @param broadcast true if you wish to broadcast the transaction
             * @return the signed version of the transaction
             */
            annotated_signed_transaction sign_transaction(signed_transaction tx, bool broadcast = false);

            /** Returns an uninitialized object representing a given blockchain operation.
             *
             * This returns a default-initialized object of the given type; it can be used
             * during early development of the wallet when we don't yet have custom commands for
             * creating all of the operations the blockchain supports.
             *
             * Any operation the blockchain supports can be created using the transaction builder's
             * \c add_operation_to_builder_transaction() , but to do that from the CLI you need to
             * know what the JSON form of the operation looks like. This will give you a template
             * you can fill in. It's better than nothing.
             *
             * @param operation_type the type of operation to return, must be one of the
             *                       operations defined in `graphene/chain/operations.hpp`
             *                       (e.g., "global_parameters_update_operation")
             * @return a default-constructed operation of the given type
             */
            operation get_prototype_operation(string operation_type);

            /**
             *  Post or update a content.
             *
             *  @param author the name of the account authoring the content
             *  @param permlink the accountwide unique permlink for the content
             *  @param parent_author can be null if this is a top level content
             *  @param parent_permlink can be null if this is a top level content
             *  @param title the title of the content
             *  @param body the body of the content
             *  @param json the json metadata of the content
             *  @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction post_content( string author, string permlink, string parent_author, string parent_permlink, string title, string body, int16_t curation_percent, string json, bool broadcast );

            /**
             * Vote on a content to be paid TOKEN
             *
             * @param voter The account voting
             * @param author The author of the content to be voted on
             * @param permlink The permlink of the content to be voted on. (author, permlink) is a unique pair
             * @param weight The weight [-100,100] of the vote
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction vote( string voter, string author, string permlink, int16_t weight, bool broadcast );

            /**
             * Sets the amount of time in the future until a transaction expires.
             */
            void set_transaction_expiration(uint32_t seconds);

            /**
             * Create an account recovery request as a recover account. The syntax for this command contains a serialized authority object
             * so there is an example below on how to pass in the authority.
             *
             * request_account_recovery "your_account" "account_to_recover" {"weight_threshold": 1,"account_auths": [], "key_auths": [["new_public_key",1]]} true
             *
             * @param recovery_account The name of your account
             * @param account_to_recover The name of the account you are trying to recover
             * @param new_authority The new master authority for the recovered account. This should be given to you by the holder of the compromised or lost account.
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction request_account_recovery( string recovery_account, string account_to_recover, authority new_authority, bool broadcast );

            /**
             * Recover your account using a recovery request created by your recovery account. The syntax for this commain contains a serialized
             * authority object, so there is an example below on how to pass in the authority.
             *
             * recover_account "your_account" {"weight_threshold": 1,"account_auths": [], "key_auths": [["old_public_key",1]]} {"weight_threshold": 1,"account_auths": [], "key_auths": [["new_public_key",1]]} true
             *
             * @param account_to_recover The name of your account
             * @param recent_authority A recent master authority on your account
             * @param new_authority The new authority that your recovery account used in the account recover request.
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction recover_account( string account_to_recover, authority recent_authority, authority new_authority, bool broadcast );

            /**
             * Change your recovery account after a 30 day delay.
             *
             * @param owner The name of your account
             * @param new_recovery_account The name of the recovery account you wish to have
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction change_recovery_account( string owner, string new_recovery_account, bool broadcast );

            vector< database_api::master_authority_history_api_object > get_master_history( string account )const;

            /**
             *  Account operations have sequence numbers from 0 to N where N is the most recent operation. This method
             *  returns operations in the range [from-limit, from]
             *
             *  @param account - account whose history will be returned
             *  @param from - the absolute sequence number, -1 means most recent, limit is the number of operations before from.
             *  @param limit - the maximum number of items that can be queried (0 to 1000], must be less than from
             */
            map< uint32_t, graphene::plugins::operation_history::applied_operation >
                get_account_history( string account, uint32_t from, uint32_t limit );


            FC_TODO(Supplement API argument description)
            /**
             *  Marks one account as following another account. Requires the regular authority of the follower.
             *
             *  @param follower
             *  @param following
             *  @param what - a set of things to follow: posts, contents, votes, ignore
             *  @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction follow(
                    const string& follower,
                    const string& following,
                    const set<string>& what,
                    const bool broadcast);


            std::map<string,std::function<string(fc::variant,const fc::variants&)>> get_result_formatters() const;

            fc::signal<void(bool)> lock_changed;
            fc::signal<void(void)> quit_command;
            std::shared_ptr<detail::wallet_api_impl> my;
            void encrypt_keys();

            /**
             * Checks memos against private keys on account and imported in wallet
             */
            void check_memo( const string& memo, const graphene::api::account_api_object& account )const;

            /**
             *  Returns the encrypted memo if memo starts with '#' otherwise returns memo
             */
            string get_encrypted_memo( string from, string to, string memo );

            /**
             * Returns the decrypted memo if possible given wallet's known private keys
             */
            string decrypt_memo( string memo );

            // Private message
            vector<extended_message_object> get_inbox(
                    const std::string& to, time_point newest, uint16_t limit, std::uint64_t offset);
            vector<extended_message_object> get_outbox(
                    const std::string& from, time_point newest, uint16_t limit, std::uint64_t offset);

            message_body try_decrypt_message( const message_api_obj& mo );

            /**
             * Broadcast a custom operation.
             *
             * @param required_active_auths Accounts requiring active authority
             * @param required_regular_auths Accounts requiring regular authority
             * @param id Custom operation type identifier
             * @param json JSON data for the custom operation
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction custom(
                flat_set<string> required_active_auths,
                flat_set<string> required_regular_auths,
                string id,
                string json,
                bool broadcast = false
            );

            /**
             * Delete a content.
             *
             * @param author The author of the content
             * @param permlink The permlink of the content
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction delete_content(
                string author,
                string permlink,
                bool broadcast = false
            );

            /**
             * Delete a proposal.
             *
             * @param author The author of the proposal
             * @param title The title of the proposal
             * @param requester The account requesting deletion
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction delete_proposal(
                string author,
                string title,
                string requester,
                bool broadcast = false
            );

            /**
             * Create a committee worker request.
             *
             * @param creator The account creating the request
             * @param url URL with information about the request
             * @param worker The worker account that will receive payment
             * @param required_amount_min Minimum amount requested
             * @param required_amount_max Maximum amount requested
             * @param duration Duration of the request in seconds
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction committee_worker_create_request(
                string creator,
                string url,
                string worker,
                asset required_amount_min,
                asset required_amount_max,
                uint32_t duration,
                bool broadcast = false
            );

            /**
             * Cancel a committee worker request.
             *
             * @param creator The account that created the request
             * @param request_id The ID of the request to cancel
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction committee_worker_cancel_request(
                string creator,
                uint32_t request_id,
                bool broadcast = false
            );

            /**
             * Vote on a committee worker request.
             *
             * @param voter The account voting
             * @param request_id The ID of the request to vote on
             * @param vote_percent The vote percentage (-10000 to 10000)
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction committee_vote_request(
                string voter,
                uint32_t request_id,
                int16_t vote_percent,
                bool broadcast = false
            );

            /**
             * Create an invite with a balance.
             *
             * @param creator The account creating the invite
             * @param balance The balance to put in the invite
             * @param invite_key The public key for the invite
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction create_invite(
                string creator,
                asset balance,
                public_key_type invite_key,
                bool broadcast = false
            );

            /**
             * Claim an invite balance.
             *
             * @param initiator The account initiating the claim
             * @param receiver The account receiving the balance
             * @param invite_secret The secret key of the invite
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction claim_invite_balance(
                string initiator,
                string receiver,
                string invite_secret,
                bool broadcast = false
            );

            /**
             * Register a new account using an invite.
             *
             * @param initiator The account initiating the registration
             * @param new_account_name The name of the new account
             * @param invite_secret The secret key of the invite
             * @param new_account_key The public key for the new account
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction invite_registration(
                string initiator,
                string new_account_name,
                string invite_secret,
                public_key_type new_account_key,
                bool broadcast = false
            );

            /**
             * Use invite balance to transfer to vesting.
             *
             * @param initiator The account initiating the use
             * @param receiver The account receiving the vesting
             * @param invite_secret The secret key of the invite
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction use_invite_balance(
                string initiator,
                string receiver,
                string invite_secret,
                bool broadcast = false
            );

            /**
             * Award an account (energy-based reward).
             *
             * @param initiator The account giving the award
             * @param receiver The account receiving the award
             * @param energy The energy amount to use (0-10000)
             * @param custom_sequence Custom sequence number
             * @param memo Memo for the award
             * @param beneficiaries List of beneficiaries with weights
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction award(
                string initiator,
                string receiver,
                uint16_t energy,
                uint64_t custom_sequence,
                string memo,
                vector<beneficiary_route_type> beneficiaries,
                bool broadcast = false
            );

            /**
             * Fixed award an account (fixed amount reward).
             *
             * @param initiator The account giving the award
             * @param receiver The account receiving the award
             * @param reward_amount The fixed reward amount
             * @param max_energy Maximum energy to use
             * @param custom_sequence Custom sequence number
             * @param memo Memo for the award
             * @param beneficiaries List of beneficiaries with weights
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction fixed_award(
                string initiator,
                string receiver,
                asset reward_amount,
                uint16_t max_energy,
                uint64_t custom_sequence,
                string memo,
                vector<beneficiary_route_type> beneficiaries,
                bool broadcast = false
            );

            /**
             * Set up a paid subscription.
             *
             * @param account The account setting up the subscription
             * @param url URL with subscription information
             * @param levels Number of subscription levels
             * @param amount Cost per level
             * @param period Subscription period in seconds
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction set_paid_subscription(
                string account,
                string url,
                uint16_t levels,
                asset amount,
                uint16_t period,
                bool broadcast = false
            );

            /**
             * Subscribe to a paid subscription.
             *
             * @param subscriber The subscribing account
             * @param account The account with the subscription
             * @param level Subscription level
             * @param amount Payment amount
             * @param period Subscription period
             * @param auto_renewal Enable auto renewal
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction paid_subscribe(
                string subscriber,
                string account,
                uint16_t level,
                asset amount,
                uint16_t period,
                bool auto_renewal,
                bool broadcast = false
            );

            /**
             * Set an account up for sale.
             *
             * @param account The account being sold
             * @param account_seller The seller account
             * @param account_offer_price The sale price
             * @param account_on_sale Whether the account is on sale
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction set_account_price(
                string account,
                string account_seller,
                asset account_offer_price,
                bool account_on_sale,
                bool broadcast = false
            );

            /**
             * Set subaccount creation for sale.
             *
             * @param account The parent account
             * @param subaccount_seller The seller account
             * @param subaccount_offer_price The price for subaccount creation
             * @param subaccount_on_sale Whether subaccounts are on sale
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction set_subaccount_price(
                string account,
                string subaccount_seller,
                asset subaccount_offer_price,
                bool subaccount_on_sale,
                bool broadcast = false
            );

            /**
             * Buy an account.
             *
             * @param buyer The buying account
             * @param account The account being bought
             * @param account_offer_price The purchase price
             * @param account_authorities_key New public key for the account
             * @param tokens_to_shares Amount to convert to shares
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction buy_account(
                string buyer,
                string account,
                asset account_offer_price,
                public_key_type account_authorities_key,
                asset tokens_to_shares,
                bool broadcast = false
            );

            /**
             * Set an account for sale to a specific target buyer.
             *
             * @param account The account being sold
             * @param account_seller The seller account
             * @param target_buyer The target buyer account
             * @param account_offer_price The sale price
             * @param account_on_sale Whether the account is on sale
             * @param broadcast true if you wish to broadcast the transaction
             */
            annotated_signed_transaction target_account_sale(
                string account,
                string account_seller,
                string target_buyer,
                asset account_offer_price,
                bool account_on_sale,
                bool broadcast = false
            );

            // ========== VIZ DNS Nameserver Helpers ==========

            /**
             * Validates an IPv4 address string.
             *
             * @param ipv4 The IPv4 address string to validate (e.g., "188.120.231.153")
             * @return true if the address is valid, false otherwise
             */
            bool ns_validate_ipv4(const string& ipv4) const;

            /**
             * Validates a SHA256 hash string (64 hex characters).
             *
             * @param hash The SHA256 hash string to validate
             * @return true if the hash is valid, false otherwise
             */
            bool ns_validate_sha256_hash(const string& hash) const;

            /**
             * Validates a TTL value (must be positive integer).
             *
             * @param ttl The TTL value to validate
             * @return true if the TTL is valid, false otherwise
             */
            bool ns_validate_ttl(uint32_t ttl) const;

            /**
             * Validates an SSL TXT record format (ssl=<hash>).
             *
             * @param txt The TXT record string to validate
             * @return true if the format is valid, false otherwise
             */
            bool ns_validate_ssl_txt_record(const string& txt) const;

            /**
             * Performs complete validation of NS metadata.
             *
             * @param options The NS metadata options to validate
             * @return ns_validation_result with isValid flag and errors array
             */
            ns_validation_result ns_validate_metadata(const ns_metadata_options& options) const;

            /**
             * Creates NS metadata JSON string from options.
             * This only creates the ns and ttl fields for merging with existing metadata.
             *
             * @param options The NS metadata options (a_records, ssl_hash, ttl)
             * @return JSON object containing ns array and ttl value
             */
            variant ns_create_metadata(const ns_metadata_options& options) const;

            /**
             * Gets complete NS summary from an account's metadata.
             *
             * @param account_name The account name to query
             * @return ns_summary containing a_records, ssl_hash, ttl, and has_ns_data flag
             */
            ns_summary ns_get_summary(const string& account_name) const;

            /**
             * Extracts A records (IPv4 addresses) from an account's metadata.
             *
             * @param account_name The account name to query
             * @return vector of IPv4 address strings
             */
            vector<string> ns_extract_a_records(const string& account_name) const;

            /**
             * Extracts SSL hash from an account's metadata TXT records.
             *
             * @param account_name The account name to query
             * @return optional SSL hash string (empty if not found)
             */
            optional<string> ns_extract_ssl_hash(const string& account_name) const;

            /**
             * Extracts TTL value from an account's metadata.
             *
             * @param account_name The account name to query
             * @return TTL value in seconds (0 if not set)
             */
            uint32_t ns_extract_ttl(const string& account_name) const;

            /**
             * Sets NS records for an account. Merges NS data with existing metadata,
             * preserving other fields like profile information.
             *
             * @param account_name The account to update
             * @param options The NS metadata options (a_records, ssl_hash, ttl)
             * @param broadcast true if you wish to broadcast the transaction
             * @return the signed transaction
             */
            annotated_signed_transaction ns_set_records(
                string account_name,
                const ns_metadata_options& options,
                bool broadcast = false
            );

            /**
             * Removes NS records from an account's metadata.
             * Preserves other metadata fields like profile information.
             *
             * @param account_name The account to update
             * @param broadcast true if you wish to broadcast the transaction
             * @return the signed transaction
             */
            annotated_signed_transaction ns_remove_records(
                string account_name,
                bool broadcast = false
            );
        };

        struct plain_keys {
            fc::sha512                  checksum;
            map<public_key_type,string> keys;
        };

    } }

FC_REFLECT((graphene::wallet::wallet_data), (cipher_keys)(ws_server))

FC_REFLECT_DERIVED((graphene::wallet::signed_block_with_info), ((graphene::chain::signed_block)),
        (block_id)(signing_key)(transaction_ids))

FC_REFLECT( (graphene::wallet::brain_key_info), (brain_priv_key)(wif_priv_key) (pub_key))

FC_REFLECT( (graphene::wallet::plain_keys), (checksum)(keys) )

FC_REFLECT_ENUM( graphene::wallet::authority_type, (master)(active)(regular) )

FC_REFLECT( (graphene::wallet::ns_record), (type)(value) )
FC_REFLECT( (graphene::wallet::ns_metadata_options), (a_records)(ssl_hash)(ttl) )
FC_REFLECT( (graphene::wallet::ns_summary), (a_records)(ssl_hash)(ttl)(has_ns_data) )
FC_REFLECT( (graphene::wallet::ns_validation_result), (is_valid)(errors) )

FC_API( graphene::wallet::wallet_api,
/// wallet api
        (help)(gethelp)
                (about)(is_new)(is_locked)(lock)(unlock)(set_password)
                (load_wallet_file)(save_wallet_file)
                (quit)

                /// key api
                (import_key)
                (suggest_brain_key)
                (list_keys)
                (get_private_key)
                (get_private_key_from_password)
                (normalize_brain_key)

                /// query api
                (info)
                (database_info)
                (list_my_accounts)
                (list_accounts)
                (list_witnesses)
                (get_witness)
                (get_account)
                (get_block)
                (get_ops_in_block)
                (get_account_history)
                (get_withdraw_routes)

                /// transaction api
                (create_account)
                (create_account_with_keys)
                (update_account)
                (update_account_auth_key)
                (update_account_auth_account)
                (update_account_auth_threshold)
                (update_account_meta)
                (update_account_memo_key)
                (delegate_vesting_shares)
                (update_witness)
                (update_chain_properties)
                (versioned_update_chain_properties)
                (set_voting_proxy)
                (vote_for_witness)
                //(follow)
                (transfer)
                (escrow_transfer)
                (escrow_approve)
                (escrow_dispute)
                (escrow_release)
                (transfer_to_vesting)
                (withdraw_vesting)
                (set_withdraw_vesting_route)
                (post_content)
                (vote)
                (set_transaction_expiration)
                (request_account_recovery)
                (recover_account)
                (change_recovery_account)
                (get_master_history)
                (get_encrypted_memo)
                (decrypt_memo)

                /// new operations
                (custom)
                (delete_content)
                (delete_proposal)
                (committee_worker_create_request)
                (committee_worker_cancel_request)
                (committee_vote_request)
                (create_invite)
                (claim_invite_balance)
                (invite_registration)
                (use_invite_balance)
                (award)
                (fixed_award)
                (set_paid_subscription)
                (paid_subscribe)
                (set_account_price)
                (set_subaccount_price)
                (buy_account)
                (target_account_sale)

                /// NS DNS helpers
                (ns_validate_ipv4)
                (ns_validate_sha256_hash)
                (ns_validate_ttl)
                (ns_validate_ssl_txt_record)
                (ns_validate_metadata)
                (ns_create_metadata)
                (ns_get_summary)
                (ns_extract_a_records)
                (ns_extract_ssl_hash)
                (ns_extract_ttl)
                (ns_set_records)
                (ns_remove_records)

                /// helper api
                (begin_builder_transaction)
                (add_operation_to_builder_transaction)
                (add_operation_copy_to_builder_transaction)
                (replace_operation_in_builder_transaction)
                (preview_builder_transaction)
                (sign_builder_transaction)
                (propose_builder_transaction)
                (remove_builder_transaction)
                (approve_proposal)
                (get_proposed_transactions)
                (get_prototype_operation)
                (serialize_transaction)
                (sign_transaction)

                (get_active_witnesses)
                (get_transaction)
                (get_inbox)
                (get_outbox)
)

FC_REFLECT( (graphene::wallet::memo_data), (from)(to)(nonce)(check)(encrypted) )
FC_REFLECT(
    (graphene::wallet::approval_delta),
    (active_approvals_to_add)(active_approvals_to_remove)
    (master_approvals_to_add)(master_approvals_to_remove)
    (regular_approvals_to_add)(regular_approvals_to_remove)
    (key_approvals_to_add)(key_approvals_to_remove) )
