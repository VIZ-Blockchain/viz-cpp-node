#include <graphene/plugins/follow/follow_objects.hpp>
#include <graphene/plugins/follow/follow_operations.hpp>
#include <graphene/plugins/follow/follow_evaluators.hpp>
#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>
#else
#include <graphene/protocol/config.hpp>
#endif
#include <graphene/chain/database.hpp>
#include <graphene/chain/generic_custom_operation_interpreter.hpp>
#include <graphene/chain/operation_notification.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/content_object.hpp>
#include <memory>
#include <graphene/plugins/json_rpc/plugin.hpp>
#include <graphene/chain/index.hpp>

#define CHECK_ARG_SIZE(s) \
   FC_ASSERT( args.args->size() == s, "Expected #s argument(s), was ${n}", ("n", args.args->size()) );

namespace graphene {
    namespace plugins {
        namespace follow {
            using namespace graphene::protocol;
            using graphene::chain::generic_custom_operation_interpreter;
            using graphene::chain::custom_operation_interpreter;
            using graphene::chain::operation_notification;
            using graphene::chain::to_string;
            using graphene::chain::account_index;
            using graphene::chain::by_name;

            struct pre_operation_visitor {
                plugin &_plugin;
                graphene::chain::database &db;

                pre_operation_visitor(plugin &plugin, graphene::chain::database &db) : _plugin(plugin), db(db) {
                }

                typedef void result_type;

                template<typename T>
                void operator()(const T &) const {
                }

                void operator()(const delete_content_operation &op) const {
                    try {
                        const auto *content = db.find_content(op.author, op.permlink);

                        if (content == nullptr) {
                            return;
                        }
                        if (content->parent_author.size()) {
                            return;
                        }

                        const auto &feed_idx = db.get_index<feed_index>().indices().get<by_content>();
                        auto itr = feed_idx.lower_bound(content->id);

                        while (itr != feed_idx.end() && itr->content == content->id) {
                            const auto &old_feed = *itr;
                            ++itr;
                            db.remove(old_feed);
                        }

                        const auto &blog_idx = db.get_index<blog_index>().indices().get<by_content>();
                        auto blog_itr = blog_idx.lower_bound(content->id);

                        while (blog_itr != blog_idx.end() && blog_itr->content == content->id) {
                            const auto &old_blog = *blog_itr;
                            ++blog_itr;
                            db.remove(old_blog);
                        }
                    } FC_CAPTURE_AND_RETHROW()
                }
            };

            struct post_operation_visitor {
                plugin &_plugin;
                database &db;

                post_operation_visitor(plugin &plugin, database &db) : _plugin(plugin), db(db) {
                }

                typedef void result_type;

                template<typename T>
                void operator()(const T &) const {
                }

                void operator()(const custom_operation &op) const {
                    try {
                        if (op.id == plugin::plugin_name) {
                            custom_operation new_cop;

                            new_cop.required_active_auths = op.required_active_auths;
                            new_cop.required_regular_auths = op.required_regular_auths;
                            new_cop.id = plugin::plugin_name;
                            follow_operation fop;

                            try {
                                fop = fc::json::from_string(op.json).as<follow_operation>();
                            } catch (const fc::exception &) {
                                return;
                            }

                            auto new_fop = follow_plugin_operation(fop);
                            new_cop.json = fc::json::to_string(new_fop);
                            std::shared_ptr<custom_operation_interpreter> eval = db.get_custom_evaluator(op.id);
                            eval->apply(new_cop);
                        }
                    } FC_CAPTURE_AND_RETHROW()
                }

