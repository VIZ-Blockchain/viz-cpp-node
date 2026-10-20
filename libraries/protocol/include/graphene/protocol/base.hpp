#pragma once

#include <graphene/protocol/types.hpp>
#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/version.hpp>

#include <fc/time.hpp>

namespace graphene {
    namespace protocol {

        struct base_operation {
            void get_required_authorities(vector<authority> &) const {
            }

            void get_required_active_authorities(flat_set<account_name_type> &) const {
            }

            void get_required_regular_authorities(flat_set<account_name_type> &) const {
            }

            void get_required_master_authorities(flat_set<account_name_type> &) const {
            }

            bool is_virtual() const {
                return false;
            }

            void validate() const {
            }
        };

        struct virtual_operation : public base_operation {
            bool is_virtual() const {
                return true;
            }

            void validate() const {
                FC_ASSERT(false, "This is a virtual operation");
            }
        };

        /// Quorum certificate of the parent block (HF16). Required when the
        /// block hands the turn over (parent produced by another validator):
        /// >2/3 of scheduled validators signed block_vote_digest for the parent.
        /// Two conflicting certified blocks at one height need a validator to
        /// vote twice at that height, which the validator plugin refuses.
        struct block_qc_certificate {
            uint32_t block_num = 0;
            block_id_type block_id;
            uint32_t round = 0;
            flat_map<account_name_type, signature_type> signatures;
        };

        typedef static_variant<
                void_t,
                version,              // Normal witness version reporting, for diagnostics and voting
                hardfork_version_vote, // Voting for the next hardfork to trigger
                block_qc_certificate  // HF16: QC of the parent block (append only: index is wire format)
        > block_header_extensions;

        typedef static_variant<
                void_t
        > future_extensions;

        typedef flat_set<block_header_extensions> block_header_extensions_type;
        typedef flat_set<future_extensions> extensions_type;


    }
} // graphene::protocol

FC_REFLECT((graphene::protocol::block_qc_certificate), (block_num)(block_id)(round)(signatures))
FC_REFLECT_TYPENAME((graphene::protocol::block_header_extensions))
FC_REFLECT_TYPENAME((graphene::protocol::future_extensions))
