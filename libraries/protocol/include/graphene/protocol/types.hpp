#pragma once

#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>
#else
#include <graphene/protocol/config.hpp>
#endif
#include <fc/fixed_string.hpp>

#include <fc/container/flat_fwd.hpp>
#include <fc/io/varint.hpp>
#include <fc/io/enum_type.hpp>
#include <fc/crypto/sha224.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/optional.hpp>
#include <fc/safe.hpp>
#include <fc/container/flat.hpp>
#include <fc/string.hpp>
#include <fc/io/raw.hpp>
#include <fc/uint128_t.hpp>
#include <fc/static_variant.hpp>
#include <fc/smart_ref_fwd.hpp>

#include <boost/multiprecision/cpp_int.hpp>

#include <memory>
#include <vector>
#include <deque>
#include <cstdint>

namespace graphene {

    using fc::uint128_t;
    typedef boost::multiprecision::uint256_t u256;
    typedef boost::multiprecision::uint512_t u512;

    using std::map;
    using std::vector;
    using std::unordered_map;
    using std::string;
    using std::deque;
    using std::shared_ptr;
    using std::weak_ptr;
    using std::unique_ptr;
    using std::set;
    using std::pair;
    using std::enable_shared_from_this;
    using std::tie;
    using std::make_pair;

    using fc::smart_ref;
    using fc::variant_object;
    using fc::variant;
    using fc::enum_type;
    using fc::optional;
    using fc::unsigned_int;
    using fc::signed_int;
    using fc::time_point_sec;
    using fc::time_point;
    using fc::safe;
    using fc::flat_map;
    using fc::flat_set;
    using fc::static_variant;
    using fc::ecc::range_proof_type;
    using fc::ecc::range_proof_info;
    using fc::ecc::commitment_type;
    struct void_t {
    };

    namespace protocol {

        typedef fc::ecc::private_key private_key_type;
        typedef fc::sha256 chain_id_type;
        typedef fc::fixed_string_32 account_name_type;

        struct string_less {
            bool operator()(const std::string &a, const std::string &b) const {
                return a < b;
            }

            bool operator()(const fc::fixed_string<> &a, const fc::fixed_string<> &b) const {
                const char *ap = (const char *)&a;
                const char *ab = (const char *)&b;
                int count = sizeof(a) - 1;
                while (*ap == *ab && count > 0) {
                    ++ap;
                    ++ab;
                    --count;
                }
                return *ap < *ab;
            }

            bool operator()(const fc::fixed_string<> &a, const std::string &b) const {
                return std::string(a) < b;
            }

            bool operator()(const std::string &a, const fc::fixed_string<> &b) const {
                return a < std::string(b);
            }
        };

        // 20-byte identifier. Inherited from Graphene (BitShares -> Steem): the ids are stored in
        // fc::ripemd160 because it is a 20-byte container, NOT because they are ripemd160 digests.
        //   transaction_id_type = first 20 bytes of sha256(packed transaction, without signatures)
        //                         (transaction::id(), transaction.cpp)
        //   block_id_type       = first 20 bytes of sha224(packed header), whose first 4 bytes are
        //                         then replaced by the big-endian block number
        //                         (signed_block_header::id(), block.cpp)
        // The alias is pure naming: same type, same bytes, same wire format and reflection, so it
        // changes nothing in consensus. See docs/api/ids-and-hashes.md.
        typedef fc::ripemd160 id160_type;
        typedef id160_type block_id_type;
        typedef id160_type transaction_id_type;
        // This one IS a real ripemd160: the block merkle root is ripemd160 over the sha256 root of
        // the transaction tree (signed_block::calculate_merkle_root(), block.cpp).
        typedef fc::ripemd160 checksum_type;
        typedef fc::sha256 digest_type;
        typedef fc::ecc::compact_signature signature_type;
        typedef safe<int64_t> share_type;
        typedef uint16_t weight_type;

        struct public_key_type {
            struct binary_key {
                binary_key() {
                }

                uint32_t check = 0;
                fc::ecc::public_key_data data;
            };

            fc::ecc::public_key_data key_data;

            public_key_type();

            public_key_type(const fc::ecc::public_key_data &data);

            public_key_type(const fc::ecc::public_key &pubkey);

            explicit public_key_type(const std::string &base58str);

            operator fc::ecc::public_key_data() const;

            operator fc::ecc::public_key() const;

            explicit operator std::string() const;

            friend bool operator==(const public_key_type &p1, const fc::ecc::public_key &p2);

            friend bool operator==(const public_key_type &p1, const public_key_type &p2);

            friend bool operator<(const public_key_type &p1, const public_key_type &p2) {
                return p1.key_data < p2.key_data;
            }

            friend bool operator!=(const public_key_type &p1, const public_key_type &p2);
        };

        struct extended_public_key_type {
            struct binary_key {
                binary_key() {
                }

                uint32_t check = 0;
                fc::ecc::extended_key_data data;
            };

            fc::ecc::extended_key_data key_data;

            extended_public_key_type();

            extended_public_key_type(const fc::ecc::extended_key_data &data);

            extended_public_key_type(const fc::ecc::extended_public_key &extpubkey);

            explicit extended_public_key_type(const std::string &base58str);

            operator fc::ecc::extended_public_key() const;

            explicit operator std::string() const;

            friend bool operator==(const extended_public_key_type &p1, const fc::ecc::extended_public_key &p2);

            friend bool operator==(const extended_public_key_type &p1, const extended_public_key_type &p2);

            friend bool operator!=(const extended_public_key_type &p1, const extended_public_key_type &p2);
        };

        struct extended_private_key_type {
            struct binary_key {
                binary_key() {
                }

                uint32_t check = 0;
                fc::ecc::extended_key_data data;
            };

            fc::ecc::extended_key_data key_data;

            extended_private_key_type();

            extended_private_key_type(const fc::ecc::extended_key_data &data);

            extended_private_key_type(const fc::ecc::extended_private_key &extprivkey);

            explicit extended_private_key_type(const std::string &base58str);

            operator fc::ecc::extended_private_key() const;

            explicit operator std::string() const;

            friend bool operator==(const extended_private_key_type &p1, const fc::ecc::extended_private_key &p2);

            friend bool operator==(const extended_private_key_type &p1, const extended_private_key_type &p2);

            friend bool operator!=(const extended_private_key_type &p1, const extended_private_key_type &p2);
        };
    }
}  // graphene::protocol

namespace fc {
    void to_variant(const graphene::protocol::public_key_type &var, fc::variant &vo);

    void from_variant(const fc::variant &var, graphene::protocol::public_key_type &vo);

    void to_variant(const graphene::protocol::extended_public_key_type &var, fc::variant &vo);

    void from_variant(const fc::variant &var, graphene::protocol::extended_public_key_type &vo);

    void to_variant(const graphene::protocol::extended_private_key_type &var, fc::variant &vo);

    void from_variant(const fc::variant &var, graphene::protocol::extended_private_key_type &vo);
}

FC_REFLECT((graphene::protocol::public_key_type), (key_data))
FC_REFLECT((graphene::protocol::public_key_type::binary_key), (data)(check))
FC_REFLECT((graphene::protocol::extended_public_key_type), (key_data))
FC_REFLECT((graphene::protocol::extended_public_key_type::binary_key), (check)(data))
FC_REFLECT((graphene::protocol::extended_private_key_type), (key_data))
FC_REFLECT((graphene::protocol::extended_private_key_type::binary_key), (check)(data))

FC_REFLECT_TYPENAME((graphene::protocol::share_type))

FC_REFLECT((graphene::void_t),)