                void operator()(const content_operation &op) const {
                    try {
                        if (op.parent_author.size() > 0) {
                            return;
                        }

                        const auto &c = db.get_content(op.author, op.permlink);

                        if (c.created != db.head_block_time()) {
                            return;
                        }

                        const auto &idx = db.get_index<follow_index>().indices().get<by_following_follower>();
                        const auto &content_idx = db.get_index<feed_index>().indices().get<by_content>();
                        auto itr = idx.find(op.author);

                        const auto &feed_idx = db.get_index<feed_index>().indices().get<by_feed>();

                        while (itr != idx.end() && itr->following == op.author) {
                            if (itr->what & (1 << blog)) {
                                uint32_t next_id = 0;
                                auto last_feed = feed_idx.lower_bound(itr->follower);

                                if (last_feed != feed_idx.end() && last_feed->account == itr->follower) {
                                    next_id = last_feed->account_feed_id + 1;
                                }

                                if (content_idx.find(boost::make_tuple(c.id, itr->follower)) == content_idx.end()) {
                                    db.create<feed_object>([&](feed_object &f) {
                                        f.account = itr->follower;
                                        f.content = c.id;
                                        f.account_feed_id = next_id;
                                    });

                                    const auto &old_feed_idx = db.get_index<feed_index>().indices().get<by_old_feed>();
                                    auto old_feed = old_feed_idx.lower_bound(itr->follower);

                                    while (old_feed->account == itr->follower &&
                                           next_id - old_feed->account_feed_id > _plugin.max_feed_size()) {
                                        db.remove(*old_feed);
                                        old_feed = old_feed_idx.lower_bound(itr->follower);
                                    }
                                }
                            }

                            ++itr;
                        }

                        const auto &blog_idx = db.get_index<blog_index>().indices().get<by_blog>();
                        const auto &content_blog_idx = db.get_index<blog_index>().indices().get<by_content>();
                        auto last_blog = blog_idx.lower_bound(op.author);
                        uint32_t next_id = 0;

                        if (last_blog != blog_idx.end() && last_blog->account == op.author) {
                            next_id = last_blog->blog_feed_id + 1;
                        }

                        if (content_blog_idx.find(boost::make_tuple(c.id, op.author)) == content_blog_idx.end()) {
                            db.create<blog_object>([&](blog_object &b) {
                                b.account = op.author;
                                b.content = c.id;
                                b.blog_feed_id = next_id;
                            });

                            const auto &old_blog_idx = db.get_index<blog_index>().indices().get<by_old_blog>();
                            auto old_blog = old_blog_idx.lower_bound(op.author);

                            while (old_blog->account == op.author &&
                                   next_id - old_blog->blog_feed_id > _plugin.max_feed_size()) {
                                db.remove(*old_blog);
                                old_blog = old_blog_idx.lower_bound(op.author);
                            }
                        }
                    } FC_LOG_AND_RETHROW()
                }
            };

            inline void set_what(std::vector<follow_type> &what, uint16_t bitmask) {
                if (bitmask & 1 << blog) {
                    what.push_back(blog);
                }
                if (bitmask & 1 << ignore) {
                    what.push_back(ignore);
                }
            }

            struct plugin::impl final {
            public:
                impl() : database_(appbase::app().get_plugin<chain::plugin>().db()) {
                }

                ~impl() {
                };

                void plugin_initialize(plugin &self) {
                    // Each plugin needs its own evaluator registry.
                    _custom_operation_interpreter = std::make_shared<
                            generic_custom_operation_interpreter<follow_plugin_operation>>(database());

                    // Add each operation evaluator to the registry
                    _custom_operation_interpreter->register_evaluator<follow_evaluator>(&self);
                    _custom_operation_interpreter->register_evaluator<reblog_evaluator>(&self);

                    // Add the registry to the database so the database can delegate custom ops to the plugin
                    database().set_custom_operation_interpreter(plugin_name, _custom_operation_interpreter);
                }

                graphene::chain::database &database() {
                    return database_;
                }


                void pre_operation(const operation_notification &op_obj, plugin &self) {
                    try {
                        op_obj.op.visit(pre_operation_visitor(self, database()));
                    } catch (const fc::assert_exception &) {
                        if (database().is_producing()) {
                            throw;
                        }
                    }
                }

                void post_operation(const operation_notification &op_obj, plugin &self) {
                    try {
                        op_obj.op.visit(post_operation_visitor(self, database()));
                    } catch (const fc::assert_exception&) {
                        if (database().is_producing()) {
                            throw;
                        }
                    }
                }

                std::vector<follow_api_object> get_followers(
                        account_name_type account,
                        account_name_type start,
                        follow_type type,
                        uint32_t limit = 1000);

                std::vector<follow_api_object> get_following(
                        account_name_type account,
                        account_name_type start,
                        follow_type type,
                        uint32_t limit = 1000);

                std::vector<feed_entry> get_feed_entries(
                        account_name_type account,
                        uint32_t start_entry_id = 0,
                        uint32_t limit = 500);

                std::vector<blog_entry> get_blog_entries(
                        account_name_type account,
                        uint32_t start_entry_id = 0,
                        uint32_t limit = 500);

                std::vector<content_feed_entry> get_feed(
                        account_name_type account,
                        uint32_t start_entry_id = 0,
                        uint32_t limit = 500);

                std::vector<content_blog_entry> get_blog(
                        account_name_type account,
                        uint32_t start_entry_id = 0,
                        uint32_t limit = 500);

