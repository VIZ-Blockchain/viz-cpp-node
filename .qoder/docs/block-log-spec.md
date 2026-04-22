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

### flat_set<T>

```
[varint: element_count][element_1][element_2]...[element_n]
```

Same wire format as `vector<T>`. Elements are sorted and unique in the data structure, but serialized in sorted order.

### flat_map<K,V>

```
[varint: pair_count][key_1][value_1][key_2][value_2]...[key_n][value_n]
```

Same wire format as `vector<pair<K,V>>`. Each pair is serialized as key then value.

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

### extensions_type

```
[varint: count][varint: type_index_0]...[varint: type_index_n]
```

Defined as `flat_set<future_extensions>` where `future_extensions = static_variant<void_t>`.
Each extension item is just a varint type index (always 0 for `void_t`).
Usually empty (serialized as single byte `0x00`).

### Reflected Structures

Structures with `FC_REFLECT` macro are serialized field-by-field in the order defined by the macro. **Field order matters** — the binary layout must match the `FC_REFLECT` declaration exactly.

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

### block_header_extension (static_variant)

Defined as `static_variant<void_t, version, hardfork_version_vote>` in
`libraries/protocol/include/graphene/protocol/base.hpp`.

| Type Index | Name | Serialized Data | Description |
|------------|------|----------------|-------------|
| 0 | `void_t` | (none) | Empty placeholder |
| 1 | `version` | `uint32_t v_num` (4 bytes) | Witness version reporting (8.8.16 bit packing) |
| 2 | `hardfork_version_vote` | `uint32_t` hf_version + `uint32_t` hf_time (8 bytes) | Hardfork vote |

Version `v_num` packing: `(major << 24) | (hardfork << 16) | release`. Example: `0x00000001` = version 0.0.1.

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

> **Steem lineage note:** VIZ inherits this design from the Steem codebase. Rather than using
> the full 32-byte `fc::sha256` for block IDs and merkle roots, VIZ uses the shorter 20-byte
> `fc::ripemd160`. The `block_id_type` is defined as `typedef fc::ripemd160 block_id_type` and
> `checksum_type` as `typedef fc::ripemd160 checksum_type` in
> `libraries/protocol/include/graphene/protocol/types.hpp`. This is sometimes called a "feature"
> by the original developers — the shorter hash saves space and the collision risk is considered
> acceptable for block identification within a running chain.

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

**Asset symbol format** (inherited from Steem codebase, defined in `asset.cpp`):

The `uint64 symbol` is a packed structure with the following byte layout (little-endian):

| Byte | Field | Description |
|------|-------|-------------|
| 0 | decimals | Number of decimal places (0-14) |
| 1-6 | name | ASCII symbol name (up to 6 chars) |
| 7 | null | Always 0x00 (null terminator) |

Known symbols (from `config.hpp`):

| Name | Decimals | uint64 (LE bytes) | uint64 (hex) |
|------|----------|--------------------|--------------|
| `VIZ` | 3 | `03 56 49 5A 00 00 00 00` | `0x000000005A495603` |
| `SHARES` | 6 | `06 53 48 41 52 45 53 00` | `0x0053455241485306` |

> **Steem lineage note:** This symbol encoding format is inherited from the Steem codebase.
> Rather than using an enum or string, the symbol is packed as a uint64 with precision in byte 0
> and the ASCII name in bytes 1-6. This design allows the symbol to carry its own decimal
> precision information without requiring a separate lookup.

#### authority
```
[uint32: weight_threshold]
[flat_map<account_name_type, weight_type>: account_auths]
[flat_map<public_key_type, weight_type>: key_auths]
```

> **Note:** `account_auths` and `key_auths` are `flat_map` (not `flat_set<pair<...>>`).
> Wire format is identical to `vector<pair<K,V>>`: varint count + (key, value) pairs.

#### public_key_type

**Wire format:** 33 raw bytes of compressed secp256k1 public key:
```
[1 byte: 0x02 or 0x03 prefix][32 bytes: x-coordinate]
```

**String representation** (in JSON output, not on wire):
1. Compute `ripemd160(33_key_bytes)`
2. First 4 bytes of hash = checksum
3. Concatenate: `[33 key bytes][4 checksum bytes]` = 37 bytes
4. Base58-encode the 37-byte buffer
5. Prepend `"VIZ"` (CHAIN_ADDRESS_PREFIX)

