
#include <graphene/protocol/transaction.hpp>
#include <graphene/protocol/exceptions.hpp>

#include <fc/bitutil.hpp>
#include <fc/smart_ref_impl.hpp>

namespace graphene {
    namespace protocol {

        digest_type signed_transaction::merkle_digest() const {
            digest_type::encoder enc;
            fc::raw::pack(enc, *this);
            return enc.result();
        }

        digest_type transaction::digest() const {
            digest_type::encoder enc;
            fc::raw::pack(enc, *this);
            return enc.result();
        }

        digest_type transaction::sig_digest(const chain_id_type &chain_id) const {
            digest_type::encoder enc;
            fc::raw::pack(enc, chain_id);
            fc::raw::pack(enc, *this);
            return enc.result();
        }

        void transaction::validate() const {
            FC_ASSERT(operations.size() >
                      0, "A transaction must have at least one operation", ("trx", *this));
            for (const auto &op : operations) {
                operation_validate(op);
            }
        }

        graphene::protocol::transaction_id_type graphene::protocol::transaction::id() const {
            auto h = digest();
            transaction_id_type result;
            memcpy(result._hash, h._hash, std::min(sizeof(result), sizeof(h)));
            return result;
        }

        const signature_type &graphene::protocol::signed_transaction::sign(const private_key_type &key, const chain_id_type &chain_id) {
            digest_type h = sig_digest(chain_id);
            signatures.push_back(key.sign_compact(h));
            return signatures.back();
        }

        signature_type graphene::protocol::signed_transaction::sign(const private_key_type &key, const chain_id_type &chain_id) const {
            digest_type::encoder enc;
            fc::raw::pack(enc, chain_id);
            fc::raw::pack(enc, *this);
            return key.sign_compact(enc.result());
        }

        void transaction::set_expiration(fc::time_point_sec expiration_time) {
            expiration = expiration_time;
        }

        void transaction::set_reference_block(const block_id_type &reference_block) {
            ref_block_num = fc::endian_reverse_u32(reference_block._hash[0]);
            ref_block_prefix = reference_block._hash[1];
        }

        void transaction::get_required_authorities(flat_set<account_name_type> &active,
                flat_set<account_name_type> &master,
                flat_set<account_name_type> &regular,
                vector<authority> &other) const {
            for (const auto &op : operations) {
                operation_get_required_authorities(op, active, master, regular, other);
            }
        }

        void assert_unused_approvals(sign_state& s) {
            CHAIN_CTOR_ASSERT(
                !s.remove_unused_signatures(),
                tx_irrelevant_sig,
                [&](auto& e) {
                    e.unused_signatures = std::move(s.unused_signatures);
                    e.append_log(CHAIN_ASSERT_MESSAGE("Unnecessary signature(s) detected"));
                });

            CHAIN_CTOR_ASSERT(
                !s.filter_unused_approvals(),
                tx_irrelevant_approval,
                [&](auto& e) {
                    e.unused_approvals = std::move(s.unused_approvals);
                    e.append_log(CHAIN_ASSERT_MESSAGE("Unnecessary approval(s) detected"));
                });
        }

