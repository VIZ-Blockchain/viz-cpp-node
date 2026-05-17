#include <fc/io/datastream.hpp>

#include <graphene/protocol/proposal_operations.hpp>
#include <graphene/chain/chain_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/proposal_object.hpp>

namespace graphene { namespace chain {
    namespace {
        template <typename F, typename S>
        void remove_existing(F& first, const S& second) {
            auto src = std::move(first);

            std::set_difference(
                src.begin(), src.end(),
                second.begin(), second.end(),
                std::inserter(first, first.begin()));
        }

        void assert_irrelevant_proposal_authority(
            database& db, const proposal_object& proposal, const proposal_update_operation& o
        ) {
            fc::flat_set<account_name_type> operation_approvals;
            fc::flat_set<account_name_type> active_approvals;
            fc::flat_set<account_name_type> master_approvals;
            fc::flat_set<account_name_type> regular_approvals;
            fc::flat_set<public_key_type> used_signatures;

            operation_approvals.insert(o.active_approvals_to_add.begin(), o.active_approvals_to_add.end());
            operation_approvals.insert(o.master_approvals_to_add.begin(), o.master_approvals_to_add.end());
            operation_approvals.insert(o.regular_approvals_to_add.begin(), o.regular_approvals_to_add.end());

            // Verify authority doesn't check all cases, it throws an error on a first breaking
            // That is why on a missing authority we add them and rethrow the exception on the following conditions:
            //
            // 1. an irrelevant signature/approval exists
            // 2. the irrelevant signature/approval has came in the operation
            for (int i = 0; i < 3 /* active + master or regular */; ++i) {
                try {
                    proposal.verify_authority(db, active_approvals, master_approvals, regular_approvals);
                    return;
                } catch (const protocol::tx_missing_active_auth& e) {
                    if (!active_approvals.empty()) {
                        throw;
                    }
                    active_approvals.insert(e.missing_accounts.begin(), e.missing_accounts.end());
                    used_signatures.insert(e.used_signatures.begin(), e.used_signatures.end());
                } catch (const protocol::tx_missing_master_auth& e) {
                    if (!master_approvals.empty()) {
                        throw;
                    }
                    master_approvals.insert(e.missing_accounts.begin(), e.missing_accounts.end());
                    used_signatures.insert(e.used_signatures.begin(), e.used_signatures.end());
                } catch (const protocol::tx_missing_regular_auth& e) {
                    if (!regular_approvals.empty()) {
                        throw;
                    }
                    regular_approvals.insert(e.missing_accounts.begin(), e.missing_accounts.end());
                    used_signatures.insert(e.used_signatures.begin(), e.used_signatures.end());
                } catch (const protocol::tx_irrelevant_sig& e) {
                    for (auto& sig: e.unused_signatures) {
                        if (o.key_approvals_to_add.count(sig) && !used_signatures.count(sig)) {
                            throw;
                        }
                    }
                    return;
                } catch (const protocol::tx_irrelevant_approval& e) {
                    for (auto& account: e.unused_approvals) {
                        if (operation_approvals.count(account)) {
                            throw;
                        }
                    }
                    return;
                } catch (...) {
                    throw;
                }
            }
        }

        struct safe_int_increment {
            safe_int_increment(int& value)
                : value_(value) {
                value_++;
            }

            ~safe_int_increment() {
                value_--;
            }

            int& value_;
        };
    }

    proposal_create_evaluator::proposal_create_evaluator(database& db)
        : evaluator_impl<proposal_create_evaluator>(db) {
    }

