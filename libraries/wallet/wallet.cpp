#include <graphene/utilities/git_revision.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/utilities/words.hpp>

#include <graphene/protocol/base.hpp>
#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>
#else
#include <graphene/protocol/config.hpp>
#endif
#include <graphene/wallet/wallet.hpp>
#include <graphene/wallet/api_documentation.hpp>
#include <graphene/wallet/reflect_util.hpp>
#include <graphene/wallet/remote_node_api.hpp>
#include <graphene/plugins/follow/follow_operations.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <list>

#include <boost/version.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm_ext/erase.hpp>
#include <boost/range/algorithm/unique.hpp>
#include <boost/range/algorithm/sort.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

#include <fc/container/deque.hpp>
#include <fc/git_revision.hpp>
#include <fc/io/fstream.hpp>
#include <fc/io/json.hpp>
#include <fc/io/stdio.hpp>
#include <fc/macros.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/crypto/hex.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>
#include <fc/smart_ref_impl.hpp>

#ifndef WIN32
# include <sys/types.h>
# include <sys/stat.h>
#endif

#define BRAIN_KEY_WORD_COUNT 16

namespace graphene { namespace wallet {

        namespace detail {

            template<class T>
            optional<T> maybe_id( const string& name_or_id ) {
                if( std::isdigit( name_or_id.front() ) ) {
                    try {
                        return fc::variant(name_or_id).as<T>();
                    }
                    catch (const fc::exception&) {
                    }
                }
                return optional<T>();
            }

            string pubkey_to_shorthash( const public_key_type& key ) {
                uint32_t x = fc::sha256::hash(key)._hash[0];
                static const char hd[] = "0123456789abcdef";
                string result;

                result += hd[(x >> 0x1c) & 0x0f];
                result += hd[(x >> 0x18) & 0x0f];
                result += hd[(x >> 0x14) & 0x0f];
                result += hd[(x >> 0x10) & 0x0f];
                result += hd[(x >> 0x0c) & 0x0f];
                result += hd[(x >> 0x08) & 0x0f];
                result += hd[(x >> 0x04) & 0x0f];
                result += hd[(x        ) & 0x0f];

                return result;
            }


            fc::ecc::private_key derive_private_key( const std::string& prefix_string, int sequence_number ) {
                std::string sequence_string = std::to_string(sequence_number);
                fc::sha512 h = fc::sha512::hash(prefix_string + " " + sequence_string);
                fc::ecc::private_key derived_key = fc::ecc::private_key::regenerate(fc::sha256::hash(h));
                return derived_key;
            }

            string normalize_brain_key(string s) {
                size_t i = 0, n = s.length();
                std::string result;
                result.reserve(n);

                bool preceded_by_whitespace = false;
                bool non_empty = false;
                while (i < n) {
                    char c = s[i++];
                    switch (c) {
                        case ' ': case '\t': case '\r': case '\n': case '\v': case '\f':
                            preceded_by_whitespace = true;
                            continue;

                        case 'a':
                        case 'b':
                        case 'c':
                        case 'd':
                        case 'e':
                        case 'f':
                        case 'g':
                        case 'h':
                        case 'i':
                        case 'j':
                        case 'k':
                        case 'l':
                        case 'm':
                        case 'n':
                        case 'o':
                        case 'p':
                        case 'q':
                        case 'r':
                        case 's':
                        case 't':
                        case 'u':
                        case 'v':
                        case 'w':
                        case 'x':
                        case 'y':
                        case 'z':
                            c ^= 'a' ^ 'A';     // ASCII upper/lowercase diffs only in 1 bit
                            break;

                        default:
                            break;
                    }
                    if (preceded_by_whitespace && non_empty)
                        result.push_back(' ');
                    result.push_back(c);
                    preceded_by_whitespace = false;
                    non_empty = true;
                }
                return result;
            }

            struct op_prototype_visitor {
                typedef void result_type;

                int t = 0;
                flat_map< std::string, operation >& name2op;

                op_prototype_visitor(
                        int _t,
                        flat_map< std::string, operation >& _prototype_ops
                ):t(_t), name2op(_prototype_ops) {}

                template<typename Type>
                result_type operator()( const Type& op )const {
                    string name = fc::get_typename<Type>::name();
                    size_t p = name.rfind(':');
                    if( p != string::npos )
                        name = name.substr( p+1 );
                    name2op[ name ] = Type();
                }
            };

            class wallet_api_impl
            {
            public:
                api_documentation method_documentation;
            private:
                void enable_umask_protection() {
#ifdef __unix__
                    _old_umask = umask( S_IRWXG | S_IRWXO );
#endif
                }

                void disable_umask_protection() {
#ifdef __unix__
                    umask( _old_umask );
#endif
                }

                void init_prototype_ops() {
                    operation op;
                    for( int t=0; t<op.count(); t++ ) {
                        op.set_which( t );
                        op.visit( op_prototype_visitor(t, _prototype_ops) );
                    }
                    return;
                }

                map<transaction_handle_type, signed_transaction> _builder_transactions;

            public:
                wallet_api& self;
                wallet_api_impl( wallet_api& s, const wallet_data& initial_data, const graphene::protocol::chain_id_type& _chain_id, fc::api_connection& con ):
                    self( s ),
                    _remote_database_api( con.get_remote_api< remote_database_api >( 0, "database_api" ) ),
                    _remote_operation_history( con.get_remote_api< remote_operation_history >( 0, "operation_history" ) ),
                    _remote_account_history( con.get_remote_api< remote_account_history >( 0, "account_history" ) ),
                    _remote_social_network( con.get_remote_api< remote_social_network >( 0, "social_network" ) ),
                    _remote_network_broadcast_api( con.get_remote_api< remote_network_broadcast_api >( 0, "network_broadcast_api" ) ),
                    _remote_follow( con.get_remote_api< remote_follow >( 0, "follow" ) ),
                    _remote_private_message( con.get_remote_api< remote_private_message>( 0, "private_message" ) ),
                    _remote_account_by_key( con.get_remote_api< remote_account_by_key>( 0, "account_by_key" ) ) ,
                    _remote_witness_api( con.get_remote_api< remote_witness_api >( 0, "witness_api" ) )
                {
                    init_prototype_ops();

                    _wallet.ws_server = initial_data.ws_server;
                    chain_id = _chain_id;
                }
                virtual ~wallet_api_impl()
                {}

                void encrypt_keys() {
                    if( !is_locked() ) {
                        plain_keys data;
                        data.keys = _keys;
                        data.checksum = _checksum;
                        auto plain_txt = fc::raw::pack(data);
                        _wallet.cipher_keys = fc::aes_encrypt( data.checksum, plain_txt );
                    }
                }

                bool copy_wallet_file( string destination_filename ) {
                    fc::path src_path = get_wallet_filename();
                    if( !fc::exists( src_path ) )
                        return false;
                    fc::path dest_path = destination_filename + _wallet_filename_extension;
                    int suffix = 0;
                    while( fc::exists(dest_path) ) {
                        ++suffix;
                        dest_path = destination_filename + "-" + std::to_string( suffix ) + _wallet_filename_extension;
                    }
                    wlog( "backing up wallet ${src} to ${dest}",
                          ("src", src_path)
                                  ("dest", dest_path) );

                    fc::path dest_parent = fc::absolute(dest_path).parent_path();
                    try {
                        enable_umask_protection();
                        if( !fc::exists( dest_parent ) )
                            fc::create_directories( dest_parent );
                        fc::copy( src_path, dest_path );
                        disable_umask_protection();
                    }
                    catch(...) {
                        disable_umask_protection();
                        throw;
                    }
                    return true;
                }

                bool is_locked()const {
                    return _checksum == fc::sha512();
                }

                variant info() const {
                    auto dynamic_props = _remote_database_api->get_dynamic_global_properties();
                    auto median_props = _remote_database_api->get_chain_properties();
                    fc::mutable_variant_object result(fc::variant(dynamic_props).get_object());
                    result["witness_majority_version"] =
                        std::string(_remote_witness_api->get_witness_schedule().majority_version);
                    result["hardfork_version"] =
                        std::string(_remote_database_api->get_hardfork_version());
                    result["head_block_num"] = dynamic_props.head_block_number;
                    result["head_block_id"] = dynamic_props.head_block_id;
                    result["head_block_age"] =
                        fc::get_approximate_relative_time_string(
                            dynamic_props.time, time_point_sec(time_point::now()), " old");
                    result["participation"] =
                        (100 * dynamic_props.recent_slots_filled.popcount()) / 128.0;
                    result["account_creation_fee"] = median_props.account_creation_fee;
                    result["create_account_delegation_ratio"] = median_props.create_account_delegation_ratio;
                    result["create_account_delegation_time"] = median_props.create_account_delegation_time;
                    result["min_delegation"] = median_props.min_delegation;

                    return result;
                }

                variant_object database_info() const {
                    auto info = _remote_database_api->get_database_info();

                    auto convert = [](std::size_t value) {
                        auto gb = value / (1024 * 1024 * 1024);
                        auto mb = value / (1024 * 1024);
                        auto kb = value / (1024);

                        if (gb) {
                            return std::to_string(gb) + "G";
                        } else if (mb) {
                            return std::to_string(mb) + "M";
                        } else if (kb) {
                            return std::to_string(kb) + "K";
                        }

                        return std::to_string(value);
                    };

                    fc::mutable_variant_object result;

                    result["total_size"] = convert(info.total_size);
                    result["used_size"] = convert(info.used_size);
                    result["free_size"] = convert(info.free_size);
                    result["reserved_size"] = convert(info.reserved_size);
                    result["index_list"] = info.index_list;

                    return result;
                }