        void verify_authority(
            const std::vector<operation>& ops,
            const fc::flat_set<public_key_type>& sigs,
            const authority_getter& get_active,
            const authority_getter& get_master,
            const authority_getter& get_regular,
            uint32_t max_recursion_depth,
            bool allow_committe,
            const flat_set<account_name_type>& active_aprovals,
            const flat_set<account_name_type>& master_approvals,
            const flat_set<account_name_type>& regular_approvals,
            const std::function<bool(const account_name_type&, sign_state&)>& direct_active,
            bool allow_unused,
            flat_set<public_key_type>* used,
            const std::function<bool(const operation&, const account_name_type&, bool, sign_state&)>& direct_operation
        ) { try {
            fc::flat_set<account_name_type> required_active;
            fc::flat_set<account_name_type> required_master;
            fc::flat_set<account_name_type> required_regular;
            std::vector<authority> other;
            fc::flat_set<public_key_type> avail;
            std::vector<account_name_type> missing_accounts;
            std::vector<authority> missing_auths;

            for (const auto& op : ops) {
                operation_get_required_authorities(op, required_active, required_master, required_regular, other);
            }

            // Agent proofs cover only operations which directly require this principal.
            // Keep them out of approved_by: nested account authorities and proposal
            // approvals must continue to use ordinary account authority exclusively.
            const auto check_delegated = [&](const account_name_type& id, bool regular, sign_state& state) {
                if (!direct_operation) return false;
                for (const auto& op : ops) {
                    flat_set<account_name_type> a, m, r;
                    std::vector<authority> o;
                    operation_get_required_authorities(op, a, m, r, o);
                    if ((regular ? r : a).count(id) && !direct_operation(op, id, regular, state))
                        return false;
                }
                return true;
            };

            /**
             *  Transactions with operations required regular authority cannot be combined
             *  with transactions requiring active or master authority. This is for ease of
             *  implementation. Future versions of authority verification may be able to
             *  check for the merged authority of active and regular.
             */
            if (required_regular.size()) {
                FC_ASSERT(required_active.size() == 0);
                FC_ASSERT(required_master.size() == 0);
                FC_ASSERT(other.size() == 0);

                sign_state s(sigs, get_regular, avail);
                s.max_recursion = max_recursion_depth;

                for (const auto& id : regular_approvals) {
                   s.approved_by[id] = false;
                }

                for (const auto& id: required_regular) {
                    sign_state ordinary = s;
                    if (ordinary.check_authority(id) ||
                        ordinary.check_authority(get_active(id)) ||
                        ordinary.check_authority(get_master(id))) {
                        s.provided_signatures = std::move(ordinary.provided_signatures);
                        s.approved_by = std::move(ordinary.approved_by);
                        continue;
                    }
                    sign_state delegated = s;
                    if (check_delegated(id, true, delegated)) {
                        s.provided_signatures = std::move(delegated.provided_signatures);
                        continue;
                    }
                    missing_accounts.push_back(id);
                }

                CHAIN_CTOR_ASSERT(
                    missing_accounts.empty(),
                    tx_missing_regular_auth,
                    [&](auto& e) {
                        s.remove_unused_signatures();
                        e.used_signatures = std::move(s.used_signatures);
                        e.missing_accounts = std::move(missing_accounts);
                        e.append_log(
                            CHAIN_ASSERT_MESSAGE("Missing Regular Authority ${id}", ("id", e.missing_accounts)));
                    });

                if (used) {
                    used->clear();
                    for (const auto& k : s.provided_signatures)
                        if (k.second) used->insert(k.first);
                }
                if (!allow_unused) assert_unused_approvals(s);
                return;
            }

            sign_state s(sigs, get_active, avail);
            s.max_recursion = max_recursion_depth;
            for (auto& id: active_aprovals) {
                s.approved_by[id] = false;
            }
            for (auto& id: master_approvals) {
                s.approved_by[id] = false;
            }

            for (const auto& auth: other) {
                if (!s.check_authority(auth)) {
                    missing_auths.push_back(auth);
                }
            }

            CHAIN_CTOR_ASSERT(
                missing_auths.empty(),
                tx_missing_other_auth,
                [&](auto& e) {
                    e.missing_auths = std::move(missing_auths);
                    e.append_log(
                        CHAIN_ASSERT_MESSAGE("Missing Authority", ("auth", e.missing_auths)));
                });

            // fetch all of the top level authorities
            for (const auto& id: required_active) {
                // Preserve the ordinary active/master path, including legacy
                // partial-active signature usage before a successful master check.
                // Only an agent fallback must start without failed ordinary keys
                // or nested-account approvals; it never replaces the getter.
                sign_state ordinary = s;
                if (ordinary.check_authority(id) || ordinary.check_authority(get_master(id))) {
                    s.provided_signatures = std::move(ordinary.provided_signatures);
                    s.approved_by = std::move(ordinary.approved_by);
                    continue;
                }
                sign_state delegated = s;
                if ((direct_active && direct_active(id, delegated)) ||
                    check_delegated(id, false, delegated)) {
                    s.provided_signatures = std::move(delegated.provided_signatures);
                    s.approved_by = std::move(delegated.approved_by);
                    continue;
                }
                missing_accounts.push_back(id);
            }

            CHAIN_CTOR_ASSERT(
                missing_accounts.empty(),
                tx_missing_active_auth,
                [&](auto& e) {
                    s.remove_unused_signatures();
                    e.used_signatures = std::move(s.used_signatures);
                    e.missing_accounts = std::move(missing_accounts);
                    e.append_log(
                        CHAIN_ASSERT_MESSAGE("Missing Active Authority ${id}", ("id", e.missing_accounts)));
                });

            for (const auto& id: required_master) {
                if (master_approvals.find(id) == master_approvals.end() && !s.check_authority(get_master(id))) {
                    missing_accounts.push_back(id);
                } else {
                    s.approved_by[id] = true;
                }
            }

            CHAIN_CTOR_ASSERT(
                missing_accounts.empty(),
                tx_missing_master_auth,
                [&](auto& e) {
                    s.remove_unused_signatures();
                    e.used_signatures = std::move(s.used_signatures);
                    e.missing_accounts = std::move(missing_accounts);
                    e.append_log(
                        CHAIN_ASSERT_MESSAGE("Missing Master Authority ${id}", ("id", e.missing_accounts)));
                });

            if (used) {
                used->clear();
                for (const auto& k : s.provided_signatures)
                    if (k.second) used->insert(k.first);
            }
            if (!allow_unused) assert_unused_approvals(s);
        } FC_CAPTURE_AND_RETHROW((ops)(sigs)) }


