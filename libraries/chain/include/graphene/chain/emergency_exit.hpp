#pragma once

#include <algorithm>
#include <cstdint>

// Same choice of config the rest of the protocol headers makes (types.hpp, asset.hpp):
// including plain config.hpp under BUILD_TESTNET redefines CHAIN_NAME and picks the wrong
// CHAIN_MAX_VALIDATORS.
#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>
#else
#include <graphene/protocol/config.hpp>
#endif

namespace graphene { namespace chain {

/// Quorum the QC/TC paths demand for a schedule of `num_scheduled` validators.
///
/// This is the same expression as `database::block_qc_quorum()`; it lives here as well so
/// that (a) the exit rule and the rule the network enforces right after exit are provably
/// one function, and (b) a unit test can count it without constructing a database.
/// `database::block_qc_quorum()` delegates here, so the two cannot drift.
inline uint32_t emergency_quorum_for(uint32_t num_scheduled) {
    const uint32_t two_thirds_plus = num_scheduled * 2 / 3 + 1;
    const uint32_t threshold = uint32_t(uint64_t(num_scheduled) * CHAIN_IRREVERSIBLE_THRESHOLD / CHAIN_100_PERCENT);
    return std::max(two_thirds_plus, threshold);
}

/// Threshold at which emergency consensus ends, computed from the schedule size measured
/// ON ENTRY into emergency (`emergency_schedule_size_on_entry`).
///
/// Why the size on entry and not the size on exit: while emergency is active every real
/// validator's signing key is blanked, so "the validator is there but has not returned" is
/// indistinguishable from "the validator is gone". Counting the schedule at exit counts an
/// empty set.
///
/// `n_on_entry == 0` means NOT MEASURED — a snapshot from a node without the field, or a
/// chain that entered emergency before the field existed. It falls back to "the schedule was
/// full", i.e. quorum for CHAIN_MAX_VALIDATORS. On mainnet that is exactly the legacy
/// constant (21 slots: max(21*2/3+1, 21*75%) = max(15, 15) = 15), so this path reproduces
/// today's behaviour; on the testnet build CHAIN_MAX_VALIDATORS is 1 and the raw legacy
/// expression would yield 0 — a threshold of 0 ends emergency unconditionally, so the
/// quorum form is the only sane fallback.
/// It must NOT fall back to `emergency_quorum_for(0)` = 1 either way: with a real schedule
/// that is the cheapest possible threshold and emergency would be left at the first update.
inline uint32_t emergency_exit_threshold(uint32_t n_on_entry) {
    if (n_on_entry == 0)
        return emergency_quorum_for(CHAIN_MAX_VALIDATORS);
    return emergency_quorum_for(n_on_entry);
}

}}
