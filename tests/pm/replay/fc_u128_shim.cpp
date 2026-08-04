// Minimal, bit-exact stand-in for the two out-of-line fc::uint128_t members that
// libraries/chain/pm/parimutuel.cpp references, so the pure PM math can be unit
// tested without linking libfc. fc's real operator/= delegates to
// boost::multiprecision::uint128_t truncating division and its operator*= is a
// wrapping 128-bit multiply (fc/src/uint128_t.cpp:224,257) — both identical to
// the compiler's native unsigned __int128 ops.
#include <fc/uint128_t.hpp>

namespace fc {
    static inline unsigned __int128 pack(uint64_t h, uint64_t l) {
        return ((unsigned __int128)h << 64) | l;
    }

    uint128_t& uint128_t::operator*=(const uint128_t& b) {
        unsigned __int128 v = pack(hi, lo) * pack(b.hi, b.lo);
        hi = (uint64_t)(v >> 64); lo = (uint64_t)v; return *this;
    }

    uint128_t& uint128_t::operator/=(const uint128_t& b) {
        unsigned __int128 v = pack(hi, lo) / pack(b.hi, b.lo);
        hi = (uint64_t)(v >> 64); lo = (uint64_t)v; return *this;
    }
}
