#include <boost/algorithm/string.hpp>
#include <graphene/plugins/tags/tag_visitor.hpp>

namespace graphene { namespace plugins { namespace tags {

    operation_visitor::operation_visitor(database& db)
        : db_(db) {
    }

    void operation_visitor::remove_stats(const tag_object& tag) const {
        const auto& idx = db_.get_index<tag_stats_index>().indices().get<by_tag>();
        auto itr = idx.find(std::make_tuple(tag.type, tag.name));
        if (itr == idx.end()) {
            return;
        }

        bool need_remove = false;
        db_.modify(*itr, [&](tag_stats_object& s) {
            if (tag.parent == content_object::id_type()) {
                s.total_children_rshares -= tag.children_rshares;
                s.top_posts--;
            } else {
                s.contents--;
            }
            s.net_votes -= tag.net_votes;

            need_remove = (s.top_posts == 0) && (s.contents == 0);
        });

        if (need_remove) {
            if (itr->type == tag_type::language) {
                auto& lidx = db_.get_index<language_index>().indices().get<by_tag>();
                auto litr = lidx.find(tag.name);
                if (lidx.end() != litr) {
                    db_.remove(*litr);
                }
            }
            db_.remove(*itr);
        }
    }

    const tag_stats_object& operation_visitor::get_stats(const tag_object& tag) const {
        const auto& idx = db_.get_index<tag_stats_index>().indices().get<by_tag>();
        auto itr = idx.find(std::make_tuple(tag.type, tag.name));
        if (itr != idx.end()) {
            return *itr;
        }

        return db_.create<tag_stats_object>([&](tag_stats_object& stats) {
            stats.name = tag.name;
            stats.type = tag.type;
        });
    }

    void operation_visitor::add_stats(const tag_object& tag) const {
        db_.modify(get_stats(tag), [&](tag_stats_object& s) {
            if (tag.parent == content_object::id_type()) {
                s.total_children_rshares += tag.children_rshares;
                s.top_posts++;
            } else {
                s.contents++;
            }
            s.net_votes += tag.net_votes;
        });
    }

    void operation_visitor::remove_tag(const tag_object& tag) const {
        const auto& idx = db_.get_index<author_tag_stats_index>().indices().get<by_author_tag_posts>();
        auto itr = idx.lower_bound(std::make_tuple(tag.author, tag.type, tag.name));
        if (itr != idx.end() && itr->author == tag.author && itr->name == tag.name && itr->type == tag.type) {
            if (itr->total_posts == 1) {
                db_.remove(*itr);
            } else {
                db_.modify(*itr, [&](author_tag_stats_object& stats) {
                    stats.total_posts--;
                });
            }
        }

        remove_stats(tag);
        db_.remove(tag);
    }

    void operation_visitor::update_tag(
        const tag_object& current, const content_object& content, double hot, double trending
    ) const {
        auto cashout_time = db_.calculate_discussion_payout_time(content);
        remove_stats(current);
        db_.modify(current, [&](tag_object& obj) {
            obj.active = content.active;
            obj.cashout = cashout_time;
            obj.children = content.children;
            obj.net_rshares = content.net_rshares.value;
            obj.net_votes = content.net_votes;
            obj.children_rshares = content.children_rshares;
            obj.hot = hot;
            obj.trending = trending;
        });
        add_stats(current);
    }

    void operation_visitor::create_tag(
        const std::string& name, const tag_type type, const content_object& content, double hot, double trending
    ) const {
        auto author = db_.get_account(content.author).id;

        content_object::id_type parent;
        if (content.parent_author.size()) {
            parent = db_.get_content(content.parent_author, content.parent_permlink).id;
        }

        const auto& tag_obj = db_.create<tag_object>([&](tag_object& obj) {
            obj.name = name;
            obj.type = type;
            obj.content = content.id;
            obj.parent = parent;
            obj.created = content.created;
            obj.active = content.active;
            obj.updated = content.last_update;
            obj.cashout = content.cashout_time;
            obj.net_votes = content.net_votes;
            obj.children = content.children;
            obj.net_rshares = content.net_rshares.value;
            obj.children_rshares = content.children_rshares;
            obj.author = author;
            obj.hot = hot;
            obj.trending = trending;
        });

        add_stats(tag_obj);

        const auto& idx = db_.get_index<author_tag_stats_index>().indices().get<by_author_tag_posts>();
        auto itr = idx.lower_bound(std::make_tuple(author, type, name));
        if (itr != idx.end() && itr->author == author && itr->name == name) {
            db_.modify(*itr, [&](author_tag_stats_object& stats) {
                stats.total_posts++;
            });
        } else {
            db_.create<author_tag_stats_object>([&](author_tag_stats_object& stats) {
                stats.author = author;
                stats.name = name;
                stats.type = type;
                stats.total_posts = 1;
            });
        }

        if (type == tag_type::language) {
            auto& lidx = db_.get_index<language_index>().indices().get<by_tag>();
            auto litr = lidx.find(name);
            if (litr == lidx.end()) {
                db_.create<language_object>([&](language_object& object) {
                    object.name = name;
                });
            }
        }
    }

    double operation_visitor::calculate_hot(const share_type& score, const time_point_sec& created) const {
        return calculate_score<10000000, 10000>(score, created);
    }

    double operation_visitor::calculate_trending(const share_type& score, const time_point_sec& created) const {
        return calculate_score<10000000, 480000>(score, created);
    }