                variant_object about() const {
                    string client_version( graphene::utilities::git_revision_description );
                    const size_t pos = client_version.find( '/' );
                    if( pos != string::npos && client_version.size() > pos )
                        client_version = client_version.substr( pos + 1 );

                    fc::mutable_variant_object result;
                    result["client_version"]           = client_version;
                    result["revision"]           = graphene::utilities::git_revision_sha;
                    result["revision_age"]       = fc::get_approximate_relative_time_string( fc::time_point_sec( graphene::utilities::git_revision_unix_timestamp ) );
                    result["fc_revision"]              = fc::git_revision_sha;
                    result["fc_revision_age"]          = fc::get_approximate_relative_time_string( fc::time_point_sec( fc::git_revision_unix_timestamp ) );
                    result["compile_date"]             = "compiled on " __DATE__ " at " __TIME__;
                    result["boost_version"]            = boost::replace_all_copy(std::string(BOOST_LIB_VERSION), "_", ".");
                    result["openssl_version"]          = OPENSSL_VERSION_TEXT;

                    std::string bitness = boost::lexical_cast<std::string>(8 * sizeof(int*)) + "-bit";
#if defined(__APPLE__)
                    std::string os = "osx";
#elif defined(__linux__)
                    std::string os = "linux";
#elif defined(_MSC_VER)
      std::string os = "win32";
#else
      std::string os = "other";
#endif
                    result["build"] = os + " " + bitness;

                    try {
                        //auto v = _remote_api->get_version();
                        //result["server_blockchain_version"] = v.blockchain_version;
                        //result["server_fc_revision"] = v.fc_revision;
                    } catch( fc::exception& ) {
                        result["server"] = "could not retrieve server version information";
                    }

                    return result;
                }

                transaction_handle_type begin_builder_transaction() {
                    transaction_handle_type handle = 0;
                    if (!_builder_transactions.empty()) {
                        handle = (--_builder_transactions.end())->first + 1;
                    }
                    _builder_transactions[handle];
                    return handle;
                }

                void add_operation_to_builder_transaction(
                    transaction_handle_type handle, const operation& op
                ) {
                    FC_ASSERT(_builder_transactions.count(handle));
                    _builder_transactions[handle].operations.emplace_back(op);
                }

                void add_operation_copy_to_builder_transaction(
                    transaction_handle_type src_handle,
                    transaction_handle_type dst_handle,
                    uint32_t op_index
                ) {
                    FC_ASSERT(_builder_transactions.count(src_handle));
                    FC_ASSERT(_builder_transactions.count(dst_handle));
                    signed_transaction& trx = _builder_transactions[src_handle];
                    FC_ASSERT(op_index < trx.operations.size());
                    const auto op = trx.operations[op_index];
                    _builder_transactions[dst_handle].operations.emplace_back(op);
                }

                void replace_operation_in_builder_transaction(
                    transaction_handle_type handle,
                    uint32_t op_index,
                    const operation& new_op
                ) {
                    FC_ASSERT(_builder_transactions.count(handle));
                    signed_transaction& trx = _builder_transactions[handle];
                    FC_ASSERT(op_index < trx.operations.size());
                    trx.operations[op_index] = new_op;
                }

                transaction preview_builder_transaction(transaction_handle_type handle) {
                    FC_ASSERT(_builder_transactions.count(handle));
                    return _builder_transactions[handle];
                }

                signed_transaction sign_builder_transaction(transaction_handle_type handle, bool broadcast) {
                    FC_ASSERT(_builder_transactions.count(handle));
                    return _builder_transactions[handle] = sign_transaction(_builder_transactions[handle], broadcast);
                }

                signed_transaction propose_builder_transaction(
                    transaction_handle_type handle,
                    std::string author,
                    std::string title,
                    std::string memo,
                    time_point_sec expiration,
                    time_point_sec review_period_time,
                    bool broadcast
                ) {
                    FC_ASSERT(_builder_transactions.count(handle));
                    proposal_create_operation op;
                    op.author = author;
                    op.title = title;
                    op.memo = memo;
                    op.expiration_time = expiration;

                    // copy tx to avoid malforming if sign_transaction fails
                    signed_transaction trx = _builder_transactions[handle];
                    std::transform(
                        trx.operations.begin(), trx.operations.end(), std::back_inserter(op.proposed_operations),
                        [](const operation& op) -> operation_wrapper { return op; });

                    if (review_period_time > time_point_sec::min()) {
                        op.review_period_time = review_period_time;
                    }

                    trx.operations = {op};
                    trx = sign_transaction(trx, broadcast);
                    return _builder_transactions[handle] = trx;
                }

                void remove_builder_transaction(transaction_handle_type handle) {
                    _builder_transactions.erase(handle);
                }

                signed_transaction approve_proposal(
                    const std::string& author,
                    const std::string& title,
                    const approval_delta& delta,
                    bool broadcast)
                {
                    proposal_update_operation update_op;

                    update_op.author = author;
                    update_op.title = title;

                    for (const std::string& name : delta.active_approvals_to_add)
                        update_op.active_approvals_to_add.insert(name);
                    for (const std::string& name : delta.active_approvals_to_remove)
                        update_op.active_approvals_to_remove.insert(name);
                    for (const std::string& name : delta.master_approvals_to_add)
                        update_op.master_approvals_to_add.insert(name);
                    for (const std::string& name : delta.master_approvals_to_remove)
                        update_op.master_approvals_to_remove.insert(name);
                    for (const std::string& name : delta.regular_approvals_to_add)
                        update_op.regular_approvals_to_add.insert(name);
                    for (const std::string& name : delta.regular_approvals_to_remove)
                        update_op.regular_approvals_to_remove.insert(name);
                    for (const std::string& k : delta.key_approvals_to_add)
                        update_op.key_approvals_to_add.insert(public_key_type(k));
                    for (const std::string& k : delta.key_approvals_to_remove)
                        update_op.key_approvals_to_remove.insert(public_key_type(k));

                    signed_transaction tx;
                    tx.operations.push_back(update_op);
                    tx.validate();
                    return sign_transaction(tx, broadcast);
                }

                std::vector<database_api::proposal_api_object> get_proposed_transactions(
                    std::string account, uint32_t from, uint32_t limit
                ) {
                    return _remote_database_api->get_proposed_transactions(account, from, limit);
                }

                graphene::api::account_api_object get_account( string account_name ) const {
                    auto accounts = _remote_database_api->get_accounts( { account_name } );
                    FC_ASSERT( !accounts.empty(), "Unknown account" );
                    return accounts.front();
                }

                string get_wallet_filename() const { return _wallet_filename; }

                optional<fc::ecc::private_key> try_get_private_key(const public_key_type& id)const {
                    auto it = _keys.find(id);
                    if( it != _keys.end() )
                        return wif_to_key( it->second );
                    return optional<fc::ecc::private_key>();
                }

                fc::ecc::private_key get_private_key(const public_key_type& id)const {
                    auto has_key = try_get_private_key( id );
                    FC_ASSERT( has_key );
                    return *has_key;
                }


                fc::ecc::private_key get_private_key_for_account(const graphene::api::account_api_object& account)const {
                    vector<public_key_type> active_keys = account.active_authority.get_keys();
                    if (active_keys.size() != 1)
                        FC_THROW("Expecting a simple authority with one active key");
                    return get_private_key(active_keys.front());
                }

                // imports the private key into the wallet, and associate it in some way (?) with the
                // given account name.
                // @returns true if the key matches a current active/master/memo key for the named
                //          account, false otherwise (but it is stored either way)
                bool import_key(string wif_key) {
                    fc::optional<fc::ecc::private_key> optional_private_key = wif_to_key(wif_key);
                    if (!optional_private_key)
                        FC_THROW("Invalid private key");
                    graphene::chain::public_key_type wif_pub_key = optional_private_key->get_public_key();

                    _keys[wif_pub_key] = wif_key;
                    return true;
                }

                bool load_wallet_file(string wallet_filename = "") {
                    // TODO: Merge imported wallet with existing wallet, instead of replacing it
                    if( wallet_filename == "" )
                        wallet_filename = _wallet_filename;

                    if( ! fc::exists( wallet_filename ) )
                        return false;

                    _wallet = fc::json::from_file( wallet_filename ).as< wallet_data >();

                    return true;
                }

                void save_wallet_file(string wallet_filename = "") {
                    //
                    // Serialize in memory, then save to disk
                    //
                    // This approach lessens the risk of a partially written wallet
                    // if exceptions are thrown in serialization
                    //

                    encrypt_keys();

                    if( wallet_filename == "" )
                        wallet_filename = _wallet_filename;

                    wlog( "saving wallet to file ${fn}", ("fn", wallet_filename) );

                    string data = fc::json::to_pretty_string( _wallet );
                    try {
                        enable_umask_protection();
                        //
                        // Parentheses on the following declaration fails to compile,
                        // due to the Most Vexing Parse. Thanks, C++
                        //
                        // http://en.wikipedia.org/wiki/Most_vexing_parse
                        //
                        fc::ofstream outfile{ fc::path( wallet_filename ) };
                        outfile.write( data.c_str(), data.length() );
                        outfile.flush();
                        outfile.close();
                        disable_umask_protection();
                    } catch(...) {
                        disable_umask_protection();
                        throw;
                    }
                }

                // This function generates derived keys starting with index 0 and keeps incrementing
                // the index until it finds a key that isn't registered in the block chain. To be
                // safer, it continues checking for a few more keys to make sure there wasn't a short gap
                // caused by a failed registration or the like.
                int find_first_unused_derived_key_index(const fc::ecc::private_key& parent_key) {
                    int first_unused_index = 0;
                    int number_of_consecutive_unused_keys = 0;
                    for (int key_index = 0; ; ++key_index) {
                        fc::ecc::private_key derived_private_key = derive_private_key(key_to_wif(parent_key), key_index);
                        graphene::chain::public_key_type derived_public_key = derived_private_key.get_public_key();
                        if( _keys.find(derived_public_key) == _keys.end() ) {
                            if (number_of_consecutive_unused_keys) {
                                ++number_of_consecutive_unused_keys;
                                if (number_of_consecutive_unused_keys > 5)
                                    return first_unused_index;
                            } else {
                                first_unused_index = key_index;
                                number_of_consecutive_unused_keys = 1;
                            }
                        } else {
                            // key_index is used
                            first_unused_index = 0;
                            number_of_consecutive_unused_keys = 0;
                        }
                    }
                }