    void proposal_create_evaluator::do_apply(const proposal_create_operation& o) { try {
        safe_int_increment depth_increment(depth_);

        if (_db.is_producing()) {
            FC_ASSERT(
                depth_ <= CHAIN_MAX_PROPOSAL_DEPTH,
                "You can't create more than ${depth} nested proposals",
                ("depth", CHAIN_MAX_PROPOSAL_DEPTH));
        }

        _db.get_account(o.author);
        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
        //    FC_ASSERT(!check_author.valid, "Account flagged as invalid");

        FC_ASSERT(nullptr == _db.find_proposal(o.author, o.title), "Proposal already exists.");

        const auto now = _db.head_block_time();
        FC_ASSERT(
            o.expiration_time > now,
            "Proposal has already expired on creation.");
        FC_ASSERT(
            o.expiration_time <= now + CHAIN_MAX_PROPOSAL_LIFETIME_SEC,
            "Proposal expiration time is too far in the future.");
        FC_ASSERT(
            !o.review_period_time || *o.review_period_time > now,
            "Proposal review period has expired on creation.");
        FC_ASSERT(
            !o.review_period_time || *o.review_period_time < o.expiration_time,
            "Proposal review period must be less than its overall lifetime.");

        //Populate the required approval sets
        flat_set<account_name_type> required_master;
        flat_set<account_name_type> required_active;
        flat_set<account_name_type> required_regular;
        flat_set<account_name_type> required_total;
        std::vector<authority> other;

        for (const auto& op : o.proposed_operations) {
            operation_get_required_authorities(op.op, required_active, required_master, required_regular, other);
        }
        FC_ASSERT(other.size() == 0); // TODO: what about other???

        // All accounts which must provide both master and active authority should be omitted from
        // the active authority set. Master authority approval implies active authority approval.
        required_total.insert(required_master.begin(), required_master.end());
        remove_existing(required_active, required_total);
        required_total.insert(required_active.begin(), required_active.end());

        // For more information, see transaction.cpp
        FC_ASSERT(
            required_regular.empty() != required_total.empty(),
            "Can't combine operations required regular authority and active or master authority");
        required_total.insert(required_regular.begin(), required_regular.end());

        // Doesn't allow proposal with combination of create_account() + some_operation()
        //  because it will be never approved.
        for (const auto& account: required_total) {
            FC_ASSERT(
                nullptr != _db.find_account(account),
                "Account '${account}' for proposed operation doesn't exist", ("account", account));
        }

        FC_ASSERT(required_total.size(), "No operations require approvals");

        transaction trx;
        for (const auto& op : o.proposed_operations) {
            trx.operations.push_back(op.op);
        }
        trx.set_expiration(_db.head_block_time() + CHAIN_MAX_TIME_UNTIL_EXPIRATION);

        const uint32_t skip_steps =
            graphene::chain::database::skip_authority_check |
            graphene::chain::database::skip_transaction_signatures |
            graphene::chain::database::skip_tapos_check |
            graphene::chain::database::skip_database_locking;

        _db.validate_transaction(trx, skip_steps);

        auto ops_size = fc::raw::pack_size(trx.operations);

        const auto& proposal = _db.create<proposal_object>([&](proposal_object& p){
            p.author = o.author;
            from_string(p.title, o.title);
            from_string(p.memo, o.memo);
            p.expiration_time = o.expiration_time;
            if (o.review_period_time) {
                p.review_period_time = o.review_period_time;
            }
            p.proposed_operations.resize(ops_size);
            fc::datastream<char *> ds(p.proposed_operations.data(), ops_size);
            fc::raw::pack(ds, trx.operations);

            p.required_active_approvals.insert(required_active.begin(), required_active.end());
            p.required_master_approvals.insert(required_master.begin(), required_master.end());
            p.required_regular_approvals.insert(required_regular.begin(), required_regular.end());
        });

        for (const auto& account: required_total) {
            _db.get_account(account);
            //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
            //    FC_ASSERT(!check_account.valid, "Account flagged as invalid");
            _db.create<required_approval_object>([&](required_approval_object& o){
                o.account = account;
                o.proposal = proposal.id;
            });
        }
    } FC_CAPTURE_AND_RETHROW((o)) }

    proposal_update_evaluator::proposal_update_evaluator(database& db)
        : evaluator_impl<proposal_update_evaluator>(db) {
    }

