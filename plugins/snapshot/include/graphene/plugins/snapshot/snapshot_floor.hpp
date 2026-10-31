#pragma once

#include <cstdint>

namespace graphene { namespace plugins { namespace snapshot {

/**
 * May a peer snapshot at @p peer_block_num be imported over our own state?
 *
 * load_snapshot() makes the snapshot head irreversible (LIB = snapshot head), so a
 * snapshot at or below our floor silently takes back blocks we already committed:
 * every later block is then refused with undo_database_exception, and the validator
 * schedule is computed from a head nobody else is on (mainnet 31.10.2026 — the
 * stalled-sync recovery rewound four nodes to block 84792000 while their LIB was
 * 84792899).
 *
 * @p own_floor is max(head, LIB), measured by the caller. It is max and not head on
 * purpose: after a failed reload head < LIB, and reading head alone would let the
 * next check repeat the same mistake. A floor of 0 means "our state is unknown"
 * (a node still importing its first snapshot), so nothing is refused.
 *
 * Strictly greater, not greater-or-equal: a snapshot at exactly our floor gains
 * nothing and still costs a full state wipe and re-import.
 */
inline bool snapshot_ahead_of_floor(uint32_t own_floor, uint32_t peer_block_num) {
    return own_floor == 0 || peer_block_num > own_floor;
}

}}} // namespace graphene::plugins::snapshot
