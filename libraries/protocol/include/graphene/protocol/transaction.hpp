#pragma once

#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/sign_state.hpp>
#include <graphene/protocol/types.hpp>

#include <numeric>

namespace graphene {
    namespace protocol {

        struct transaction {
            uint16_t ref_block_num = 0;
            uint32_t ref_block_prefix = 0;

            fc::time_point_sec expiration;

            vector<operation> operations;
            extensions_type extensions;

            digest_type digest() const;

            transaction_id_type id() const;

            void validate() const;

            digest_type sig_digest(const chain_id_type &chain_id) const;

            void set_expiration(fc::time_point_sec expiration_time);

            void set_reference_block(const block_id_type &reference_block);

            template<typename Visitor>
            vector<typename Visitor::result_type> visit(Visitor &&visitor) {
                vector<typename Visitor::result_type> results;
                for (auto &op : operations) {
                    results.push_back(op.visit(std::forward<Visitor>(visitor)));
                }
                return results;
            }

            template<typename Visitor>
            vector<typename Visitor::result_type> visit(Visitor &&visitor) const {
                vector<typename Visitor::result_type> results;
                for (auto &op : operations) {
                    results.push_back(op.visit(std::forward<Visitor>(visitor)));
                }
                return results;
            }

            void get_required_authorities(flat_set<account_name_type> &active,
                    flat_set<account_name_type> &master,
                    flat_set<account_name_type> &regular,
                    vector<authority> &other) const;
        };

        struct signed_transaction : public transaction {
            signed_transaction(const transaction &trx = transaction())
                    : transaction(trx) {
            }

            const signature_type &sign(const private_key_type &key, const chain_id_type &chain_id);

            signature_type sign(const private_key_type &key, const chain_id_type &chain_id) const;

            set<public_key_type> get_required_signatures(
                    const chain_id_type &chain_id,
                    const flat_set<public_key_type> &available_keys,
                    const authority_getter &get_active,
                    const authority_getter &get_master,
                    const authority_getter &get_regular,
                    uint32_t max_recursion = CHAIN_MAX_SIG_CHECK_DEPTH,
                    const std::function<bool(const account_name_type&, sign_state&)>& direct_active = {},
                    const std::function<bool(const account_name_type&, sign_state&)>& direct_regular = {}
            ) const;

            void verify_authority(
                    const chain_id_type &chain_id,
                    const authority_getter &get_active,
                    const authority_getter &get_master,
                    const authority_getter &get_regular,
                    uint32_t max_recursion = CHAIN_MAX_SIG_CHECK_DEPTH) const;

            set<public_key_type> minimize_required_signatures(
                    const chain_id_type &chain_id,
                    const flat_set<public_key_type> &available_keys,
                    const authority_getter &get_active,
                    const authority_getter &get_master,
                    const authority_getter &get_regular,
                    uint32_t max_recursion = CHAIN_MAX_SIG_CHECK_DEPTH
            ) const;

            flat_set<public_key_type> get_signature_keys(const chain_id_type &chain_id) const;

            vector<signature_type> signatures;

            digest_type merkle_digest() const;

            void clear() {
                operations.clear();
                signatures.clear();
            }
        };

        void verify_authority(const vector<operation> &ops, const flat_set<public_key_type> &sigs,
                const authority_getter &get_active,
                const authority_getter &get_master,
                const authority_getter &get_regular,
                uint32_t max_recursion = CHAIN_MAX_SIG_CHECK_DEPTH,
                bool allow_committe = false,
                const flat_set<account_name_type> &active_aprovals = flat_set<account_name_type>(),
                const flat_set<account_name_type> &master_aprovals = flat_set<account_name_type>(),
                const flat_set<account_name_type> &regular_approvals = flat_set<account_name_type>(),
                const std::function<bool(const account_name_type&, sign_state&)>& direct_active = {},
                bool allow_unused = false,
                flat_set<public_key_type>* used = nullptr,
                const std::function<bool(const operation&, const account_name_type&, bool, sign_state&)>& direct_operation = {});


        struct annotated_signed_transaction : public signed_transaction {
            annotated_signed_transaction() {
            }

            annotated_signed_transaction(const signed_transaction &trx)
                    : signed_transaction(trx), transaction_id(trx.id()) {
            }

            transaction_id_type transaction_id;
            uint32_t block_num = 0;
            uint32_t transaction_num = 0;
        };


        /// @} transactions group

    }
} // graphene::protocol

FC_REFLECT((graphene::protocol::transaction), (ref_block_num)(ref_block_prefix)(expiration)(operations)(extensions))
FC_REFLECT_DERIVED((graphene::protocol::signed_transaction), ((graphene::protocol::transaction)), (signatures))
FC_REFLECT_DERIVED((graphene::protocol::annotated_signed_transaction), ((graphene::protocol::signed_transaction)), (transaction_id)(block_num)(transaction_num));
