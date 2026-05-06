#pragma once
#include <graphene/protocol/block.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/protocol/transaction.hpp>
#include <graphene/protocol/operations.hpp>

#include <graphene/plugins/mongo_db/mongo_db_types.hpp>
#include <graphene/plugins/mongo_db/mongo_db_state.hpp>

#include <libraries/chain/include/graphene/chain/operation_notification.hpp>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/stream/value_context.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

#include <appbase/application.hpp>

#include <thread>
#include <map>
#include <mutex>
#include <condition_variable>


namespace graphene {
namespace plugins {
namespace mongo_db {

    using graphene::protocol::signed_block;
    using graphene::protocol::signed_transaction;
    using bsoncxx::builder::stream::document;
    using bsoncxx::builder::stream::open_document;
    using graphene::chain::operation_notification;
    using namespace graphene::protocol;

    using bulk_ptr = std::unique_ptr<mongocxx::bulk_write>;

    class mongo_db_writer final {
    public:
        mongo_db_writer();
        ~mongo_db_writer();

        bool initialize(const std::string& uri_str, const bool write_raw, const std::vector<std::string>& op);

        void on_block(const signed_block& block);
        void on_operation(const graphene::chain::operation_notification& note);

    private:
        using operations = std::vector<operation>;

        void write_blocks();
        void write_raw_block(const signed_block& block, const operations&);
        void write_block_operations(state_writer& st_writer, const signed_block& block, const operations&);
        void write_document(named_document const& named_doc);
        void remove_document(named_document const& named_doc);

        void format_block_info(const signed_block& block, document& doc);
        void format_transaction_info(const signed_transaction& tran, document& doc);

        void write_data();

        uint64_t processed_blocks = 0;

        std::string db_name;

        // Key = Block num, Value = block
        uint32_t last_irreversible_block_num;
        std::map<uint32_t, signed_block> blocks;
        std::map<uint32_t, operations> virtual_ops;
        // Table name, bulk write
        std::map<std::string, bulk_ptr> formatted_blocks;

        bool write_raw_blocks;
        flat_set<std::string> write_operations;

        // Mongo connection members
        mongocxx::instance mongo_inst;
        mongocxx::database mongo_database;
        mongocxx::uri uri;
        mongocxx::client mongo_conn;
        mongocxx::options::bulk_write bulk_opts;

        std::unordered_map<std::string, std::string> indexes; // Prevent repeative create_index() calls. Only in current session 

        graphene::chain::database &_db;
    };
}}}