                signed_transaction create_account_with_private_key(fc::ecc::private_key master_privkey,
                                                                   string account_name,
                                                                   string creator_account_name,
                                                                   bool broadcast = false,
                                                                   bool save_wallet = true) {
                    try {
                        int active_key_index = find_first_unused_derived_key_index(master_privkey);
                        fc::ecc::private_key active_privkey = derive_private_key( key_to_wif(master_privkey), active_key_index);

                        int memo_key_index = find_first_unused_derived_key_index(active_privkey);
                        fc::ecc::private_key memo_privkey = derive_private_key( key_to_wif(active_privkey), memo_key_index);

                        graphene::chain::public_key_type master_pubkey = master_privkey.get_public_key();
                        graphene::chain::public_key_type active_pubkey = active_privkey.get_public_key();
                        graphene::chain::public_key_type memo_pubkey = memo_privkey.get_public_key();

                        account_create_operation account_create_op;

                        account_create_op.creator = creator_account_name;
                        account_create_op.new_account_name = account_name;
                        account_create_op.fee = _remote_database_api->get_chain_properties().account_creation_fee;
                        account_create_op.master = authority(1, master_pubkey, 1);
                        account_create_op.active = authority(1, active_pubkey, 1);
                        account_create_op.memo_key = memo_pubkey;
                        account_create_op.delegation = asset(0, SHARES_SYMBOL );

                        signed_transaction tx;

                        tx.operations.push_back( account_create_op );
                        tx.validate();

                        if( save_wallet )
                            save_wallet_file();
                        if( broadcast ) {
                            auto result = _remote_network_broadcast_api->broadcast_transaction_synchronous( tx );
                            FC_UNUSED(result);
                        }
                        return tx;
                    } FC_CAPTURE_AND_RETHROW( (account_name)(creator_account_name)(broadcast) ) }

                signed_transaction set_voting_proxy(string account_to_modify, string proxy, bool broadcast /* = false */) {
                    try {
                        account_witness_proxy_operation op;
                        op.account = account_to_modify;
                        op.proxy = proxy;

                        signed_transaction tx;
                        tx.operations.push_back( op );
                        tx.validate();

                        return sign_transaction( tx, broadcast );
                    } FC_CAPTURE_AND_RETHROW( (account_to_modify)(proxy)(broadcast) ) }

                optional< witness_api::witness_api_object > get_witness( string owner_account ) {
                    return _remote_witness_api->get_witness_by_account( owner_account );
                }

                void set_transaction_expiration( uint32_t tx_expiration_seconds ) {
                    FC_ASSERT( tx_expiration_seconds < CHAIN_MAX_TIME_UNTIL_EXPIRATION );
                    _tx_expiration_seconds = tx_expiration_seconds;
                }

                annotated_signed_transaction sign_transaction(signed_transaction tx, bool broadcast = false)
                {
                    flat_set< account_name_type > req_active_approvals;
                    flat_set< account_name_type > req_master_approvals;
                    flat_set< account_name_type > req_regular_approvals;
                    vector< authority > other_auths;

                    tx.get_required_authorities( req_active_approvals, req_master_approvals, req_regular_approvals, other_auths );

                    for( const auto& auth : other_auths )
                        for( const auto& a : auth.account_auths )
                            req_active_approvals.insert(a.first);

                    // std::merge lets us de-duplicate account_id's that occur in both
                    //   sets, and dump them into a vector (as required by remote_db api)
                    //   at the same time
                    vector< account_name_type > v_approving_account_names;
                    std::merge(req_active_approvals.begin(), req_active_approvals.end(),
                               req_master_approvals.begin() , req_master_approvals.end(),
                               std::back_inserter( v_approving_account_names ) );

                    for( const auto& a : req_regular_approvals )
                        v_approving_account_names.push_back(a);

                    /// TODO: fetch the accounts specified via other_auths as well.

                    auto approving_account_objects = _remote_database_api->get_accounts( v_approving_account_names );

                    /// TODO: recursively check one layer deeper in the authority tree for keys

                    FC_ASSERT( approving_account_objects.size() == v_approving_account_names.size(), "", ("aco.size:", approving_account_objects.size())("acn",v_approving_account_names.size()) );

                    flat_map< string, graphene::api::account_api_object > approving_account_lut;
                    size_t i = 0;
                    for( const optional< graphene::api::account_api_object >& approving_acct : approving_account_objects ) {
                        if( !approving_acct.valid() ) {
                            wlog( "get_required_authorities said approval of non-existing account ${name} was needed",
                                  ("name", v_approving_account_names[i]) );
                            i++;
                            continue;
                        }
                        approving_account_lut[ approving_acct->name ] = *approving_acct;
                        i++;
                    }
                    auto get_account_from_lut = [&]( const std::string& name ) -> const graphene::api::account_api_object& {
                        auto it = approving_account_lut.find( name );
                        FC_ASSERT( it != approving_account_lut.end() );
                        return it->second;
                    };

                    flat_set<public_key_type> approving_key_set;
                    for( account_name_type& acct_name : req_active_approvals ) {
                        const auto it = approving_account_lut.find( acct_name );
                        if( it == approving_account_lut.end() )
                            continue;
                        const graphene::api::account_api_object& acct = it->second;
                        vector<public_key_type> v_approving_keys = acct.active_authority.get_keys();
                        wdump((v_approving_keys));
                        for( const public_key_type& approving_key : v_approving_keys ) {
                            wdump((approving_key));
                            approving_key_set.insert( approving_key );
                        }
                    }

                    for( account_name_type& acct_name : req_regular_approvals ) {
                        const auto it = approving_account_lut.find( acct_name );
                        if( it == approving_account_lut.end() )
                            continue;
                        const graphene::api::account_api_object& acct = it->second;
                        vector<public_key_type> v_approving_keys = acct.regular_authority.get_keys();
                        wdump((v_approving_keys));
                        for( const public_key_type& approving_key : v_approving_keys )
                        {
                            wdump((approving_key));
                            approving_key_set.insert( approving_key );
                        }
                    }

                    for( const account_name_type& acct_name : req_master_approvals ) {
                        const auto it = approving_account_lut.find( acct_name );
                        if( it == approving_account_lut.end() )
                            continue;
                        const graphene::api::account_api_object& acct = it->second;
                        vector<public_key_type> v_approving_keys = acct.master_authority.get_keys();
                        for( const public_key_type& approving_key : v_approving_keys ) {
                            wdump((approving_key));
                            approving_key_set.insert( approving_key );
                        }
                    }
                    for( const authority& a : other_auths ) {
                        for( const auto& k : a.key_auths ) {
                            wdump((k.first));
                            approving_key_set.insert( k.first );
                        }
                    }

                    auto dyn_props = _remote_database_api->get_dynamic_global_properties();
                    tx.set_reference_block( dyn_props.head_block_id );
                    tx.set_expiration( dyn_props.time + fc::seconds(_tx_expiration_seconds) );
                    tx.signatures.clear();

                    //idump((_keys));
                    flat_set< public_key_type > available_keys;
                    flat_map< public_key_type, fc::ecc::private_key > available_private_keys;
                    for( const public_key_type& key : approving_key_set )
                    {
                        auto it = _keys.find(key);
                        if( it != _keys.end() )
                        {
                            fc::optional<fc::ecc::private_key> privkey = wif_to_key( it->second );
                            FC_ASSERT( privkey.valid(), "Malformed private key in _keys" );
                            available_keys.insert(key);
                            available_private_keys[key] = *privkey;
                        }
                    }

                    auto minimal_signing_keys = tx.minimize_required_signatures(
                            chain_id,
                            available_keys,
                            [&]( const string& account_name ) -> const authority&
                            { return (get_account_from_lut( account_name ).active_authority); },
                            [&]( const string& account_name ) -> const authority&
                            { return (get_account_from_lut( account_name ).master_authority); },
                            [&]( const string& account_name ) -> const authority&
                            { return (get_account_from_lut( account_name ).regular_authority); },
                            CHAIN_MAX_SIG_CHECK_DEPTH
                    );

                    for( const public_key_type& k : minimal_signing_keys ) {
                        auto it = available_private_keys.find(k);
                        FC_ASSERT( it != available_private_keys.end() );
                        tx.sign( it->second, chain_id );
                    }

                    if( broadcast ) {
                        try {
                            auto result = _remote_network_broadcast_api->broadcast_transaction_synchronous( tx );
                            annotated_signed_transaction rtrx(tx);
                            rtrx.block_num = result.block_num;
                            rtrx.transaction_num = result.trx_num;
                            return rtrx;
                        } catch (const fc::exception& e) {
                            elog("Caught exception while broadcasting tx ${id}: ${e}", ("id", tx.id().str())("e", e.to_detail_string()) );
                            throw;
                        }
                    }
                    return tx;
                }

                std::map<string,std::function<string(fc::variant,const fc::variants&)>> get_result_formatters() const {
                    std::map<string,std::function<string(fc::variant,const fc::variants&)> > m;
                    m["help"] = [](variant result, const fc::variants& a) {
                        return result.get_string();
                    };

                    m["gethelp"] = [](variant result, const fc::variants& a) {
                        return result.get_string();
                    };

                    m["list_my_accounts"] = [](variant result, const fc::variants& a ) {
                        std::stringstream out;

                        auto accounts = result.as<vector<graphene::api::account_api_object>>();
                        asset total_tokens;
                        asset total_vest(0, SHARES_SYMBOL );
                        for( const auto& a : accounts ) {
                            total_tokens += a.balance;
                            total_vest  += a.vesting_shares;
                            out << std::left << std::setw( 17 ) << std::string(a.name)
                                << std::right << std::setw(18) << fc::variant(a.balance).as_string() <<" "
                                << std::right << std::setw(26) << fc::variant(a.vesting_shares).as_string() <<"\n";
                        }
                        out << "-------------------------------------------------------------------------\n";
                        out << std::left << std::setw( 17 ) << "TOTAL"
                            << std::right << std::setw(18) << fc::variant(total_tokens).as_string() <<" "
                            << std::right << std::setw(26) << fc::variant(total_vest).as_string() <<"\n";
                        return out.str();
                    };
                    m["get_account_history"] = []( variant result, const fc::variants& a ) {
                        std::stringstream ss;
                        ss << std::left << std::setw( 5 )  << "#" << " ";
                        ss << std::left << std::setw( 10 ) << "BLOCK #" << " ";
                        ss << std::left << std::setw( 15 ) << "TRX ID" << " ";
                        ss << std::left << std::setw( 20 ) << "OPERATION" << " ";
                        ss << std::left << std::setw( 50 ) << "DETAILS" << "\n";
                        ss << "-------------------------------------------------------------------------------\n";
                        const auto& results = result.get_array();
                        for( const auto& item : results ) {
                            ss << std::left << std::setw(5) << item.get_array()[0].as_string() << " ";
                            const auto& op = item.get_array()[1].get_object();
                            ss << std::left << std::setw(10) << op["block"].as_string() << " ";
                            ss << std::left << std::setw(15) << op["trx_id"].as_string() << " ";
                            const auto& opop = op["op"].get_array();
                            ss << std::left << std::setw(20) << opop[0].as_string() << " ";
                            ss << std::left << std::setw(50) << fc::json::to_string(opop[1]) << "\n ";
                        }
                        return ss.str();
                    };
                    return m;
                }