        flat_set<public_key_type> signed_transaction::get_signature_keys(const chain_id_type &chain_id) const {
            try {
                auto d = sig_digest(chain_id);
                flat_set<public_key_type> result;
                for (const auto &sig : signatures) {
                    CHAIN_ASSERT(
                        result.insert(fc::ecc::public_key(sig, d)).second,
                        tx_duplicate_sig,
                        "Duplicate Signature detected");
                }
                return result;
            } FC_CAPTURE_AND_RETHROW()
        }


        set<public_key_type> signed_transaction::get_required_signatures(
                const chain_id_type &chain_id,
                const flat_set<public_key_type> &available_keys,
                const authority_getter &get_active,
                const authority_getter &get_master,
                const authority_getter &get_regular,
                uint32_t max_recursion_depth,
                const std::function<bool(const account_name_type&, sign_state&)>& direct_active) const {
            flat_set<account_name_type> required_active;
            flat_set<account_name_type> required_master;
            flat_set<account_name_type> required_regular;
            vector<authority> other;
            get_required_authorities(required_active, required_master, required_regular, other);

            /** regular authority cannot be mixed with active authority in same transaction */
            if (required_regular.size()) {
                sign_state s(get_signature_keys(chain_id), get_regular, available_keys);
                s.max_recursion = max_recursion_depth;

                FC_ASSERT(!required_master.size());
                FC_ASSERT(!required_active.size());
                for (auto &regular : required_regular) {
                    s.check_authority(regular);
                }

                s.remove_unused_signatures();

                set<public_key_type> result;

                for (auto &provided_sig : s.provided_signatures) {
                    if (available_keys.find(provided_sig.first) !=
                        available_keys.end()) {
                        result.insert(provided_sig.first);
                    }
                }

                return result;
            }


            sign_state s(get_signature_keys(chain_id), get_active, available_keys);
            s.max_recursion = max_recursion_depth;

            for (const auto &auth : other) {
                s.check_authority(auth);
            }
            for (auto &active : required_active) {
                if (!direct_active) {
                    s.check_authority(active);
                    continue;
                }
                sign_state ordinary = s;
                if (ordinary.check_authority(active)) {
                    s.provided_signatures = std::move(ordinary.provided_signatures);
                    s.approved_by = std::move(ordinary.approved_by);
                    continue;
                }
                sign_state delegated = s;
                if (direct_active(active, delegated)) {
                    s.provided_signatures = std::move(delegated.provided_signatures);
                    s.approved_by = std::move(delegated.approved_by);
                } else {
                    // Discovery must retain partial ordinary multisig keys.
                    s.provided_signatures = std::move(ordinary.provided_signatures);
                    s.approved_by = std::move(ordinary.approved_by);
                }
            }

            s.remove_unused_signatures();

            sign_state s_master(get_signature_keys(chain_id), get_master, available_keys);
            s_master.max_recursion = max_recursion_depth;

            for (auto &master : required_master) {
                s_master.check_authority(master);
            }

            s_master.remove_unused_signatures();

            set<public_key_type> result;

            for (auto &provided_sig : s.provided_signatures) {
                if (available_keys.find(provided_sig.first) !=
                    available_keys.end()) {
                    result.insert(provided_sig.first);
                }
            }
            for (auto &provided_sig : s_master.provided_signatures) {
                if (available_keys.find(provided_sig.first) !=
                    available_keys.end()) {
                    result.insert(provided_sig.first);
                }
            }

            return result;
        }

        set<public_key_type> signed_transaction::minimize_required_signatures(
                const chain_id_type &chain_id,
                const flat_set<public_key_type> &available_keys,
                const authority_getter &get_active,
                const authority_getter &get_master,
                const authority_getter &get_regular,
                uint32_t max_recursion
        ) const {
            set<public_key_type> s = get_required_signatures(chain_id, available_keys, get_active, get_master, get_regular, max_recursion);
            flat_set<public_key_type> result(s.begin(), s.end());

            for (const public_key_type &k : s) {
                result.erase(k);
                try {
                    graphene::protocol::verify_authority(operations, result, get_active, get_master, get_regular, max_recursion);
                    continue;  // element stays erased if verify_authority is ok
                }
                catch (const tx_missing_master_auth &e) {
                }
                catch (const tx_missing_active_auth &e) {
                }
                catch (const tx_missing_regular_auth &e) {
                }
                catch (const tx_missing_other_auth &e) {
                }
                result.insert(k);
            }
            return set<public_key_type>(result.begin(), result.end());
        }

        void signed_transaction::verify_authority(
                const chain_id_type &chain_id,
                const authority_getter &get_active,
                const authority_getter &get_master,
                const authority_getter &get_regular,
                uint32_t max_recursion) const {
            try {
                graphene::protocol::verify_authority(operations, get_signature_keys(chain_id), get_active, get_master, get_regular, max_recursion);
            } FC_CAPTURE_AND_RETHROW((*this))
        }

    }
} // graphene::protocol