Example: `VIZ7wMEutJdCfdSKNgVAp17v9uoTqwwkUqn2kwVsJ6zG5XYJcvj81`

Matches C++ `public_key_type::operator std::string()` in `libraries/protocol/types.cpp`.

#### chain_properties_init

Used by `chain_properties_update_operation` (ID 25). **12 fields only** — NOT the same as `chain_properties_hf9`.
```
[asset: account_creation_fee]
[uint32: maximum_block_size]
[uint32: create_account_delegation_ratio]
[uint32: create_account_delegation_time]
[asset: min_delegation]
[uint16: min_curation_percent]
[uint16: max_curation_percent]
[uint16: bandwidth_reserve_percent]
[asset: bandwidth_reserve_below]
[uint16: flag_energy_additional_cost]
[uint32: vote_accounting_min_rshares]
[uint16: committee_request_approve_min_percent]
```

#### versioned_chain_properties

Used by `versioned_chain_properties_update_operation` (ID 46). A `static_variant` of chain property variants:

```
[varint: type_index][chain_properties_init base fields][variant-specific additional fields]
```

| Type Index | Name | Base | Additional Fields |
|------------|------|------|-------------------|
| 0 | `chain_properties_init` | — | (12 base fields only) |
| 1 | `chain_properties_hf4` | init | +3: `inflation_witness_percent`(uint16), `inflation_ratio_committee_vs_reward_fund`(uint16), `inflation_recalc_period`(uint32) |
| 2 | `chain_properties_hf6` | hf4 | +3: `data_operations_cost_additional_bandwidth`(uint32), `witness_miss_penalty_percent`(uint16), `witness_miss_penalty_duration`(uint32) |
| 3 | `chain_properties_hf9` | hf6 | +7: `create_invite_min_balance`(asset), `committee_create_request_fee`(asset), `create_paid_subscription_fee`(asset), `account_on_sale_fee`(asset), `subaccount_on_sale_fee`(asset), `witness_declaration_fee`(asset), `withdraw_intervals`(uint16) |

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

#### invite_registration_operation (ID: 45)
```
[account_name_type: account]
[public_key_type: new_account_key]
[string: invite_secret]  ← WIF-encoded private key (e.g. '5Kd...'), NOT raw bytes
[extensions_type: extensions]
```

> **Important:** `invite_secret` is a `string` (WIF-encoded private key), NOT raw 32 bytes.
> The same applies to `claim_invite_balance_operation` (ID 44) and `use_invite_balance_operation` (ID 58).

#### claim_invite_balance_operation (ID: 44)
```
[account_name_type: initiator]
[account_name_type: receiver]
[string: invite_secret]  ← WIF-encoded private key string
[extensions_type: extensions]
```

#### use_invite_balance_operation (ID: 58)
```
[account_name_type: initiator]
[account_name_type: receiver]
[string: invite_secret]  ← WIF-encoded private key string
[extensions_type: extensions]
```

#### chain_properties_update_operation (ID: 25)
```
[account_name_type: owner]
[chain_properties_init: props]  ← 12-field struct, NOT chain_properties_hf9
[extensions_type: extensions]
```

#### versioned_chain_properties_update_operation (ID: 46)
```
[account_name_type: owner]
[versioned_chain_properties: props]  ← static_variant<init/hf4/hf6/hf9>
[extensions_type: extensions]
```

#### benefactor_award_operation (ID: 49, virtual)
```
[account_name_type: initiator]     ← FC_REFLECT order: initiator BEFORE benefactor
[account_name_type: benefactor]
[account_name_type: receiver]
[uint64: custom_sequence]
[string: memo]
[asset: shares]
```

#### set_paid_subscription_operation (ID: 50)
```
[account_name_type: account]
[string: url]
[uint16: levels]        ← uint16, NOT uint8
[asset: amount]
[uint16: period]        ← uint16, NOT uint32
[extensions_type: extensions]
```

#### paid_subscribe_operation (ID: 51)
```
[account_name_type: account]
[account_name_type: subscriber]
[uint16: level]         ← uint16, NOT uint8
[asset: amount]
[uint16: period]        ← uint16, NOT uint32
[extensions_type: extensions]
```

#### paid_subscription_action_operation (ID: 52, virtual)
```
[account_name_type: subscriber]
[account_name_type: account]
[uint16: level]
[asset: amount]
[uint16: period]
[uint64: summary_duration_sec]
[asset: summary_amount]
```