                operation get_prototype_operation( string operation_name ) {
                    auto it = _prototype_ops.find( operation_name );
                    if( it == _prototype_ops.end() )
                        FC_THROW("Unsupported operation: \"${operation_name}\"", ("operation_name", operation_name));
                    return it->second;
                }

                string                                  _wallet_filename;
                wallet_data                             _wallet;
                graphene::protocol::chain_id_type          chain_id;

                map<public_key_type,string>             _keys;
                fc::sha512                              _checksum;
                fc::api< remote_database_api >          _remote_database_api;
                fc::api< remote_operation_history >     _remote_operation_history;
                fc::api< remote_account_history >       _remote_account_history;
                fc::api< remote_social_network >        _remote_social_network;
                fc::api< remote_network_broadcast_api>  _remote_network_broadcast_api;
                fc::api< remote_follow >                _remote_follow;
                fc::api< remote_private_message >       _remote_private_message;
                fc::api< remote_account_by_key >        _remote_account_by_key;
                fc::api< remote_witness_api >           _remote_witness_api;
                uint32_t                                _tx_expiration_seconds = 30;

                flat_map<string, operation>             _prototype_ops;

                static_variant_map _operation_which_map = create_static_variant_map< operation >();

#ifdef __unix__
                mode_t                  _old_umask;
#endif
                const string _wallet_filename_extension = ".wallet";
            };

        } } } // graphene::wallet::detail



namespace graphene { namespace wallet {

        wallet_api::wallet_api(const wallet_data& initial_data, const graphene::protocol::chain_id_type& _chain_id, fc::api_connection& con)
                : my(new detail::wallet_api_impl(*this, initial_data, _chain_id, con))
        {}

        wallet_api::~wallet_api(){}

        bool wallet_api::copy_wallet_file(string destination_filename)
        {
            return my->copy_wallet_file(destination_filename);
        }

        optional<signed_block_with_info> wallet_api::get_block(uint32_t num) {
            return my->_remote_database_api->get_block( num );
        }

        vector< graphene::plugins::operation_history::applied_operation > wallet_api::get_ops_in_block(uint32_t block_num, bool only_virtual) {
            return my->_remote_operation_history->get_ops_in_block( block_num, only_virtual );
        }

        vector< graphene::api::account_api_object > wallet_api::list_my_accounts() {
            FC_ASSERT( !is_locked(), "Wallet must be unlocked to list accounts" );
            vector<graphene::api::account_api_object> result;

            vector<public_key_type> pub_keys;
            pub_keys.reserve( my->_keys.size() );

            for( const auto& item : my->_keys )
                pub_keys.push_back(item.first);

            auto refs = my->_remote_account_by_key->get_key_references( pub_keys );
            set<string> names;
            for( const auto& item : refs )
                for( const auto& name : item )
                    names.insert( name );


            result.reserve( names.size() );
            for( const auto& name : names )
                result.emplace_back( get_account( name ) );

            return result;
        }

        vector< account_name_type > wallet_api::list_accounts(const string& lowerbound, uint32_t limit) {
            return my->_remote_database_api->lookup_accounts( lowerbound, limit );
        }

        vector< account_name_type > wallet_api::get_active_witnesses()const {
            return my->_remote_witness_api->get_active_witnesses();
        }

        brain_key_info wallet_api::suggest_brain_key()const {
            brain_key_info result;
            // create a private key for secure entropy
            fc::sha256 sha_entropy1 = fc::ecc::private_key::generate().get_secret();
            fc::sha256 sha_entropy2 = fc::ecc::private_key::generate().get_secret();
            fc::bigint entropy1( sha_entropy1.data(), sha_entropy1.data_size() );
            fc::bigint entropy2( sha_entropy2.data(), sha_entropy2.data_size() );
            fc::bigint entropy(entropy1);
            entropy <<= 8*sha_entropy1.data_size();
            entropy += entropy2;
            string brain_key = "";

            for( int i=0; i<BRAIN_KEY_WORD_COUNT; i++ ) {
                fc::bigint choice = entropy % graphene::words::word_list_size;
                entropy /= graphene::words::word_list_size;
                if( i > 0 )
                    brain_key += " ";
                brain_key += graphene::words::word_list[ choice.to_int64() ];
            }

            brain_key = normalize_brain_key(brain_key);
            fc::ecc::private_key priv_key = detail::derive_private_key( brain_key, 0 );
            result.brain_priv_key = brain_key;
            result.wif_priv_key = key_to_wif( priv_key );
            result.pub_key = priv_key.get_public_key();
            return result;
        }

        string wallet_api::serialize_transaction( signed_transaction tx )const {
            return fc::to_hex(fc::raw::pack(tx));
        }

        string wallet_api::get_wallet_filename() const {
            return my->get_wallet_filename();
        }


        graphene::api::account_api_object wallet_api::get_account( string account_name ) const {
            return my->get_account( account_name );
        }

        bool wallet_api::import_key(string wif_key)
        {
            FC_ASSERT(!is_locked());
            // backup wallet
            fc::optional<fc::ecc::private_key> optional_private_key = wif_to_key(wif_key);
            if (!optional_private_key)
                FC_THROW("Invalid private key");
//   string shorthash = detail::pubkey_to_shorthash( optional_private_key->get_public_key() );
//   copy_wallet_file( "before-import-key-" + shorthash );

            if( my->import_key(wif_key) )
            {
                save_wallet_file();
                //     copy_wallet_file( "after-import-key-" + shorthash );
                return true;
            }
            return false;
        }

        string wallet_api::normalize_brain_key(string s) const
        {
            return detail::normalize_brain_key( s );
        }

        variant wallet_api::info() const
        {
            return my->info();
        }

        variant_object wallet_api::database_info() const
        {
            return my->database_info();
        }

        variant_object wallet_api::about() const
        {
            return my->about();
        }

/*
fc::ecc::private_key wallet_api::derive_private_key(const std::string& prefix_string, int sequence_number) const
{
   return detail::derive_private_key( prefix_string, sequence_number );
}
*/

        vector< account_name_type > wallet_api::list_witnesses(const string& lowerbound, uint32_t limit)
        {
            return my->_remote_witness_api->lookup_witness_accounts( lowerbound, limit );
        }

        optional< witness_api::witness_api_object > wallet_api::get_witness(string owner_account)
        {
            return my->get_witness(owner_account);
        }

        annotated_signed_transaction wallet_api::set_voting_proxy(string account_to_modify, string voting_account, bool broadcast /* = false */)
        { return my->set_voting_proxy(account_to_modify, voting_account, broadcast); }

        void wallet_api::set_wallet_filename(string wallet_filename) { my->_wallet_filename = wallet_filename; }

        annotated_signed_transaction wallet_api::sign_transaction(signed_transaction tx, bool broadcast /* = false */)
        { try {
                return my->sign_transaction( tx, broadcast);
            } FC_CAPTURE_AND_RETHROW( (tx) ) }

        operation wallet_api::get_prototype_operation(string operation_name) {
            return my->get_prototype_operation( operation_name );
        }

        string wallet_api::help()const
        {
            std::vector<std::string> method_names = my->method_documentation.get_method_names();
            std::stringstream ss;
            for (const std::string method_name : method_names)
            {
                try
                {
                    ss << my->method_documentation.get_brief_description(method_name);
                }
                catch (const fc::key_not_found_exception&)
                {
                    ss << method_name << " (no help available)\n";
                }
            }
            return ss.str();
        }

        string wallet_api::gethelp(const string& method)const
        {
            fc::api<wallet_api> tmp;
            std::stringstream ss;
            ss << "\n";

            std::string doxygenHelpString = my->method_documentation.get_detailed_description(method);
            if (!doxygenHelpString.empty())
                ss << doxygenHelpString;
            else
                ss << "No help defined for method " << method << "\n";

            return ss.str();
        }

        bool wallet_api::load_wallet_file( string wallet_filename )
        {
            return my->load_wallet_file( wallet_filename );
        }

        void wallet_api::save_wallet_file( string wallet_filename )
        {
            my->save_wallet_file( wallet_filename );
        }

        std::map<string,std::function<string(fc::variant,const fc::variants&)> >
        wallet_api::get_result_formatters() const
        {
            return my->get_result_formatters();
        }

        bool wallet_api::is_locked()const
        {
            return my->is_locked();
        }
        bool wallet_api::is_new()const
        {
            return my->_wallet.cipher_keys.size() == 0;
        }

        void wallet_api::encrypt_keys()
        {
            my->encrypt_keys();
        }

        void wallet_api::quit() {
            my->self.quit_command();
        }

        transaction_handle_type wallet_api::begin_builder_transaction() {
            return my->begin_builder_transaction();
        }

        void wallet_api::add_operation_to_builder_transaction(transaction_handle_type handle, const operation& op) {
            my->add_operation_to_builder_transaction(handle, op);
        }
        void wallet_api::add_operation_copy_to_builder_transaction(
            transaction_handle_type src_handle,
            transaction_handle_type dst_handle,
            uint32_t op_index
        ) {
            my->add_operation_copy_to_builder_transaction(src_handle, dst_handle, op_index);
        }

        void wallet_api::replace_operation_in_builder_transaction(
            transaction_handle_type handle, unsigned op_index, const operation& new_op
        ) {
            my->replace_operation_in_builder_transaction(handle, op_index, new_op);
        }

        transaction wallet_api::preview_builder_transaction(transaction_handle_type handle) {
            return my->preview_builder_transaction(handle);
        }

        signed_transaction wallet_api::sign_builder_transaction(transaction_handle_type handle, bool broadcast) {
            return my->sign_builder_transaction(handle, broadcast);
        }

        signed_transaction wallet_api::propose_builder_transaction(
            transaction_handle_type handle,
            std::string author,
            std::string title,
            std::string memo,
            time_point_sec expiration,
            time_point_sec review,
            bool broadcast
        ) {
            return my->propose_builder_transaction(handle, author, title, memo, expiration, review, broadcast);
        }

        void wallet_api::remove_builder_transaction(transaction_handle_type handle) {
            return my->remove_builder_transaction(handle);
        }

        signed_transaction wallet_api::approve_proposal(
            const std::string& author,
            const std::string& title,
            const approval_delta& delta,
            bool broadcast
        ) {
            return my->approve_proposal(author, title, delta, broadcast);
        }

