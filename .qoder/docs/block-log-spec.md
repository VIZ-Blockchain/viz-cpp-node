# Block Log File Format Specification

This document specifies the binary file formats for VIZ blockchain block logs.

## Overview

VIZ uses two types of block logs:
- **block_log**: Full blockchain history (used by archive nodes)
- **dlt_block_log**: Rolling window of recent blocks (used by DLT/snapshot nodes)

Each log consists of two files:
- **Data file**: Contains serialized block data
- **Index file**: Contains offsets for random access by block number

---

## fc::raw Binary Serialization

All data in block logs is serialized using `fc::raw` format.

### Primitive Types

| Type | Size | Format |
|------|------|--------|
| `uint8_t` | 1 byte | Little-endian |
| `uint16_t` | 2 bytes | Little-endian |
| `uint32_t` | 4 bytes | Little-endian |
| `uint64_t` | 8 bytes | Little-endian |
| `int8_t` | 1 byte | Two's complement |
| `int16_t` | 2 bytes | Little-endian, two's complement |
| `int32_t` | 4 bytes | Little-endian, two's complement |
| `int64_t` | 8 bytes | Little-endian, two's complement |
| `bool` | 1 byte | `0x00` = false, `0x01` = true |

### Variable-Length Integer (varint)

`fc::unsigned_int` uses a variable-length encoding similar to protobuf:

```
Each byte: [7 data bits][1 continuation bit]
- Continuation bit = 1: more bytes follow
- Continuation bit = 0: last byte

Value is reconstructed by concatenating 7-bit chunks in order.
```

**Examples:**
- `0x00` → 0
- `0x01` → 1
- `0x7F` → 127
- `0x80 0x01` → 128
- `0xFF 0x01` → 255
- `0x80 0x02` → 256

`fc::signed_int` uses zigzag encoding before varint:
- Zigzag: `(n << 1) ^ (n >> 31)` for encoding
- Un-zigzag: `(n >> 1) ^ -(n & 1)` for decoding

### String

```
[varint: length][bytes: UTF-8 string data]
```

- Length is serialized as `fc::unsigned_int` (varint)
- Empty string: `[0x00]`

### Vector<T>

```
[varint: element_count][element_1][element_2]...[element_n]
```

### Optional<T>

```
[uint8: flag][value if flag=1]
```

- `flag = 0`: no value (empty optional)
- `flag = 1`: value follows

### Static Variant

```
[varint: type_index][serialized_value]
```

The type index identifies which type in the variant is stored.

### Reflected Structures

Structures with `FC_REFLECT` macro are serialized field-by-field in the order defined.

---

## block_log (Data File)

**Filename:** `block_log`

### File Layout

```
+------------------+--------------------+------------------+--------------------+-----+
| Block 1 (binary) | Position (8 bytes) | Block 2 (binary) | Position (8 bytes) | ... |
+------------------+--------------------+------------------+--------------------+-----+
```

Each block entry consists of:
1. **Block data**: fc::raw serialized `signed_block`
2. **Position**: `uint64_t` little-endian offset of this block's start position

### Reading the Head Block

1. Seek to the last 8 bytes of the file
2. Read `uint64_t` position value
3. Seek to that position
4. Deserialize `signed_block`

### Sequential Scan

Starting from position 0:
1. Deserialize `signed_block`
2. Read next 8 bytes as `uint64_t` position (should match current position)
3. Next block starts at `current_position + block_size + 8`
4. Repeat until end of file

### Block Number Extraction

Block number is NOT stored directly. It is derived from:
- `block.previous` field (20-byte `block_id_type` / `ripemd160` hash)
- `block_num = num_from_id(previous) + 1`
- For genesis block: `previous` is all zeros, `block_num = 1`

The `num_from_id` function extracts bytes 0-3 (first 4 bytes) of the hash as `uint32_t` little-endian.

---

## block_log.index (Index File)

**Filename:** `block_log.index`

