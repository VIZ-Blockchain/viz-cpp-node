#include <limits>

#include <boost/program_options/options_description.hpp>
#include <graphene/plugins/custom_protocol_api/custom_protocol_api.hpp>
#include <graphene/plugins/custom_protocol_api/custom_protocol_api_object.hpp>
#include <graphene/plugins/custom_protocol_api/custom_protocol_api_visitor.hpp>
#include <graphene/api/account_api_object.hpp>
#include <graphene/chain/index.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/operation_notification.hpp>

#define CHECK_ARG_SIZE(_S)                                 \
   FC_ASSERT(                                              \
       args.args->size() == _S,                            \
       "Expected #_S argument(s), was ${n}",               \
       ("n", args.args->size()) );

#define CHECK_ARG_MIN_SIZE(_S, _M)                         \
   FC_ASSERT(                                              \
       args.args->size() >= _S && args.args->size() <= _M, \
       "Expected #_S (maximum #_M) argument(s), was ${n}", \
       ("n", args.args->size()) );

#define GET_OPTIONAL_ARG(_I, _T, _D)   \
   (args.args->size() > _I) ?          \
   (args.args->at(_I).as<_T>()) :      \
   static_cast<_T>(_D)

#ifndef DEFAULT_VOTE_LIMIT
#  define DEFAULT_VOTE_LIMIT 10000
#endif

namespace graphene { namespace plugins { namespace custom_protocol_api {

    struct custom_protocol_api_plugin::impl final {
        impl(): database_(appbase::app().get_plugin<chain::plugin>().db()) {
        }

        ~impl() = default;

        void on_operation(const operation_notification& note) {
            try {
                /// plugins shouldn't ever throw
                note.op.visit(custom_protocol_api::operation_visitor(database(),custom_protocol_store_size));
            } catch (const fc::exception& e) {
                edump((e.to_detail_string()));
            } catch (...) {
                elog("unhandled exception");
            }
        }

        graphene::chain::database& database() {
            return database_;
        }

        graphene::chain::database& database() const {
            return database_;
        }

        uint8_t custom_protocol_store_size = 10;

    private:
        graphene::chain::database& database_;
    };


    void custom_protocol_api_plugin::plugin_startup() {
        wlog("custom_protocol_api plugin: plugin_startup()");
    }

    void custom_protocol_api_plugin::plugin_shutdown() {
        wlog("custom_protocol_api plugin: plugin_shutdown()");
    }

    const std::string& custom_protocol_api_plugin::name() {
        static const std::string name = "custom_protocol_api";
        return name;
    }

    custom_protocol_api_plugin::custom_protocol_api_plugin() {
    }

    void custom_protocol_api_plugin::set_program_options(
        boost::program_options::options_description& cli,
        boost::program_options::options_description& cfg
    ) {
        cli.add_options()
            ("custom-protocol-store-size", boost::program_options::value<uint32_t>()->default_value(10),
                "Set the maximum store size for custom protocols used by account");
        cfg.add(cli);
    }

    void custom_protocol_api_plugin::plugin_initialize(const boost::program_options::variables_map& options) {
        pimpl = std::make_unique<impl>();
        auto& db = pimpl->database();
        db.post_apply_operation.connect([&](const operation_notification& note) {
            pimpl->on_operation(note);
        });
        add_plugin_index<custom_protocol_api::custom_protocol_index>(db);

        if (options.count("custom-protocol-store-size")) {
            uint32_t _custom_protocol_store_size = options["custom-protocol-store-size"].as<uint32_t>();
            FC_ASSERT(_custom_protocol_store_size <= std::numeric_limits<uint8_t>::max(),
                "custom-protocol-store-size must be <= ${max}", ("max", std::numeric_limits<uint8_t>::max()));
            pimpl->custom_protocol_store_size = static_cast<uint8_t>(_custom_protocol_store_size);
        }

        JSON_RPC_REGISTER_API(name());
    }

    custom_protocol_api_plugin::~custom_protocol_api_plugin() = default;

    DEFINE_API(custom_protocol_api_plugin, get_account) {
        CHECK_ARG_SIZE(2)
        auto account = args.args->at(0).as<string>();
        auto custom_protocol_id = args.args->at(1).as<string>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto &idx = db.get_index<account_index>().indices().get<by_name>();
            const auto &vidx = db.get_index<witness_vote_index>().indices().get<by_account_witness>();
            account_api_object result;

            auto itr = idx.find(account);
            if (itr != idx.end()) {
                result=account_api_object(*itr, db);

                auto vitr = vidx.lower_bound(boost::make_tuple(itr->id, witness_id_type()));
                while (vitr != vidx.end() && vitr->account == itr->id) {
                    result.witness_votes.insert(db.get(vitr->witness).owner);
                    ++vitr;
                }

                if(""!=custom_protocol_id){
                    result.custom_sequence=0;
                    result.custom_sequence_block_num=0;
                    const auto &cpidx = db.get_index<custom_protocol_index>().indices().get<by_account_custom_sequence_block_num>();
                    auto cpitr = cpidx.lower_bound(boost::make_tuple(account, uint64_t(-1)));
                    bool find=false;
                    while(!find && cpitr != cpidx.end() && cpitr->account == account){
                        const auto& custom_protocol_item = *cpitr;
                        ++cpitr;
                        if(custom_protocol_id==custom_protocol_item.custom_protocol_id){
                            result.custom_sequence=custom_protocol_item.custom_sequence;
                            result.custom_sequence_block_num=custom_protocol_item.custom_sequence_block_num;
                            find=true;
                        }
                    }
                }
            }
            else{
                FC_ASSERT(false,"Account with name \"${name}\" not found.", ("name", account) );
            }
            return result;
        });
    }

} } } // graphene::plugins::custom_protocol_api