        std::vector<database_api::proposal_api_object> wallet_api::get_proposed_transactions(
            std::string account, uint32_t from, uint32_t limit
        ) {
            return my->get_proposed_transactions(account, from, limit);
        }

        void wallet_api::lock() {
            try {
                FC_ASSERT( !is_locked() );
                encrypt_keys();
                for( auto& key : my->_keys )
                    key.second = key_to_wif(fc::ecc::private_key());
                my->_keys.clear();
                my->_checksum = fc::sha512();
                my->self.lock_changed(true);
            } FC_CAPTURE_AND_RETHROW() }

        void wallet_api::unlock(string password) {
            try {
                FC_ASSERT(password.size() > 0);
                auto pw = fc::sha512::hash(password.c_str(), password.size());
                vector<char> decrypted = fc::aes_decrypt(pw, my->_wallet.cipher_keys);
                auto pk = fc::raw::unpack<plain_keys>(decrypted);
                FC_ASSERT(pk.checksum == pw);
                my->_keys = std::move(pk.keys);
                my->_checksum = pk.checksum;
                my->self.lock_changed(false);
            } FC_CAPTURE_AND_RETHROW() }

        void wallet_api::set_password( string password )
        {
            if( !is_new() )
                FC_ASSERT( !is_locked(), "The wallet must be unlocked before the password can be set" );
            my->_checksum = fc::sha512::hash( password.c_str(), password.size() );
            lock();
        }

        map<public_key_type, string> wallet_api::list_keys()
        {
            FC_ASSERT(!is_locked());
            return my->_keys;
        }

        string wallet_api::get_private_key( public_key_type pubkey )const
        {
            return key_to_wif( my->get_private_key( pubkey ) );
        }

        pair<public_key_type,string> wallet_api::get_private_key_from_password( string account, string role, string password )const {
            auto seed = account + role + password;
            FC_ASSERT( seed.size() );
            auto secret = fc::sha256::hash( seed.c_str(), seed.size() );
            auto priv = fc::ecc::private_key::regenerate( secret );
            return std::make_pair( public_key_type( priv.get_public_key() ), key_to_wif( priv ) );
        }

        signed_block_with_info::signed_block_with_info(const signed_block& block): signed_block(block) {
            block_id = id();
            signing_key = signee();
            transaction_ids.reserve(transactions.size());
            for (const signed_transaction& tx : transactions) {
                transaction_ids.push_back(tx.id());
            }
        }

/**
 *  This method will generate new master, active, regular and memo keys for the new account
 *  which will be controlable by this wallet.
 */
        annotated_signed_transaction wallet_api::create_account(
            string creator, asset tokens_fee, asset delegated_vests, string new_account_name,
            string json_metadata, bool broadcast
        ) {
            try {
                FC_ASSERT(!is_locked());
                auto master = suggest_brain_key();
                auto active = suggest_brain_key();
                auto regular = suggest_brain_key();
                auto memo = suggest_brain_key();
                import_key(master.wif_priv_key);
                import_key(active.wif_priv_key);
                import_key(regular.wif_priv_key);
                import_key(memo.wif_priv_key);
                return create_account_with_keys(
                    creator, tokens_fee, delegated_vests, new_account_name, json_metadata,
                    master.pub_key, active.pub_key, regular.pub_key, memo.pub_key, broadcast);
            }
            FC_CAPTURE_AND_RETHROW((creator)(new_account_name)(json_metadata));
        }
/**
 * This method is used by faucets to create new accounts for other users which must
 * provide their desired keys. The resulting account may not be controllable by this
 * wallet.
 */
        annotated_signed_transaction wallet_api::create_account_with_keys(
            string creator,
            asset tokens_fee,
            asset delegated_vests,
            string new_account_name,
            string json_metadata,
            public_key_type master,
            public_key_type active,
            public_key_type regular,
            public_key_type memo,
            bool broadcast
        ) const {
            try {
                FC_ASSERT(!is_locked());
                account_create_operation op;
                op.creator = creator;
                op.new_account_name = new_account_name;
                op.master = authority(1, master, 1);
                op.active = authority(1, active, 1);
                op.regular = authority(1, regular, 1);
                op.memo_key = memo;
                op.json_metadata = json_metadata;
                op.fee = tokens_fee;
                op.delegation = delegated_vests;

                signed_transaction tx;
                tx.operations.push_back(op);
                tx.validate();
                return my->sign_transaction(tx, broadcast);
            }
            FC_CAPTURE_AND_RETHROW((creator)(new_account_name)(json_metadata)(master)(active)(regular)(memo)(broadcast));
        }

/**
 * This method is used by faucets to create new accounts for other users which must
 * provide their desired keys. The resulting account may not be controllable by this
 * wallet.
 */