### File Layout

```
+------------------+------------------+-----+------------------------+
| Position of #1   | Position of #2   | ... | Position of Head Block |
+------------------+------------------+-----+------------------------+
        8 bytes            8 bytes              8 bytes
```

### Index Entry Location

For block number `N`:
```
offset = 8 * (N - 1)
```

**Example:**
- Block 1: offset 0
- Block 2: offset 8
- Block 1000000: offset 7999992

### Index Validation

The last 8 bytes of both files should contain the same position value:
- `block_log`: position of head block
- `block_log.index`: position of head block

If they differ, the index should be reconstructed from the data file.

---

## dlt_block_log (Data File)

**Filename:** `dlt_block_log`

### File Layout

Identical to regular `block_log`:
```
+------------------+--------------------+------------------+--------------------+-----+
| Block N (binary) | Position (8 bytes) | Block N+1 (bin)  | Position (8 bytes) | ... |
+------------------+--------------------+------------------+--------------------+-----+
```

The key difference: **can start at any block number**, not necessarily block 1.

---

## dlt_block_log.index (Index File)

**Filename:** `dlt_block_log.index`

### File Layout

```
+-------------------+------------------+------------------+-----+------------------------+
| start_block_num   | Position of #S   | Position of #S+1 | ... | Position of Head Block |
| (8 bytes header)  | (8 bytes)        | (8 bytes)        |     | (8 bytes)              |
+-------------------+------------------+------------------+-----+------------------------+
```

### Header

- **Bytes 0-7**: `uint64_t` little-endian `start_block_num`
- The first block number stored in this rolling log

### Index Entry Location

For block number `N`:
```
offset = 8 + 8 * (N - start_block_num)
```

**Example** (start_block_num = 10000000):
- Block 10000000: offset 8
- Block 10000001: offset 16
- Block 10050000: offset 400008

### Reading a Block by Number

1. Read header (first 8 bytes) → `start_block_num`
2. Verify: `N >= start_block_num` and `N <= head_block_num`
3. Calculate offset: `8 + 8 * (N - start_block_num)`
4. Read `uint64_t` position at that offset
5. Seek to position in `dlt_block_log`
6. Deserialize `signed_block`

---

## signed_block Structure

```
signed_block extends signed_block_header
  └─ signed_block_header extends block_header
       └─ block_header
```

### block_header Fields

| Field | Type | Description |
|-------|------|-------------|
| `previous` | `block_id_type` (20 bytes) | Hash of previous block |
| `timestamp` | `time_point_sec` (4 bytes) | Block creation time (Unix timestamp) |
| `witness` | `string` | Witness account name |
| `transaction_merkle_root` | `checksum_type` (20 bytes) | Merkle root of transactions |
| `extensions` | `vector<block_header_extension>` | Future extensions (usually empty) |

### signed_block_header Additional Fields

| Field | Type | Description |
|-------|------|-------------|
| `witness_signature` | `signature_type` (65 bytes) | Witness signature (compact) |

### signed_block Additional Fields

| Field | Type | Description |
|-------|------|-------------|
| `transactions` | `vector<signed_transaction>` | List of transactions |

### Serialization Order

```
block_header:
  [20 bytes: previous]
  [4 bytes: timestamp]
  [varint + string: witness]
  [20 bytes: transaction_merkle_root]
  [varint + extensions: extensions]

signed_block_header (after block_header):
  [65 bytes: witness_signature]

signed_block (after signed_block_header):
  [varint + transactions: transactions]
```

---

## signed_transaction Structure

### transaction Fields

| Field | Type | Description |
|-------|------|-------------|
| `ref_block_num` | `uint16_t` (2 bytes) | Reference block number |
| `ref_block_prefix` | `uint32_t` (4 bytes) | Reference block prefix |
| `expiration` | `time_point_sec` (4 bytes) | Transaction expiration |
| `operations` | `vector<operation>` | List of operations |
| `extensions` | `extensions_type` | Extensions (usually empty) |