    void operation_visitor::create_update_tags(
        const account_name_type& author, const std::string& permlink
    ) const { try {
        const auto& content = db_.get_content(author, permlink);
        auto hot = calculate_hot(content.net_rshares, content.created);
        auto trending = calculate_trending(content.net_rshares, content.created);
        const auto& content_idx = db_.get_index<tag_index>().indices().get<by_content>();

        auto meta = get_metadata(content_api_object(content, db_));
        auto citr = content_idx.lower_bound(content.id);
        const tag_object* language_tag = nullptr;

        if (meta.tags.empty()) {
            meta.tags.insert(std::string());
        }

        std::map<std::string, const tag_object*> existing_tags;
        std::vector<const tag_object*> remove_queue;
        for (; citr != content_idx.end() && citr->content == content.id; ++citr) {
            const tag_object* tag = &*citr;
            switch (tag->type) {
                case tag_type::tag:
                    if (meta.tags.find(tag->name) == meta.tags.end()) {
                        remove_queue.push_back(tag);
                    } else {
                        existing_tags[tag->name] = tag;
                    }
                    break;

                case tag_type::language:
                    language_tag = tag;
                    if (meta.language != language_tag->name) {
                        remove_queue.push_back(tag);
                    }
                    break;
            }
        }

        for (const auto& name : meta.tags) {
            auto existing = existing_tags.find(name);
            if (existing == existing_tags.end()) {
                create_tag(name, tag_type::tag, content, hot, trending);
            } else {
                update_tag(*existing->second, content, hot, trending);
            }
        }

        if (!meta.language.empty() && (!language_tag || meta.language != language_tag->name)) {
            create_tag(meta.language, tag_type::language, content, hot, trending);
        }

        for (const auto& item : remove_queue) {
            remove_tag(*item);
        }
    } FC_CAPTURE_LOG_AND_RETHROW(()) }

    void operation_visitor::update_tags(const account_name_type& author, const std::string& permlink) const {
        const auto& content = db_.get_content(author, permlink);
        auto hot = calculate_hot(content.net_rshares, content.created);
        auto trending = calculate_trending(content.net_rshares, content.created);
        const auto& content_idx = db_.get_index<tag_index>().indices().get<by_content>();

        auto citr = content_idx.lower_bound(content.id);
        for (; citr != content_idx.end() && citr->content == content.id; ++citr) {
            update_tag(*citr, content, hot, trending);
        }

        if (content.parent_author.size()) {
            update_tags(content.parent_author, to_string(content.parent_permlink));
        }
    }

    void operation_visitor::remove_tags(const account_name_type& author, const std::string& permlink) const {
        const auto& content = db_.get_content(author, permlink);
        const auto& content_idx = db_.get_index<tag_index>().indices().get<by_content>();
        std::vector<const tag_object*> remove_queue;

        remove_queue.reserve(10);
        auto citr = content_idx.lower_bound(content.id);
        for (; citr != content_idx.end() && citr->content == content.id; ++citr) {
            const tag_object* tag = &*citr;
            remove_queue.push_back(tag);
        }

        for (const auto& tag : remove_queue) {
            remove_tag(*tag);
        }

        if (content.parent_author.size()) {
            update_tags(content.parent_author, to_string(content.parent_permlink));
        }
    }

    void operation_visitor::operator()(const delete_content_operation& op) const {
        const auto& author = db_.get_account(op.author).id;
        const auto& idx = db_.get_index<tag_index>().indices().get<by_author_content>();
        auto itr = idx.lower_bound(author);
        while (itr != idx.end() && itr->author == author) {
            const auto& tag = *itr;
            const auto* content = db_.find<content_object>(itr->content);
            ++itr;
            if (!content) {
                remove_tag(tag);
            }
        }
    }

    void operation_visitor::operator()(const content_reward_operation& op) const {
        // only update existing tags
        update_tags(op.author, op.permlink);

        const auto& content = db_.get_content(op.author, op.permlink);
        const auto& author = db_.get_account(op.author).id;

        auto meta = get_metadata(content_api_object(content, db_));
        const auto& stats_idx = db_.get_index<tag_stats_index>().indices().get<by_tag>();
        const auto& auth_idx = db_.get_index<author_tag_stats_index>().indices().get<by_author_tag_posts>();

        auto update_payout = [&](const tag_type type, const auto& name) {
            auto sitr = stats_idx.find(std::make_tuple(type, name));
            if (stats_idx.end() != sitr) {
                db_.modify(*sitr, [&](tag_stats_object& ts) {
                    ts.total_payout.amount += op.payout.amount;
                });
            }

            auto aitr = auth_idx.find(std::make_tuple(author, type, name));
            if (auth_idx.end() != aitr) {
                db_.modify(*aitr, [&](author_tag_stats_object& as) {
                    as.total_rewards.amount += op.payout.amount;
                });
            }
        };

        for (const auto& name : meta.tags) {
            update_payout(tag_type::tag, name );
        }

        if (!meta.language.empty()) {
            update_payout(tag_type::language, meta.language);
        }
    }

    void operation_visitor::operator()(const content_operation& op) const {
        const auto& content = db_.get_content(op.author, op.permlink);

        if (db_.calculate_discussion_payout_time(content) != fc::time_point_sec::maximum()) {
            // in a cashout window
            create_update_tags(op.author, op.permlink);
        }
    }

    void operation_visitor::operator()(const vote_operation& op) const {
        // only update existing tags
        update_tags(op.author, op.permlink);
    }

    void operation_visitor::operator()(const content_payout_update_operation& op) const {
        const auto& content = db_.get_content(op.author, op.permlink);
        const auto cashout_time = db_.calculate_discussion_payout_time(content);

        if (cashout_time != fc::time_point_sec::maximum()) {
            update_tags(op.author, op.permlink);
        }
        /*
        else {
            // it can be the end of a cashout window
            remove_tags(op.author, op.permlink);
        }
        */
    }

} } } // graphene::plugins::tags