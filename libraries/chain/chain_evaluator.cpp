#include <graphene/chain/chain_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/custom_operation_interpreter.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/block_summary_object.hpp>

namespace graphene { namespace chain {
        using fc::uint128_t;

        inline u256 to256(const fc::uint128_t &t) {
            u256 v(t.hi);
            v <<= 64;
            v += t.lo;
            return v;
        }

        inline void validate_permlink(const string &permlink) {
            FC_ASSERT(permlink.size() < CHAIN_MAX_URL_LENGTH, "permlink is too long");
            FC_ASSERT(fc::is_utf8(permlink), "permlink not formatted in UTF8");
        }

        struct strcmp_equal {
            bool operator()(const shared_string &a, const string &b) {
                return a.size() == b.size() ||
                       std::strcmp(a.c_str(), b.c_str()) == 0;
            }
        };

        void store_account_json_metadata(
            database& db, const account_name_type& account, const string& json_metadata, bool skip_empty = false
        ) {
#ifndef IS_LOW_MEM
            if (skip_empty && json_metadata.size() == 0)
                return;

            const auto& idx = db.get_index<account_metadata_index>().indices().get<by_account>();
            auto itr = idx.find(account);
            if (itr != idx.end()) {
                db.modify(*itr, [&](account_metadata_object& a) {
                    from_string(a.json_metadata, json_metadata);
                });
            } else {
                // Note: this branch should be executed only on account creation.
                db.create<account_metadata_object>([&](account_metadata_object& a) {
                    a.account = account;
                    from_string(a.json_metadata, json_metadata);
                });
            }
#endif
        }

        void account_create_evaluator::do_apply(const account_create_operation& o) {
            const auto& creator = _db.get_account(o.creator);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!creator.valid, "Account flagged as invalid");

            const auto now = _db.head_block_time();
            FC_ASSERT(creator.balance >= o.fee, "Insufficient balance to create account.",
                ("creator.balance", creator.balance)("required", o.fee));
            FC_ASSERT(creator.available_vesting_shares(true) >= o.delegation,
                "Insufficient vesting shares to delegate to new account.",
                ("creator.vesting_shares", creator.vesting_shares)
                ("creator.delegated_vesting_shares", creator.delegated_vesting_shares)
                ("required", o.delegation));

            if(_db.has_hardfork(CHAIN_HARDFORK_6)){
                if (o.delegation.amount > 0) {
                    int64_t elapsed_seconds = (now - creator.last_vote_time).to_seconds();

                    int64_t regenerated_energy =
                            (CHAIN_100_PERCENT * elapsed_seconds) /
                            CHAIN_ENERGY_REGENERATION_SECONDS;
                    int64_t current_energy = std::min(int64_t(creator.energy +
                                                             regenerated_energy), int64_t(CHAIN_100_PERCENT));
                    FC_ASSERT(current_energy >=
                              0, "Cannot delegate with negative energy.");

                }
            }

            const auto& v_share_price = _db.get_dynamic_global_properties().get_vesting_share_price();
            const auto& median_props = _db.get_witness_schedule_object().median_props;
            auto target_delegation =
                median_props.create_account_delegation_ratio *
                median_props.account_creation_fee * v_share_price;
            auto current_delegation =
                median_props.create_account_delegation_ratio * o.fee * v_share_price + o.delegation;

            FC_ASSERT(current_delegation >= target_delegation,
                "Inssufficient Delegation ${f} required, ${p} provided.",
                ("f", target_delegation)("p", current_delegation)("o.fee", o.fee) ("o.delegation", o.delegation));

            for (auto& a : o.master.account_auths) {
                _db.get_account(a.first);
            }
            for (auto& a : o.active.account_auths) {
                _db.get_account(a.first);
            }
            for (auto& a : o.regular.account_auths) {
                _db.get_account(a.first);
            }

            _db.shares_sender_recalc_energy(creator,o.delegation);
            _db.modify(creator, [&](account_object& c) {
                c.balance -= o.fee;
                c.delegated_vesting_shares += o.delegation;
            });
            const auto& new_account = _db.create<account_object>([&](account_object& acc) {
                acc.name = o.new_account_name;
                acc.memo_key = o.memo_key;
                acc.created = now;
                acc.energy = CHAIN_100_PERCENT;
                acc.last_vote_time = now;
                acc.recovery_account = o.creator;
                acc.received_vesting_shares = o.delegation;
                if(o.referrer.size()){
                    const auto &referrer = _db.get_account(o.referrer);
                    acc.referrer = referrer.name;
                }
            });
            store_account_json_metadata(_db, o.new_account_name, o.json_metadata);

            _db.create<account_authority_object>([&](account_authority_object& auth) {
                auth.account = o.new_account_name;
                auth.master = o.master;
                auth.active = o.active;
                auth.regular = o.regular;
                auth.last_master_update = fc::time_point_sec::min();
            });
            if (o.delegation.amount > 0) {  // Is it needed to allow zero delegation in this method ?
                _db.create<vesting_delegation_object>([&](vesting_delegation_object& d) {
                    d.delegator = o.creator;
                    d.delegatee = o.new_account_name;
                    d.vesting_shares = o.delegation;
                    d.min_delegation_time = now + fc::seconds(median_props.create_account_delegation_time);
                });
            }
            if (o.fee.amount > 0) {
                _db.create_vesting(new_account, o.fee);
            }
        }

        void account_update_evaluator::do_apply(const account_update_operation &o) {
            database &_db = db();
            FC_ASSERT(o.account != CHAIN_INVITE_ACCOUNT, "Cannot update invite account.");

            if (o.regular) {
                     o.regular->validate();
            }

            const auto &account = _db.get_account(o.account);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account.valid, "Account flagged as invalid");
            const auto &account_auth = _db.get<account_authority_object, by_account>(o.account);

            if (o.master) {
                FC_ASSERT(_db.head_block_time() -
                          account_auth.last_master_update >
                          CHAIN_MASTER_UPDATE_LIMIT, "Master authority can only be updated once an hour.");
                for (auto a: o.master->account_auths) {
                    _db.get_account(a.first);
                }

                _db.update_master_authority(account, *o.master);
            }

            if (o.active)
            {
                for (auto a: o.active->account_auths) {
                    _db.get_account(a.first);
                }
            }

            if (o.regular)
            {
                for (auto a: o.regular->account_auths) {
                    _db.get_account(a.first);
                }
            }

            _db.modify(account, [&](account_object &acc) {
                if (o.memo_key != public_key_type()) {
                    acc.memo_key = o.memo_key;
                }
                acc.last_account_update = _db.head_block_time();
            });
            store_account_json_metadata(_db, account.name, o.json_metadata, true);

            if (o.active || o.regular) {
                _db.modify(account_auth, [&](account_authority_object &auth) {
                    if (o.active) {
                        auth.active = *o.active;
                    }
                    if (o.regular) {
                        auth.regular = *o.regular;
                    }
                });
            }

        }

        void account_metadata_evaluator::do_apply(const account_metadata_operation& o) {
            const auto& account = _db.get_account(o.account);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account.valid, "Account flagged as invalid");
            _db.modify(account, [&](account_object& a) {
                a.last_account_update = _db.head_block_time();
            });
            store_account_json_metadata(_db, o.account, o.json_metadata);
        }