### signed_transaction Additional Fields

| Field | Type | Description |
|-------|------|-------------|
| `signatures` | `vector<signature_type>` | Transaction signatures |

### Serialization Order

```
transaction:
  [2 bytes: ref_block_num]
  [4 bytes: ref_block_prefix]
  [4 bytes: expiration]
  [varint + operations: operations]
  [varint + extensions: extensions]

signed_transaction (after transaction):
  [varint + signatures: signatures]
```

---

## Special Types

### block_id_type / checksum_type / transaction_id_type

All are `fc::ripemd160` hashes (20 bytes).

### signature_type

`fc::ecc::compact_signature` - 65 bytes:
- 1 byte: recovery id
- 32 bytes: r coordinate
- 32 bytes: s coordinate

### time_point_sec

`uint32_t` Unix timestamp (seconds since 1970-01-01 00:00:00 UTC).

### account_name_type

`fc::fixed_string_32` - serialized as regular string (varint length + UTF-8 bytes).

---

## JavaScript Implementation Notes

### Endianness

All multi-byte integers are **little-endian**.

### BigInt Handling

- Use `BigInt` for `uint64_t` positions (JavaScript numbers lose precision above 2^53)
- Convert to `Number` only when safe (< 2^53)

### Buffer Reading

```javascript
const fs = require('fs');
const { Buffer } = require('buffer');

// Read uint64_t as BigInt
function readUint64LE(buffer, offset) {
  return buffer.readBigUInt64LE(offset);
}

// Read uint32_t
function readUint32LE(buffer, offset) {
  return buffer.readUInt32LE(offset);
}

// Read varint (fc::unsigned_int)
function readVarint(buffer, offset) {
  let value = 0n;
  let shift = 0;
  let pos = offset;

  while (true) {
    const byte = buffer.readUInt8(pos++);
    value |= BigInt(byte & 0x7F) << BigInt(shift);
    if (!(byte & 0x80)) break;
    shift += 7;
  }

  return { value: Number(value), bytesRead: pos - offset };
}
```

### File Reading Strategy

1. Use `fs.open()` + `fs.read()` for random access
2. Or use `mmap`-like approach with `Buffer` for frequent access
3. Cache the index file in memory for fast lookups

---

## Error Handling

### Index Mismatch

When `block_log` and `block_log.index` positions don't match:
1. Delete the index file
2. Reconstruct by scanning the data file
3. Write new index entries

### Corrupted Block

If a block fails to deserialize:
1. Check if position marker matches actual position
2. If mismatch, scan backward from end of file
3. The file may be truncated from a crash

### Empty Files

- New/empty `block_log`: 1 null byte (implementation artifact)
- Valid minimum size: > 8 bytes (at least one position marker)

---

## Operation Types

Operations are serialized as `fc::static_variant` with a type index followed by the operation data.

### Operation Type IDs