#### cancel_paid_subscription_operation (ID: 53, virtual)
```
[account_name_type: subscriber]
[account_name_type: account]
```

> Only 2 fields — no `level` field.

#### buy_account_operation (ID: 56)
```
[account_name_type: account]
[account_name_type: buyer]
[asset: tokens_to_shares]  ← asset type, NOT bool/uint8
[extensions_type: extensions]
```

#### account_sale_operation (ID: 57, virtual)
```
[account_name_type: account]
[asset: price]
[account_name_type: buyer]
[account_name_type: seller]
```

#### expire_escrow_ratification_operation (ID: 59, virtual)
```
[account_name_type: from]
[account_name_type: to]
[account_name_type: agent]
[uint32: escrow_id]
[asset: token_amount]
[asset: fee]
[time_point_sec: ratification_deadline]
```

#### bid_operation (ID: 62, virtual)
```
[account_name_type: account]     ← FC_REFLECT order: account BEFORE bidder
[account_name_type: bidder]
[asset: bid]
```

#### outbid_operation (ID: 63, virtual)
```
[account_name_type: account]     ← FC_REFLECT order: account BEFORE bidder
[account_name_type: bidder]
[asset: bid]
```

#### proposal_delete_operation (ID: 24)
```
[account_name_type: author]
[string: title]
[account_name_type: requester]   ← was previously missing
[extensions_type: extensions]
```

## Tools

### block-log-reader.js

JavaScript module for reading block_log and dlt_block_log files. Provides programmatic access to block data.

**Usage:**
```javascript
const { createBlockLogReader, getBlockNum, publicKeyToString } = require('./block-log-reader');

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
node block-log-viewer.js <path> [--dlt] [--reader=<module_path>]
```

`<path>` can be either:
- Path to a `block_log` or `dlt_block_log` file directly
- Path to a directory containing `block_log` / `dlt_block_log` (auto-detected)

**Options:**

| Option | Description |
|--------|-------------|
| `--dlt` | Use DLT (rolling) block log reader |
| `--reader=<path>` | Path to `block-log-reader.js` module |

**Directory auto-detection** (when `<path>` is a directory):

| Files present | `--dlt` | Result |
|--------------|---------|--------|
| `block_log` only | no | Standard mode |
| `dlt_block_log` only | no | Auto-switches to DLT mode |
| Both | no | Standard mode (`block_log`) |
| Both | yes | DLT mode (`dlt_block_log`) |
| Neither | — | Error |

**Module resolution** (if `block-log-reader.js` is not in the same directory):
- `--reader=/path/to/block-log-reader.js` — explicit CLI option
- `BLOCK_LOG_READER=/path/to/block-log-reader.js` — environment variable

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
| `S <string>` | Search forward for substring in any operation's data (incl. virtual) |
| `S =<string>` | Search forward for **exact** string match (`=` prefix disables substring matching) |
| `R <string>` | Fast raw ASCII byte search in block data (no UTF-8/emoji) |
| `e <string>` | Export all ops containing string to `search_export_<unixtime>.json` |
| `e =<string>` | Export all ops **exactly matching** string (`=` prefix for exact match) |
| `c` | Continue last search (s/S/R/e) |

> **Search modes:** Without `=` prefix, string search uses substring matching (e.g. `S VIZ` matches
> any occurrence of "VIZ" in data). With `=` prefix, uses exact match (e.g. `e ="V"` finds only
> the value `V`, not `VIZ`). Surrounding quotes are automatically stripped.

#### Other

| Command | Description |
|---------|-------------|
| `scan` | Scan all blocks, build & save bitmask for fast navigation |
| `i` | Show block header info |
| `hex` | Show raw block data in hex |
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

#### String Search (`S`), Raw Search (`R`), and Export (`e`)

The `S` command searches the **full operation data** (including virtual) for a substring match. The `=` prefix enables exact match mode — `S ="V"` finds only the exact value `V`, not `VIZ` or other strings containing `V`.

The `R` command performs a fast raw ASCII byte search directly in the block's binary data, without deserialization. No UTF-8/emoji support. Useful for quickly locating blocks containing specific ASCII patterns.

The `e` command performs the same search as `S` across **all** blocks and writes results to a JSON file in the block_log directory:

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