                follow_count_api_obj get_follow_count(account_name_type start);

                std::vector<account_name_type> get_reblogged_by(
                        account_name_type author,
                        std::string permlink);

                blog_authors_r get_blog_authors(account_name_type );

                graphene::chain::database &database_;

                uint32_t max_feed_size_ = 500;

                std::shared_ptr<generic_custom_operation_interpreter<
                        follow::follow_plugin_operation>> _custom_operation_interpreter;
            };

            plugin::plugin() {

            }

            void plugin::set_program_options(boost::program_options::options_description &cli,
                                                    boost::program_options::options_description &cfg) {
                cli.add_options()
                    ("follow-max-feed-size", boost::program_options::value<uint32_t>()->default_value(500),
                        "Set the maximum size of cached feed for an account");
                cfg.add(cli);
            }

            void plugin::plugin_initialize(const boost::program_options::variables_map &options) {
                try {
                    ilog("Intializing follow plugin");
                    pimpl.reset(new impl());
                    auto &db = pimpl->database();
                    pimpl->plugin_initialize(*this);

                    db.pre_apply_operation.connect([&](operation_notification &o) {
                        pimpl->pre_operation(o, *this);
                    });
                    db.post_apply_operation.connect([&](const operation_notification &o) {
                        pimpl->post_operation(o, *this);
                    });
                    graphene::chain::add_plugin_index<follow_index>(db);
                    graphene::chain::add_plugin_index<feed_index>(db);
                    graphene::chain::add_plugin_index<blog_index>(db);
                    graphene::chain::add_plugin_index<follow_count_index>(db);
                    graphene::chain::add_plugin_index<blog_author_stats_index>(db);

                    if (options.count("follow-max-feed-size")) {
                        uint32_t feed_size = options["follow-max-feed-size"].as<uint32_t>();
                        pimpl->max_feed_size_ = feed_size;
                    }

                    JSON_RPC_REGISTER_API ( name() ) ;
                } FC_CAPTURE_AND_RETHROW()
            }

            void plugin::plugin_startup() {
            }

            uint32_t plugin::max_feed_size() {
                return pimpl->max_feed_size_;
            }

            plugin::~plugin() {

            }


            std::vector<follow_api_object> plugin::impl::get_followers(
                    account_name_type account,
                    account_name_type start,
                    follow_type type,
                    uint32_t limit) {

                FC_ASSERT(limit <= 1000);
                std::vector<follow_api_object> result;
                result.reserve(limit);

                const auto &idx = database().get_index<follow_index>().indices().get<by_following_follower>();
                auto itr = idx.lower_bound(std::make_tuple(account, start));
                while (itr != idx.end() && result.size() < limit && itr->following == account) {
                    if (type == undefined || itr->what & (1 << type)) {
                        follow_api_object entry;
                        entry.follower = itr->follower;
                        entry.following = itr->following;
                        set_what(entry.what, itr->what);
                        result.push_back(entry);
                    }

                    ++itr;
                }

                return result;
            }

            std::vector<follow_api_object> plugin::impl::get_following(
                    account_name_type account,
                    account_name_type start,
                    follow_type type,
                    uint32_t limit) {
                FC_ASSERT(limit <= 100);
                std::vector<follow_api_object> result;
                const auto &idx = database().get_index<follow_index>().indices().get<by_follower_following>();
                auto itr = idx.lower_bound(std::make_tuple(account, start));
                while (itr != idx.end() && result.size() < limit && itr->follower == account) {
                    if (type == undefined || itr->what & (1 << type)) {
                        follow_api_object entry;
                        entry.follower = itr->follower;
                        entry.following = itr->following;
                        set_what(entry.what, itr->what);
                        result.push_back(entry);
                    }

                    ++itr;
                }

                return result;
            }

            follow_count_api_obj plugin::impl::get_follow_count(account_name_type acct) {
                follow_count_api_obj result;
                auto itr = database().find<follow_count_object, by_account>(acct);

                if (itr != nullptr) {
                    result = follow_count_api_obj(itr->account, itr->follower_count, itr->following_count, 1000);
                } else {
                    result.account = acct;
                }

                return result;
            }