| ID | Operation Name | Type |
|----|----------------|------|
| 0 | `vote_operation` | deprecated |
| 1 | `content_operation` | deprecated |
| 2 | `transfer_operation` | regular |
| 3 | `transfer_to_vesting_operation` | regular |
| 4 | `withdraw_vesting_operation` | regular |
| 5 | `account_update_operation` | regular |
| 6 | `witness_update_operation` | regular |
| 7 | `account_witness_vote_operation` | regular |
| 8 | `account_witness_proxy_operation` | regular |
| 9 | `delete_content_operation` | deprecated |
| 10 | `custom_operation` | regular |
| 11 | `set_withdraw_vesting_route_operation` | regular |
| 12 | `request_account_recovery_operation` | regular |
| 13 | `recover_account_operation` | regular |
| 14 | `change_recovery_account_operation` | regular |
| 15 | `escrow_transfer_operation` | regular |
| 16 | `escrow_dispute_operation` | regular |
| 17 | `escrow_release_operation` | regular |
| 18 | `escrow_approve_operation` | regular |
| 19 | `delegate_vesting_shares_operation` | regular |
| 20 | `account_create_operation` | regular |
| 21 | `account_metadata_operation` | regular |
| 22 | `proposal_create_operation` | regular |
| 23 | `proposal_update_operation` | regular |
| 24 | `proposal_delete_operation` | regular |
| 25 | `chain_properties_update_operation` | regular |
| 26 | `author_reward_operation` | virtual |
| 27 | `curation_reward_operation` | virtual |
| 28 | `content_reward_operation` | virtual |
| 29 | `fill_vesting_withdraw_operation` | virtual |
| 30 | `shutdown_witness_operation` | virtual |
| 31 | `hardfork_operation` | virtual |
| 32 | `content_payout_update_operation` | virtual |
| 33 | `content_benefactor_reward_operation` | virtual |
| 34 | `return_vesting_delegation_operation` | virtual |
| 35 | `committee_worker_create_request_operation` | regular |
| 36 | `committee_worker_cancel_request_operation` | regular |
| 37 | `committee_vote_request_operation` | regular |
| 38 | `committee_cancel_request_operation` | virtual |
| 39 | `committee_approve_request_operation` | virtual |
| 40 | `committee_payout_request_operation` | virtual |
| 41 | `committee_pay_request_operation` | virtual |
| 42 | `witness_reward_operation` | virtual |
| 43 | `create_invite_operation` | regular |
| 44 | `claim_invite_balance_operation` | regular |
| 45 | `invite_registration_operation` | regular |
| 46 | `versioned_chain_properties_update_operation` | regular |
| 47 | `award_operation` | regular |
| 48 | `receive_award_operation` | virtual |
| 49 | `benefactor_award_operation` | virtual |
| 50 | `set_paid_subscription_operation` | regular |
| 51 | `paid_subscribe_operation` | regular |
| 52 | `paid_subscription_action_operation` | virtual |
| 53 | `cancel_paid_subscription_operation` | virtual |
| 54 | `set_account_price_operation` | regular |
| 55 | `set_subaccount_price_operation` | regular |
| 56 | `buy_account_operation` | regular |
| 57 | `account_sale_operation` | virtual |
| 58 | `use_invite_balance_operation` | regular |
| 59 | `expire_escrow_ratification_operation` | virtual |
| 60 | `fixed_award_operation` | regular |
| 61 | `target_account_sale_operation` | regular |
| 62 | `bid_operation` | virtual |
| 63 | `outbid_operation` | virtual |

### Operation Serialization Format

```
[varint: type_index][operation_specific_fields...]
```

### Common Types

#### asset
```
[int64: amount][uint64: symbol]
```

Symbol encoding:
- `VIZ`: 0x0000000000000003
- `SHARES`: 0x0000000000000004

#### authority
```
[uint32: weight_threshold]
[flat_set<pair<account_name_type, weight_type>>: account_auths]
[flat_set<pair<public_key_type, weight_type>>: key_auths]
```

#### public_key_type
```
[33 bytes: compressed secp256k1 public key]
```

### Operation Structures

#### transfer_operation (ID: 2)
```
[account_name_type: from]
[account_name_type: to]
[asset: amount]
[string: memo]
```

#### account_create_operation (ID: 20)
```
[asset: fee]
[asset: delegation]
[account_name_type: creator]
[account_name_type: new_account_name]
[authority: master]
[authority: active]
[authority: regular]
[public_key_type: memo_key]
[string: json_metadata]
[account_name_type: referrer]
[extensions_type: extensions]
```

#### witness_update_operation (ID: 6)
```
[account_name_type: owner]
[string: url]
[public_key_type: block_signing_key]
```

#### award_operation (ID: 47)
```
[account_name_type: initiator]
[account_name_type: receiver]
[uint16: energy]
[uint64: custom_sequence]
[string: memo]
[vector<beneficiary_route_type>: beneficiaries]
```

