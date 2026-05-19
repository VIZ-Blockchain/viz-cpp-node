# Block Log

VIZ stores blocks in binary log files. Two variants exist:

| Variant | Files | Purpose |
|---------|-------|---------|
| `block_log` | `block_log` + `block_log.index` | Full history (archive nodes) |
| `dlt_block_log` | `dlt_block_log` + `dlt_block_log.index` | Rolling window (DLT/snapshot nodes) |

Both share the same data-file format; the index format differs slightly.

---

## Binary Serialization (`fc::raw`)

All data uses little-endian encoding.

| Type | Format |
|------|--------|
| `uint8_t` – `uint64_t` | Fixed-width little-endian |
| `fc::unsigned_int` | Variable-length (varint): 7 data bits + 1 continuation bit per byte |
| `string` | `[varint: length][UTF-8 bytes]` |
| `vector<T>` | `[varint: count][elements...]` |
| `optional<T>` | `[uint8: 0 or 1][value if 1]` |
| `static_variant` | `[varint: type_index][serialized value]` |

---

## Data File Layout

Both `block_log` and `dlt_block_log` use the same format:

```
[block 1 binary][uint64 LE: position of block 1]
[block 2 binary][uint64 LE: position of block 2]
...
```

Each entry = serialized `signed_block` followed by its own start offset as a `uint64_t`.

**Reading the head block:** seek to the last 8 bytes, read the offset, seek there, deserialize.

---

## Index Files

### `block_log.index`

Each entry is an 8-byte `uint64_t` offset into `block_log`.

```
offset = 8 × (block_num − 1)
```

### `dlt_block_log.index`

Starts with an 8-byte header:

```
[uint64 LE: start_block_num][uint64 LE: offset of start_block_num][...]
```

```
offset = 8 + 8 × (block_num − start_block_num)
```

---

## `signed_block` Structure

```
block_header:
  [20 bytes: previous block ID (ripemd160)]
  [4 bytes:  timestamp (uint32 Unix seconds)]
  [varint + string: witness account name]
  [20 bytes: transaction_merkle_root (ripemd160)]
  [varint + vector: extensions]

signed_block_header (appended):
  [65 bytes: witness_signature (1 recovery byte + 32 r + 32 s)]

signed_block (appended):
  [varint + vector<signed_transaction>: transactions]
```

Block number is **not stored directly**. Derive it as:
```
block_num = num_from_id(previous) + 1
num_from_id = first 4 bytes of block_id as uint32_t LE
```
(Genesis: `previous` all zeros → `block_num = 1`.)

---

## `signed_transaction` Structure

```
transaction:
  [2 bytes:  ref_block_num  (uint16 LE)]
  [4 bytes:  ref_block_prefix (uint32 LE)]
  [4 bytes:  expiration (uint32 Unix seconds)]
  [varint + vector<operation>: operations]
  [varint + extensions_type: extensions]

signed_transaction (appended):
  [varint + vector<signature_type>: signatures]
```

---

## Operation Serialization

Each operation in the `operations` vector is a static variant:

```
[varint: type_id][operation-specific fields...]
```

Type IDs: see [Operations Overview](../protocol/operations/overview.md).

**Asset wire format:**
```
[int64: amount][uint64: symbol]
```
Symbol is a packed `uint64`: byte 0 = decimal places, bytes 1–6 = ASCII name, byte 7 = 0x00.

| Symbol | Hex (LE) |
|--------|----------|
| VIZ (3 decimals) | `03 56 49 5A 00 00 00 00` |
| SHARES (6 decimals) | `06 53 48 41 52 45 53 00` |

**Public key wire format:** 33 raw bytes (compressed secp256k1): `[0x02 or 0x03][32-byte x]`.

---

## Block Header Extensions

| Index | Type | Data |
|-------|------|------|
| 0 | `void_t` | (none) |
| 1 | `version` | `uint32_t` version number (major 8 \| hf 8 \| release 16 bits) |
| 2 | `hardfork_version_vote` | `uint32_t` hf_version + `uint32_t` hf_time |

---

## `dlt_block_log` Rolling Window

The DLT log keeps only a recent window of blocks; older blocks are pruned. It starts at `start_block_num > 1`. Nodes using snapshots use this file for crash recovery (replay from snapshot + dlt_block_log).

---

## Block Log Viewer

A terminal block log viewer is included in the tooling (`block-log-viewer.js`):

```
node block-log-viewer.js <path> [--dlt]
```

Key commands: `f` first, `l` last, `n`/`p` next/prev, `g <N>` go to block N, `o` show operations, `s <type>` search by operation type, `S <str>` search by content, `scan` build fast-navigation bitmask.

The `scan` command builds a bitmask file (`block_log.bitmask`) that marks which blocks contain non-empty operations, enabling instant `N`/`P` jumps.

---

See also: [Shared Memory](./shared-memory.md), [Snapshots](./snapshots.md), [Chain Plugin](../plugins/chain.md).
