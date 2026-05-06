#include <graphene/chain/database.hpp>

#include <graphene/plugins/block_info/plugin.hpp>

#include <graphene/protocol/types.hpp>
#include <graphene/plugins/json_rpc/utility.hpp>
#include <graphene/plugins/json_rpc/plugin.hpp>

#include <unordered_map>

namespace graphene {
namespace plugins {
namespace block_info {

using namespace graphene::chain;

struct plugin::plugin_impl  {
public:
    plugin_impl() : db_(appbase::app().get_plugin<plugins::chain::plugin>().db()) {
    }

    // API
    std::vector<block_info> get_block_info(
        uint32_t start_block_num = 0,
        uint32_t count = 1000);
    std::vector<block_with_info> get_blocks_with_info(
        uint32_t start_block_num = 0,
        uint32_t count = 1000);

    // PLUGIN_METHODS
    void on_applied_block(const protocol::signed_block &b);

    // HELPING METHODS
    graphene::chain::database &database() {
        return db_;
    }
// protected:
    boost::signals2::scoped_connection applied_block_conn_;
private:
    std::unordered_map<uint32_t, block_info> block_info_;

    graphene::chain::database & db_;
};

std::vector<block_info> plugin::plugin_impl::get_block_info(uint32_t start_block_num, uint32_t count) {
    std::vector<block_info> result;

    FC_ASSERT(start_block_num > 0);
    FC_ASSERT(count <= 10000);

    result.reserve(count);
    for (uint32_t block_num = start_block_num;
         block_num < start_block_num + count; block_num++) {
        auto it = block_info_.find(block_num);
        if (it == block_info_.end()) {
            // No info stored for this block (e.g. blocks before snapshot in DLT mode)
            result.emplace_back();
        } else {
            result.emplace_back(it->second);
        }
    }

    return result;
}

std::vector<block_with_info> plugin::plugin_impl::get_blocks_with_info(
        uint32_t start_block_num, uint32_t count) {
    std::vector<block_with_info> result;
    const auto & db = database();

    FC_ASSERT(start_block_num > 0);
    FC_ASSERT(count <= 10000);

    uint64_t total_size = 0;
    for (uint32_t block_num = start_block_num;
         block_num < start_block_num + count; block_num++) {
        auto it = block_info_.find(block_num);
        if (it == block_info_.end()) {
            // No info stored for this block (e.g. blocks before snapshot in DLT mode)
            break;
        }

        uint64_t new_size = total_size + it->second.block_size;
        if ((new_size > 8 * 1024 * 1024) &&
            (block_num != start_block_num)) {
            break;
        }
        total_size = new_size;
        result.emplace_back();
        result.back().block = *db.fetch_block_by_number(block_num);
        result.back().info = it->second;
    }

    return result;
}

void plugin::plugin_impl::on_applied_block(const protocol::signed_block &b) {
    uint32_t block_num = b.block_num();
    const auto &db = appbase::app().get_plugin<chain::plugin>().db();

    block_info &info = block_info_[block_num];
    const dynamic_global_property_object &dgpo = db.get_dynamic_global_properties();

    info.block_id = b.id();
    info.block_size = fc::raw::pack_size(b);
    info.average_block_size = dgpo.average_block_size;
    info.aslot = dgpo.current_aslot;
    info.last_irreversible_block_num = dgpo.last_irreversible_block_num;
}

DEFINE_API ( plugin, get_block_info ) {
    auto start_block_num = args.args->at(0).as<uint32_t>();
    auto count = args.args->at(1).as<uint32_t>();
    auto &db = my->database();
    return db.with_weak_read_lock([&]() {
        return  my->get_block_info(start_block_num, count);
    });
}

DEFINE_API ( plugin, get_blocks_with_info ) {
    auto start_block_num = args.args->at(0).as<uint32_t>();
    auto count = args.args->at(1).as<uint32_t>();
    auto &db = my->database();
    return db.with_weak_read_lock([&]() {
        return my->get_blocks_with_info(start_block_num, count);
    });
}

void plugin::on_applied_block(const protocol::signed_block &b) {
    return my->on_applied_block(b);
}


plugin::plugin() {
}

plugin::~plugin() {
}

void plugin::plugin_initialize(const boost::program_options::variables_map &options) {

    auto &db = appbase::app().get_plugin<chain::plugin>().db();

    my.reset(new plugin_impl);

    my->applied_block_conn_ = db.applied_block.connect([this](const protocol::signed_block &b) {
        on_applied_block(b);
    });

    JSON_RPC_REGISTER_API ( name() ) ;
}

void plugin::plugin_startup() {
}

void plugin::plugin_shutdown() {
}

} } } // graphene::plugin::block_info