    void proposal_update_evaluator::do_apply(const proposal_update_operation& o) { try {
        safe_int_increment depth_increment(depth_);

        if (_db.is_producing()) {
            FC_ASSERT(
                depth_ <= CHAIN_MAX_PROPOSAL_DEPTH,
                "You can't create more than ${depth} nested proposals",
                ("depth", CHAIN_MAX_PROPOSAL_DEPTH));
        }

        auto& proposal = _db.get_proposal(o.author, o.title);
        const auto now = _db.head_block_time();

        if (proposal.review_period_time && now >= *proposal.review_period_time) {
            FC_ASSERT(
                o.active_approvals_to_add.empty() &&
                o.master_approvals_to_add.empty() &&
                o.regular_approvals_to_add.empty() &&
                o.key_approvals_to_add.empty(),
                "This proposal is in its review period. No new approvals may be added.");
        }

        auto check_existing = [&](const auto& to_remove, const auto& dst) {
            for (const auto& a: to_remove) {
                FC_ASSERT(dst.find(a) != dst.end(), "Can't remove the non existing approval '${id}'", ("id", a));
            }
        };

        check_existing(o.active_approvals_to_remove, proposal.available_active_approvals);
        check_existing(o.master_approvals_to_remove, proposal.available_master_approvals);
        check_existing(o.regular_approvals_to_remove, proposal.available_regular_approvals);
        check_existing(o.key_approvals_to_remove, proposal.available_key_approvals);

        auto check_duplicate = [&](const auto& to_add, const auto& dst) {
            for (const auto& a: to_add) {
                FC_ASSERT(dst.find(a) == dst.end(), "Can't add already exist approval '${id}'", ("id", a));
            }
        };

        check_duplicate(o.active_approvals_to_add, proposal.available_active_approvals);
        check_duplicate(o.master_approvals_to_add, proposal.available_master_approvals);
        check_duplicate(o.regular_approvals_to_add, proposal.available_regular_approvals);
        check_duplicate(o.key_approvals_to_add, proposal.available_key_approvals);

        _db.modify(proposal, [&](proposal_object &p){
            p.available_active_approvals.insert(o.active_approvals_to_add.begin(), o.active_approvals_to_add.end());
            p.available_master_approvals.insert(o.master_approvals_to_add.begin(), o.master_approvals_to_add.end());
            p.available_regular_approvals.insert(o.regular_approvals_to_add.begin(), o.regular_approvals_to_add.end());
            p.available_key_approvals.insert(o.key_approvals_to_add.begin(), o.key_approvals_to_add.end());

            remove_existing(p.available_active_approvals, o.active_approvals_to_remove);
            remove_existing(p.available_master_approvals, o.master_approvals_to_remove);
            remove_existing(p.available_regular_approvals, o.regular_approvals_to_remove);
            remove_existing(p.available_key_approvals, o.key_approvals_to_remove);
        });

        if (proposal.review_period_time) {
            // if no ability to add an approval, there is no reason to keep the proposal
            if (now >= *proposal.review_period_time &&
                proposal.available_active_approvals.empty() &&
                proposal.available_master_approvals.empty() &&
                proposal.available_regular_approvals.empty() &&
                proposal.available_key_approvals.empty()
            ) {
                _db.remove(proposal);
            }
            return;
        }

        assert_irrelevant_proposal_authority(_db, proposal, o);

        if (proposal.is_authorized_to_execute(_db)) {
            // All required approvals are satisfied. Execute!
            try {
                _db.push_proposal(proposal);
            } catch (fc::exception &e) {
                wlog(
                    "Proposed transaction ${author}::${title} failed to apply once approved with exception:\n"
                    "----\n${reason}\n"
                    "----\nWill try again when it expires.",
                    ("author", o.author)("title", o.title)("reason", e.to_detail_string()));
            }
        }
    } FC_CAPTURE_AND_RETHROW((o)) }

    void proposal_delete_evaluator::do_apply(const proposal_delete_operation& o) { try {
        const auto& proposal = _db.get_proposal(o.author, o.title);

        FC_ASSERT(
            proposal.author == o.requester ||
            proposal.required_active_approvals.count(o.requester) ||
            proposal.required_master_approvals.count(o.requester) ||
            proposal.required_regular_approvals.count(o.requester),
            "Provided authority is not authoritative for this proposal.",
            ("author", o.author)("title", o.title)("requester", o.requester));

        _db.remove(proposal);

    } FC_CAPTURE_AND_RETHROW((o)) }

} } // graphene::chain