/**
 *  Because net_rshares is 0 there is no need to update any pending payout calculations or parent posts.
 */
        void delete_content_evaluator::do_apply(const delete_content_operation &o) {
            database &_db = db();
            FC_ASSERT( !_db.has_hardfork(CHAIN_HARDFORK_4), "delete_content is deprecated as of HF 4" );
            const auto &content = _db.get_content(o.author, o.permlink);
            FC_ASSERT(content.children ==
                      0, "Cannot delete a content with replies.");

            if (_db.is_producing()) {
                FC_ASSERT(content.net_rshares <=
                          0, "Cannot delete a content with network positive votes.");
            }
            if (content.net_rshares > 0) {
                return;
            }

            const auto &vote_idx = _db.get_index<content_vote_index>().indices().get<by_content_voter>();

            auto vote_itr = vote_idx.lower_bound(content_id_type(content.id));
            while (vote_itr != vote_idx.end() &&
                   vote_itr->content == content.id) {
                const auto &cur_vote = *vote_itr;
                ++vote_itr;
                _db.remove(cur_vote);
            }

            const auto &auth = _db.get_account(content.author); /// prove it exists
            db().modify(auth, [&](account_object &a) {
                if( content.parent_author == CHAIN_ROOT_POST_PARENT ) {
                    a.content_count--;
                }
                else{
                    a.subcontent_count--;
                }
            });

            /// this loop can be skiped for validate-only nodes as it is merely gathering stats for indicies
            if (content.parent_author != CHAIN_ROOT_POST_PARENT) {
                auto parent = &_db.get_content(content.parent_author, content.parent_permlink);
                auto now = _db.head_block_time();
                while (parent) {
                    _db.modify(*parent, [&](content_object &p) {
                        p.children--;
                        p.active = now;
                    });
#ifndef IS_LOW_MEM
                    if (parent->parent_author != CHAIN_ROOT_POST_PARENT) {
                        parent = &_db.get_content(parent->parent_author, parent->parent_permlink);
                    } else
#endif
                    {
                        parent = nullptr;
                    }
                }
            }
#ifndef IS_LOW_MEM
            auto& content_link = _db.get_content_type(content.id);
            _db.remove(content_link);
#endif
            _db.remove(content);
        }

        struct content_extension_visitor {
            content_extension_visitor(const content_object &c, database &db)
                    : _c(c), _db(db) {
            }

            using result_type = void;

            const content_object &_c;
            database &_db;

            void operator()(const content_payout_beneficiaries &cpb) const {
                if (_db.is_producing()) {
                    FC_ASSERT(cpb.beneficiaries.size() <= CHAIN_MAX_COMMENT_BENEFICIARIES,
                              "Cannot specify more than ${m} beneficiaries.", ("m", CHAIN_MAX_COMMENT_BENEFICIARIES));
                }

                FC_ASSERT(_c.beneficiaries.size() == 0, "Comment already has beneficiaries specified.");
                FC_ASSERT(_c.abs_rshares == 0, "Comment must not have been voted on before specifying beneficiaries.");

                _db.modify(_c, [&](content_object &c) {
                    for (auto &b : cpb.beneficiaries) {
                        auto acc = _db.find< account_object, by_name >( b.account );
                        FC_ASSERT( acc != nullptr, "Beneficiary \"${a}\" must exist.", ("a", b.account) );
                        c.beneficiaries.push_back(b);
                    }
                });
            }
        };

        void award_evaluator::do_apply(const award_operation &o) {
            FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_4), "award_evaluator not enabled until HF 4" );
            try {
                database &_db = db();
                const auto& median_props = _db.get_witness_schedule_object().median_props;
                const auto &initiator = _db.get_account(o.initiator);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!initiator.valid, "Account flagged as invalid");
                const auto &receiver = _db.get_account(o.receiver);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!receiver.valid, "Account flagged as invalid");

                int64_t elapsed_seconds = (_db.head_block_time() -
                                           initiator.last_vote_time).to_seconds();

                int64_t regenerated_energy =
                        (CHAIN_100_PERCENT * elapsed_seconds) /
                        CHAIN_ENERGY_REGENERATION_SECONDS;
                int64_t current_energy = std::min(int64_t(initiator.energy +
                                                         regenerated_energy), int64_t(CHAIN_100_PERCENT));
                FC_ASSERT(current_energy >
                          0, "Account currently does not have voting energy. Current energy: ${e}",("e",current_energy));

                int64_t used_energy = o.energy;
                FC_ASSERT(used_energy <=
                          current_energy,"Account does not have enough energy to award. Current energy: ${e}, used energy: ${u}",("e",current_energy)("u",used_energy));

                int64_t rshares = (
                    (uint128_t(initiator.effective_vesting_shares().amount.value) * used_energy) /
                    (CHAIN_100_PERCENT)).to_uint64();

                // Consensus by median props - vote accounting affects only with rshares greater than vote_accounting_min_rshares
                asset tokens=asset(0,SHARES_SYMBOL);
                if(rshares < int64_t(median_props.vote_accounting_min_rshares)){
                    rshares=0;
                }
                else{
                    uint128_t reward_tokens = uint128_t(
                         _db.claim_rshare_award(rshares));
                    if(_db.has_hardfork(CHAIN_HARDFORK_5)){
                        tokens=asset(reward_tokens.to_uint64(),TOKEN_SYMBOL);
                    }
                    else{
                        tokens=asset(reward_tokens.to_uint64(),SHARES_SYMBOL);
                    }
                }

                _db.modify(initiator, [&](account_object &a) {
                    a.energy = current_energy - used_energy;
                    a.last_vote_time = _db.head_block_time();
                    a.vote_count++;
                });

                if(tokens.amount>0){
                    if (_db.is_producing()) {
                        FC_ASSERT(o.beneficiaries.size() <= CHAIN_MAX_COMMENT_BENEFICIARIES,
                                  "Cannot specify more than ${m} beneficiaries.", ("m", CHAIN_MAX_COMMENT_BENEFICIARIES));
                    }
                    share_type receiver_tokens = tokens.amount;
                    share_type total_beneficiary = 0;
                    for (auto &b : o.beneficiaries) {
                        auto acc = _db.find< account_object, by_name >( b.account );
                        FC_ASSERT( acc != nullptr, "Beneficiary \"${a}\" must exist.", ("a", b.account) );

                        const auto &b_account = _db.get_account(b.account);
                        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                        //    FC_ASSERT(!b_account.valid, "Account flagged as invalid");

                        //pay benefactor
                        auto benefactor_tokens = (tokens.amount * b.weight) / CHAIN_100_PERCENT;
                        auto shares_created = _db.create_vesting(_db.get_account(b.account), benefactor_tokens);
                        _db.modify(_db.get_account(b.account), [&](account_object &a) {
                            a.benefactor_awards += benefactor_tokens;
                        });
                        _db.push_virtual_operation(
                            benefactor_award_operation(o.initiator,b.account,o.receiver,o.custom_sequence,o.memo,shares_created));
                        total_beneficiary += benefactor_tokens;
                    }

                    receiver_tokens -= total_beneficiary;
                    if(receiver_tokens>0){
                        auto shares_created = _db.create_vesting(receiver, receiver_tokens);
                        _db.modify(receiver, [&](account_object &a) {
                            a.receiver_awards += receiver_tokens;
                        });
                        _db.push_virtual_operation(
                            receive_award_operation(o.initiator,o.receiver,o.custom_sequence,o.memo,shares_created));
                    }
                }
            } FC_CAPTURE_AND_RETHROW((o))
        }

        void fixed_award_evaluator::do_apply(const fixed_award_operation &o) {
            FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_11), "fixed_award_evaluator not enabled until HF 11" );
            try {
                database &_db = db();
                const dynamic_global_property_object &dgpo = _db.get_dynamic_global_properties();
                const auto& median_props = _db.get_witness_schedule_object().median_props;
                const auto &initiator = _db.get_account(o.initiator);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!initiator.valid, "Account flagged as invalid");
                const auto &receiver = _db.get_account(o.receiver);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!receiver.valid, "Account flagged as invalid");

                int64_t elapsed_seconds = (_db.head_block_time() -
                                           initiator.last_vote_time).to_seconds();

                int64_t regenerated_energy =
                        (CHAIN_100_PERCENT * elapsed_seconds) /
                        CHAIN_ENERGY_REGENERATION_SECONDS;
                int64_t current_energy = std::min(int64_t(initiator.energy +
                                                         regenerated_energy), int64_t(CHAIN_100_PERCENT));
                FC_ASSERT(current_energy >
                          0, "Account currently does not have voting energy. Current energy: ${e}",("e",current_energy));

                //need to calc energy amount and rshares for it

                int64_t max_energy = o.max_energy;
                FC_ASSERT(max_energy <=
                          current_energy,"Account does not have enough energy to award. Current energy: ${e}, max energy: ${u}",("e",current_energy)("u",max_energy));

                int64_t max_rshares = (
                    (uint128_t(initiator.effective_vesting_shares().amount.value) * max_energy) /
                    (CHAIN_100_PERCENT)).to_uint64();

                uint128_t max_reward = uint128_t(_db.calc_rshare_award(max_rshares));
                FC_ASSERT(max_reward >=
                          o.reward_amount.amount.value,"Max available reward: ${r}, requested reward amount: ${u}",("r",max_reward)("u",o.reward_amount.amount.value));
                //int64_t rs = _db.calc_rshare_by_reward(o.reward_amount);
                //calc_rshare_by_reward
                //calc reward shares (rs) by fixed reward amount (ra)
                u256 rs_calc(0);//unknown reward shares
                u256 ra(o.reward_amount.amount.value);//fixed reward amount
                u256 rf(dgpo.total_reward_fund.amount.value);//reward fund
                u256 total_rshares = to256(dgpo.total_reward_shares);//competition for reward fund

                //ra = rf * rs / (total_rshares + rs)
                //ra * total_rshares + ra * rs = rf * rs
                //ra * total_rshares = rf * rs - ra * rs
                //ra * total_rshares = rs * (rf - ra)
                //rs = (ra * total_rshares) / (rf - ra)
                rs_calc = u256(ra * total_rshares) / u256(rf - ra);
                //ilog("Calculated (ra * total_rshares): ${r}",("r",uint64_t(ra * total_rshares)));
                //ilog("Calculated (rf - ra): ${r}",("r",uint64_t(rf - ra)));
                //ilog("Calculated rs_calc: ${r}",("r",uint64_t(rs_calc)));

                //check rs not exceed int64_t
                FC_ASSERT(rs_calc <=
                          u256(uint64_t(std::numeric_limits<int64_t>::max())));

                int64_t rs = static_cast< int64_t >( rs_calc );


                FC_ASSERT(rs <=
                          max_rshares,"Account does not have enough potential to fixed award. Current rs: ${rs}, max rs: ${mrs}",("rs",rs)("mrs",max_rshares));

                int64_t need_energy = ( (uint128_t(rs) * max_energy ) / max_rshares ).to_uint64();

                need_energy++;//increase 0.01% for calculations overlap
                //to not exceed 100.00%
                need_energy=std::min(need_energy, int64_t(CHAIN_100_PERCENT));

                int64_t rshares = (
                    (uint128_t(initiator.effective_vesting_shares().amount.value) * need_energy) /
                    (CHAIN_100_PERCENT)).to_uint64();

                // Consensus by median props - vote accounting affects only with rshares greater than vote_accounting_min_rshares
                asset tokens=asset(0,SHARES_SYMBOL);
                uint128_t reward_tokens = 0;
                if(rshares < int64_t(median_props.vote_accounting_min_rshares)){
                    rshares=0;
                }
                else{
                    reward_tokens = uint128_t(_db.calc_rshare_award(rshares));
                    tokens=asset(reward_tokens.to_uint64(),TOKEN_SYMBOL);
                }

                FC_ASSERT(tokens.amount >=
                          o.reward_amount.amount,"Calculated reward is lesser than fixed reward amount. Calculated reward: ${cr}, fixed reward amount: ${fra}",("cr",tokens)("fra",o.reward_amount));

                //claim_rshare_award only after FC_ASSERT checks
                if(0 != rshares){
                    reward_tokens = uint128_t(_db.claim_rshare_award(rshares));
                    tokens=asset(reward_tokens.to_uint64(),TOKEN_SYMBOL);
                }
                else{
                    FC_ASSERT(false, "Potential rshares is 0");
                }

                if(tokens.amount>0){
                    _db.modify(initiator, [&](account_object &a) {
                        a.energy = current_energy - need_energy;
                        a.last_vote_time = _db.head_block_time();
                        a.vote_count++;
                    });
                    if (_db.is_producing()) {
                        FC_ASSERT(o.beneficiaries.size() <= CHAIN_MAX_COMMENT_BENEFICIARIES,
                                  "Cannot specify more than ${m} beneficiaries.", ("m", CHAIN_MAX_COMMENT_BENEFICIARIES));
                    }
                    share_type receiver_tokens = tokens.amount;
                    share_type donation_tokens = receiver_tokens - o.reward_amount.amount;
                    receiver_tokens -= donation_tokens;
                    share_type total_beneficiary = 0;
                    for (auto &b : o.beneficiaries) {
                        auto acc = _db.find< account_object, by_name >( b.account );
                        FC_ASSERT( acc != nullptr, "Beneficiary \"${a}\" must exist.", ("a", b.account) );

                        const auto &b_account = _db.get_account(b.account);
                        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                        //    FC_ASSERT(!b_account.valid, "Account flagged as invalid");

                        //pay benefactor
                        auto benefactor_tokens = (tokens.amount * b.weight) / CHAIN_100_PERCENT;
                        auto shares_created = _db.create_vesting(_db.get_account(b.account), benefactor_tokens);
                        _db.modify(_db.get_account(b.account), [&](account_object &a) {
                            a.benefactor_awards += benefactor_tokens;
                        });
                        _db.push_virtual_operation(
                            benefactor_award_operation(o.initiator,b.account,o.receiver,o.custom_sequence,o.memo,shares_created));
                        total_beneficiary += benefactor_tokens;
                    }

                    receiver_tokens -= total_beneficiary;
                    if(receiver_tokens>0){
                        auto shares_created = _db.create_vesting(receiver, receiver_tokens);
                        _db.modify(receiver, [&](account_object &a) {
                            a.receiver_awards += receiver_tokens;
                        });
                        _db.push_virtual_operation(
                            receive_award_operation(o.initiator,o.receiver,o.custom_sequence,o.memo,shares_created));
                    }

                    if(donation_tokens>0){//exceed reward going to committee fund
                        _db.modify(dgpo, [&](dynamic_global_property_object& dgpo) {
                            dgpo.committee_fund.amount += donation_tokens;
                        });
                    }
                }
                else{
                    FC_ASSERT(false, "Potential reward is 0");
                }
            } FC_CAPTURE_AND_RETHROW((o))
        }

        void content_evaluator::do_apply(const content_operation &o) {
            database &_db = db();
            FC_ASSERT( !_db.has_hardfork(CHAIN_HARDFORK_4), "content is deprecated as of HF 4" );
            try {
                FC_ASSERT(o.title.size() + o.body.size() +
                          o.json_metadata.size(), "Cannot update content because nothing appears to be changing.");

                const auto &by_permlink_idx = _db.get_index<content_index>().indices().get<by_permlink>();
                auto itr = by_permlink_idx.find(boost::make_tuple(o.author, o.permlink));

                const auto &auth = _db.get_account(o.author); /// prove it exists

                content_id_type id;

                const content_object *parent = nullptr;
                if (o.parent_author != CHAIN_ROOT_POST_PARENT) {
                    parent = &_db.get_content(o.parent_author, o.parent_permlink);
                    auto max_depth = CHAIN_MAX_COMMENT_DEPTH;
                    FC_ASSERT(parent->depth < max_depth,
                              "Content is nested ${x} posts deep, maximum depth is ${y}.",
                              ("x", parent->depth)("y", max_depth));
                }
                auto now = _db.head_block_time();

                if (itr == by_permlink_idx.end()) {
                    db().modify(auth, [&](account_object &a) {
                        a.last_post = now;
                        if( o.parent_author == CHAIN_ROOT_POST_PARENT ) {
                            a.last_root_post = now;
                            a.content_count++;
                        }
                        else{
                            a.subcontent_count++;
                        }
                    });

                    const auto &new_content = _db.create<content_object>([&](content_object &com) {
                        validate_permlink(o.parent_permlink);
                        validate_permlink(o.permlink);

                        com.author = o.author;
                        from_string(com.permlink, o.permlink);
                        com.last_update = _db.head_block_time();
                        com.created = com.last_update;
                        com.active = com.last_update;
                        com.last_payout = fc::time_point_sec::min();
                        com.cashout_time = com.created + CHAIN_CASHOUT_WINDOW_SECONDS;
                        com.curation_percent = o.curation_percent;

                        if (o.parent_author == CHAIN_ROOT_POST_PARENT) {
                            com.parent_author = "";
                            from_string(com.parent_permlink, o.parent_permlink);
                            com.root_content = com.id;
                        }
                        else {
                            com.parent_author = parent->author;
                            com.parent_permlink = parent->permlink;
                            com.depth = parent->depth + 1;
                            com.root_content = parent->root_content;
                        }
                    });

                    for (auto &e : o.extensions) {
                        e.visit(content_extension_visitor(new_content, _db));
                    }

                    id = new_content.id;
#ifndef IS_LOW_MEM
                    _db.create<content_type_object>([&](content_type_object& con) {
                        con.content = id;
                        from_string(con.title, o.title);
                        if (o.body.size() < 1024*1024*128) {
                            from_string(con.body, o.body);
                        }
                        if (fc::is_utf8(o.json_metadata)) {
                            from_string(con.json_metadata, o.json_metadata);
                        } else {
                            wlog("Content ${a}/${p} contains invalid UTF-8 metadata",
                                 ("a", o.author)("p", o.permlink));
                        }
                    });
#endif
/// this loop can be skiped for validate-only nodes as it is merely gathering stats for indicies
                    auto now = _db.head_block_time();
                    while (parent) {
                        _db.modify(*parent, [&](content_object &p) {
                            p.children++;
                            p.active = now;
                        });
#ifndef IS_LOW_MEM
                        if (parent->parent_author != CHAIN_ROOT_POST_PARENT) {
                            parent = &_db.get_content(parent->parent_author, parent->parent_permlink);
                        } else
#endif
                        {
                            parent = nullptr;
                        }
                    }

                } else // start edit case
                {
                    const auto &content = *itr;
                    _db.modify(content, [&](content_object &com) {
                        com.last_update = _db.head_block_time();
                        com.active = com.last_update;
                        strcmp_equal equal;

                        if (!parent) {
                            FC_ASSERT(com.parent_author ==
                                      account_name_type(), "The parent of a content cannot change.");
                            FC_ASSERT(equal(com.parent_permlink, o.parent_permlink), "The permlink of a content cannot change.");
                        } else {
                            FC_ASSERT(com.parent_author ==
                                      o.parent_author, "The parent of a content cannot change.");
                            FC_ASSERT(equal(com.parent_permlink, o.parent_permlink), "The permlink of a content cannot change.");
                        }
                    });

                    for (auto &e : o.extensions) {
                        e.visit(content_extension_visitor(content, _db));
                    }

#ifndef IS_LOW_MEM
                    _db.modify(_db.get< content_type_object, by_content >( content.id ), [&]( content_type_object& con ) {
                        if (o.title.size())
                            from_string(con.title, o.title);
                        if (o.json_metadata.size())
                            from_string(con.json_metadata, o.json_metadata );
                        if (o.body.size())
                            from_string(con.body, o.body);
                    });
#endif

                } // end EDIT case

            } FC_CAPTURE_AND_RETHROW((o))
        }

        void escrow_transfer_evaluator::do_apply(const escrow_transfer_operation &o) {
            try {
                database &_db = db();

                const auto &from_account = _db.get_account(o.from);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!from_account.valid, "Account flagged as invalid");
                const auto &to_account = _db.get_account(o.to);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!to_account.valid, "Account flagged as invalid");
                const auto &agent_account = _db.get_account(o.agent);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!agent_account.valid, "Account flagged as invalid");

                FC_ASSERT(o.ratification_deadline >
                          _db.head_block_time(), "The escorw ratification deadline must be after head block time.");
                FC_ASSERT(o.escrow_expiration >
                          _db.head_block_time(), "The escrow expiration must be after head block time.");

                asset token_spent = o.token_amount;
                if (o.fee.symbol == TOKEN_SYMBOL) {
                    token_spent += o.fee;
                }

                FC_ASSERT(from_account.balance >=
                          token_spent, "Account cannot cover token costs of escrow. Required: ${r} Available: ${a}", ("r", token_spent)("a", from_account.balance));

                _db.adjust_balance(from_account, -token_spent);

                _db.create<escrow_object>([&](escrow_object &esc) {
                    esc.escrow_id = o.escrow_id;
                    esc.from = o.from;
                    esc.to = o.to;
                    esc.agent = o.agent;
                    esc.ratification_deadline = o.ratification_deadline;
                    esc.escrow_expiration = o.escrow_expiration;
                    esc.token_balance = o.token_amount;
                    esc.pending_fee = o.fee;
                });
            }
            FC_CAPTURE_AND_RETHROW((o))
        }

        void escrow_approve_evaluator::do_apply(const escrow_approve_operation &o) {
            try {
                database &_db = db();
                const auto &escrow = _db.get_escrow(o.from, o.escrow_id);

                FC_ASSERT(escrow.to ==
                          o.to, "Operation 'to' (${o}) does not match escrow 'to' (${e}).", ("o", o.to)("e", escrow.to));
                FC_ASSERT(escrow.agent ==
                          o.agent, "Operation 'agent' (${a}) does not match escrow 'agent' (${e}).", ("o", o.agent)("e", escrow.agent));
                FC_ASSERT(escrow.ratification_deadline >=
                          _db.head_block_time(), "The escrow ratification deadline has passed. Escrow can no longer be ratified.");

                bool reject_escrow = !o.approve;

                if (o.who == o.to) {
                    FC_ASSERT(!escrow.to_approved, "Account 'to' (${t}) has already approved the escrow.", ("t", o.to));

                    if (!reject_escrow) {
                        _db.modify(escrow, [&](escrow_object &esc) {
                            esc.to_approved = true;
                        });
                    }
                }
                if (o.who == o.agent) {
                    FC_ASSERT(!escrow.agent_approved, "Account 'agent' (${a}) has already approved the escrow.", ("a", o.agent));

                    if (!reject_escrow) {
                        _db.modify(escrow, [&](escrow_object &esc) {
                            esc.agent_approved = true;
                        });
                    }
                }

                if (reject_escrow) {
                    const auto &from_account = _db.get_account(o.from);
                    _db.adjust_balance(from_account, escrow.token_balance);
                    _db.adjust_balance(from_account, escrow.pending_fee);

                    _db.remove(escrow);
                } else if (escrow.to_approved && escrow.agent_approved) {
                    const auto &agent_account = _db.get_account(o.agent);
                    _db.adjust_balance(agent_account, escrow.pending_fee);

                    _db.modify(escrow, [&](escrow_object &esc) {
                        esc.pending_fee.amount = 0;
                    });
                }
            }
            FC_CAPTURE_AND_RETHROW((o))
        }

        void escrow_dispute_evaluator::do_apply(const escrow_dispute_operation &o) {
            try {
                database &_db = db();
                _db.get_account(o.from); // Verify from account exists

                const auto &e = _db.get_escrow(o.from, o.escrow_id);
                FC_ASSERT(_db.head_block_time() <
                          e.escrow_expiration, "Disputing the escrow must happen before expiration.");
                FC_ASSERT(e.to_approved &&
                          e.agent_approved, "The escrow must be approved by all parties before a dispute can be raised.");
                FC_ASSERT(!e.disputed, "The escrow is already under dispute.");
                FC_ASSERT(e.to ==
                          o.to, "Operation 'to' (${o}) does not match escrow 'to' (${e}).", ("o", o.to)("e", e.to));
                FC_ASSERT(e.agent ==
                          o.agent, "Operation 'agent' (${a}) does not match escrow 'agent' (${e}).", ("o", o.agent)("e", e.agent));

                _db.modify(e, [&](escrow_object &esc) {
                    esc.disputed = true;
                });
            }
            FC_CAPTURE_AND_RETHROW((o))
        }

        void escrow_release_evaluator::do_apply(const escrow_release_operation &o) {
            try {
                database &_db = db();
                _db.get_account(o.from); // Verify from account exists
                const auto &receiver_account = _db.get_account(o.receiver);

                const auto &e = _db.get_escrow(o.from, o.escrow_id);
                FC_ASSERT(e.token_balance >=
                          o.token_amount, "Release amount exceeds escrow balance. Amount: ${a}, Balance: ${b}", ("a", o.token_amount)("b", e.token_balance));
                FC_ASSERT(e.to ==
                          o.to, "Operation 'to' (${o}) does not match escrow 'to' (${e}).", ("o", o.to)("e", e.to));
                FC_ASSERT(e.agent ==
                          o.agent, "Operation 'agent' (${a}) does not match escrow 'agent' (${e}).", ("o", o.agent)("e", e.agent));
                FC_ASSERT(o.receiver == e.from || o.receiver ==
                                                  e.to, "Funds must be released to 'from' (${f}) or 'to' (${t})", ("f", e.from)("t", e.to));
                FC_ASSERT(e.to_approved &&
                          e.agent_approved, "Funds cannot be released prior to escrow approval.");

                // If there is a dispute regardless of expiration, the agent can release funds to either party
                if (e.disputed) {
                    FC_ASSERT(o.who ==
                              e.agent, "Only 'agent' (${a}) can release funds in a disputed escrow.", ("a", e.agent));
                } else {
                    FC_ASSERT(o.who == e.from || o.who ==
                                                 e.to, "Only 'from' (${f}) and 'to' (${t}) can release funds from a non-disputed escrow", ("f", e.from)("t", e.to));

                    if (e.escrow_expiration > _db.head_block_time()) {
                        // If there is no dispute and escrow has not expired, either party can release funds to the other.
                        if (o.who == e.from) {
                            FC_ASSERT(o.receiver ==
                                      e.to, "Only 'from' (${f}) can release funds to 'to' (${t}).", ("f", e.from)("t", e.to));
                        } else if (o.who == e.to) {
                            FC_ASSERT(o.receiver ==
                                      e.from, "Only 'to' (${t}) can release funds to 'from' (${t}).", ("f", e.from)("t", e.to));
                        }
                    }
                }
                // If escrow expires and there is no dispute, either party can release funds to either party.

                _db.adjust_balance(receiver_account, o.token_amount);

                _db.modify(e, [&](escrow_object &esc) {
                    esc.token_balance -= o.token_amount;
                });

                if (e.token_balance.amount == 0) {
                    _db.remove(e);
                }
            }
            FC_CAPTURE_AND_RETHROW((o))
        }

        void transfer_evaluator::do_apply(const transfer_operation &o) {
            database &_db = db();
            const auto &from_account = _db.get_account(o.from);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!from_account.valid, "Account flagged as invalid");
            const auto &to_account = _db.get_account(o.to);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!to_account.valid, "Account flagged as invalid");

            FC_ASSERT(_db.get_balance(from_account, o.amount.symbol) >=
                      o.amount, "Account does not have sufficient funds for transfer.");
            _db.adjust_balance(from_account, -o.amount);
            _db.adjust_balance(to_account, o.amount);

            //VIZ support anonymous account creation
            if(CHAIN_ANONYMOUS_ACCOUNT==to_account.name){
                const auto& median_props = _db.get_witness_schedule_object().median_props;
                FC_ASSERT(o.amount >= median_props.account_creation_fee,
                    "Inssufficient amount ${f} required, ${p} provided.",
                    ("f", median_props.account_creation_fee)("p", o.amount));
                if(o.memo.size()){
                    account_name_type new_account_name;
                    public_key_type key_from_memo;
                    int anonymous_num;
                    bool valid=true;
                    if(_db.has_hardfork(CHAIN_HARDFORK_6)){
                        string memo = fc::trim(o.memo);
                        auto colon_pos = memo.find(":");
                        if(colon_pos != std::string::npos) {
                            auto login_part = memo.substr(0, colon_pos);
                            auto acc = _db.find< account_object, by_name >( login_part );
                            FC_ASSERT( acc == nullptr, "Account login \"${a}\" must be free.", ("a", login_part) );

                            if(_db.has_hardfork(CHAIN_HARDFORK_7)){
                                FC_ASSERT( is_valid_domain_name( login_part, CHAIN_ANONYMOUS_ACCOUNT ), "Domain name ${n} is invalid, creator name ${c}", ("n", login_part)("c", CHAIN_ANONYMOUS_ACCOUNT) );
                                FC_ASSERT( is_valid_account_name( login_part ), "Account name ${n} is invalid", ("n", login_part) );
                            }
                            else{
                                if(!is_valid_domain_name( login_part, CHAIN_ANONYMOUS_ACCOUNT )){
                                    valid=false;
                                }
                                if(!is_valid_account_name( login_part )){
                                    valid=false;
                                }
                            }

                            new_account_name=login_part;
                            auto key_part = memo.substr(colon_pos + 1);
                            key_from_memo=public_key_type(key_part);
                        }
                        else{
                            key_from_memo=public_key_type(o.memo);
                            const auto& meta = _db.get<account_metadata_object, by_account>(to_account.name);
                            anonymous_num=std::stoi(meta.json_metadata.c_str());
                            anonymous_num++;
                            store_account_json_metadata(_db, CHAIN_ANONYMOUS_ACCOUNT,fc::to_string(anonymous_num));
                            new_account_name="n" + fc::to_string(anonymous_num) + "." + CHAIN_ANONYMOUS_ACCOUNT;
                        }
                    }
                    else{
                        key_from_memo=public_key_type(o.memo);
                        const auto& meta = _db.get<account_metadata_object, by_account>(to_account.name);
                        anonymous_num=std::stoi(meta.json_metadata.c_str());
                        anonymous_num++;
                        store_account_json_metadata(_db, CHAIN_ANONYMOUS_ACCOUNT,fc::to_string(anonymous_num));
                        new_account_name="n" + fc::to_string(anonymous_num) + "." + CHAIN_ANONYMOUS_ACCOUNT;
                    }
                    const auto now = _db.head_block_time();

                    _db.create<account_object>([&](account_object &acc) {
                        acc.name = new_account_name;
                        acc.memo_key = key_from_memo;
                        acc.created = now;
                        acc.energy = CHAIN_100_PERCENT;
                        acc.last_vote_time = now;
                        acc.recovery_account = "";
                        acc.valid=valid;
                    });
                    _db.create<account_authority_object>([&](account_authority_object &auth) {
                        auth.account = new_account_name;
                        auth.master.add_authority(key_from_memo, 1);
                        auth.master.weight_threshold = 1;
                        auth.active = auth.master;
                        auth.regular = auth.active;
                    });
                    _db.create<account_metadata_object>([&](account_metadata_object& m) {
                        m.account = new_account_name;
                    });
                    const auto &new_account = _db.get_account(new_account_name);
                    _db.adjust_balance(to_account, -o.amount);
                    _db.create_vesting(new_account, o.amount);
                }
            }
        }

        void transfer_to_vesting_evaluator::do_apply(const transfer_to_vesting_operation &o) {
            database &_db = db();

            const auto &from_account = _db.get_account(o.from);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!from_account.valid, "Account flagged as invalid");
            const auto &to_account = o.to.size() ? _db.get_account(o.to)
                                                 : from_account;
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!to_account.valid, "Account flagged as invalid");

            FC_ASSERT(_db.get_balance(from_account, TOKEN_SYMBOL) >=
                      o.amount, "Account does not have sufficient TOKEN for transfer.");
            _db.adjust_balance(from_account, -o.amount);
            _db.create_vesting(to_account, o.amount);
        }

        void withdraw_vesting_evaluator::do_apply(const withdraw_vesting_operation &o) {
            database &_db = db();
            const auto& median_props = _db.get_witness_schedule_object().median_props;

            const auto &account = _db.get_account(o.account);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account.valid, "Account flagged as invalid");

            FC_ASSERT(account.vesting_shares.amount >= 0,
                "Account does not have sufficient SHARES for withdraw.");
            FC_ASSERT(account.available_vesting_shares() >= o.vesting_shares,
                "Account does not have sufficient SHARES for withdraw.");
            FC_ASSERT(o.vesting_shares.amount >= 0, "Cannot withdraw negative SHARES.");

            if (o.vesting_shares.amount == 0) {
                _db.modify(account, [&](account_object &a) {
                    a.vesting_withdraw_rate = asset(0, SHARES_SYMBOL);
                    a.next_vesting_withdrawal = time_point_sec::maximum();
                    a.to_withdraw = 0;
                    a.withdrawn = 0;
                });
            }
            else {
                int vesting_withdraw_intervals = CHAIN_VESTING_WITHDRAW_INTERVALS;
                if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                    vesting_withdraw_intervals=median_props.withdraw_intervals;
                }

                _db.modify(account, [&](account_object &a) {
                    auto new_vesting_withdraw_rate = asset(
                            o.vesting_shares.amount /
                            vesting_withdraw_intervals, SHARES_SYMBOL);

                    if(_db.has_hardfork(CHAIN_HARDFORK_5)){
                        new_vesting_withdraw_rate = asset(a.available_vesting_shares().amount / vesting_withdraw_intervals, SHARES_SYMBOL);
                    }

                    if (new_vesting_withdraw_rate.amount == 0)
                        new_vesting_withdraw_rate.amount = 1;

                    FC_ASSERT(account.vesting_withdraw_rate !=
                              new_vesting_withdraw_rate, "This operation would not change the vesting withdraw rate.");

                    a.vesting_withdraw_rate = new_vesting_withdraw_rate;
                    a.next_vesting_withdrawal = _db.head_block_time() +
                                                fc::seconds(CHAIN_VESTING_WITHDRAW_INTERVAL_SECONDS);
                    a.to_withdraw = o.vesting_shares.amount;
                    a.withdrawn = 0;
                });
            }
        }

        void set_withdraw_vesting_route_evaluator::do_apply(const set_withdraw_vesting_route_operation &o) {
            try {
                database &_db = db();
                const auto &from_account = _db.get_account(o.from_account);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!from_account.valid, "Account flagged as invalid");
                const auto &to_account = _db.get_account(o.to_account);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!to_account.valid, "Account flagged as invalid");
                const auto &wd_idx = _db.get_index<withdraw_vesting_route_index>().indices().get<by_withdraw_route>();
                auto itr = wd_idx.find(boost::make_tuple(from_account.id, to_account.id));

                if (itr == wd_idx.end()) {
                    FC_ASSERT(
                            o.percent != 0, "Cannot create a 0% destination.");
                    FC_ASSERT(from_account.withdraw_routes <
                              CHAIN_MAX_WITHDRAW_ROUTES, "Account already has the maximum number of routes.");

                    _db.create<withdraw_vesting_route_object>([&](withdraw_vesting_route_object &wvdo) {
                        wvdo.from_account = from_account.id;
                        wvdo.to_account = to_account.id;
                        wvdo.percent = o.percent;
                        wvdo.auto_vest = o.auto_vest;
                    });

                    _db.modify(from_account, [&](account_object &a) {
                        a.withdraw_routes++;
                    });
                } else if (o.percent == 0) {
                    _db.remove(*itr);

                    _db.modify(from_account, [&](account_object &a) {
                        a.withdraw_routes--;
                    });
                } else {
                    _db.modify(*itr, [&](withdraw_vesting_route_object &wvdo) {
                        wvdo.from_account = from_account.id;
                        wvdo.to_account = to_account.id;
                        wvdo.percent = o.percent;
                        wvdo.auto_vest = o.auto_vest;
                    });
                }

                itr = wd_idx.upper_bound(boost::make_tuple(from_account.id, account_id_type()));
                uint16_t total_percent = 0;

                while (itr->from_account == from_account.id &&
                       itr != wd_idx.end()) {
                    total_percent += itr->percent;
                    ++itr;
                }

                FC_ASSERT(total_percent <=
                          CHAIN_100_PERCENT, "More than 100% of vesting withdrawals allocated to destinations.");
            }
            FC_CAPTURE_AND_RETHROW()
        }

        void account_witness_proxy_evaluator::do_apply(const account_witness_proxy_operation &o) {
            database &_db = db();
            const auto &account = _db.get_account(o.account);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account.valid, "Account flagged as invalid");
            FC_ASSERT(account.proxy != o.proxy, "Proxy must change.");

            /// remove all current votes
            std::array<share_type, CHAIN_MAX_PROXY_RECURSION_DEPTH + 1> delta;
            delta[0] = -account.vesting_shares.amount;
            for (int i = 0; i < CHAIN_MAX_PROXY_RECURSION_DEPTH; ++i) {
                delta[i + 1] = -account.proxied_vsf_votes[i];
            }
            _db.adjust_proxied_witness_votes(account, delta);

            if (o.proxy.size()) {
                const auto &new_proxy = _db.get_account(o.proxy);
                flat_set<account_id_type> proxy_chain({account.id, new_proxy.id
                });
                proxy_chain.reserve(CHAIN_MAX_PROXY_RECURSION_DEPTH + 1);

                /// check for proxy loops and fail to update the proxy if it would create a loop
                auto cprox = &new_proxy;
                while (cprox->proxy.size() != 0) {
                    const auto next_proxy = _db.get_account(cprox->proxy);
                    FC_ASSERT(proxy_chain.insert(next_proxy.id).second, "This proxy would create a proxy loop.");
                    cprox = &next_proxy;
                    FC_ASSERT(proxy_chain.size() <=
                              CHAIN_MAX_PROXY_RECURSION_DEPTH, "Proxy chain is too long.");
                }

                /// clear all individual vote records
                _db.clear_witness_votes(account);

                _db.modify(account, [&](account_object &a) {
                    a.proxy = o.proxy;
                });

                /// add all new votes
                for (int i = 0; i <= CHAIN_MAX_PROXY_RECURSION_DEPTH; ++i) {
                    delta[i] = -delta[i];
                }
                _db.adjust_proxied_witness_votes(account, delta);
            } else { /// we are clearing the proxy which means we simply update the account
                _db.modify(account, [&](account_object &a) {
                    a.proxy = o.proxy;
                });
            }
        }

        void account_witness_vote_evaluator::do_apply(const account_witness_vote_operation &o) {
            database &_db = db();
            const auto &voter = _db.get_account(o.account);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!voter.valid, "Account flagged as invalid");
            if(_db.has_hardfork(CHAIN_HARDFORK_4)){
                //clear proxy if it exist
                if(voter.proxy.size()){
                    /// remove all current votes
                    std::array<share_type, CHAIN_MAX_PROXY_RECURSION_DEPTH + 1> delta;
                    delta[0] = -voter.vesting_shares.amount;
                    for (int i = 0; i < CHAIN_MAX_PROXY_RECURSION_DEPTH; ++i) {
                        delta[i + 1] = -voter.proxied_vsf_votes[i];
                    }
                    _db.adjust_proxied_witness_votes(voter, delta);
                    _db.modify(voter, [&](account_object &a) {
                        a.proxy = CHAIN_PROXY_TO_SELF_ACCOUNT;//blank proxy
                    });
                }
            }
            else{
                FC_ASSERT(voter.proxy.size() ==
                          0, "A proxy is currently set, please clear the proxy before voting for a witness.");
            }

            const auto &witness = _db.get_witness(o.witness);

            const auto &by_account_witness_idx = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
            auto itr = by_account_witness_idx.find(boost::make_tuple(voter.id, witness.id));

            if (itr == by_account_witness_idx.end()) {
                FC_ASSERT(o.approve, "Vote doesn't exist, user must indicate a desire to approve witness.");

                if(_db.has_hardfork(CHAIN_HARDFORK_4)){
                    FC_ASSERT(voter.witnesses_voted_for <
                              CHAIN_MAX_ACCOUNT_WITNESS_VOTES, "Account has voted for too many witnesses.");
                }
                else{
                    FC_ASSERT(voter.witnesses_voted_for <
                              CHAIN_MAX_ACCOUNT_WITNESS_VOTES_PRE_HF4, "Account has voted for too many witnesses."); // TODO: Remove after hardfork 2
                }

                if(_db.has_hardfork(CHAIN_HARDFORK_5)){
                    const auto &vidx = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
                    auto vitr = vidx.lower_bound(boost::make_tuple(voter.id, witness_id_type()));
                    while (vitr != vidx.end() && vitr->account == voter.id) {
                        _db.adjust_witness_vote(_db.get(vitr->witness), -voter.witnesses_vote_weight);
                        ++vitr;
                    }

                    _db.create<witness_vote_object>([&](witness_vote_object &v) {
                        v.witness = witness.id;
                        v.account = voter.id;
                    });

                    _db.modify(voter, [&](account_object &a) {
                        a.witnesses_voted_for++;
                    });
                    share_type fair_vote_weight = voter.witness_vote_fair_weight();
                    _db.modify(voter, [&](account_object &a) {
                        a.witnesses_vote_weight = fair_vote_weight;
                    });

                    const auto &vidx2 = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
                    auto vitr2 = vidx2.lower_bound(boost::make_tuple(voter.id, witness_id_type()));
                    while (vitr2 != vidx2.end() && vitr2->account == voter.id) {
                        _db.adjust_witness_vote(_db.get(vitr2->witness), voter.witnesses_vote_weight);
                        ++vitr2;
                    }
                }
                else if(_db.has_hardfork(CHAIN_HARDFORK_4)){
                    const auto &vidx = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
                    auto vitr = vidx.lower_bound(boost::make_tuple(voter.id, witness_id_type()));
                    while (vitr != vidx.end() && vitr->account == voter.id) {
                        _db.adjust_witness_vote(_db.get(vitr->witness), -voter.witness_vote_fair_weight_prehf5());
                        ++vitr;
                    }

                    _db.create<witness_vote_object>([&](witness_vote_object &v) {
                        v.witness = witness.id;
                        v.account = voter.id;
                    });

                    _db.modify(voter, [&](account_object &a) {
                        a.witnesses_voted_for++;
                    });

                    const auto &vidx2 = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
                    auto vitr2 = vidx2.lower_bound(boost::make_tuple(voter.id, witness_id_type()));
                    while (vitr2 != vidx2.end() && vitr2->account == voter.id) {
                        _db.adjust_witness_vote(_db.get(vitr2->witness), voter.witness_vote_fair_weight_prehf5());
                        ++vitr2;
                    }
                }
                else{
                    _db.create<witness_vote_object>([&](witness_vote_object &v) {
                        v.witness = witness.id;
                        v.account = voter.id;
                    });
                    _db.adjust_witness_vote(witness, voter.witness_vote_weight());
                    _db.modify(voter, [&](account_object &a) {
                        a.witnesses_voted_for++;
                    });
                }
            } else {
                FC_ASSERT(!o.approve, "Vote currently exists, user must indicate a desire to reject witness.");

                if(_db.has_hardfork(CHAIN_HARDFORK_5)){
                    const auto &vidx = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
                    auto vitr = vidx.lower_bound(boost::make_tuple(voter.id, witness_id_type()));
                    while (vitr != vidx.end() && vitr->account == voter.id) {
                        _db.adjust_witness_vote(_db.get(vitr->witness), -voter.witnesses_vote_weight);
                        ++vitr;
                    }

                    _db.remove(*itr);

                    _db.modify(voter, [&](account_object &a) {
                        a.witnesses_voted_for--;
                    });
                    share_type fair_vote_weight = voter.witness_vote_fair_weight();
                    _db.modify(voter, [&](account_object &a) {
                        a.witnesses_vote_weight = fair_vote_weight;
                    });

                    const auto &vidx2 = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
                    auto vitr2 = vidx2.lower_bound(boost::make_tuple(voter.id, witness_id_type()));
                    while (vitr2 != vidx2.end() && vitr2->account == voter.id) {
                        _db.adjust_witness_vote(_db.get(vitr2->witness), voter.witnesses_vote_weight);
                        ++vitr2;
                    }
                }
                else if(_db.has_hardfork(CHAIN_HARDFORK_4)){
                    const auto &vidx = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
                    auto vitr = vidx.lower_bound(boost::make_tuple(voter.id, witness_id_type()));
                    while (vitr != vidx.end() && vitr->account == voter.id) {
                        _db.adjust_witness_vote(_db.get(vitr->witness), -voter.witness_vote_fair_weight_prehf5());
                        ++vitr;
                    }

                    _db.remove(*itr);

                    _db.modify(voter, [&](account_object &a) {
                        a.witnesses_voted_for--;
                    });

                    const auto &vidx2 = _db.get_index<witness_vote_index>().indices().get<by_account_witness>();
                    auto vitr2 = vidx2.lower_bound(boost::make_tuple(voter.id, witness_id_type()));
                    while (vitr2 != vidx2.end() && vitr2->account == voter.id) {
                        _db.adjust_witness_vote(_db.get(vitr2->witness), voter.witness_vote_fair_weight_prehf5());
                        ++vitr2;
                    }
                }
                else{
                    _db.adjust_witness_vote(witness, -voter.witness_vote_weight());
                    _db.modify(voter, [&](account_object &a) {
                        a.witnesses_voted_for--;
                    });
                    _db.remove(*itr);
                }
            }
        }

        void vote_evaluator::do_apply(const vote_operation &o) {
            database &_db = db();
            FC_ASSERT( !_db.has_hardfork(CHAIN_HARDFORK_4), "vote is deprecated as of HF 4" );
            try {
                const auto &content = _db.get_content(o.author, o.permlink);
                const auto &voter = _db.get_account(o.voter);
                const auto& median_props = _db.get_witness_schedule_object().median_props;

                const auto &content_vote_idx = _db.get_index<content_vote_index>().indices().get<by_content_voter>();
                auto itr = content_vote_idx.find(std::make_tuple(content.id, voter.id));

                int64_t elapsed_seconds = (_db.head_block_time() -
                                           voter.last_vote_time).to_seconds();

                int64_t regenerated_energy =
                        (CHAIN_100_PERCENT * elapsed_seconds) /
                        CHAIN_ENERGY_REGENERATION_SECONDS;
                int64_t current_energy = std::min(int64_t(voter.energy +
                                                         regenerated_energy), int64_t(CHAIN_100_PERCENT));
                FC_ASSERT(current_energy >
                          0, "Account currently does not have voting energy.");

                int64_t abs_weight = abs(o.weight);
                int64_t used_energy = abs_weight;

                const dynamic_global_property_object &dgpo = _db.get_dynamic_global_properties();

                // used_energy = (current_energy * abs_weight / CHAIN_100_PERCENT) * (reserve / max_vote_denom)
                // The second multiplication is rounded up as of HF 259
                int64_t max_vote_denom = dgpo.vote_regeneration_per_day *
                                         CHAIN_ENERGY_REGENERATION_SECONDS /
                                         (60 * 60 * 24);//5
                if(max_vote_denom <= 0) {
                    max_vote_denom = 1; // Prevent division by zero on testnet (CHAIN_ENERGY_REGENERATION_SECONDS < 86400)
                }
                FC_ASSERT(max_vote_denom > 0);

                // Consensus by median props - flag energy additional cost
                if(o.weight < 0){
                    used_energy=used_energy + (used_energy * median_props.flag_energy_additional_cost / CHAIN_100_PERCENT);
                }
                used_energy = used_energy / max_vote_denom;
                FC_ASSERT(used_energy <=
                          current_energy, "Account does not have enough energy to vote.");

                int64_t abs_rshares = (
                    (uint128_t(voter.effective_vesting_shares().amount.value) * used_energy) /
                    (CHAIN_100_PERCENT)).to_uint64();

                // Consensus by median props - vote accounting affects only with abs_rshares greater than vote_accounting_min_rshares
                if(abs_rshares < int64_t(median_props.vote_accounting_min_rshares)){
                    abs_rshares=0;
                }

                if (itr != content_vote_idx.end() && itr->num_changes == -1) {
                    FC_ASSERT(false, "Cannot vote again on a content after payout.");
                }

                if (itr == content_vote_idx.end()) {
                    FC_ASSERT(o.weight != 0, "Vote weight cannot be 0.");
                    /// this is the rshares voting for or against the post
                    int64_t rshares = o.weight < 0 ? -abs_rshares : abs_rshares;

                    if(_db.has_hardfork(CHAIN_HARDFORK_3)){
                        if(voter.awarded_rshares >= static_cast< uint64_t >(abs_rshares)){
                            _db.modify(voter, [&](account_object &a) {
                                a.awarded_rshares -= static_cast< uint64_t >(abs_rshares);
                                a.energy = current_energy;
                                a.last_vote_time = _db.head_block_time();
                                a.vote_count++;
                            });
                        }
                        else{
                            _db.modify(voter, [&](account_object &a) {
                                a.energy = current_energy - used_energy;
                                a.last_vote_time = _db.head_block_time();
                                a.vote_count++;
                            });
                        }
                    }
                    else{
                        if(voter.awarded_rshares >= static_cast< uint64_t >(abs_rshares)){
                            _db.modify(voter, [&](account_object &a) {
                                a.awarded_rshares -= static_cast< uint64_t >(abs_rshares);
                                a.last_vote_time = _db.head_block_time();
                                a.vote_count++;
                            });
                        }
                        else{
                            _db.modify(voter, [&](account_object &a) {
                                a.energy = current_energy - used_energy;
                                a.last_vote_time = _db.head_block_time();
                                a.vote_count++;
                            });
                        }
                    }

                    if (_db.calculate_discussion_payout_time(content) ==
                        fc::time_point_sec::maximum()) {
                        FC_ASSERT(o.weight > 0,"Cannot flag post after payout window");
                        // VIZ: if payout window closed then award author with rshares and create unchangable vote
                        const auto &content_author = _db.get_account(content.author);
                        _db.modify(content_author, [&](account_object &a) {
                            a.awarded_rshares += static_cast< uint64_t >(abs_rshares);
                        });
                        _db.create<content_vote_object>([&](content_vote_object &cv) {
                            cv.voter = voter.id;
                            cv.content = content.id;
                            cv.rshares = rshares;
                            cv.vote_percent = o.weight;
                            cv.last_update = _db.head_block_time();
                            cv.weight = 0;
                            cv.num_changes = -1;
                        });
                    }
                    else{
                        // VIZ: if payout window opened then create vote and calc new rshares for content
                        /// if the current net_rshares is less than 0, the post is getting 0 rewards so it is not factored into total rshares^2
                        fc::uint128_t old_rshares = std::max(content.net_rshares.value, int64_t(0));

                        _db.modify(content, [&](content_object &c) {
                            c.net_rshares += rshares;
                            c.abs_rshares += abs_rshares;
                            if (rshares > 0) {
                                c.vote_rshares += rshares;
                            }
                            if (rshares > 0) {
                                c.net_votes++;
                            } else {
                                c.net_votes--;
                            }
                        });

                        fc::uint128_t new_rshares = std::max(content.net_rshares.value, int64_t(0));

                        uint64_t max_vote_weight = 0;

                        _db.create<content_vote_object>([&](content_vote_object &cv) {
                            cv.voter = voter.id;
                            cv.content = content.id;
                            cv.rshares = rshares;
                            cv.vote_percent = o.weight;
                            cv.last_update = _db.head_block_time();

                            if (rshares > 0 && (content.last_payout == fc::time_point_sec())) {
                                cv.weight = static_cast< uint64_t >(rshares);
                                max_vote_weight = cv.weight;
                            } else {
                                cv.weight = 0;
                            }
                        });

                        if (max_vote_weight) // Optimization
                        {
                            _db.modify(content, [&](content_object &c) {
                                c.total_vote_weight += max_vote_weight;
                            });
                        }

                        _db.adjust_rshares(content, old_rshares, new_rshares);
                    }
                } else {
                    FC_ASSERT(itr->num_changes <
                              CHAIN_MAX_VOTE_CHANGES, "Voter has used the maximum number of vote changes on this content.");

                    FC_ASSERT(itr->vote_percent !=
                              o.weight, "You have already voted in a similar way.");

                    /// this is the rshares voting for or against the post
                    int64_t rshares = o.weight < 0 ? -abs_rshares : abs_rshares;

                    _db.modify(voter, [&](account_object &a) {
                        if(itr->vote_percent < o.weight){
                            a.energy = current_energy - used_energy;
                        }
                        a.last_vote_time = _db.head_block_time();
                    });

                    /// if the current net_rshares is less than 0, the post is getting 0 rewards so it is not factored into total rshares^2
                    fc::uint128_t old_rshares = std::max(content.net_rshares.value, int64_t(0));

                    _db.modify(content, [&](content_object &c) {
                        c.net_rshares -= itr->rshares;
                        c.net_rshares += rshares;
                        c.abs_rshares += abs_rshares;

                        /// TODO: figure out how to handle remove a vote (rshares == 0 )
                        if (rshares > 0 && itr->rshares < 0) {
                            c.net_votes += 2;
                        } else if (rshares > 0 && itr->rshares == 0) {
                            c.net_votes += 1;
                        } else if (rshares == 0 && itr->rshares < 0) {
                            c.net_votes += 1;
                        } else if (rshares == 0 && itr->rshares > 0) {
                            c.net_votes -= 1;
                        } else if (rshares < 0 && itr->rshares == 0) {
                            c.net_votes -= 1;
                        } else if (rshares < 0 && itr->rshares > 0) {
                            c.net_votes -= 2;
                        }
                    });

                    fc::uint128_t new_rshares = std::max(content.net_rshares.value, int64_t(0));

                    _db.modify(content, [&](content_object &c) {
                        c.total_vote_weight -= itr->weight;
                    });

                    _db.modify(*itr, [&](content_vote_object &cv) {
                        cv.rshares = rshares;
                        cv.vote_percent = o.weight;
                        cv.last_update = _db.head_block_time();
                        cv.weight = 0;
                        cv.num_changes += 1;
                    });

                    _db.adjust_rshares(content, old_rshares, new_rshares);
                }

            } FC_CAPTURE_AND_RETHROW((o))
        }

        void custom_evaluator::do_apply(const custom_operation &o) {
            database &_db = db();
            for (const auto &i : o.required_active_auths) {
                auto& account = _db.get_account(i);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!account.valid, "Account flagged as invalid");
                _db.modify(account, [&](account_object& a) {
                    a.custom_sequence++;
                    a.custom_sequence_block_num=1 + _db.head_block_num();//head_block_num contains previous block num
                });
            }
            for (const auto &i : o.required_regular_auths) {
                auto& account = _db.get_account(i);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!account.valid, "Account flagged as invalid");
                _db.modify(account, [&](account_object& a) {
                    a.custom_sequence++;
                    a.custom_sequence_block_num=1 + _db.head_block_num();//head_block_num contains previous block num
                });
            }
            std::shared_ptr<custom_operation_interpreter> eval = _db.get_custom_evaluator(o.id);
            if (!eval) {
                return;
            }

            try {
                eval->apply(o);
            }
            catch (const fc::exception &e) {
                if (_db.is_producing()) {
                    throw e;
                }
            }
            catch (...) {
                elog("Unexpected exception applying custom json evaluator.");
            }
        }

        void request_account_recovery_evaluator::do_apply(const request_account_recovery_operation &o) {
            database &_db = db();
            const auto &recovery_account = _db.get_account(o.recovery_account);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!recovery_account.valid, "Account flagged as invalid");
            const auto &account_to_recover = _db.get_account(o.account_to_recover);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account_to_recover.valid, "Account flagged as invalid");

            if (account_to_recover.recovery_account.length())   // Make sure recovery matches expected recovery account
                FC_ASSERT(account_to_recover.recovery_account ==
                          o.recovery_account, "Cannot recover an account that does not have you as there recovery partner.");
            else                                                  // Empty string recovery account defaults to top witness
                FC_ASSERT(
                        _db.get_index<witness_index>().indices().get<by_vote_name>().begin()->owner ==
                        o.recovery_account, "Top witness must recover an account with no recovery partner.");

            const auto &recovery_request_idx = _db.get_index<account_recovery_request_index>().indices().get<by_account>();
            auto request = recovery_request_idx.find(o.account_to_recover);

            if (request == recovery_request_idx.end()) // New Request
            {
                FC_ASSERT(!o.new_master_authority.is_impossible(), "Cannot recover using an impossible authority.");
                FC_ASSERT(o.new_master_authority.weight_threshold, "Cannot recover using an open authority.");

                // Check accounts in the new authority exist
                for (auto &a : o.new_master_authority.account_auths) {
                    _db.get_account(a.first);
                }

                _db.create<account_recovery_request_object>([&](account_recovery_request_object &req) {
                    req.account_to_recover = o.account_to_recover;
                    req.new_master_authority = o.new_master_authority;
                    req.expires = _db.head_block_time() +
                                  CHAIN_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD;
                });
            } else if (o.new_master_authority.weight_threshold ==
                       0) // Cancel Request if authority is open
            {
                _db.remove(*request);
            } else // Change Request
            {
                FC_ASSERT(!o.new_master_authority.is_impossible(), "Cannot recover using an impossible authority.");

                // Check accounts in the new authority exist
                for (auto &a : o.new_master_authority.account_auths) {
                    _db.get_account(a.first);
                }

                _db.modify(*request, [&](account_recovery_request_object &req) {
                    req.new_master_authority = o.new_master_authority;
                    req.expires = _db.head_block_time() +
                                  CHAIN_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD;
                });
            }
        }

        void recover_account_evaluator::do_apply(const recover_account_operation &o) {
            database &_db = db();
            const auto &account = _db.get_account(o.account_to_recover);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account.valid, "Account flagged as invalid");

            FC_ASSERT(
                    _db.head_block_time() - account.last_account_recovery >
                    CHAIN_MASTER_UPDATE_LIMIT, "Master authority can only be updated once an hour.");

            const auto &recovery_request_idx = _db.get_index<account_recovery_request_index>().indices().get<by_account>();
            auto request = recovery_request_idx.find(o.account_to_recover);

            FC_ASSERT(request !=
                      recovery_request_idx.end(), "There are no active recovery requests for this account.");
            FC_ASSERT(request->new_master_authority ==
                      o.new_master_authority, "New master authority does not match recovery request.");

            const auto &recent_auth_idx = _db.get_index<master_authority_history_index>().indices().get<by_account>();
            auto hist = recent_auth_idx.lower_bound(o.account_to_recover);
            bool found = false;

            while (hist != recent_auth_idx.end() &&
                   hist->account == o.account_to_recover && !found) {
                found = hist->previous_master_authority ==
                        o.recent_master_authority;
                if (found) {
                    break;
                }
                ++hist;
            }

            FC_ASSERT(found, "Recent authority not found in authority history.");

            _db.remove(*request); // Remove first, update_master_authority may invalidate iterator
            _db.update_master_authority(account, o.new_master_authority);
            _db.modify(account, [&](account_object &a) {
                a.last_account_recovery = _db.head_block_time();
                if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                    //reset on sale properties
                    a.account_seller = "";
                    a.account_offer_price = asset(0, TOKEN_SYMBOL);
                    a.account_on_sale=false;
                    a.account_on_sale_start_time = fc::time_point_sec::min();
                }
            });
        }

        void change_recovery_account_evaluator::do_apply(const change_recovery_account_operation &o) {
            database &_db = db();
            const auto &new_recovery_account = _db.get_account(o.new_recovery_account); // Simply validate account exists
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!new_recovery_account.valid, "Account flagged as invalid");
            const auto &account_to_recover = _db.get_account(o.account_to_recover);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account_to_recover.valid, "Account flagged as invalid");

            const auto &change_recovery_idx = _db.get_index<change_recovery_account_request_index>().indices().get<by_account>();
            auto request = change_recovery_idx.find(o.account_to_recover);

            if (request == change_recovery_idx.end()) // New request
            {
                _db.create<change_recovery_account_request_object>([&](change_recovery_account_request_object &req) {
                    req.account_to_recover = o.account_to_recover;
                    req.recovery_account = o.new_recovery_account;
                    req.effective_on = _db.head_block_time() +
                                       CHAIN_MASTER_AUTH_RECOVERY_PERIOD;
                });
            } else if (account_to_recover.recovery_account !=
                       o.new_recovery_account) // Change existing request
            {
                _db.modify(*request, [&](change_recovery_account_request_object &req) {
                    req.recovery_account = o.new_recovery_account;
                    req.effective_on = _db.head_block_time() +
                                       CHAIN_MASTER_AUTH_RECOVERY_PERIOD;
                });
            } else // Request exists and changing back to current recovery account
            {
                _db.remove(*request);
            }
        }

        void delegate_vesting_shares_evaluator::do_apply(const delegate_vesting_shares_operation& op) {
            const auto& delegator = _db.get_account(op.delegator);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!delegator.valid, "Account flagged as invalid");
            const auto& delegatee = _db.get_account(op.delegatee);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!delegatee.valid, "Account flagged as invalid");
            auto delegation = _db.find<vesting_delegation_object, by_delegation>(std::make_tuple(op.delegator, op.delegatee));

            if ( delegation == nullptr ) {
                FC_ASSERT(op.vesting_shares.amount != 0,"Delegation did not exist for initiate revoke.");
            }

            const auto& median_props = _db.get_witness_schedule_object().median_props;
            const auto v_share_price = _db.get_dynamic_global_properties().get_vesting_share_price();
            auto min_delegation = median_props.min_delegation * v_share_price;

            auto now = _db.head_block_time();
            auto delta = delegation ?
                op.vesting_shares - delegation->vesting_shares :
                op.vesting_shares;
            auto increasing = delta.amount > 0;

            if (increasing) {
                auto delegated = delegator.delegated_vesting_shares;
                FC_ASSERT(delegator.available_vesting_shares(true) >= delta,
                    "Account does not have enough vesting shares to delegate.",
                    ("available", delegator.available_vesting_shares(true))
                    ("delta", delta)("vesting_shares", delegator.vesting_shares)("delegated", delegated)
                    ("to_withdraw", delegator.to_withdraw)("withdrawn", delegator.withdrawn));

                if(_db.has_hardfork(CHAIN_HARDFORK_6)){
                    int64_t elapsed_seconds = (now - delegator.last_vote_time).to_seconds();

                    int64_t regenerated_energy =
                            (CHAIN_100_PERCENT * elapsed_seconds) /
                            CHAIN_ENERGY_REGENERATION_SECONDS;
                    int64_t current_energy = std::min(int64_t(delegator.energy +
                                                             regenerated_energy), int64_t(CHAIN_100_PERCENT));
                    FC_ASSERT(current_energy >=
                              0, "Cannot delegate with negative energy.");
                }

                if (!delegation) {
                    FC_ASSERT(op.vesting_shares >= min_delegation,
                        "Account must delegate a minimum of ${v}", ("v",min_delegation)("vesting_shares",op.vesting_shares));
                    _db.create<vesting_delegation_object>([&](vesting_delegation_object& o) {
                        o.delegator = op.delegator;
                        o.delegatee = op.delegatee;
                        o.vesting_shares = op.vesting_shares;
                        o.min_delegation_time = now;
                    });
                }

                _db.shares_sender_recalc_energy(delegator,delta);
                _db.modify(delegator, [&](account_object& a) {
                    a.delegated_vesting_shares += delta;
                });
            } else {
                FC_ASSERT(op.vesting_shares.amount == 0 || op.vesting_shares >= min_delegation,
                    "Delegation must be removed or leave minimum delegation amount of ${v}",
                    ("v",min_delegation)("vesting_shares",op.vesting_shares));
                _db.create<vesting_delegation_expiration_object>([&](vesting_delegation_expiration_object& o) {
                    o.delegator = op.delegator;
                    o.vesting_shares = -delta;
                    if(_db.has_hardfork(CHAIN_HARDFORK_10)){
                        o.expiration = std::max(now + CHAIN_ENERGY_REGENERATION_SECONDS, delegation->min_delegation_time);
                    }
                    else{
                        //old math: possible "double" energy spend in delegation by difference between CHAIN_ENERGY_REGENERATION_SECONDS and CHAIN_CASHOUT_WINDOW_SECONDS
                        o.expiration = std::max(now + CHAIN_CASHOUT_WINDOW_SECONDS, delegation->min_delegation_time);
                    }
                });
            }

            _db.modify(delegatee, [&](account_object& a) {
                a.received_vesting_shares += delta;
            });
            if (delegation) {
                if (op.vesting_shares.amount > 0) {
                    _db.modify(*delegation, [&](vesting_delegation_object& o) {
                        o.vesting_shares = op.vesting_shares;
                    });
                } else {
                    _db.remove(*delegation);
                }
            }
        }

        void set_account_price_evaluator::do_apply(const set_account_price_operation& op) {
            const auto& account = _db.get_account(op.account);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account.valid, "Account flagged as invalid");
            const auto& account_seller = _db.get_account(op.account_seller);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account_seller.valid, "Account flagged as invalid");
            if(_db.has_hardfork(CHAIN_HARDFORK_11)){//new auction mechanics while account have time to sale start
                if(""==account.account_seller){//pay fee if seller is empty
                    const auto& median_props = _db.get_witness_schedule_object().median_props;
                    const dynamic_global_property_object &dgp = _db.get_dynamic_global_properties();

                    FC_ASSERT(account.balance >=
                            median_props.account_on_sale_fee, "Account does not have sufficient funds to place account for sale: required ${a}.",("a",median_props.account_on_sale_fee));

                    _db.adjust_balance(account, -median_props.account_on_sale_fee);
                    _db.modify(dgp, [&](dynamic_global_property_object &dgp) {
                        dgp.committee_fund += median_props.account_on_sale_fee;
                    });
                }
                _db.modify(account, [&](account_object& a) {
                    a.account_seller = op.account_seller;//change seller account

                    //check on sale status and reset bid if needed
                    if(false==op.account_on_sale){//disable sale
                        if(true==a.account_on_auction){//have bid
                            if(account.current_bidder != ""){
                                const auto& account_bidder = _db.get_account(account.current_bidder);
                                //return bid from reserved balance
                                _db.modify(account_bidder, [&](account_object &ab) {
                                    ab.reserved_balance -= account.current_bid;
                                    ab.balance += account.current_bid;
                                });

                                _db.push_virtual_operation(
                                    outbid_operation(account.name,account_bidder.name,account.current_bid));
                            }
                        }

                        //clear params
                        //account_seller as indicator that fee was paid in set_account_price or arget_account_sale
                        //account.account_seller = "";
                        a.account_on_sale=false;
                        a.account_offer_price = asset(0, TOKEN_SYMBOL);
                        a.account_on_sale_start_time = fc::time_point_sec::min();

                        a.account_on_auction=false;
                        a.current_bid = asset(0, TOKEN_SYMBOL);
                        a.current_bidder = "";
                        a.current_bidder_key = public_key_type();
                        a.last_bid = asset(0, TOKEN_SYMBOL);
                    }
                    else{//enable sale
                        if(a.account_offer_price < op.account_offer_price){//new price is greater
                            if(account.current_bidder != ""){//have bid
                                if(account.current_bid.amount >= op.account_offer_price.amount){
                                    //all is good, bid is enough for the new price
                                }
                                else{
                                    //reset bid
                                    const auto& account_bidder = _db.get_account(account.current_bidder);
                                    //return bid from reserved balance
                                    _db.modify(account_bidder, [&](account_object &ab) {
                                        ab.reserved_balance -= account.current_bid;
                                        ab.balance += account.current_bid;
                                    });

                                    _db.push_virtual_operation(
                                        outbid_operation(account.name,account_bidder.name,account.current_bid));

                                    //clear auction params
                                    a.account_on_auction=false;
                                    a.current_bid = asset(0, TOKEN_SYMBOL);
                                    a.current_bidder = "";
                                    a.current_bidder_key = public_key_type();
                                    a.last_bid = asset(0, TOKEN_SYMBOL);
                                }
                            }
                        }

                    }

                    a.account_offer_price = op.account_offer_price;

                    if(false==a.account_on_sale){//was not on sale
                        if(true==op.account_on_sale){
                            a.account_on_sale_start_time = _db.head_block_time() + CHAIN_ACCOUNT_ON_SALE_DELAY;
                        }
                    }
                    a.account_on_sale = op.account_on_sale;
                });
            }
            else{//old behaviour
                if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                    if(""==account.account_seller){
                        const auto& median_props = _db.get_witness_schedule_object().median_props;
                        const dynamic_global_property_object &dgp = _db.get_dynamic_global_properties();

                        FC_ASSERT(account.balance >=
                                median_props.account_on_sale_fee, "Account does not have sufficient funds to place account for sale: required ${a}.",("a",median_props.account_on_sale_fee));

                        _db.adjust_balance(account, -median_props.account_on_sale_fee);
                        _db.modify(dgp, [&](dynamic_global_property_object &dgp) {
                            dgp.committee_fund += median_props.account_on_sale_fee;
                        });
                    }
                }
                _db.modify(account, [&](account_object& a) {
                    a.account_seller = op.account_seller;
                    a.account_offer_price = op.account_offer_price;
                    if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                        if(false==a.account_on_sale){
                            if(true==op.account_on_sale){
                                a.account_on_sale_start_time = _db.head_block_time() + CHAIN_ACCOUNT_ON_SALE_DELAY;
                            }
                        }
                        if(false==op.account_on_sale){
                            a.account_on_sale_start_time = fc::time_point_sec::min();
                        }
                    }
                    a.account_on_sale = op.account_on_sale;
                });
            }
        }

        void target_account_sale_evaluator::do_apply(const target_account_sale_operation& op) {
            const auto& account = _db.get_account(op.account);
            const auto& account_seller = _db.get_account(op.account_seller);
            const auto& target_buyer = _db.get_account(op.target_buyer);

            if(""==account.account_seller){//pay fee if seller is empty
                const auto& median_props = _db.get_witness_schedule_object().median_props;
                const dynamic_global_property_object &dgp = _db.get_dynamic_global_properties();

                FC_ASSERT(account.balance >=
                        median_props.account_on_sale_fee, "Account does not have sufficient funds to place account for sale: required ${a}.",("a",median_props.account_on_sale_fee));

                _db.adjust_balance(account, -median_props.account_on_sale_fee);
                _db.modify(dgp, [&](dynamic_global_property_object &dgp) {
                    dgp.committee_fund += median_props.account_on_sale_fee;
                });
            }
            _db.modify(account, [&](account_object& a) {
                a.account_seller = op.account_seller;//change seller account

                //check on sale status and reset bid if needed
                if(false==op.account_on_sale){//disable sale
                    if(true==a.account_on_auction){//have bid
                        if(account.current_bidder != ""){
                            const auto& account_bidder = _db.get_account(account.current_bidder);
                            //return bid from reserved balance
                            _db.modify(account_bidder, [&](account_object &ab) {
                                ab.reserved_balance -= account.current_bid;
                                ab.balance += account.current_bid;
                            });

                            _db.push_virtual_operation(
                                outbid_operation(account.name,account_bidder.name,account.current_bid));
                        }
                    }

                    //clear params
                    //account_seller as indicator that fee was paid in set_account_price or arget_account_sale
                    //account.account_seller = "";
                    a.account_on_sale=false;
                    a.account_offer_price = asset(0, TOKEN_SYMBOL);
                    a.account_on_sale_start_time = fc::time_point_sec::min();
                    a.target_buyer = "";//clear target buyer if sale disabled

                    a.account_on_auction=false;
                    a.current_bid = asset(0, TOKEN_SYMBOL);
                    a.current_bidder = "";
                    a.current_bidder_key = public_key_type();
                    a.last_bid = asset(0, TOKEN_SYMBOL);
                }
                else{//enable sale
                    if(a.account_offer_price < op.account_offer_price){//new price is greater
                        if(account.current_bidder != ""){//have bid
                            if(account.current_bid.amount >= op.account_offer_price.amount){
                                //all is good, bid is enough for the new price
                                //but need check target_buyer
                                if(account.current_bidder != op.target_buyer){
                                    //reset bid
                                    const auto& account_bidder = _db.get_account(account.current_bidder);
                                    //return bid from reserved balance
                                    _db.modify(account_bidder, [&](account_object &ab) {
                                        ab.reserved_balance -= account.current_bid;
                                        ab.balance += account.current_bid;
                                    });

                                    _db.push_virtual_operation(
                                        outbid_operation(account.name,account_bidder.name,account.current_bid));

                                    //clear auction params
                                    a.account_on_auction=false;
                                    a.current_bid = asset(0, TOKEN_SYMBOL);
                                    a.current_bidder = "";
                                    a.current_bidder_key = public_key_type();
                                    a.last_bid = asset(0, TOKEN_SYMBOL);
                                }
                            }
                            else{
                                //reset bid if new price is bigger than current bid
                                const auto& account_bidder = _db.get_account(account.current_bidder);
                                //return bid from reserved balance
                                _db.modify(account_bidder, [&](account_object &ab) {
                                    ab.reserved_balance -= account.current_bid;
                                    ab.balance += account.current_bid;
                                });

                                _db.push_virtual_operation(
                                    outbid_operation(account.name,account_bidder.name,account.current_bid));

                                //clear auction params
                                a.account_on_auction=false;
                                a.current_bid = asset(0, TOKEN_SYMBOL);
                                a.current_bidder = "";
                                a.current_bidder_key = public_key_type();
                                a.last_bid = asset(0, TOKEN_SYMBOL);
                            }
                        }
                        //no current bid, nothing to do
                    }
                    else{//new price is lesser
                        //need check bidder
                        if(account.current_bidder != ""){//have bid
                            if(account.current_bidder != op.target_buyer){
                                //reset bid for not target buyer
                                const auto& account_bidder = _db.get_account(account.current_bidder);
                                //return bid from reserved balance
                                _db.modify(account_bidder, [&](account_object &ab) {
                                    ab.reserved_balance -= account.current_bid;
                                    ab.balance += account.current_bid;
                                });

                                _db.push_virtual_operation(
                                    outbid_operation(account.name,account_bidder.name,account.current_bid));

                                //clear auction params
                                a.account_on_auction=false;
                                a.current_bid = asset(0, TOKEN_SYMBOL);
                                a.current_bidder = "";
                                a.current_bidder_key = public_key_type();
                                a.last_bid = asset(0, TOKEN_SYMBOL);
                            }
                        }
                    }

                    a.account_offer_price = op.account_offer_price;

                    if(false==a.account_on_sale){//was not on sale
                        if(true==op.account_on_sale){
                            a.account_on_sale_start_time = _db.head_block_time() + CHAIN_ACCOUNT_ON_SALE_DELAY;
                        }
                    }
                    a.account_on_sale=true;
                    a.target_buyer=op.target_buyer;
                }
            });


            _db.modify(account, [&](account_object& a) {
                a.account_offer_price = op.account_offer_price;
                if(false==a.account_on_sale){
                    if(true==op.account_on_sale){
                        a.account_on_sale_start_time = _db.head_block_time() + CHAIN_ACCOUNT_ON_SALE_DELAY;
                    }
                }
                if(false==op.account_on_sale){
                    a.target_buyer = "";//clear target buyer if sale disabled
                    a.account_on_sale_start_time = fc::time_point_sec::min();
                }
                else{
                    a.target_buyer = op.target_buyer;
                }
                a.account_on_sale = op.account_on_sale;
            });
        }

        void set_subaccount_price_evaluator::do_apply(const set_subaccount_price_operation& op) {
            const auto& account = _db.get_account(op.account);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!account.valid, "Account flagged as invalid");
            const auto& subaccount_seller = _db.get_account(op.subaccount_seller);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!subaccount_seller.valid, "Account flagged as invalid");
            if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                if(""==account.subaccount_seller){
                    const auto& median_props = _db.get_witness_schedule_object().median_props;
                    const dynamic_global_property_object &dgp = _db.get_dynamic_global_properties();

                    FC_ASSERT(account.balance >=
                              median_props.subaccount_on_sale_fee, "Account does not have sufficient funds to place subaccount for sale: required ${a}.",("a",median_props.subaccount_on_sale_fee));

                    _db.adjust_balance(account, -median_props.subaccount_on_sale_fee);
                    _db.modify(dgp, [&](dynamic_global_property_object &dgp) {
                        dgp.committee_fund += median_props.subaccount_on_sale_fee;
                    });
                }
            }
            _db.modify(account, [&](account_object& a) {
                a.subaccount_seller = op.subaccount_seller;
                a.subaccount_offer_price = op.subaccount_offer_price;
                a.subaccount_on_sale = op.subaccount_on_sale;
            });
        }

        void buy_account_evaluator::do_apply(const buy_account_operation& op) {
            const auto& buyer = _db.get_account(op.buyer);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!buyer.valid, "Account flagged as invalid");
            const auto& median_props = _db.get_witness_schedule_object().median_props;

            FC_ASSERT(op.account_authorities_key != public_key_type(), "Account authorities key cannot be blank.");

            FC_ASSERT(op.tokens_to_shares >= median_props.account_creation_fee,
                "Inssufficient amount tokens_to_shares: required ${f}, ${p} provided.",
                ("f", median_props.account_creation_fee)("p", op.tokens_to_shares));

            auto acc = _db.find< account_object, by_name >( op.account );
            if(acc != nullptr){//exist
                const auto& account = _db.get_account(op.account);
                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                //    FC_ASSERT(!account.valid, "Account flagged as invalid");
                const auto& account_auth = _db.get<account_authority_object, by_account>(op.account);
                if(!account.account_on_sale){
                    FC_ASSERT(false, "Account not on sale.");
                }
                else{
                    if(_db.has_hardfork(CHAIN_HARDFORK_11)){
                        //check account recovery request
                        const auto &recovery_request_idx = _db.get_index<account_recovery_request_index>().indices().get<by_account>();
                        auto request = recovery_request_idx.find(op.account);
                        if(request != recovery_request_idx.end()){
                            FC_ASSERT(false, "Account have recovery request.");
                        }

                        if(account.target_buyer != ""){//have target buyer
                            if(account.target_buyer != op.buyer){
                                FC_ASSERT(false, "Account have target buyer: ${b}.", ("b", account.target_buyer));
                            }
                        }

                        if(account.account_on_sale_start_time > _db.head_block_time()){//not >=, only > for bid
                            //auction mode
                            asset min_step=asset((account.account_offer_price.amount.value * CHAIN_ACCOUNT_AUCTION_MIN_STEP ) / CHAIN_100_PERCENT, TOKEN_SYMBOL);
                            if(account.last_bid.amount.value != 0){
                                min_step.amount.value=std::max(min_step.amount.value, account.current_bid.amount.value - account.last_bid.amount.value);
                            }
                            if(account.current_bid.amount.value == 0){
                                min_step=account.account_offer_price;//minimal bid is offered price, if no bids before
                            }
                            if(op.account_offer_price >= (account.current_bid + min_step) ){
                                //remove old bid
                                _db.modify(account, [&](account_object &a) {
                                    if(a.current_bidder != ""){//if was bidder
                                        //return bid from reserved balance to balance
                                        const auto& account_bidder = _db.get_account(account.current_bidder);
                                        _db.modify(account_bidder, [&](account_object &ab) {
                                            ab.reserved_balance -= account.current_bid;
                                            ab.balance += account.current_bid;
                                        });

                                        _db.push_virtual_operation(
                                            outbid_operation(account.name,account_bidder.name,account.current_bid));

                                        if(a.current_bidder != op.buyer){
                                            a.last_bid = a.current_bid;//update last bid if bidder not same as current
                                        }
                                    }

                                    a.current_bid = op.account_offer_price;
                                    a.current_bidder = op.buyer;
                                    a.current_bidder_key = public_key_type(op.account_authorities_key);
                                    a.account_on_auction=true;

                                    time_point_sec expand_start_time=fc::time_point::now() + CHAIN_ACCOUNT_AUCTION_EXTENSION_TIME;
                                    a.account_on_sale_start_time = std::max(a.account_on_sale_start_time, expand_start_time);

                                    const auto& new_account_bidder = _db.get_account(op.buyer);
                                    _db.modify(new_account_bidder, [&](account_object &nab) {
                                        nab.reserved_balance += op.account_offer_price;
                                        nab.balance -= op.account_offer_price;
                                    });

                                    _db.push_virtual_operation(
                                        bid_operation(account.name,a.current_bidder,a.current_bid));
                                });
                            }
                            else{
                                FC_ASSERT(false, "Account offer price must be equal or greater minimal bid price: ${bp}.", ("bp",(account.current_bid + min_step)));
                            }
                        }
                        else{//try buy if no bids
                            if(account.current_bidder == ""){//no bids
                                const auto& account_seller = _db.get_account(account.account_seller);
                                //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                                //    FC_ASSERT(!account_seller.valid, "Account flagged as invalid");
                                FC_ASSERT(account.account_offer_price == op.account_offer_price,
                                    "Account offer price must be equal account_offer_price in target account: required ${a}, ${p} provided.",("a",account.account_offer_price)("p",op.account_offer_price));
                                FC_ASSERT(buyer.balance >= (account.account_offer_price + op.tokens_to_shares),
                                    "Inssufficient balance for account_offer_price and tokens_to_shares: required ${a}, ${b} available,",("a",(account.account_offer_price + op.tokens_to_shares))("b",buyer.balance));

                                _db.adjust_balance(buyer, -account.account_offer_price);
                                _db.adjust_balance(account_seller, account.account_offer_price);

                                _db.adjust_balance(buyer, -op.tokens_to_shares);
                                _db.create_vesting(account, op.tokens_to_shares);

                                public_key_type account_authorities_key(op.account_authorities_key);

                                _db.modify(account, [&](account_object &a) {
                                    a.account_seller = "";
                                    a.account_offer_price = asset(0, TOKEN_SYMBOL);
                                    a.account_on_sale=false;
                                    if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                                        a.account_on_sale_start_time = fc::time_point_sec::min();
                                    }

                                    a.subaccount_seller = "";
                                    a.subaccount_offer_price = asset(0, TOKEN_SYMBOL);
                                    a.subaccount_on_sale=false;

                                    a.memo_key = account_authorities_key;
                                    a.recovery_account = op.buyer;
                                    a.last_account_update = _db.head_block_time();
                                });
                                _db.modify(account_auth, [&](account_authority_object &auth) {
                                    auth.master.clear();
                                    auth.master.add_authority( account_authorities_key, 1 );
                                    auth.master.weight_threshold = 1;
                                    auth.active  = auth.master;
                                    auth.regular = auth.active;
                                    auth.last_master_update = _db.head_block_time();
                                });
                                _db.push_virtual_operation(
                                    account_sale_operation(op.account,op.account_offer_price,op.buyer,account_seller.name));
                            }
                            else{//impossible condition when tx run on moment when auction is already ended
                                FC_ASSERT(false, "Account already have bid and waiting release.");
                            }
                        }
                    }
                    else{
                        if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                            //check account recovery request
                            const auto &recovery_request_idx = _db.get_index<account_recovery_request_index>().indices().get<by_account>();
                            auto request = recovery_request_idx.find(op.account);
                            if(request != recovery_request_idx.end()){
                                FC_ASSERT(false, "Account have recovery request.");
                            }
                            //check account on sale delay
                            FC_ASSERT(account.account_on_sale_start_time <= _db.head_block_time(),
                                "Account selling will start on ${t}.",("t",account.account_on_sale_start_time));
                        }
                        const auto& account_seller = _db.get_account(account.account_seller);
                        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                        //    FC_ASSERT(!account_seller.valid, "Account flagged as invalid");
                        FC_ASSERT(account.account_offer_price == op.account_offer_price,
                            "Account offer price must be equal account_offer_price in target account: required ${a}, ${p} provided.",("a",account.account_offer_price)("p",op.account_offer_price));
                        FC_ASSERT(buyer.balance >= (account.account_offer_price + op.tokens_to_shares),
                            "Inssufficient balance for account_offer_price and tokens_to_shares: required ${a}, ${b} available,",("a",(account.account_offer_price + op.tokens_to_shares))("b",buyer.balance));

                        _db.adjust_balance(buyer, -account.account_offer_price);
                        _db.adjust_balance(account_seller, account.account_offer_price);

                        _db.adjust_balance(buyer, -op.tokens_to_shares);
                        _db.create_vesting(account, op.tokens_to_shares);

                        public_key_type account_authorities_key(op.account_authorities_key);

                        _db.modify(account, [&](account_object &a) {
                            a.account_seller = "";
                            a.account_offer_price = asset(0, TOKEN_SYMBOL);
                            a.account_on_sale=false;
                            if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                                a.account_on_sale_start_time = fc::time_point_sec::min();
                            }

                            a.subaccount_seller = "";
                            a.subaccount_offer_price = asset(0, TOKEN_SYMBOL);
                            a.subaccount_on_sale=false;

                            a.memo_key = account_authorities_key;
                            a.recovery_account = op.buyer;
                            a.last_account_update = _db.head_block_time();
                        });
                        _db.modify(account_auth, [&](account_authority_object &auth) {
                            auth.master.clear();
                            auth.master.add_authority( account_authorities_key, 1 );
                            auth.master.weight_threshold = 1;
                            auth.active  = auth.master;
                            auth.regular = auth.active;
                            auth.last_master_update = _db.head_block_time();
                        });
                        _db.push_virtual_operation(
                            account_sale_operation(op.account,op.account_offer_price,op.buyer,account_seller.name));
                    }
                }
            }
            else{//not exist, try buy subaccount
                string account_string = fc::trim(op.account);
                auto colon_pos = account_string.find(".");
                if(colon_pos != std::string::npos) {
                    auto subaccount_part = account_string.substr(0, colon_pos);
                    auto account_part = account_string.substr(colon_pos + 1);

                    auto acc = _db.find< account_object, by_name >( account_part );
                    FC_ASSERT( acc != nullptr, "Account \"${a}\" not found.", ("a", account_part) );

                    FC_ASSERT( is_valid_account_name( account_part ), "Account part ${n} is invalid", ("n", account_part) );
                    FC_ASSERT( is_valid_account_name( subaccount_part ), "Subaccount part name ${n} is invalid", ("n", subaccount_part) );
                    FC_ASSERT( is_valid_domain_name( subaccount_part, account_part ), "Subaccount part ${s} is invalid, account part ${c}", ("s", subaccount_part)("a", account_part) );

                    const auto& account = _db.get_account(account_part);
                    //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
                    //    FC_ASSERT(!account.valid, "Account flagged as invalid");

                    if(!account.subaccount_on_sale){
                        FC_ASSERT(false, "Subccount not on sale.");
                    }
                    else{
                        const auto& account_seller = _db.get_account(account.subaccount_seller);
                        FC_ASSERT(account.subaccount_offer_price == op.account_offer_price,
                            "Account offer price must be equal subaccount_offer_price in target account: required ${a}, ${p} provided.",("a",account.subaccount_offer_price)("p",op.account_offer_price));
                        FC_ASSERT(buyer.balance >= (account.subaccount_offer_price + op.tokens_to_shares),
                            "Inssufficient balance for subaccount_offer_price and tokens_to_shares: required ${a}, ${b} available,",("a",(account.subaccount_offer_price + op.tokens_to_shares))("b",buyer.balance));

                        _db.adjust_balance(buyer, -account.subaccount_offer_price);
                        _db.adjust_balance(account_seller, account.subaccount_offer_price);

                        public_key_type account_authorities_key(op.account_authorities_key);

                        account_name_type new_account_name=op.account;
                        const auto now = _db.head_block_time();

                        _db.create<account_object>([&](account_object &a) {
                            a.name = new_account_name;
                            a.memo_key = account_authorities_key;
                            a.created = now;
                            a.energy = CHAIN_100_PERCENT;
                            a.last_vote_time = now;
                            a.recovery_account = op.buyer;
                        });
                        _db.create<account_authority_object>([&](account_authority_object &auth) {
                            auth.account = new_account_name;
                            auth.master.add_authority(account_authorities_key, 1);
                            auth.master.weight_threshold = 1;
                            auth.active = auth.master;
                            auth.regular = auth.active;
                        });
                        _db.create<account_metadata_object>([&](account_metadata_object& m) {
                            m.account = new_account_name;
                        });
                        const auto &new_account = _db.get_account(new_account_name);
                        _db.adjust_balance(buyer, -op.tokens_to_shares);
                        _db.create_vesting(new_account, op.tokens_to_shares);
                        _db.push_virtual_operation(
                            account_sale_operation(op.account,op.account_offer_price,op.buyer,account_seller.name));
                    }
                }
                else{
                    FC_ASSERT( false, "Account \"${a}\" not found.", ("a", op.account) );
                }
            }
        }

} } // graphene::chain