            std::vector<feed_entry> plugin::impl::get_feed_entries(
                    account_name_type account,
                    uint32_t entry_id,
                    uint32_t limit) {
                FC_ASSERT(limit <= 500, "Cannot retrieve more than 500 feed entries at a time.");

                if (entry_id == 0) {
                    entry_id = ~0;
                }

                std::vector<feed_entry> result;
                result.reserve(limit);

                const auto &db = database();
                const auto &feed_idx = db.get_index<feed_index>().indices().get<by_feed>();
                auto itr = feed_idx.lower_bound(boost::make_tuple(account, entry_id));

                while (itr != feed_idx.end() && itr->account == account && result.size() < limit) {
                    const auto &content = db.get(itr->content);
                    feed_entry entry;
                    entry.author = content.author;
                    entry.permlink = to_string(content.permlink);
                    entry.entry_id = itr->account_feed_id;
                    if (itr->first_reblogged_by != account_name_type()) {
                        entry.reblog_by.reserve(itr->reblogged_by.size());
                        for (const auto &a : itr->reblogged_by) {
                            entry.reblog_by.push_back(a);
                        }
                        //entry.reblog_by = itr->first_reblogged_by;
                        entry.reblog_on = itr->first_reblogged_on;
                    }
                    result.push_back(entry);

                    ++itr;
                }

                return result;
            }

            std::vector<content_feed_entry> plugin::impl::get_feed(
                    account_name_type account,
                    uint32_t entry_id,
                    uint32_t limit) {
                FC_ASSERT(limit <= 500, "Cannot retrieve more than 500 feed entries at a time.");

                if (entry_id == 0) {
                    entry_id = ~0;
                }

                std::vector<content_feed_entry> result;
                result.reserve(limit);

                const auto &db = database();
                const auto &feed_idx = db.get_index<feed_index>().indices().get<by_feed>();
                auto itr = feed_idx.lower_bound(boost::make_tuple(account, entry_id));

                while (itr != feed_idx.end() && itr->account == account && result.size() < limit) {
                    const auto &content = db.get(itr->content);
                    content_feed_entry entry;
                    entry.content = content_api_object(content, db);
                    entry.entry_id = itr->account_feed_id;
                    if (itr->first_reblogged_by != account_name_type()) {
                        //entry.reblog_by = itr->first_reblogged_by;
                        entry.reblog_by.reserve(itr->reblogged_by.size());
                        for (const auto &a : itr->reblogged_by) {
                            entry.reblog_by.push_back(a);
                        }
                        entry.reblog_on = itr->first_reblogged_on;
                    }
                    result.push_back(entry);

                    ++itr;
                }

                return result;
            }

            std::vector<blog_entry> plugin::impl::get_blog_entries(
                    account_name_type account,
                    uint32_t entry_id,
                    uint32_t limit) {
                FC_ASSERT(limit <= 500, "Cannot retrieve more than 500 blog entries at a time.");

                if (entry_id == 0) {
                    entry_id = ~0;
                }

                std::vector<blog_entry> result;
                result.reserve(limit);

                const auto &db = database();
                const auto &blog_idx = db.get_index<blog_index>().indices().get<by_blog>();
                auto itr = blog_idx.lower_bound(boost::make_tuple(account, entry_id));

                while (itr != blog_idx.end() && itr->account == account && result.size() < limit) {
                    const auto &content = db.get(itr->content);
                    blog_entry entry;
                    entry.author = content.author;
                    entry.permlink = to_string(content.permlink);
                    entry.blog = account;
                    entry.reblog_on = itr->reblogged_on;
                    entry.entry_id = itr->blog_feed_id;

                    result.push_back(entry);

                    ++itr;
                }

                return result;
            }

            std::vector<content_blog_entry> plugin::impl::get_blog(
                    account_name_type account,
                    uint32_t entry_id,
                    uint32_t limit) {
                FC_ASSERT(limit <= 500, "Cannot retrieve more than 500 blog entries at a time.");

                if (entry_id == 0) {
                    entry_id = ~0;
                }

                std::vector<content_blog_entry> result;
                result.reserve(limit);

                const auto &db = database();
                const auto &blog_idx = db.get_index<blog_index>().indices().get<by_blog>();
                auto itr = blog_idx.lower_bound(boost::make_tuple(account, entry_id));

                while (itr != blog_idx.end() && itr->account == account && result.size() < limit) {
                    const auto &content = db.get(itr->content);
                    content_blog_entry entry;
                    entry.content = content_api_object(content, db);
                    entry.blog = account;
                    entry.reblog_on = itr->reblogged_on;
                    entry.entry_id = itr->blog_feed_id;

                    result.push_back(entry);

                    ++itr;
                }

                return result;
            }