        annotated_signed_transaction wallet_api::request_account_recovery( string recovery_account, string account_to_recover, authority new_authority, bool broadcast ) {
            FC_ASSERT( !is_locked() );
            request_account_recovery_operation op;
            op.recovery_account = recovery_account;
            op.account_to_recover = account_to_recover;
            op.new_master_authority = new_authority;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::recover_account( string account_to_recover, authority recent_authority, authority new_authority, bool broadcast ) {
            FC_ASSERT( !is_locked() );

            recover_account_operation op;
            op.account_to_recover = account_to_recover;
            op.new_master_authority = new_authority;
            op.recent_master_authority = recent_authority;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::change_recovery_account( string owner, string new_recovery_account, bool broadcast ) {
            FC_ASSERT( !is_locked() );

            change_recovery_account_operation op;
            op.account_to_recover = owner;
            op.new_recovery_account = new_recovery_account;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        vector< database_api::master_authority_history_api_object > wallet_api::get_master_history( string account )const {
            return my->_remote_database_api->get_master_history( account );
        }

        annotated_signed_transaction wallet_api::update_account(
                string account_name,
                string json_metadata,
                public_key_type master,
                public_key_type active,
                public_key_type regular,
                public_key_type memo,
                bool broadcast )const
        {
            try
            {
                FC_ASSERT( !is_locked() );

                account_update_operation op;
                op.account = account_name;
                op.master = authority( 1, master, 1 );
                op.active = authority( 1, active, 1);
                op.regular = authority( 1, regular, 1);
                op.memo_key = memo;
                op.json_metadata = json_metadata;

                signed_transaction tx;
                tx.operations.push_back(op);
                tx.validate();

                return my->sign_transaction( tx, broadcast );
            }
            FC_CAPTURE_AND_RETHROW( (account_name)(json_metadata)(master)(active)(memo)(broadcast) )
        }

        annotated_signed_transaction wallet_api::update_account_auth_key( string account_name, authority_type type, public_key_type key, weight_type weight, bool broadcast )
        {
            FC_ASSERT( !is_locked() );

            auto accounts = my->_remote_database_api->get_accounts( { account_name } );
            FC_ASSERT( accounts.size() == 1, "Account does not exist" );
            FC_ASSERT( account_name == accounts[0].name, "Account name doesn't match?" );

            account_update_operation op;
            op.account = account_name;
            op.memo_key = accounts[0].memo_key;
            op.json_metadata = accounts[0].json_metadata;

            authority new_auth;

            switch( type )
            {
                case( master ):
                    new_auth = accounts[0].master_authority;
                    break;
                case( active ):
                    new_auth = accounts[0].active_authority;
                    break;
                case( regular ):
                    new_auth = accounts[0].regular_authority;
                    break;
            }

            if( weight == 0 ) // Remove the key
            {
                new_auth.key_auths.erase( key );
            }
            else
            {
                new_auth.add_authority( key, weight );
            }

            if( new_auth.is_impossible() ) {
                if ( type == master ) {
                    FC_ASSERT( false, "Master authority change would render account irrecoverable." );
                }

                wlog( "Authority is now impossible." );
            }

            switch( type ) {
                case( master ):
                    op.master = new_auth;
                    break;
                case( active ):
                    op.active = new_auth;
                    break;
                case( regular ):
                    op.regular = new_auth;
                    break;
            }

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::update_account_auth_account( string account_name, authority_type type, string auth_account, weight_type weight, bool broadcast )
        {
            FC_ASSERT( !is_locked() );

            auto accounts = my->_remote_database_api->get_accounts( { account_name } );
            FC_ASSERT( accounts.size() == 1, "Account does not exist" );
            FC_ASSERT( account_name == accounts[0].name, "Account name doesn't match?" );

            account_update_operation op;
            op.account = account_name;
            op.memo_key = accounts[0].memo_key;
            op.json_metadata = accounts[0].json_metadata;

            authority new_auth;

            switch( type )
            {
                case( master ):
                    new_auth = accounts[0].master_authority;
                    break;
                case( active ):
                    new_auth = accounts[0].active_authority;
                    break;
                case( regular ):
                    new_auth = accounts[0].regular_authority;
                    break;
            }

            if( weight == 0 ) // Remove the key
            {
                new_auth.account_auths.erase( auth_account );
            }
            else
            {
                new_auth.add_authority( auth_account, weight );
            }

            if( new_auth.is_impossible() )
            {
                if ( type == master )
                {
                    FC_ASSERT( false, "Master authority change would render account irrecoverable." );
                }

                wlog( "Authority is now impossible." );
            }

            switch( type )
            {
                case( master ):
                    op.master = new_auth;
                    break;
                case( active ):
                    op.active = new_auth;
                    break;
                case( regular ):
                    op.regular = new_auth;
                    break;
            }

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::update_account_auth_threshold( string account_name, authority_type type, uint32_t threshold, bool broadcast )
        {
            FC_ASSERT( !is_locked() );

            auto accounts = my->_remote_database_api->get_accounts( { account_name } );
            FC_ASSERT( accounts.size() == 1, "Account does not exist" );
            FC_ASSERT( account_name == accounts[0].name, "Account name doesn't match?" );
            FC_ASSERT( threshold != 0, "Authority is implicitly satisfied" );

            account_update_operation op;
            op.account = account_name;
            op.memo_key = accounts[0].memo_key;
            op.json_metadata = accounts[0].json_metadata;

            authority new_auth;

            switch( type )
            {
                case( master ):
                    new_auth = accounts[0].master_authority;
                    break;
                case( active ):
                    new_auth = accounts[0].active_authority;
                    break;
                case( regular ):
                    new_auth = accounts[0].regular_authority;
                    break;
            }

            new_auth.weight_threshold = threshold;

            if( new_auth.is_impossible() )
            {
                if ( type == master )
                {
                    FC_ASSERT( false, "Master authority change would render account irrecoverable." );
                }

                wlog( "Authority is now impossible." );
            }

            switch( type )
            {
                case( master ):
                    op.master = new_auth;
                    break;
                case( active ):
                    op.active = new_auth;
                    break;
                case( regular ):
                    op.regular = new_auth;
                    break;
            }

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::update_account_meta(string account_name, string json_metadata, bool broadcast) {
            FC_ASSERT(!is_locked());
            auto accounts = my->_remote_database_api->get_accounts({account_name});
            FC_ASSERT(accounts.size() == 1, "Account does not exist");
            FC_ASSERT(account_name == accounts[0].name, "Account name doesn't match?");

            signed_transaction tx;
            account_metadata_operation op;
            op.account = account_name;
            op.json_metadata = json_metadata;
            tx.operations.push_back(op);
            tx.validate();
            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::update_account_memo_key( string account_name, public_key_type key, bool broadcast )
        {
            FC_ASSERT( !is_locked() );

            auto accounts = my->_remote_database_api->get_accounts( { account_name } );
            FC_ASSERT( accounts.size() == 1, "Account does not exist" );
            FC_ASSERT( account_name == accounts[0].name, "Account name doesn't match?" );

            account_update_operation op;
            op.account = account_name;
            op.memo_key = key;
            op.json_metadata = accounts[0].json_metadata;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::delegate_vesting_shares(string delegator, string delegatee, asset vesting_shares, bool broadcast) {
            FC_ASSERT(!is_locked());
            auto accounts = my->_remote_database_api->get_accounts({delegator, delegatee});
            FC_ASSERT(accounts.size() == 2, "One or more of the accounts specified do not exist.");
            FC_ASSERT(delegator == accounts[0].name, "Delegator account is not right?");
            FC_ASSERT(delegatee == accounts[1].name, "Delegatee account is not right?");

            delegate_vesting_shares_operation op;
            op.delegator = delegator;
            op.delegatee = delegatee;
            op.vesting_shares = vesting_shares;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }


/**
 *  This method will generate new master, active, and memo keys for the new account which
 *  will be controlable by this wallet.
 */

        annotated_signed_transaction wallet_api::update_witness(
            string witness_account_name,
            string url,
            public_key_type block_signing_key,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());

            signed_transaction tx;
            witness_update_operation op;

            if (url.empty()) {
                auto wit = my->_remote_witness_api->get_witness_by_account(witness_account_name);
                if (wit.valid()) {
                    FC_ASSERT(wit->owner == witness_account_name);
                    url = wit->url;
                }
            }
            op.url = url;
            op.owner = witness_account_name;
            op.block_signing_key = block_signing_key;

            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::update_chain_properties(
            string witness_account_name,
            const chain_properties& props,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());

            signed_transaction tx;
            chain_properties_update_operation op;

            op.owner = witness_account_name;
            op.props = props;
            tx.operations.push_back(op);

            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::versioned_update_chain_properties(
            string witness_account_name,
            const chain_properties& props,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());

            signed_transaction tx;
            versioned_chain_properties_update_operation op;

            op.owner = witness_account_name;
            op.props = props;
            tx.operations.push_back(op);

            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::vote_for_witness(string voting_account, string witness_to_vote_for, bool approve, bool broadcast )
        { try {
                FC_ASSERT( !is_locked() );
                account_witness_vote_operation op;
                op.account = voting_account;
                op.witness = witness_to_vote_for;
                op.approve = approve;

                signed_transaction tx;
                tx.operations.push_back( op );
                tx.validate();

                return my->sign_transaction( tx, broadcast );
            } FC_CAPTURE_AND_RETHROW( (voting_account)(witness_to_vote_for)(approve)(broadcast) ) }

        void wallet_api::check_memo( const string& memo, const graphene::api::account_api_object& account )const
        {
            vector< public_key_type > keys;

            try
            {
                // Check if memo is a private key
                keys.push_back( fc::ecc::extended_private_key::from_base58( memo ).get_public_key() );
            }
            catch( fc::parse_error_exception& ) {}
            catch( fc::assert_exception& ) {}

            // Get possible keys if memo was an account password
            string master_seed = account.name + "master" + memo;
            auto master_secret = fc::sha256::hash( master_seed.c_str(), master_seed.size() );
            keys.push_back( fc::ecc::private_key::regenerate( master_secret ).get_public_key() );

            string active_seed = account.name + "active" + memo;
            auto active_secret = fc::sha256::hash( active_seed.c_str(), active_seed.size() );
            keys.push_back( fc::ecc::private_key::regenerate( active_secret ).get_public_key() );

            string regular_seed = account.name + "regular" + memo;
            auto regular_secret = fc::sha256::hash( regular_seed.c_str(), regular_seed.size() );
            keys.push_back( fc::ecc::private_key::regenerate( regular_secret ).get_public_key() );

            // Check keys against public keys in authorites
            for( auto& key_weight_pair : account.master_authority.key_auths )
            {
                for( auto& key : keys )
                    FC_ASSERT( key_weight_pair.first != key, "Detected private master key in memo field. Cancelling transaction." );
            }

            for( auto& key_weight_pair : account.active_authority.key_auths )
            {
                for( auto& key : keys )
                    FC_ASSERT( key_weight_pair.first != key, "Detected private active key in memo field. Cancelling transaction." );
            }

            for( auto& key_weight_pair : account.regular_authority.key_auths )
            {
                for( auto& key : keys )
                    FC_ASSERT( key_weight_pair.first != key, "Detected private regular key in memo field. Cancelling transaction." );
            }

            const auto& memo_key = account.memo_key;
            for( auto& key : keys )
                FC_ASSERT( memo_key != key, "Detected private memo key in memo field. Cancelling transaction." );

            // Check against imported keys
            for( auto& key_pair : my->_keys )
            {
                for( auto& key : keys )
                    FC_ASSERT( key != key_pair.first, "Detected imported private key in memo field. Cancelling trasanction." );
            }
        }

        string wallet_api::get_encrypted_memo( string from, string to, string memo ) {

            if( memo.size() > 0 && memo[0] == '#' ) {
                memo_data m;

                auto from_account = get_account( from );
                auto to_account   = get_account( to );

                m.from            = from_account.memo_key;
                m.to              = to_account.memo_key;
                m.nonce = fc::time_point::now().time_since_epoch().count();

                auto from_priv = my->get_private_key( m.from );
                auto shared_secret = from_priv.get_shared_secret( m.to );

                fc::sha512::encoder enc;
                fc::raw::pack( enc, m.nonce );
                fc::raw::pack( enc, shared_secret );
                auto encrypt_key = enc.result();

                m.encrypted = fc::aes_encrypt( encrypt_key, fc::raw::pack(memo.substr(1)) );
                m.check = fc::sha256::hash( encrypt_key )._hash[0];
                return m;
            } else {
                return memo;
            }
        }

        annotated_signed_transaction wallet_api::transfer(string from, string to, asset amount, string memo, bool broadcast)
        { try {
                FC_ASSERT( !is_locked() );
                check_memo( memo, get_account( from ) );
                transfer_operation op;
                op.from = from;
                op.to = to;
                op.amount = amount;

                op.memo = get_encrypted_memo( from, to, memo );

                signed_transaction tx;
                tx.operations.push_back( op );
                tx.validate();

                return my->sign_transaction( tx, broadcast );
            } FC_CAPTURE_AND_RETHROW( (from)(to)(amount)(memo)(broadcast) ) }

        annotated_signed_transaction wallet_api::escrow_transfer(
                string from,
                string to,
                string agent,
                uint32_t escrow_id,
                asset token_amount,
                asset fee,
                time_point_sec ratification_deadline,
                time_point_sec escrow_expiration,
                string json_metadata,
                bool broadcast
        )
        {
            FC_ASSERT( !is_locked() );
            escrow_transfer_operation op;
            op.from = from;
            op.to = to;
            op.agent = agent;
            op.escrow_id = escrow_id;
            op.token_amount = token_amount;
            op.fee = fee;
            op.ratification_deadline = ratification_deadline;
            op.escrow_expiration = escrow_expiration;
            op.json_metadata = json_metadata;

            signed_transaction tx;
            tx.operations.push_back( op );
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::escrow_approve(
                string from,
                string to,
                string agent,
                string who,
                uint32_t escrow_id,
                bool approve,
                bool broadcast
        )
        {
            FC_ASSERT( !is_locked() );
            escrow_approve_operation op;
            op.from = from;
            op.to = to;
            op.agent = agent;
            op.who = who;
            op.escrow_id = escrow_id;

            signed_transaction tx;
            tx.operations.push_back( op );
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::escrow_dispute(
                string from,
                string to,
                string agent,
                string who,
                uint32_t escrow_id,
                bool broadcast
        )
        {
            FC_ASSERT( !is_locked() );
            escrow_dispute_operation op;
            op.from = from;
            op.to = to;
            op.agent = agent;
            op.who = who;
            op.escrow_id = escrow_id;

            signed_transaction tx;
            tx.operations.push_back( op );
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::escrow_release(
                string from,
                string to,
                string agent,
                string who,
                string receiver,
                uint32_t escrow_id,
                asset token_amount,
                bool broadcast
        )
        {
            FC_ASSERT( !is_locked() );
            escrow_release_operation op;
            op.from = from;
            op.to = to;
            op.agent = agent;
            op.who = who;
            op.receiver = receiver;
            op.escrow_id = escrow_id;
            op.token_amount = token_amount;

            signed_transaction tx;
            tx.operations.push_back( op );
            tx.validate();
            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::transfer_to_vesting(string from, string to, asset amount, bool broadcast )
        {
            FC_ASSERT( !is_locked() );
            transfer_to_vesting_operation op;
            op.from = from;
            op.to = (to == from ? "" : to);
            op.amount = amount;

            signed_transaction tx;
            tx.operations.push_back( op );
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::withdraw_vesting(string from, asset vesting_shares, bool broadcast )
        {
            FC_ASSERT( !is_locked() );
            withdraw_vesting_operation op;
            op.account = from;
            op.vesting_shares = vesting_shares;

            signed_transaction tx;
            tx.operations.push_back( op );
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::set_withdraw_vesting_route( string from, string to, uint16_t percent, bool auto_vest, bool broadcast )
        {
            FC_ASSERT( !is_locked() );
            set_withdraw_vesting_route_operation op;
            op.from_account = from;
            op.to_account = to;
            op.percent = percent;
            op.auto_vest = auto_vest;

            signed_transaction tx;
            tx.operations.push_back( op );
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        string wallet_api::decrypt_memo( string encrypted_memo ) {
            if( is_locked() ) return encrypted_memo;

            if( encrypted_memo.size() && encrypted_memo[0] == '#' ) {
                auto m = memo_data::from_string( encrypted_memo );
                if( m ) {
                    fc::sha512 shared_secret;
                    auto from_key = my->try_get_private_key( m->from );
                    if( !from_key ) {
                        auto to_key = my->try_get_private_key( m->to );
                        if( !to_key ) return encrypted_memo;
                        shared_secret = to_key->get_shared_secret( m->from );
                    } else {
                        shared_secret = from_key->get_shared_secret( m->to );
                    }
                    fc::sha512::encoder enc;
                    fc::raw::pack( enc, m->nonce );
                    fc::raw::pack( enc, shared_secret );
                    auto encryption_key = enc.result();

                    uint32_t check = fc::sha256::hash( encryption_key )._hash[0];
                    if( check != m->check ) return encrypted_memo;

                    try {
                        vector<char> decrypted = fc::aes_decrypt( encryption_key, m->encrypted );
                        return fc::raw::unpack<std::string>( decrypted );
                    } catch ( ... ){}
                }
            }
            return encrypted_memo;
        }

        map< uint32_t, graphene::plugins::operation_history::applied_operation> wallet_api::get_account_history( string account, uint32_t from, uint32_t limit ) {
            auto result = my->_remote_account_history->get_account_history( account, from, limit );
            if( !is_locked() ) {
                for( auto& item : result ) {
                    if( item.second.op.which() == operation::tag<transfer_operation>::value ) {
                        auto& top = item.second.op.get<transfer_operation>();
                        top.memo = decrypt_memo( top.memo );
                    }
                }
            }
            return result;
        }

        vector< database_api::withdraw_vesting_route_api_object > wallet_api::get_withdraw_routes( string account, database_api::withdraw_route_type type )const {
            return my->_remote_database_api->get_withdraw_routes( account, type );
        }

        annotated_signed_transaction wallet_api::post_content( string author, string permlink, string parent_author, string parent_permlink, string title, string body, int16_t curation_percent, string json, bool broadcast ) {
            FC_ASSERT( !is_locked() );
            content_operation op;
            op.parent_author = parent_author;
            op.parent_permlink = parent_permlink;
            op.author = author;
            op.permlink = permlink;
            op.title = title;
            op.body = body;
            op.curation_percent = curation_percent;
            op.json_metadata = json;

            signed_transaction tx;
            tx.operations.push_back( op );
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        annotated_signed_transaction wallet_api::vote( string voter, string author, string permlink, int16_t weight, bool broadcast ) {
            FC_ASSERT( !is_locked() );
            FC_ASSERT( abs(weight) <= 100, "Weight must be between -100 and 100 and not 0" );

            vote_operation op;
            op.voter = voter;
            op.author = author;
            op.permlink = permlink;
            op.weight = weight * CHAIN_1_PERCENT;

            signed_transaction tx;
            tx.operations.push_back( op );
            tx.validate();

            return my->sign_transaction( tx, broadcast );
        }

        void wallet_api::set_transaction_expiration(uint32_t seconds) {
            my->set_transaction_expiration(seconds);
        }

        annotated_signed_transaction wallet_api::get_transaction( transaction_id_type id )const {
            return my->_remote_operation_history->get_transaction( id );
        }

        vector<extended_message_object> wallet_api::get_inbox(const std::string& to, time_point newest, uint16_t limit, std::uint64_t offset) {
            FC_ASSERT( !is_locked() );
            vector<extended_message_object> result;
            auto remote_result = my->_remote_private_message->get_inbox(to, newest, limit, offset);
            for( const auto& item : remote_result ) {
                result.emplace_back( item );
                message_body tmp = try_decrypt_message( item );
                result.back().message = std::move(tmp);
            }
            return result;
        }

        vector<extended_message_object> wallet_api::get_outbox(const std::string& from, time_point newest, uint16_t limit, std::uint64_t offset) {
            FC_ASSERT( !is_locked() );
            vector<extended_message_object> result;
            auto remote_result = my->_remote_private_message->get_outbox(from, newest, limit, offset);
            for( const auto& item : remote_result ) {
                result.emplace_back( item );
                message_body tmp = try_decrypt_message( item );
                result.back().message = std::move(tmp);
            }
            return result;
        }

        message_body wallet_api::try_decrypt_message( const message_api_obj& mo ) {
            message_body result;

            fc::sha512 shared_secret;

            auto it = my->_keys.find(mo.from_memo_key);
            if( it == my->_keys.end() )
            {
                it = my->_keys.find(mo.to_memo_key);
                if( it == my->_keys.end() )
                {
                    wlog( "unable to find keys" );
                    return result;
                }
                auto priv_key = wif_to_key( it->second );
                if( !priv_key ) return result;
                shared_secret = priv_key->get_shared_secret( mo.from_memo_key );
            } else {
                auto priv_key = wif_to_key( it->second );
                if( !priv_key ) return result;
                shared_secret = priv_key->get_shared_secret( mo.to_memo_key );
            }


            fc::sha512::encoder enc;
            fc::raw::pack( enc, mo.sent_time );
            fc::raw::pack( enc, shared_secret );
            auto encrypt_key = enc.result();

            uint32_t check = fc::sha256::hash( encrypt_key )._hash[0];

            if( mo.checksum != check )
                return result;

            auto decrypt_data = fc::aes_decrypt( encrypt_key, mo.encrypted_message );
            try {
                return fc::raw::unpack<message_body>( decrypt_data );
            } catch ( ... ) {
                return result;
            }
        }

        annotated_signed_transaction wallet_api::follow(
                const string& follower,
                const string& following,
                const set<string>& what,
                const bool broadcast) {
            string _following = following;

            auto follwer_account = get_account( follower );
            FC_ASSERT( _following.size() );
            if( _following[0] != '@' || _following[0] != '#' ) {
                _following = '@' + _following;
            }
            if( _following[0] == '@' ) {
                get_account( _following.substr(1) );
            }
            FC_ASSERT( _following.size() > 1 );

            follow::follow_operation fop;
            fop.follower = follower;
            fop.following = _following;
            fop.what = what;
            follow::follow_plugin_operation op = fop;

            custom_operation jop;
            jop.id = "follow";
            jop.json = fc::json::to_string(op);
            jop.required_regular_auths.insert(follower);

            signed_transaction trx;
            trx.operations.push_back( jop );
            trx.validate();

            return my->sign_transaction( trx, broadcast );
        }

        annotated_signed_transaction wallet_api::custom(
            flat_set<string> required_active_auths,
            flat_set<string> required_regular_auths,
            string id,
            string json,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            custom_operation op;
            for (const auto& acc : required_active_auths) {
                op.required_active_auths.insert(acc);
            }
            for (const auto& acc : required_regular_auths) {
                op.required_regular_auths.insert(acc);
            }
            op.id = id;
            op.json = json;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::delete_content(
            string author,
            string permlink,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            delete_content_operation op;
            op.author = author;
            op.permlink = permlink;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::delete_proposal(
            string author,
            string title,
            string requester,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            proposal_delete_operation op;
            op.author = author;
            op.title = title;
            op.requester = requester;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::committee_worker_create_request(
            string creator,
            string url,
            string worker,
            asset required_amount_min,
            asset required_amount_max,
            uint32_t duration,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            committee_worker_create_request_operation op;
            op.creator = creator;
            op.url = url;
            op.worker = worker;
            op.required_amount_min = required_amount_min;
            op.required_amount_max = required_amount_max;
            op.duration = duration;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::committee_worker_cancel_request(
            string creator,
            uint32_t request_id,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            committee_worker_cancel_request_operation op;
            op.creator = creator;
            op.request_id = request_id;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::committee_vote_request(
            string voter,
            uint32_t request_id,
            int16_t vote_percent,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            committee_vote_request_operation op;
            op.voter = voter;
            op.request_id = request_id;
            op.vote_percent = vote_percent;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::create_invite(
            string creator,
            asset balance,
            public_key_type invite_key,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            create_invite_operation op;
            op.creator = creator;
            op.balance = balance;
            op.invite_key = invite_key;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::claim_invite_balance(
            string initiator,
            string receiver,
            string invite_secret,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            claim_invite_balance_operation op;
            op.initiator = initiator;
            op.receiver = receiver;
            op.invite_secret = invite_secret;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::invite_registration(
            string initiator,
            string new_account_name,
            string invite_secret,
            public_key_type new_account_key,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            invite_registration_operation op;
            op.initiator = initiator;
            op.new_account_name = new_account_name;
            op.invite_secret = invite_secret;
            op.new_account_key = new_account_key;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::use_invite_balance(
            string initiator,
            string receiver,
            string invite_secret,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            use_invite_balance_operation op;
            op.initiator = initiator;
            op.receiver = receiver;
            op.invite_secret = invite_secret;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::award(
            string initiator,
            string receiver,
            uint16_t energy,
            uint64_t custom_sequence,
            string memo,
            vector<beneficiary_route_type> beneficiaries,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            award_operation op;
            op.initiator = initiator;
            op.receiver = receiver;
            op.energy = energy;
            op.custom_sequence = custom_sequence;
            op.memo = memo;
            op.beneficiaries = beneficiaries;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::fixed_award(
            string initiator,
            string receiver,
            asset reward_amount,
            uint16_t max_energy,
            uint64_t custom_sequence,
            string memo,
            vector<beneficiary_route_type> beneficiaries,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            fixed_award_operation op;
            op.initiator = initiator;
            op.receiver = receiver;
            op.reward_amount = reward_amount;
            op.max_energy = max_energy;
            op.custom_sequence = custom_sequence;
            op.memo = memo;
            op.beneficiaries = beneficiaries;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::set_paid_subscription(
            string account,
            string url,
            uint16_t levels,
            asset amount,
            uint16_t period,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            set_paid_subscription_operation op;
            op.account = account;
            op.url = url;
            op.levels = levels;
            op.amount = amount;
            op.period = period;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::paid_subscribe(
            string subscriber,
            string account,
            uint16_t level,
            asset amount,
            uint16_t period,
            bool auto_renewal,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            paid_subscribe_operation op;
            op.subscriber = subscriber;
            op.account = account;
            op.level = level;
            op.amount = amount;
            op.period = period;
            op.auto_renewal = auto_renewal;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::set_account_price(
            string account,
            string account_seller,
            asset account_offer_price,
            bool account_on_sale,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            set_account_price_operation op;
            op.account = account;
            op.account_seller = account_seller;
            op.account_offer_price = account_offer_price;
            op.account_on_sale = account_on_sale;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::set_subaccount_price(
            string account,
            string subaccount_seller,
            asset subaccount_offer_price,
            bool subaccount_on_sale,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            set_subaccount_price_operation op;
            op.account = account;
            op.subaccount_seller = subaccount_seller;
            op.subaccount_offer_price = subaccount_offer_price;
            op.subaccount_on_sale = subaccount_on_sale;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::buy_account(
            string buyer,
            string account,
            asset account_offer_price,
            public_key_type account_authorities_key,
            asset tokens_to_shares,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            buy_account_operation op;
            op.buyer = buyer;
            op.account = account;
            op.account_offer_price = account_offer_price;
            op.account_authorities_key = account_authorities_key;
            op.tokens_to_shares = tokens_to_shares;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::target_account_sale(
            string account,
            string account_seller,
            string target_buyer,
            asset account_offer_price,
            bool account_on_sale,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());
            target_account_sale_operation op;
            op.account = account;
            op.account_seller = account_seller;
            op.target_buyer = target_buyer;
            op.account_offer_price = account_offer_price;
            op.account_on_sale = account_on_sale;

            signed_transaction tx;
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        // ========== VIZ DNS Nameserver Helper Implementations ==========

        bool wallet_api::ns_validate_ipv4(const string& ipv4) const {
            // Validate IPv4 address format: x.x.x.x where x is 0-255
            if (ipv4.empty()) return false;

            vector<string> octets;
            string current;
            for (char c : ipv4) {
                if (c == '.') {
                    if (current.empty()) return false;
                    octets.push_back(current);
                    current.clear();
                } else if (c >= '0' && c <= '9') {
                    current += c;
                } else {
                    return false; // Invalid character
                }
            }
            if (!current.empty()) {
                octets.push_back(current);
            }

            if (octets.size() != 4) return false;

            for (const auto& octet : octets) {
                if (octet.empty() || octet.size() > 3) return false;
                // Check for leading zeros (invalid except for "0")
                if (octet.size() > 1 && octet[0] == '0') return false;
                int value = std::stoi(octet);
                if (value < 0 || value > 255) return false;
            }

            return true;
        }

        bool wallet_api::ns_validate_sha256_hash(const string& hash) const {
            // SHA256 hash must be exactly 64 hex characters
            if (hash.size() != NS_SHA256_HEX_LENGTH) return false;

            for (char c : hash) {
                if (!((c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F'))) {
                    return false;
                }
            }
            return true;
        }

        bool wallet_api::ns_validate_ttl(uint32_t ttl) const {
            // TTL must be a positive value
            return ttl > 0;
        }

        bool wallet_api::ns_validate_ssl_txt_record(const string& txt) const {
            // Format: ssl=<64-char-hex-hash>
            if (txt.size() < 5) return false; // Minimum: "ssl=x"
            if (txt.substr(0, 4) != "ssl=") return false;
            string hash = txt.substr(4);
            return ns_validate_sha256_hash(hash);
        }

        ns_validation_result wallet_api::ns_validate_metadata(const ns_metadata_options& options) const {
            ns_validation_result result;
            result.is_valid = true;

            // Validate A records
            for (const auto& ip : options.a_records) {
                if (!ns_validate_ipv4(ip)) {
                    result.is_valid = false;
                    result.errors.push_back("Invalid IPv4 address: " + ip);
                }
            }

            // Validate SSL hash if provided
            if (options.ssl_hash) {
                if (!ns_validate_sha256_hash(*options.ssl_hash)) {
                    result.is_valid = false;
                    result.errors.push_back("Invalid SHA256 hash: " + *options.ssl_hash);
                }
            }

            // Validate TTL
            if (!ns_validate_ttl(options.ttl)) {
                result.is_valid = false;
                result.errors.push_back("Invalid TTL value: must be positive");
            }

            // Validate at least one record exists
            if (options.a_records.empty() && !options.ssl_hash) {
                result.is_valid = false;
                result.errors.push_back("At least one A record or SSL hash must be provided");
            }

            return result;
        }

        variant wallet_api::ns_create_metadata(const ns_metadata_options& options) const {
            // Validate first
            auto validation = ns_validate_metadata(options);
            FC_ASSERT(validation.is_valid, "Invalid NS metadata: ${errors}", ("errors", validation.errors));

            // Build the ns array
            fc::mutable_variant_object result;
            fc::variants ns_array;

            // Add A records
            for (const auto& ip : options.a_records) {
                fc::variants record;
                record.push_back("A");
                record.push_back(ip);
                ns_array.push_back(record);
            }

            // Add SSL TXT record if provided
            if (options.ssl_hash) {
                fc::variants record;
                record.push_back("TXT");
                record.push_back("ssl=" + *options.ssl_hash);
                ns_array.push_back(record);
            }

            result["ns"] = ns_array;
            result["ttl"] = options.ttl;

            return result;
        }

        ns_summary wallet_api::ns_get_summary(const string& account_name) const {
            ns_summary summary;

            auto accounts = my->_remote_database_api->get_accounts({account_name});
            FC_ASSERT(accounts.size() == 1, "Account does not exist");

            const auto& account = accounts[0];
            if (account.json_metadata.empty()) {
                return summary;
            }

            try {
                fc::variant metadata = fc::json::from_string(account.json_metadata);
                if (!metadata.is_object()) {
                    return summary;
                }

                fc::variant_object obj = metadata.get_object();

                // Extract ns array
                if (obj.contains("ns") && obj["ns"].is_array()) {
                    summary.has_ns_data = true;
                    fc::variants ns_array = obj["ns"].get_array();

                    for (const auto& record : ns_array) {
                        if (!record.is_array()) continue;
                        fc::variants rec_arr = record.get_array();
                        if (rec_arr.size() < 2) continue;

                        string type = rec_arr[0].as_string();
                        string value = rec_arr[1].as_string();

                        if (type == "A") {
                            summary.a_records.push_back(value);
                        } else if (type == "TXT") {
                            // Check for ssl= prefix
                            if (value.substr(0, 4) == "ssl=") {
                                summary.ssl_hash = value.substr(4);
                            }
                        }
                    }
                }

                // Extract TTL
                if (obj.contains("ttl") && obj["ttl"].is_uint64()) {
                    summary.ttl = static_cast<uint32_t>(obj["ttl"].as_uint64());
                    if (!summary.has_ns_data && summary.ttl > 0) {
                        summary.has_ns_data = true;
                    }
                }
            } catch (...) {
                // Invalid JSON, return empty summary
            }

            return summary;
        }

        vector<string> wallet_api::ns_extract_a_records(const string& account_name) const {
            auto summary = ns_get_summary(account_name);
            return summary.a_records;
        }

        optional<string> wallet_api::ns_extract_ssl_hash(const string& account_name) const {
            auto summary = ns_get_summary(account_name);
            return summary.ssl_hash;
        }

        uint32_t wallet_api::ns_extract_ttl(const string& account_name) const {
            auto summary = ns_get_summary(account_name);
            return summary.ttl;
        }

        annotated_signed_transaction wallet_api::ns_set_records(
            string account_name,
            const ns_metadata_options& options,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());

            // Validate NS metadata
            auto validation = ns_validate_metadata(options);
            FC_ASSERT(validation.is_valid, "Invalid NS metadata: ${errors}", ("errors", validation.errors));

            auto accounts = my->_remote_database_api->get_accounts({account_name});
            FC_ASSERT(accounts.size() == 1, "Account does not exist");

            const auto& account = accounts[0];

            // Parse existing metadata or create empty object
            fc::mutable_variant_object metadata;
            if (!account.json_metadata.empty()) {
                try {
                    fc::variant existing = fc::json::from_string(account.json_metadata);
                    if (existing.is_object()) {
                        // Copy all existing fields except ns and ttl
                        for (const auto& item : existing.get_object()) {
                            if (item.key() != "ns" && item.key() != "ttl") {
                                metadata[item.key()] = item.value();
                            }
                        }
                    }
                } catch (...) {
                    // Invalid JSON, start fresh
                }
            }

            // Build the ns array
            fc::variants ns_array;

            // Add A records
            for (const auto& ip : options.a_records) {
                fc::variants record;
                record.push_back("A");
                record.push_back(ip);
                ns_array.push_back(record);
            }

            // Add SSL TXT record if provided
            if (options.ssl_hash) {
                fc::variants record;
                record.push_back("TXT");
                record.push_back("ssl=" + *options.ssl_hash);
                ns_array.push_back(record);
            }

            metadata["ns"] = ns_array;
            metadata["ttl"] = options.ttl;

            // Create and broadcast transaction
            signed_transaction tx;
            account_metadata_operation op;
            op.account = account_name;
            op.json_metadata = fc::json::to_string(metadata);
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

        annotated_signed_transaction wallet_api::ns_remove_records(
            string account_name,
            bool broadcast
        ) {
            FC_ASSERT(!is_locked());

            auto accounts = my->_remote_database_api->get_accounts({account_name});
            FC_ASSERT(accounts.size() == 1, "Account does not exist");

            const auto& account = accounts[0];

            // Parse existing metadata
            fc::mutable_variant_object metadata;
            if (!account.json_metadata.empty()) {
                try {
                    fc::variant existing = fc::json::from_string(account.json_metadata);
                    if (existing.is_object()) {
                        // Copy all existing fields except ns and ttl
                        for (const auto& item : existing.get_object()) {
                            if (item.key() != "ns" && item.key() != "ttl") {
                                metadata[item.key()] = item.value();
                            }
                        }
                    }
                } catch (...) {
                    // Invalid JSON, nothing to remove
                    FC_ASSERT(false, "Account has invalid JSON metadata");
                }
            }

            // Create and broadcast transaction
            signed_transaction tx;
            account_metadata_operation op;
            op.account = account_name;
            op.json_metadata = fc::json::to_string(metadata);
            tx.operations.push_back(op);
            tx.validate();

            return my->sign_transaction(tx, broadcast);
        }

    } } // graphene::wallet