#### beneficiary_route_type
```
[account_name_type: account]
[uint16: weight]
```

## Tools

### block-log-reader.js

JavaScript module for reading block_log and dlt_block_log files. Provides programmatic access to block data.

**Usage:**
```javascript
const { createBlockLogReader, getBlockNum } = require('./block-log-reader');

const reader = createBlockLogReader('/path/to/block_log');
// Or for DLT: createBlockLogReader('/path/to/dlt_block_log', undefined, true);

const block = reader.readBlockByNum(1000);
console.log(getBlockNum(block), block.witness);
reader.close();
```

### block-log-viewer.js

Interactive terminal UI for browsing block logs. No external dependencies.

**Usage:**
```
node block-log-viewer.js <block_log_path> [--dlt]
```

#### Navigation Commands

| Command | Description |
|---------|-------------|
| `f` | First block |
| `l` | Last block |
| `n` | Next block |
| `p` | Previous block |
| `N` | Next block with non-free operations (bitmask-accelerated) |
| `P` | Prev block with non-free operations (bitmask-accelerated) |
| `g <num>` | Go to block #num |
| `<num>` | Jump to block number directly |

#### Operation Commands

| Command | Description |
|---------|-------------|
| `o` | Show all operations in current block (JSON) |
| `o <name>` | Show operations matching type name (e.g. `o transfer`) |
| `s <name>` | Search forward for block containing operation by type name |
| `S <string>` | Search forward for string in any operation's full JSON (incl. virtual) |
| `e <string>` | Export all matching operations to `search_export_<unixtime>.json` |

#### Other

| Command | Description |
|---------|-------------|
| `scan` | Scan all blocks, build & save bitmask for fast navigation |
| `i` | Show block header info |
| `h` | Help |
| `q` | Quit |

#### Bitmask File (block_log.bitmask)

The `scan` command builds a compact bitmask file that marks which blocks contain non-free (non-virtual) operations. Once built, `N` and `P` commands jump instantly between non-empty blocks without deserializing skipped blocks.

**File format:**

```
+-------------------+-------------------+-------------------------------+
| start_block_num   | end_block_num     | bit array                     |
| (8 bytes, uint64) | (8 bytes, uint64) | 1 bit per block               |
+-------------------+-------------------+-------------------------------+
```

- **Bytes 0–7**: `uint64_t` LE — `start_block_num`
- **Bytes 8–15**: `uint64_t` LE — `end_block_num`
- **Bytes 16+**: bit array, 1 bit per block (bit 0 = `start_block_num`)
  - `1` = block has non-free operations
  - `0` = block is empty or has only virtual operations
- Total size: `16 + ceil((end - start + 1) / 8)` bytes

**Example:** 10,000,000 blocks → ~1.25 MB bitmask file.

The bitmask is auto-loaded on startup if it exists and matches the current block range. If the range differs, a rescan is suggested.

#### String Search (`S`) and Export (`e`)

The `S` command searches the **full JSON** of every operation (including virtual) for a case-insensitive substring match. This allows finding blocks by account name, memo text, hex hash, or any other field value — not just operation type name.

The `e` command performs the same search across **all** blocks and writes results to a JSON file in the block_log directory:

```
search_export_1745312345.json
```

Export format:
```json
[
  {
    "block": 12345,
    "timestamp": "2024-01-15 10:30:00 UTC",
    "witness": "on1x",
    "typeId": 47,
    "typeName": "award_operation",
    "isVirtual": false,
    "data": { "initiator": "alice", "receiver": "on1x", ... }
  }
]
```

Buffers are serialized as hex strings; BigInts as decimal strings.

---

## See Also

- [data-types.md](data-types.md) - VIZ data type definitions
- [snapshot-plugin.md](snapshot-plugin.md) - DLT mode documentation
- [block-log-spec.json](block-log-spec.json) - Machine-readable specification