            std::vector<account_name_type> plugin::impl::get_reblogged_by(
                    account_name_type author,
                    std::string permlink
            ) {
                auto &db = database();
                std::vector<account_name_type> result;
                const auto &post = db.get_content(author, permlink);
                const auto &blog_idx = db.get_index<blog_index, by_content>();
                auto itr = blog_idx.lower_bound(post.id);
                while (itr != blog_idx.end() && itr->content == post.id && result.size() < 2000) {
                    result.push_back(itr->account);
                    ++itr;
                }
                return result;
            }

            blog_authors_r plugin::impl::get_blog_authors(account_name_type blog_account) {
                auto &db = database();
                blog_authors_r result;
                const auto &stats_idx = db.get_index<blog_author_stats_index, by_blogger_guest_count>();
                auto itr = stats_idx.lower_bound(boost::make_tuple(blog_account));
                while (itr != stats_idx.end() && itr->blogger == blog_account && result.size()) {
                    result.emplace_back(itr->guest, itr->count);
                    ++itr;
                }
                return result;
            }

            DEFINE_API(plugin, get_followers) {
                CHECK_ARG_SIZE(4)
                auto following = args.args->at(0).as<account_name_type>();
                auto start_follower = args.args->at(1).as<account_name_type>();
                auto type = args.args->at(2).as<follow_type>();
                auto limit = args.args->at(3).as<uint32_t>();
                return pimpl->database().with_weak_read_lock([&]() {
                    return pimpl->get_followers(following, start_follower, type, limit);
                });
            }

            DEFINE_API(plugin, get_following) {
                CHECK_ARG_SIZE(4)
                auto follower = args.args->at(0).as<account_name_type>();
                auto start_following = args.args->at(1).as<account_name_type>();
                auto type = args.args->at(2).as<follow_type>();
                auto limit = args.args->at(3).as<uint32_t>();
                return pimpl->database().with_weak_read_lock([&]() {
                    return pimpl->get_following(follower, start_following, type, limit);
                });
            }

            DEFINE_API(plugin, get_follow_count) {
                auto tmp = args.args->at(0).as<account_name_type>();
                return pimpl->database().with_weak_read_lock([&]() {
                    return pimpl->get_follow_count(tmp);
                });
            }

            DEFINE_API(plugin, get_feed_entries){
                CHECK_ARG_SIZE(3)
                auto account = args.args->at(0).as<account_name_type>();
                auto entry_id = args.args->at(1).as<uint32_t>();
                auto limit = args.args->at(2).as<uint32_t>();
                return pimpl->database().with_weak_read_lock([&]() {
                    return pimpl->get_feed_entries(account, entry_id, limit);
                });
            }

            DEFINE_API(plugin, get_feed) {
                CHECK_ARG_SIZE(3)
                auto account = args.args->at(0).as<account_name_type>();
                auto entry_id = args.args->at(1).as<uint32_t>();
                auto limit = args.args->at(2).as<uint32_t>();
                return pimpl->database().with_weak_read_lock([&]() {
                    return pimpl->get_feed(account, entry_id, limit);
                });
            }

            DEFINE_API(plugin, get_blog_entries) {
                CHECK_ARG_SIZE(3)
                auto account = args.args->at(0).as<account_name_type>();
                auto entry_id = args.args->at(1).as<uint32_t>();
                auto limit = args.args->at(2).as<uint32_t>();
                return pimpl->database().with_weak_read_lock([&]() {
                    return pimpl->get_blog_entries(account, entry_id, limit);
                });
            }

            DEFINE_API(plugin, get_blog) {
                CHECK_ARG_SIZE(3)
                auto account = args.args->at(0).as<account_name_type>();
                auto entry_id = args.args->at(1).as<uint32_t>();
                auto limit = args.args->at(2).as<uint32_t>();
                return pimpl->database().with_weak_read_lock([&]() {
                    return pimpl->get_blog(account, entry_id, limit);
                });
            }

            DEFINE_API(plugin, get_reblogged_by) {
                CHECK_ARG_SIZE(2)
                auto author = args.args->at(0).as<account_name_type>();
                auto permlink = args.args->at(1).as<std::string>();
                return pimpl->database().with_weak_read_lock([&]() {
                    return pimpl->get_reblogged_by(author, permlink);
                });
            }

            DEFINE_API(plugin, get_blog_authors) {
                auto tmp = args.args->at(0).as<account_name_type>();
                return pimpl->database().with_weak_read_lock([&]() {
                    return pimpl->get_blog_authors(tmp);
                });
            }
        }
    }
} // graphene::follow
