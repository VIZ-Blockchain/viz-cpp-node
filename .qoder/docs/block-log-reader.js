/**
 * VIZ Block Log Reader for JavaScript/Node.js
 *
 * Reads block_log and dlt_block_log files from VIZ blockchain nodes.
 * Based on block-log-spec.json specification.
 *
 * @module block-log-reader
 */

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

// ============================================================================
// Constants
// ============================================================================

const CONSTANTS = {
  CHAIN_BLOCK_SIZE: 1048576,      // Max block size (1 MB)
  MIN_VALID_FILE_SIZE: 8,          // Minimum valid file size
  INDEX_HEADER_SIZE: 8,            // DLT index header size
  SIGNATURE_SIZE: 65,              // Compact signature size
  HASH_SIZE_RIPEMD160: 20,         // RIPEMD-160 hash size
  HASH_SIZE_SHA256: 32,            // SHA-256 hash size
  NPOS: BigInt('0xFFFFFFFFFFFFFFFF'), // Invalid position marker

  CHAIN_ADDRESS_PREFIX: 'VIZ',   // Public key base58 prefix

  // Asset symbols (Steem-style: byte 0 = decimals, bytes 1-6 = ASCII name, byte 7 = 0x00)
  // From config.hpp: SHARES_SYMBOL = 6|('S'<<8)|('H'<<16)|('A'<<24)|('R'<<32)|('E'<<40)|('S'<<48)
  //                 TOKEN_SYMBOL  = 3|('V'<<8)|('I'<<16)|('Z'<<24)
  TOKEN_SYMBOL:  BigInt('0x000000005A495603'),
  SHARES_SYMBOL: BigInt('0x0053455241485306')
};

// Operation type IDs mapping
const OPERATION_TYPES = {
  0: { name: 'vote_operation', isVirtual: false },
  1: { name: 'content_operation', isVirtual: false },
  2: { name: 'transfer_operation', isVirtual: false },
  3: { name: 'transfer_to_vesting_operation', isVirtual: false },
  4: { name: 'withdraw_vesting_operation', isVirtual: false },
  5: { name: 'account_update_operation', isVirtual: false },
  6: { name: 'witness_update_operation', isVirtual: false },
  7: { name: 'account_witness_vote_operation', isVirtual: false },
  8: { name: 'account_witness_proxy_operation', isVirtual: false },
  9: { name: 'delete_content_operation', isVirtual: false },
  10: { name: 'custom_operation', isVirtual: false },
  11: { name: 'set_withdraw_vesting_route_operation', isVirtual: false },
  12: { name: 'request_account_recovery_operation', isVirtual: false },
  13: { name: 'recover_account_operation', isVirtual: false },
  14: { name: 'change_recovery_account_operation', isVirtual: false },
  15: { name: 'escrow_transfer_operation', isVirtual: false },
  16: { name: 'escrow_dispute_operation', isVirtual: false },
  17: { name: 'escrow_release_operation', isVirtual: false },
  18: { name: 'escrow_approve_operation', isVirtual: false },
  19: { name: 'delegate_vesting_shares_operation', isVirtual: false },
  20: { name: 'account_create_operation', isVirtual: false },
  21: { name: 'account_metadata_operation', isVirtual: false },
  22: { name: 'proposal_create_operation', isVirtual: false },
  23: { name: 'proposal_update_operation', isVirtual: false },
  24: { name: 'proposal_delete_operation', isVirtual: false },
  25: { name: 'chain_properties_update_operation', isVirtual: false },
  26: { name: 'author_reward_operation', isVirtual: true },
  27: { name: 'curation_reward_operation', isVirtual: true },
  28: { name: 'content_reward_operation', isVirtual: true },
  29: { name: 'fill_vesting_withdraw_operation', isVirtual: true },
  30: { name: 'shutdown_witness_operation', isVirtual: true },
  31: { name: 'hardfork_operation', isVirtual: true },
  32: { name: 'content_payout_update_operation', isVirtual: true },
  33: { name: 'content_benefactor_reward_operation', isVirtual: true },
  34: { name: 'return_vesting_delegation_operation', isVirtual: true },
  35: { name: 'committee_worker_create_request_operation', isVirtual: false },
  36: { name: 'committee_worker_cancel_request_operation', isVirtual: false },
  37: { name: 'committee_vote_request_operation', isVirtual: false },
  38: { name: 'committee_cancel_request_operation', isVirtual: true },
  39: { name: 'committee_approve_request_operation', isVirtual: true },
  40: { name: 'committee_payout_request_operation', isVirtual: true },
  41: { name: 'committee_pay_request_operation', isVirtual: true },
  42: { name: 'witness_reward_operation', isVirtual: true },
  43: { name: 'create_invite_operation', isVirtual: false },
  44: { name: 'claim_invite_balance_operation', isVirtual: false },
  45: { name: 'invite_registration_operation', isVirtual: false },
  46: { name: 'versioned_chain_properties_update_operation', isVirtual: false },
  47: { name: 'award_operation', isVirtual: false },
  48: { name: 'receive_award_operation', isVirtual: true },
  49: { name: 'benefactor_award_operation', isVirtual: true },
  50: { name: 'set_paid_subscription_operation', isVirtual: false },
  51: { name: 'paid_subscribe_operation', isVirtual: false },
  52: { name: 'paid_subscription_action_operation', isVirtual: true },
  53: { name: 'cancel_paid_subscription_operation', isVirtual: true },
  54: { name: 'set_account_price_operation', isVirtual: false },
  55: { name: 'set_subaccount_price_operation', isVirtual: false },
  56: { name: 'buy_account_operation', isVirtual: false },
  57: { name: 'account_sale_operation', isVirtual: true },
  58: { name: 'use_invite_balance_operation', isVirtual: false },
  59: { name: 'expire_escrow_ratification_operation', isVirtual: true },
  60: { name: 'fixed_award_operation', isVirtual: false },
  61: { name: 'target_account_sale_operation', isVirtual: false },
  62: { name: 'bid_operation', isVirtual: true },
  63: { name: 'outbid_operation', isVirtual: true }
};

// ============================================================================
// Binary Reading Utilities
// ============================================================================

class BinaryReader {
  constructor(buffer, offset = 0) {
    this.buffer = buffer;
    this.offset = offset;
  }

  /**
   * Read unsigned 8-bit integer
   */
  readUint8() {
    return this.buffer.readUInt8(this.offset++);
  }

  /**
   * Read unsigned 16-bit integer (little-endian)
   */
  readUint16LE() {
    const value = this.buffer.readUInt16LE(this.offset);
    this.offset += 2;
    return value;
  }

  /**
   * Read unsigned 32-bit integer (little-endian)
   */
  readUint32LE() {
    const value = this.buffer.readUInt32LE(this.offset);
    this.offset += 4;
    return value;
  }

  /**
   * Read unsigned 64-bit integer as BigInt (little-endian)
   */
  readUint64LE() {
    const value = this.buffer.readBigUInt64LE(this.offset);
    this.offset += 8;
    return value;
  }

  /**
   * Read signed 32-bit integer (little-endian)
   */
  readInt32LE() {
    const value = this.buffer.readInt32LE(this.offset);
    this.offset += 4;
    return value;
  }

  /**
   * Read signed 64-bit integer as BigInt (little-endian)
   */
  readInt64LE() {
    const value = this.buffer.readBigInt64LE(this.offset);
    this.offset += 8;
    return value;
  }

  /**
   * Read variable-length unsigned integer (fc::unsigned_int)
   * Uses protobuf-style varint encoding
   */
  readVarint() {
    let value = BigInt(0);
    let shift = 0;

    while (true) {
      const byte = this.readUint8();
      value |= BigInt(byte & 0x7F) << BigInt(shift);

      if (!(byte & 0x80)) break;
      shift += 7;
    }

    return value;
  }

  /**
   * Read variable-length signed integer (fc::signed_int)
   * Uses zigzag encoding + varint
   */
  readSignedVarint() {
    const n = this.readVarint();
    // Un-zigzag: (n >> 1) ^ -(n & 1)
    return Number((n >> BigInt(1)) ^ (-(n & BigInt(1))));
  }

  /**
   * Read fixed-length bytes
   */
  readBytes(length) {
    const data = this.buffer.slice(this.offset, this.offset + length);
    this.offset += length;
    return data;
  }

  /**
   * Read fc::raw serialized string
   */
  readString() {
    const length = Number(this.readVarint());
    if (length === 0) return '';
    const data = this.readBytes(length);
    return data.toString('utf8');
  }

  /**
   * Read fc::raw serialized vector of items using provided reader function
   * @param {Function} itemReader - Function to read each item
   * @param {number} [maxItems=100000] - Safety cap to prevent OOM from corrupted data
   */
  readVector(itemReader, maxItems = 100000) {
    const count = Number(this.readVarint());
    if (count > maxItems) {
      throw new Error(`readVector: count ${count} exceeds safety cap ${maxItems} (offset ${this.offset})`);
    }
    const items = [];

    for (let i = 0; i < count; i++) {
      items.push(itemReader(this));
    }

    return items;
  }

  /**
   * Read fc::raw serialized optional value
   */
  readOptional(itemReader) {
    const flag = this.readUint8();
    if (flag === 0) return null;
    return itemReader(this);
  }

  /**
   * Read flat_set of pairs
   */
  readFlatSetOfPairs(keyReader, valueReader) {
    const count = Number(this.readVarint());
    const items = [];
    for (let i = 0; i < count; i++) {
      const key = keyReader(this);
      const value = valueReader(this);
      items.push([key, value]);
    }
    return items;
  }

  /**
   * Get current position
   */
  getPosition() {
    return this.offset;
  }

  /**
   * Set position
   */
  setPosition(pos) {
    this.offset = pos;
  }

  /**
   * Get remaining bytes
   */
  getRemaining() {
    return this.buffer.length - this.offset;
  }
}

// ============================================================================
// Type Deserializers
// ============================================================================

/**
 * Read RIPEMD-160 hash (20 bytes)
 */
function readRipemd160(reader) {
  return reader.readBytes(CONSTANTS.HASH_SIZE_RIPEMD160);
}

/**
 * Read SHA-256 hash (32 bytes)
 */
function readSha256(reader) {
  return reader.readBytes(CONSTANTS.HASH_SIZE_SHA256);
}

/**
 * Read compact signature (65 bytes)
 */
function readCompactSignature(reader) {
  return reader.readBytes(CONSTANTS.SIGNATURE_SIZE);
}

/**
 * Read time_point_sec (uint32 Unix timestamp)
 */
function readTimePointSec(reader) {
  const timestamp = reader.readUint32LE();
  return new Date(timestamp * 1000);
}

/**
 * Read block_header_extension (static_variant<void_t, version, hardfork_version_vote>)
 *
 * Type 0: void_t — empty struct, no serialized data
 * Type 1: version — uint32_t v_num (major.hardfork.release packed as 8.8.16 bits)
 * Type 2: hardfork_version_vote — hardfork_version (uint32_t) + time_point_sec (uint32_t)
 */
function readBlockHeaderExtension(reader) {
  const typeIndex = Number(reader.readVarint());
  switch (typeIndex) {
    case 0: return { typeIndex, name: 'void_t', data: {} };
    case 1: {
      const vNum = reader.readUint32LE();
      const major = (vNum >> 24) & 0xFF;
      const hardfork = (vNum >> 16) & 0xFF;
      const release = vNum & 0xFFFF;
      return { typeIndex, name: 'version', data: { v_num: vNum, version: `${major}.${hardfork}.${release}` } };
    }
    case 2: {
      const vNum = reader.readUint32LE();
      const major = (vNum >> 24) & 0xFF;
      const hardfork = (vNum >> 16) & 0xFF;
      const hfTime = readTimePointSec(reader);
      return { typeIndex, name: 'hardfork_version_vote', data: { hf_version: `${major}.${hardfork}.0`, hf_time: hfTime } };
    }
    default:
      // Unknown extension type — can't skip without schema, deserialization will likely fail
      return { typeIndex, name: 'unknown_extension', data: { _warning: 'unknown extension type, stream may be corrupted' } };
  }
}

/**
 * Read block_header
 *
 * Note (Steem/VIZ lineage): block_id_type and checksum_type are fc::ripemd160 (20 bytes),
 * NOT fc::sha256 (32 bytes). This is an inherited design from the Steem codebase where
 * block IDs and merkle roots use the shorter ripemd160 hash.
 */
function readBlockHeader(reader) {
  return {
    previous: readRipemd160(reader),
    timestamp: readTimePointSec(reader),
    witness: reader.readString(),
    transaction_merkle_root: readRipemd160(reader),
    extensions: reader.readVector(readBlockHeaderExtension)
  };
}

/**
 * Read signed_block_header
 */
function readSignedBlockHeader(reader) {
  const header = readBlockHeader(reader);
  return {
    ...header,
    witness_signature: readCompactSignature(reader)
  };
}

// ============================================================================
// Common Types for Operations
// ============================================================================

/**
 * Decode Steem-style asset symbol from uint64.
 * Format (from asset.cpp comments):
 *   byte 0 : decimals (precision)
 *   bytes 1-6 : ASCII symbol name
 *   byte 7 : 0x00 (null terminator)
 */
function decodeAssetSymbol(symbol) {
  // Read as 8-byte buffer (little-endian uint64)
  const buf = Buffer.alloc(8);
  buf.writeBigUInt64LE(BigInt(symbol), 0);

  const decimals = buf.readUInt8(0);
  let name = '';
  for (let i = 1; i < 7; i++) {
    const c = buf.readUInt8(i);
    if (c === 0) break;
    name += String.fromCharCode(c);
  }
  return { decimals, name };
}

/**
 * Read asset (int64 amount + uint64 symbol)
 *
 * Asset symbol format inherited from Steem codebase:
 *   byte 0 = decimal precision, bytes 1-6 = ASCII name, byte 7 = null
 */
function readAsset(reader) {
  const amount = reader.readInt64LE();
  const symbol = reader.readUint64LE();
  const decoded = decodeAssetSymbol(symbol);
  return { amount: Number(amount), symbol: decoded.name, decimals: decoded.decimals };
}

/**
 * Base58 encode (Bitcoin alphabet).
 * Encodes a Buffer to a base58 string.
 */
const BASE58_ALPHABET = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';
function base58Encode(buf) {
  let num = BigInt('0x' + buf.toString('hex'));
  let result = '';
  while (num > 0n) {
    result = BASE58_ALPHABET[Number(num % 58n)] + result;
    num = num / 58n;
  }
  // Leading zero bytes -> leading '1' chars
  for (let i = 0; i < buf.length && buf[i] === 0; i++) {
    result = '1' + result;
  }
  return result;
}

/**
 * Convert raw 33-byte compressed public key to "VIZ..." address string.
 * Mirrors C++ public_key_type::operator std::string():
 *   1. Compute ripemd160 of the 33 raw bytes
 *   2. Take first 4 bytes of the hash as checksum
 *   3. Pack: [33 bytes key data][4 bytes checksum LE] = 37 bytes
 *   4. Base58 encode and prepend "VIZ" prefix
 */
function publicKeyToString(keyBytes) {
  const checksum = crypto.createHash('ripemd160').update(keyBytes).digest();
  const packed = Buffer.alloc(37);
  keyBytes.copy(packed, 0);           // 33 bytes key data
  checksum.copy(packed, 33, 0, 4);   // 4 bytes checksum
  return CONSTANTS.CHAIN_ADDRESS_PREFIX + base58Encode(packed);
}

/**
 * Read public_key_type (33 bytes compressed) as "VIZ..." string
 */
function readPublicKey(reader) {
  const keyBytes = reader.readBytes(33);
  return publicKeyToString(keyBytes);
}

/**
 * Read account_name_type (string)
 */
function readAccountName(reader) {
  return reader.readString();
}

/**
 * Read authority
 */
function readAuthority(reader) {
  return {
    weight_threshold: reader.readUint32LE(),
    account_auths: reader.readFlatSetOfPairs(readAccountName, (r) => r.readUint16LE()),
    key_auths: reader.readFlatSetOfPairs(readPublicKey, (r) => r.readUint16LE())
  };
}

/**
 * Read beneficiary_route_type
 */
function readBeneficiaryRoute(reader) {
  return {
    account: readAccountName(reader),
    weight: reader.readUint16LE()
  };
}

// ============================================================================
// Operation Deserializers
// ============================================================================

/**
 * Read transfer_operation (ID: 2)
 */
function readTransferOperation(reader) {
  return {
    from: readAccountName(reader),
    to: readAccountName(reader),
    amount: readAsset(reader),
    memo: reader.readString()
  };
}

/**
 * Read account_create_operation (ID: 20)
 */
function readAccountCreateOperation(reader) {
  return {
    fee: readAsset(reader),
    delegation: readAsset(reader),
    creator: readAccountName(reader),
    new_account_name: readAccountName(reader),
    master: readAuthority(reader),
    active: readAuthority(reader),
    regular: readAuthority(reader),
    memo_key: readPublicKey(reader),
    json_metadata: reader.readString(),
    referrer: readAccountName(reader),
    extensions: reader.readVector(() => reader.readVarint())
  };
}

/**
 * Read witness_update_operation (ID: 6)
 */
function readWitnessUpdateOperation(reader) {
  return {
    owner: readAccountName(reader),
    url: reader.readString(),
    block_signing_key: readPublicKey(reader)
  };
}

/**
 * Read award_operation (ID: 47)
 */
function readAwardOperation(reader) {
  return {
    initiator: readAccountName(reader),
    receiver: readAccountName(reader),
    energy: reader.readUint16LE(),
    custom_sequence: Number(reader.readUint64LE()),
    memo: reader.readString(),
    beneficiaries: reader.readVector(readBeneficiaryRoute)
  };
}

/**
 * Read transfer_to_vesting_operation (ID: 3)
 */
function readTransferToVestingOperation(reader) {
  return {
    from: readAccountName(reader),
    to: readAccountName(reader),
    amount: readAsset(reader)
  };
}

/**
 * Read delegate_vesting_shares_operation (ID: 19)
 */
function readDelegateVestingSharesOperation(reader) {
  return {
    delegator: readAccountName(reader),
    delegatee: readAccountName(reader),
    vesting_shares: readAsset(reader)
  };
}

/**
 * Read create_invite_operation (ID: 43)
 */
function readCreateInviteOperation(reader) {
  return {
    creator: readAccountName(reader),
    balance: readAsset(reader),
    invite_key: readPublicKey(reader)
  };
}

/**
 * Read custom_operation (ID: 10)
 */
function readCustomOperation(reader) {
  return {
    required_active_auths: reader.readVector(readAccountName),
    required_regular_auths: reader.readVector(readAccountName),
    id: reader.readString(),
    json: reader.readString()
  };
}

/**
 * Read author_reward_operation (ID: 26) - virtual
 */
function readAuthorRewardOperation(reader) {
  return {
    author: readAccountName(reader),
    permlink: reader.readString(),
    token_payout: readAsset(reader),
    vesting_payout: readAsset(reader)
  };
}

/**
 * Read witness_reward_operation (ID: 42) - virtual
 */
function readWitnessRewardOperation(reader) {
  return {
    witness: readAccountName(reader),
    shares: readAsset(reader)
  };
}

/**
 * Read account_update_operation (ID: 5)
 */
function readAccountUpdateOperation(reader) {
  return {
    account: readAccountName(reader),
    master: reader.readOptional(readAuthority),
    active: reader.readOptional(readAuthority),
    regular: reader.readOptional(readAuthority),
    memo_key: readPublicKey(reader),
    json_metadata: reader.readString()
  };
}

/**
 * Read withdraw_vesting_operation (ID: 4)
 */
function readWithdrawVestingOperation(reader) {
  return {
    account: readAccountName(reader),
    vesting_shares: readAsset(reader)
  };
}

// ============================================================================
// Missing operation readers — must advance reader past all fields correctly
// to prevent stream corruption. Field order from FC_REFLECT in C++ source.
// ============================================================================

/**
 * Read vote_operation (ID: 0) - deprecated but still in blocks
 * Fields: voter, author, permlink, weight
 */
function readVoteOperation(reader) {
  return {
    voter: readAccountName(reader),
    author: readAccountName(reader),
    permlink: reader.readString(),
    weight: reader.readUint16LE() | 0
  };
}

/**
 * Read content_operation (ID: 1) - deprecated
 * Fields: parent_author, parent_permlink, author, permlink, title, body, curation_percent, json_metadata, extensions
 */
function readContentOperation(reader) {
  return {
    parent_author: readAccountName(reader),
    parent_permlink: reader.readString(),
    author: readAccountName(reader),
    permlink: reader.readString(),
    title: reader.readString(),
    body: reader.readString(),
    curation_percent: reader.readUint16LE(),
    json_metadata: reader.readString(),
    extensions: reader.readVector(readContentExtension)
  };
}

/**
 * Read content_extension (static_variant for content_operation extensions)
 */
function readContentExtension(reader) {
  const typeIndex = Number(reader.readVarint());
  switch (typeIndex) {
    case 0: {
      // content_payout_beneficiaries
      const beneficiaries = reader.readVector(readBeneficiaryRoute);
      return { typeIndex, name: 'content_payout_beneficiaries', data: { beneficiaries } };
    }
    default:
      return { typeIndex, name: 'unknown_content_extension', data: {} };
  }
}

/**
 * Read account_witness_vote_operation (ID: 7)
 * Fields: account, witness, approve
 */
function readAccountWitnessVoteOperation(reader) {
  return {
    account: readAccountName(reader),
    witness: readAccountName(reader),
    approve: reader.readUint8() !== 0
  };
}

/**
 * Read account_witness_proxy_operation (ID: 8)
 * Fields: account, proxy
 */
function readAccountWitnessProxyOperation(reader) {
  return {
    account: readAccountName(reader),
    proxy: readAccountName(reader)
  };
}

/**
 * Read delete_content_operation (ID: 9) - deprecated
 * Fields: author, permlink
 */
function readDeleteContentOperation(reader) {
  return {
    author: readAccountName(reader),
    permlink: reader.readString()
  };
}

/**
 * Read set_withdraw_vesting_route_operation (ID: 11)
 * Fields: from_account, to_account, percent, auto_vest
 */
function readSetWithdrawVestingRouteOperation(reader) {
  return {
    from_account: readAccountName(reader),
    to_account: readAccountName(reader),
    percent: reader.readUint16LE(),
    auto_vest: reader.readUint8() !== 0
  };
}

/**
 * Read request_account_recovery_operation (ID: 12)
 * Fields: recovery_account, account_to_recover, new_master_authority, extensions
 */
function readRequestAccountRecoveryOperation(reader) {
  return {
    recovery_account: readAccountName(reader),
    account_to_recover: readAccountName(reader),
    new_master_authority: readAuthority(reader),
    extensions: reader.readVector(() => reader.readVarint())
  };
}

/**
 * Read recover_account_operation (ID: 13)
 * Fields: account_to_recover, new_master_authority, recent_master_authority, extensions
 */
function readRecoverAccountOperation(reader) {
  return {
    account_to_recover: readAccountName(reader),
    new_master_authority: readAuthority(reader),
    recent_master_authority: readAuthority(reader),
    extensions: reader.readVector(() => reader.readVarint())
  };
}

/**
 * Read change_recovery_account_operation (ID: 14)
 * Fields: account_to_recover, new_recovery_account, extensions
 */
function readChangeRecoveryAccountOperation(reader) {
  return {
    account_to_recover: readAccountName(reader),
    new_recovery_account: readAccountName(reader),
    extensions: reader.readVector(() => reader.readVarint())
  };
}

/**
 * Read escrow_transfer_operation (ID: 15)
 * Fields: from, to, token_amount, escrow_id, agent, fee, json_metadata, ratification_deadline, escrow_expiration
 */
function readEscrowTransferOperation(reader) {
  return {
    from: readAccountName(reader),
    to: readAccountName(reader),
    token_amount: readAsset(reader),
    escrow_id: reader.readUint32LE(),
    agent: readAccountName(reader),
    fee: readAsset(reader),
    json_metadata: reader.readString(),
    ratification_deadline: readTimePointSec(reader),
    escrow_expiration: readTimePointSec(reader)
  };
}

/**
 * Read escrow_dispute_operation (ID: 16)
 * Fields: from, to, agent, who, escrow_id
 */
function readEscrowDisputeOperation(reader) {
  return {
    from: readAccountName(reader),
    to: readAccountName(reader),
    agent: readAccountName(reader),
    who: readAccountName(reader),
    escrow_id: reader.readUint32LE()
  };
}

/**
 * Read escrow_release_operation (ID: 17)
 * Fields: from, to, agent, who, receiver, escrow_id, token_amount
 */
function readEscrowReleaseOperation(reader) {
  return {
    from: readAccountName(reader),
    to: readAccountName(reader),
    agent: readAccountName(reader),
    who: readAccountName(reader),
    receiver: readAccountName(reader),
    escrow_id: reader.readUint32LE(),
    token_amount: readAsset(reader)
  };
}

/**
 * Read escrow_approve_operation (ID: 18)
 * Fields: from, to, agent, who, escrow_id, approve
 */
function readEscrowApproveOperation(reader) {
  return {
    from: readAccountName(reader),
    to: readAccountName(reader),
    agent: readAccountName(reader),
    who: readAccountName(reader),
    escrow_id: reader.readUint32LE(),
    approve: reader.readUint8() !== 0
  };
}

/**
 * Read account_metadata_operation (ID: 21)
 * Fields: account, json_metadata
 */
function readAccountMetadataOperation(reader) {
  return {
    account: readAccountName(reader),
    json_metadata: reader.readString()
  };
}

/**
 * Read proposal_create_operation (ID: 22)
 * Fields: author, title, memo, expiration_time, proposed_operations, review_period_time, extensions
 */
function readProposalCreateOperation(reader) {
  return {
    author: readAccountName(reader),
    title: reader.readString(),
    memo: reader.readString(),
    expiration_time: readTimePointSec(reader),
    proposed_operations: reader.readVector(readOperationWrapper),
    review_period_time: reader.readOptional(readTimePointSec),
    extensions: reader.readVector(() => reader.readVarint())
  };
}

/**
 * Read operation_wrapper (just wraps an operation)
 */
function readOperationWrapper(reader) {
  return { op: readOperation(reader) };
}

/**
 * Read proposal_update_operation (ID: 23)
 * Fields: author, title, active_approvals_to_add, active_approvals_to_remove,
 *         master_approvals_to_add, master_approvals_to_remove,
 *         regular_approvals_to_add, regular_approvals_to_remove,
 *         key_approvals_to_add, key_approvals_to_remove, extensions
 */
function readProposalUpdateOperation(reader) {
  return {
    author: readAccountName(reader),
    title: reader.readString(),
    active_approvals_to_add: reader.readVector(readAccountName),
    active_approvals_to_remove: reader.readVector(readAccountName),
    master_approvals_to_add: reader.readVector(readAccountName),
    master_approvals_to_remove: reader.readVector(readAccountName),
    regular_approvals_to_add: reader.readVector(readAccountName),
    regular_approvals_to_remove: reader.readVector(readAccountName),
    key_approvals_to_add: reader.readVector(readPublicKey),
    key_approvals_to_remove: reader.readVector(readPublicKey),
    extensions: reader.readVector(() => reader.readVarint())
  };
}

/**
 * Read proposal_delete_operation (ID: 24)
 * FC_REFLECT: (author)(title)(requester)(extensions)
 */
function readProposalDeleteOperation(reader) {
  return {
    author: readAccountName(reader),
    title: reader.readString(),
    requester: readAccountName(reader),
    extensions: reader.readVector(() => reader.readVarint())
  };
}

/**
 * Read chain_properties_update_operation (ID: 25)
 * Fields: owner, props
 */
function readChainPropertiesUpdateOperation(reader) {
  return {
    owner: readAccountName(reader),
    props: readChainPropertiesInit(reader)
  };
}

/**
 * Read chain_properties_init (used by chain_properties_update_operation)
 * FC_REFLECT: (account_creation_fee)(maximum_block_size)(create_account_delegation_ratio)
 *   (create_account_delegation_time)(min_delegation)(min_curation_percent)(max_curation_percent)
 *   (bandwidth_reserve_percent)(bandwidth_reserve_below)(flag_energy_additional_cost)
 *   (vote_accounting_min_rshares)(committee_request_approve_min_percent)
 */
function readChainPropertiesInit(reader) {
  return {
    account_creation_fee: readAsset(reader),
    maximum_block_size: reader.readUint32LE(),
    create_account_delegation_ratio: reader.readUint32LE(),
    create_account_delegation_time: reader.readUint32LE(),
    min_delegation: readAsset(reader),
    min_curation_percent: reader.readUint16LE() | 0,
    max_curation_percent: reader.readUint16LE() | 0,
    bandwidth_reserve_percent: reader.readUint16LE() | 0,
    bandwidth_reserve_below: readAsset(reader),
    flag_energy_additional_cost: reader.readUint16LE() | 0,
    vote_accounting_min_rshares: reader.readUint32LE(),
    committee_request_approve_min_percent: reader.readUint16LE() | 0
  };
}

/**
 * Read versioned_chain_properties (static_variant of chain_properties_init/hf4/hf6/hf9)
 * Used by versioned_chain_properties_update_operation.
 * FC_REFLECT_DERIVED means each variant includes all base fields + its own.
 */
function readVersionedChainProperties(reader) {
  const typeIndex = Number(reader.readVarint());
  const init = readChainPropertiesInit(reader);

  if (typeIndex === 0) {
    // chain_properties_init — no additional fields
    return { _type: 'chain_properties_init', ...init };
  }

  // chain_properties_hf4 = init + 3 fields
  init.inflation_witness_percent = reader.readUint16LE() | 0;
  init.inflation_ratio_committee_vs_reward_fund = reader.readUint16LE() | 0;
  init.inflation_recalc_period = reader.readUint32LE();

  if (typeIndex === 1) {
    return { _type: 'chain_properties_hf4', ...init };
  }

  // chain_properties_hf6 = hf4 + 3 fields
  init.data_operations_cost_additional_bandwidth = reader.readUint32LE();
  init.witness_miss_penalty_percent = reader.readUint16LE() | 0;
  init.witness_miss_penalty_duration = reader.readUint32LE();

  if (typeIndex === 2) {
    return { _type: 'chain_properties_hf6', ...init };
  }

  // chain_properties_hf9 = hf6 + 7 fields
  init.create_invite_min_balance = readAsset(reader);
  init.committee_create_request_fee = readAsset(reader);
  init.create_paid_subscription_fee = readAsset(reader);
  init.account_on_sale_fee = readAsset(reader);
  init.subaccount_on_sale_fee = readAsset(reader);
  init.witness_declaration_fee = readAsset(reader);
  init.withdraw_intervals = reader.readUint16LE();

  return { _type: 'chain_properties_hf9', ...init };
}

// ---- Virtual operations (ID: 27-34, 38-41, 48-49, 52-53, 57, 59, 62-63) ----

/**
 * Read curation_reward_operation (ID: 27) - virtual
 * Fields: curator, reward, content_author, content_permlink
 */
function readCurationRewardOperation(reader) {
  return {
    curator: readAccountName(reader),
    reward: readAsset(reader),
    content_author: readAccountName(reader),
    content_permlink: reader.readString()
  };
}

/**
 * Read content_reward_operation (ID: 28) - virtual
 * Fields: author, permlink, payout
 */
function readContentRewardOperation(reader) {
  return {
    author: readAccountName(reader),
    permlink: reader.readString(),
    payout: readAsset(reader)
  };
}

/**
 * Read fill_vesting_withdraw_operation (ID: 29) - virtual
 * Fields: from_account, to_account, withdrawn, deposited
 */
function readFillVestingWithdrawOperation(reader) {
  return {
    from_account: readAccountName(reader),
    to_account: readAccountName(reader),
    withdrawn: readAsset(reader),
    deposited: readAsset(reader)
  };
}

/**
 * Read shutdown_witness_operation (ID: 30) - virtual
 * Fields: owner
 */
function readShutdownWitnessOperation(reader) {
  return {
    owner: readAccountName(reader)
  };
}

/**
 * Read hardfork_operation (ID: 31) - virtual
 * Fields: hardfork_id
 */
function readHardforkOperation(reader) {
  return {
    hardfork_id: reader.readUint32LE()
  };
}

/**
 * Read content_payout_update_operation (ID: 32) - virtual
 * Fields: author, permlink
 */
function readContentPayoutUpdateOperation(reader) {
  return {
    author: readAccountName(reader),
    permlink: reader.readString()
  };
}

/**
 * Read content_benefactor_reward_operation (ID: 33) - virtual
 * Fields: benefactor, author, permlink, reward
 */
function readContentBenefactorRewardOperation(reader) {
  return {
    benefactor: readAccountName(reader),
    author: readAccountName(reader),
    permlink: reader.readString(),
    reward: readAsset(reader)
  };
}

/**
 * Read return_vesting_delegation_operation (ID: 34) - virtual
 * Fields: account, vesting_shares
 */
function readReturnVestingDelegationOperation(reader) {
  return {
    account: readAccountName(reader),
    vesting_shares: readAsset(reader)
  };
}

/**
 * Read committee_worker_create_request_operation (ID: 35)
 * Fields: creator, url, worker, required_amount_min, required_amount_max, duration
 */
function readCommitteeWorkerCreateRequestOperation(reader) {
  return {
    creator: readAccountName(reader),
    url: reader.readString(),
    worker: readAccountName(reader),
    required_amount_min: readAsset(reader),
    required_amount_max: readAsset(reader),
    duration: reader.readUint32LE()
  };
}

/**
 * Read committee_worker_cancel_request_operation (ID: 36)
 * Fields: creator, request_id
 */
function readCommitteeWorkerCancelRequestOperation(reader) {
  return {
    creator: readAccountName(reader),
    request_id: reader.readUint32LE()
  };
}

/**
 * Read committee_vote_request_operation (ID: 37)
 * Fields: voter, request_id, vote_percent
 */
function readCommitteeVoteRequestOperation(reader) {
  return {
    voter: readAccountName(reader),
    request_id: reader.readUint32LE(),
    vote_percent: reader.readUint16LE() | 0
  };
}

/**
 * Read committee_cancel_request_operation (ID: 38) - virtual
 * Fields: request_id
 */
function readCommitteeCancelRequestOperation(reader) {
  return {
    request_id: reader.readUint32LE()
  };
}

/**
 * Read committee_approve_request_operation (ID: 39) - virtual
 * Fields: request_id
 */
function readCommitteeApproveRequestOperation(reader) {
  return {
    request_id: reader.readUint32LE()
  };
}

/**
 * Read committee_payout_request_operation (ID: 40) - virtual
 * Fields: request_id
 */
function readCommitteePayoutRequestOperation(reader) {
  return {
    request_id: reader.readUint32LE()
  };
}

/**
 * Read committee_pay_request_operation (ID: 41) - virtual
 * Fields: worker, request_id, tokens
 */
function readCommitteePayRequestOperation(reader) {
  return {
    worker: readAccountName(reader),
    request_id: reader.readUint32LE(),
    tokens: readAsset(reader)
  };
}

/**
 * Read claim_invite_balance_operation (ID: 44)
 * Fields: initiator, receiver, invite_secret
 */
function readClaimInviteBalanceOperation(reader) {
  return {
    initiator: readAccountName(reader),
    receiver: readAccountName(reader),
    invite_secret: reader.readString() // WIF-encoded private key string
  };
}

/**
 * Read invite_registration_operation (ID: 45)
 * Fields: initiator, new_account_name, invite_secret, new_account_key
 */
function readInviteRegistrationOperation(reader) {
  return {
    initiator: readAccountName(reader),
    new_account_name: readAccountName(reader),
    invite_secret: reader.readString(), // WIF-encoded private key string
    new_account_key: readPublicKey(reader)
  };
}

/**
 * Read versioned_chain_properties_update_operation (ID: 46)
 * Fields: owner, props
 */
function readVersionedChainPropertiesUpdateOperation(reader) {
  return {
    owner: readAccountName(reader),
    props: readVersionedChainProperties(reader)
  };
}

/**
 * Read receive_award_operation (ID: 48) - virtual
 * Fields: initiator, receiver, custom_sequence, memo, shares
 */
function readReceiveAwardOperation(reader) {
  return {
    initiator: readAccountName(reader),
    receiver: readAccountName(reader),
    custom_sequence: Number(reader.readUint64LE()),
    memo: reader.readString(),
    shares: readAsset(reader)
  };
}

/**
 * Read benefactor_award_operation (ID: 49) - virtual
 * Fields: same as award but for benefactor
 */
function readBenefactorAwardOperation(reader) {
  return {
    initiator: readAccountName(reader),
    benefactor: readAccountName(reader),
    receiver: readAccountName(reader),
    custom_sequence: Number(reader.readUint64LE()),
    memo: reader.readString(),
    shares: readAsset(reader)
  };
}

/**
 * Read set_paid_subscription_operation (ID: 50)
 * Fields: account, url, levels, amount, period
 */
function readSetPaidSubscriptionOperation(reader) {
  return {
    account: readAccountName(reader),
    url: reader.readString(),
    levels: reader.readUint16LE(),
    amount: readAsset(reader),
    period: reader.readUint16LE()
  };
}

/**
 * Read paid_subscribe_operation (ID: 51)
 * Fields: subscriber, account, level, amount, period, auto_renewal
 */
function readPaidSubscribeOperation(reader) {
  return {
    subscriber: readAccountName(reader),
    account: readAccountName(reader),
    level: reader.readUint16LE(),
    amount: readAsset(reader),
    period: reader.readUint16LE(),
    auto_renewal: reader.readUint8() !== 0
  };
}

/**
 * Read paid_subscription_action_operation (ID: 52) - virtual
 * FC_REFLECT: (subscriber)(account)(level)(amount)(period)(summary_duration_sec)(summary_amount)
 */
function readPaidSubscriptionActionOperation(reader) {
  return {
    subscriber: readAccountName(reader),
    account: readAccountName(reader),
    level: reader.readUint16LE(),
    amount: readAsset(reader),
    period: reader.readUint16LE(),
    summary_duration_sec: Number(reader.readUint64LE()),
    summary_amount: readAsset(reader)
  };
}

/**
 * Read cancel_paid_subscription_operation (ID: 53) - virtual
 * FC_REFLECT: (subscriber)(account)
 */
function readCancelPaidSubscriptionOperation(reader) {
  return {
    subscriber: readAccountName(reader),
    account: readAccountName(reader)
  };
}

/**
 * Read set_account_price_operation (ID: 54)
 * Fields: account, account_seller, account_offer_price, account_on_sale
 */
function readSetAccountPriceOperation(reader) {
  return {
    account: readAccountName(reader),
    account_seller: readAccountName(reader),
    account_offer_price: readAsset(reader),
    account_on_sale: reader.readUint8() !== 0
  };
}

/**
 * Read set_subaccount_price_operation (ID: 55)
 * Fields: account, subaccount_seller, subaccount_offer_price, subaccount_on_sale
 */
function readSetSubaccountPriceOperation(reader) {
  return {
    account: readAccountName(reader),
    subaccount_seller: readAccountName(reader),
    subaccount_offer_price: readAsset(reader),
    subaccount_on_sale: reader.readUint8() !== 0
  };
}

/**
 * Read buy_account_operation (ID: 56)
 * Fields: buyer, account, account_offer_price, account_authorities_key, tokens_to_shares
 */
function readBuyAccountOperation(reader) {
  return {
    buyer: readAccountName(reader),
    account: readAccountName(reader),
    account_offer_price: readAsset(reader),
    account_authorities_key: readPublicKey(reader),
    tokens_to_shares: readAsset(reader)
  };
}

/**
 * Read account_sale_operation (ID: 57) - virtual
 */
function readAccountSaleOperation(reader) {
  return {
    account: readAccountName(reader),
    price: readAsset(reader),
    buyer: readAccountName(reader),
    seller: readAccountName(reader)
  };
}

/**
 * Read use_invite_balance_operation (ID: 58)
 * Fields: initiator, receiver, invite_secret
 */
function readUseInviteBalanceOperation(reader) {
  return {
    initiator: readAccountName(reader),
    receiver: readAccountName(reader),
    invite_secret: reader.readString() // WIF-encoded private key string
  };
}

/**
 * Read expire_escrow_ratification_operation (ID: 59) - virtual
 */
function readExpireEscrowRatificationOperation(reader) {
  return {
    from: readAccountName(reader),
    to: readAccountName(reader),
    agent: readAccountName(reader),
    escrow_id: reader.readUint32LE(),
    token_amount: readAsset(reader),
    fee: readAsset(reader),
    ratification_deadline: readTimePointSec(reader)
  };
}

/**
 * Read fixed_award_operation (ID: 60)
 * Fields: initiator, receiver, reward_amount, max_energy, custom_sequence, memo, beneficiaries
 */
function readFixedAwardOperation(reader) {
  return {
    initiator: readAccountName(reader),
    receiver: readAccountName(reader),
    reward_amount: readAsset(reader),
    max_energy: reader.readUint16LE(),
    custom_sequence: Number(reader.readUint64LE()),
    memo: reader.readString(),
    beneficiaries: reader.readVector(readBeneficiaryRoute)
  };
}

/**
 * Read target_account_sale_operation (ID: 61)
 * Fields: account, account_seller, target_buyer, account_offer_price, account_on_sale
 */
function readTargetAccountSaleOperation(reader) {
  return {
    account: readAccountName(reader),
    account_seller: readAccountName(reader),
    target_buyer: readAccountName(reader),
    account_offer_price: readAsset(reader),
    account_on_sale: reader.readUint8() !== 0
  };
}

/**
 * Read bid_operation (ID: 62) - virtual
 */
function readBidOperation(reader) {
  return {
    account: readAccountName(reader),
    bidder: readAccountName(reader),
    bid: readAsset(reader)
  };
}

/**
 * Read outbid_operation (ID: 63) - virtual
 */
function readOutbidOperation(reader) {
  return {
    account: readAccountName(reader),
    bidder: readAccountName(reader),
    bid: readAsset(reader)
  };
}

// Operation deserializer registry
const OPERATION_READERS = {
  0: readVoteOperation,
  1: readContentOperation,
  2: readTransferOperation,
  3: readTransferToVestingOperation,
  4: readWithdrawVestingOperation,
  5: readAccountUpdateOperation,
  6: readWitnessUpdateOperation,
  7: readAccountWitnessVoteOperation,
  8: readAccountWitnessProxyOperation,
  9: readDeleteContentOperation,
  10: readCustomOperation,
  11: readSetWithdrawVestingRouteOperation,
  12: readRequestAccountRecoveryOperation,
  13: readRecoverAccountOperation,
  14: readChangeRecoveryAccountOperation,
  15: readEscrowTransferOperation,
  16: readEscrowDisputeOperation,
  17: readEscrowReleaseOperation,
  18: readEscrowApproveOperation,
  19: readDelegateVestingSharesOperation,
  20: readAccountCreateOperation,
  21: readAccountMetadataOperation,
  22: readProposalCreateOperation,
  23: readProposalUpdateOperation,
  24: readProposalDeleteOperation,
  25: readChainPropertiesUpdateOperation,
  26: readAuthorRewardOperation,
  27: readCurationRewardOperation,
  28: readContentRewardOperation,
  29: readFillVestingWithdrawOperation,
  30: readShutdownWitnessOperation,
  31: readHardforkOperation,
  32: readContentPayoutUpdateOperation,
  33: readContentBenefactorRewardOperation,
  34: readReturnVestingDelegationOperation,
  35: readCommitteeWorkerCreateRequestOperation,
  36: readCommitteeWorkerCancelRequestOperation,
  37: readCommitteeVoteRequestOperation,
  38: readCommitteeCancelRequestOperation,
  39: readCommitteeApproveRequestOperation,
  40: readCommitteePayoutRequestOperation,
  41: readCommitteePayRequestOperation,
  42: readWitnessRewardOperation,
  43: readCreateInviteOperation,
  44: readClaimInviteBalanceOperation,
  45: readInviteRegistrationOperation,
  46: readVersionedChainPropertiesUpdateOperation,
  47: readAwardOperation,
  48: readReceiveAwardOperation,
  49: readBenefactorAwardOperation,
  50: readSetPaidSubscriptionOperation,
  51: readPaidSubscribeOperation,
  52: readPaidSubscriptionActionOperation,
  53: readCancelPaidSubscriptionOperation,
  54: readSetAccountPriceOperation,
  55: readSetSubaccountPriceOperation,
  56: readBuyAccountOperation,
  57: readAccountSaleOperation,
  58: readUseInviteBalanceOperation,
  59: readExpireEscrowRatificationOperation,
  60: readFixedAwardOperation,
  61: readTargetAccountSaleOperation,
  62: readBidOperation,
  63: readOutbidOperation
};

/**
 * Read operation (static_variant)
 * Fully deserializes known operations, returns raw data for unknown ones
 */
function readOperation(reader) {
  const typeIndex = Number(reader.readVarint());
  if (typeIndex > 1000) {
    throw new Error(`readOperation: typeIndex ${typeIndex} exceeds safety cap (offset ${reader.offset})`);
  }
  const typeInfo = OPERATION_TYPES[typeIndex] || { name: 'unknown_operation', isVirtual: false };
  const opReader = OPERATION_READERS[typeIndex];

  let data;
  const opStartOffset = reader.offset;
  if (opReader) {
    try {
      data = opReader(reader);
    } catch (e) {
      const bytesRead = reader.offset - opStartOffset;
      console.error(`  readOperation ERROR: type=${typeIndex}(${typeInfo.name}) offset=${opStartOffset} bytesRead=${bytesRead} err=${e.message}`);
      throw e;
    }
  } else {
    // Unknown operation - can't skip without schema, deserialization will likely fail
    console.error(`  readOperation: unknown type ${typeIndex} at offset ${opStartOffset}, stream will be corrupted`);
    data = { _raw: `unknown operation type ${typeIndex}` };
  }

  // Log per-operation byte consumption when verbose debug is enabled
  if (global.__BLR_VERBOSE_OPS__) {
    const bytesRead = reader.offset - opStartOffset;
    process.stderr.write(`  op[${typeIndex}]=${typeInfo.name} @${opStartOffset} +${bytesRead}B\n`);
  }

  return {
    typeId: typeIndex,
    typeName: typeInfo.name,
    isVirtual: typeInfo.isVirtual,
    data
  };
}

/**
 * Read transaction
 */
function readTransaction(reader) {
  return {
    ref_block_num: reader.readUint16LE(),
    ref_block_prefix: reader.readUint32LE(),
    expiration: readTimePointSec(reader),
    operations: reader.readVector(readOperation),
    extensions: reader.readVector(() => reader.readVarint()) // extensions_type is empty vector
  };
}

/**
 * Read signed_transaction
 */
function readSignedTransaction(reader) {
  const trx = readTransaction(reader);
  return {
    ...trx,
    signatures: reader.readVector(readCompactSignature)
  };
}

/**
 * Read signed_block
 */
function readSignedBlock(reader) {
  const startOffset = reader.offset;
  const header = readSignedBlockHeader(reader);
  const headerEndOffset = reader.offset;
  let transactions;
  try {
    const txCount = Number(reader.readVarint());
    if (txCount > 100000) {
      throw new Error(`readSignedBlock: tx count ${txCount} exceeds safety cap (offset ${reader.offset})`);
    }
    const items = [];
    for (let i = 0; i < txCount; i++) {
      const txStartOffset = reader.offset;
      try {
        items.push(readSignedTransaction(reader));
      } catch (e) {
        console.error(`  readSignedBlock: tx[${i}] FAILED at offset ${txStartOffset} (headerEnd=${headerEndOffset}) err=${e.message}`);
        throw e;
      }
    }
    transactions = items;
  } catch (e) {
    console.error(`  readSignedBlock ERROR at offset ${startOffset}: headerEnd=${headerEndOffset} err=${e.message}`);
    throw e;
  }
  return {
    ...header,
    transactions
  };
}

// ============================================================================
// Block Number Utilities
// ============================================================================

/**
 * Extract block number from block_id_type (ripemd160 hash)
 * The block number is stored in the first 4 bytes.
 * C++ uses fc::endian_reverse_u32 which reads as big-endian uint32.
 */
function numFromId(blockId) {
  if (!blockId || blockId.length < 4) return 0;
  return blockId.readUInt32BE(0);
}

/**
 * Get block number from signed_block
 */
function getBlockNum(block) {
  return numFromId(block.previous) + 1;
}

/**
 * Convert block ID to hex string
 */
function blockIdToHex(blockId) {
  return blockId.toString('hex');
}

// ============================================================================
// Block Log Reader
// ============================================================================

/**
 * Block Log Reader for standard block_log files
 */
class BlockLogReader {
  constructor() {
    this.dataFd = null;
    this.indexFd = null;
    this.dataPath = null;
    this.indexPath = null;
    this.dataSize = 0n;
    this.indexSize = 0n;
    this._headBlock = null;
    this._headBlockNum = 0;
  }

  /**
   * Open block log files
   * @param {string} dataPath - Path to block_log file
   * @param {string} [indexPath] - Path to block_log.index file (auto-derived if not provided)
   */
  open(dataPath, indexPath) {
    this.dataPath = dataPath;
    this.indexPath = indexPath || (dataPath + '.index');

    // Open data file
    this.dataFd = fs.openSync(this.dataPath, 'r');
    const dataStats = fs.fstatSync(this.dataFd);
    this.dataSize = BigInt(dataStats.size);

    // Open index file
    this.indexFd = fs.openSync(this.indexPath, 'r');
    const indexStats = fs.fstatSync(this.indexFd);
    this.indexSize = BigInt(indexStats.size);

    // Determine head block number from the index file size.
    // Index has 8 bytes per block, starting from block 1 at offset 0.
    // head_block_num = index_size / 8
    // This is reliable — we don't need to deserialize the head block for this.
    if (this.indexSize >= 8n) {
      this._headBlockNum = Number(this.indexSize / 8n);
    }

    // Optionally cache the head block (may fail for blocks with unknown ops)
    if (this.dataSize > BigInt(CONSTANTS.MIN_VALID_FILE_SIZE)) {
      try {
        this._headBlock = this.readHead();
      } catch (e) {
        this._headBlock = null;
      }
    }
  }

  /**
   * Close block log files
   */
  close() {
    if (this.dataFd !== null) {
      fs.closeSync(this.dataFd);
      this.dataFd = null;
    }
    if (this.indexFd !== null) {
      fs.closeSync(this.indexFd);
      this.indexFd = null;
    }
    this._headBlock = null;
    this._headBlockNum = 0;
  }

  /**
   * Check if log is open
   */
  isOpen() {
    return this.dataFd !== null;
  }

  /**
   * Read bytes from data file
   */
  _readData(position, length) {
    const buffer = Buffer.allocUnsafe(Number(length));
    fs.readSync(this.dataFd, buffer, 0, Number(length), Number(position));
    return buffer;
  }

  /**
   * Read bytes from index file
   */
  _readIndex(position, length) {
    const buffer = Buffer.allocUnsafe(Number(length));
    fs.readSync(this.indexFd, buffer, 0, Number(length), Number(position));
    return buffer;
  }

  /**
   * Read uint64 from data file
   */
  _readDataUint64(position) {
    const buffer = this._readData(position, 8);
    return buffer.readBigUInt64LE(0);
  }

  /**
   * Read uint64 from index file
   */
  _readIndexUint64(position) {
    const buffer = this._readIndex(position, 8);
    return buffer.readBigUInt64LE(0);
  }

  /**
   * Get head block position (last 8 bytes of data file)
   */
  _getHeadPosition() {
    return this._readDataUint64(this.dataSize - 8n);
  }

  /**
   * Read block at specified position
   * @param {bigint} position - File offset
   * @returns {{ block: object, nextPosition: bigint }}
   */
  readBlock(position) {
    const maxBlockSize = CONSTANTS.CHAIN_BLOCK_SIZE;
    const availableSize = Number(this.dataSize - position);
    const readSize = Math.min(availableSize, maxBlockSize + 8);

    const buffer = this._readData(position, readSize);
    const reader = new BinaryReader(buffer);

    const block = readSignedBlock(reader);
    const blockEndPos = reader.getPosition();

    // Read position marker
    const posMarker = buffer.readBigUInt64LE(blockEndPos);

    // Verify position marker matches our read position
    if (posMarker !== position) {
      throw new Error(`Position mismatch: expected ${position}, got ${posMarker}`);
    }

    const nextPosition = blockEndPos + 8;

    return { block, nextPosition };
  }

  /**
   * Read head block
   */
  readHead() {
    if (this._headBlock) return this._headBlock;

    const pos = this._getHeadPosition();
    const result = this.readBlock(pos);
    return result.block;
  }

  /**
   * Get block position from index by block number
   * Matches C++ block_log: offset = 8 * (block_num - 1)
   * @param {number} blockNum - Block number (1-indexed, matching C++)
   * @returns {bigint} Position in data file, or NPOS if out of range
   */
  getBlockPos(blockNum) {
    if (blockNum < 1 || blockNum > this._headBlockNum) {
      return CONSTANTS.NPOS;
    }

    const indexOffset = BigInt(blockNum - 1) * 8n;
    // Bounds check: index may be incomplete (fewer entries than head block)
    if (indexOffset + 8n > this.indexSize) {
      return CONSTANTS.NPOS;
    }
    return this._readIndexUint64(indexOffset);
  }

  /**
   * Read block by number
   * @param {number} blockNum - Block number
   * @returns {object|null} Signed block, or null if block_num out of range
   * @throws Error if block found but deserialization failed
   */
  readBlockByNum(blockNum) {
    const pos = this.getBlockPos(blockNum);
    if (pos === CONSTANTS.NPOS) return null;

    const result = this.readBlock(pos);
    return result.block;
  }

  /**
   * Read only the block header by number (no transactions/operations).
   * Useful when full block deserialization fails due to unknown operation types.
   * @param {number} blockNum - Block number
   * @returns {object|null} Block header object, or null if out of range
   */
  readBlockHeaderByNum(blockNum) {
    const pos = this.getBlockPos(blockNum);
    if (pos === CONSTANTS.NPOS) return null;

    try {
      const maxBlockSize = CONSTANTS.CHAIN_BLOCK_SIZE;
      const availableSize = Number(this.dataSize - pos);
      const readSize = Math.min(availableSize, maxBlockSize + 8);

      const buffer = this._readData(pos, readSize);
      const br = new BinaryReader(buffer);

      // Only read the signed_block_header part
      const header = readSignedBlockHeader(br);
      return {
        ...header,
        _blockNum: getBlockNum(header),
        _position: Number(pos),
        _headerOnly: true
      };
    } catch (e) {
      return null;
    }
  }

  /**
   * Read raw block bytes by number.
   * Uses the index to find block start offset and the next block's offset to determine size.
   * Returns the raw data including the trailing 8-byte position marker.
   * @param {number} blockNum - Block number
   * @returns {Buffer|null} Raw block bytes, or null if out of range
   */
  readBlockRawData(blockNum) {
    const startPos = this.getBlockPos(blockNum);
    if (startPos === CONSTANTS.NPOS) return null;

    let endPos;
    // If there's a next block in the index, its offset is our end
    const nextPos = this.getBlockPos(blockNum + 1);
    if (nextPos !== CONSTANTS.NPOS) {
      endPos = Number(nextPos);
    } else {
      // Last block: read until end of data file minus the final 8-byte head position pointer
      endPos = Number(this.dataSize) - 8;
    }

    const size = endPos - Number(startPos);
    if (size <= 0 || size > CONSTANTS.CHAIN_BLOCK_SIZE + 16) return null;

    return this._readData(startPos, size);
  }

  /**
   * Read only the transaction count for a block by number.
   * Uses exact block size from index (no 1MB over-read) and skips operation deserialization.
   * Virtual operations are NOT in block_log transactions, so tx_count > 0 means the block
   * has non-free operations.
   * @param {number} blockNum - Block number
   * @returns {number} Transaction count, or -1 if out of range / error
   */
  readBlockTxCountByNum(blockNum) {
    const startPos = this.getBlockPos(blockNum);
    if (startPos === CONSTANTS.NPOS) return -1;

    // Calculate exact block size from index
    let endPos;
    const nextPos = this.getBlockPos(blockNum + 1);
    if (nextPos !== CONSTANTS.NPOS) {
      endPos = Number(nextPos);
    } else {
      endPos = Number(this.dataSize) - 8;
    }
    const size = endPos - Number(startPos);
    if (size <= 0 || size > CONSTANTS.CHAIN_BLOCK_SIZE + 16) return -1;

    try {
      const buffer = this._readData(startPos, size);
      const br = new BinaryReader(buffer);

      // Skip past the signed_block_header (we don't need its data)
      readSignedBlockHeader(br);

      // The next varint is the transactions vector length
      return Number(br.readVarint());
    } catch (e) {
      return -1;
    }
  }

  /**
   * Read a batch of block positions from the index file in one I/O operation.
   * Much faster than calling getBlockPos() one at a time for sequential scanning.
   * @param {number} startBlockNum - First block number
   * @param {number} count - Number of positions to read
   * @returns {bigint[]} Array of positions, may be shorter than count if near end
   */
  readBlockPosBatch(startBlockNum, count) {
    if (startBlockNum < 1 || startBlockNum > this._headBlockNum) return [];

    const actualCount = Math.min(count, this._headBlockNum - startBlockNum + 2); // +1 for next-block boundary
    const indexOffset = BigInt(startBlockNum - 1) * 8n;
    const readBytes = Number(BigInt(actualCount) * 8n);

    if (indexOffset + BigInt(readBytes) > this.indexSize) {
      // Trim to available index
      const avail = Number((this.indexSize - indexOffset) / 8n);
      if (avail <= 0) return [];
      return this.readBlockPosBatch(startBlockNum, avail);
    }

    const buf = Buffer.alloc(readBytes);
    fs.readSync(this.indexFd, buf, 0, readBytes, Number(indexOffset));

    const positions = [];
    for (let i = 0; i < actualCount; i++) {
      positions.push(buf.readBigUInt64LE(i * 8));
    }
    return positions;
  }

  /**
   * Get head block number
   */
  getHeadBlockNum() {
    return this._headBlockNum;
  }

  /**
   * Get start block number (always 1 for standard block_log, matching C++)
   */
  getStartBlockNum() {
    return 1;
  }

  /**
   * Get total number of blocks (based on index entries)
   */
  getNumBlocks() {
    return Number(this.indexSize / 8n);
  }

  /**
   * Iterate all blocks sequentially
   * @yields {object} Signed block
   */
  *iterateBlocks() {
    let pos = 0n;

    while (pos < this.dataSize - 8n) {
      const result = this.readBlock(pos);
      yield result.block;
      pos = BigInt(result.nextPosition);
    }
  }

  /**
   * Validate index file against data file
   */
  validateIndex() {
    const dataHeadPos = this._getHeadPosition();
    const indexHeadPos = this._readIndexUint64(this.indexSize - 8n);

    return dataHeadPos === indexHeadPos;
  }
}

// ============================================================================
// DLT Block Log Reader
// ============================================================================

/**
 * Block Log Reader for DLT (rolling) block log files
 */
class DltBlockLogReader extends BlockLogReader {
  constructor() {
    super();
    this._startBlockNum = 0;
  }

  /**
   * Open DLT block log files
   */
  open(dataPath, indexPath) {
    super.open(dataPath, indexPath);

    // Read start_block_num from index header
    if (this.indexSize >= BigInt(CONSTANTS.INDEX_HEADER_SIZE)) {
      this._startBlockNum = Number(this._readIndexUint64(0n));
    }
  }

  /**
   * Get block position from index by block number
   * @param {number} blockNum - Block number
   * @returns {bigint} Position in data file
   */
  getBlockPos(blockNum) {
    if (this._startBlockNum === 0) return CONSTANTS.NPOS;

    if (blockNum < this._startBlockNum || blockNum > this._headBlockNum) {
      return CONSTANTS.NPOS;
    }

    const indexOffset = BigInt(CONSTANTS.INDEX_HEADER_SIZE) + BigInt(blockNum - this._startBlockNum) * 8n;
    return this._readIndexUint64(indexOffset);
  }

  /**
   * Get start block number
   */
  getStartBlockNum() {
    return this._startBlockNum;
  }

  /**
   * Get total number of blocks
   */
  getNumBlocks() {
    if (!this._headBlock || this._startBlockNum === 0) return 0;
    return this._headBlockNum - this._startBlockNum + 1;
  }

  /**
   * Validate index file
   */
  validateIndex() {
    const dataHeadPos = this._getHeadPosition();
    const indexHeadPos = this._readIndexUint64(this.indexSize - 8n);

    return dataHeadPos === indexHeadPos;
  }
}

// ============================================================================
// Factory Function
// ============================================================================

/**
 * Create appropriate block log reader based on file type
 * @param {string} dataPath - Path to block log data file
 * @param {string} [indexPath] - Path to index file (auto-derived if not provided)
 * @param {boolean} [isDlt=false] - True for DLT block log
 * @returns {BlockLogReader|DltBlockLogReader}
 */
function createBlockLogReader(dataPath, indexPath, isDlt = false) {
  const reader = isDlt ? new DltBlockLogReader() : new BlockLogReader();
  reader.open(dataPath, indexPath);
  return reader;
}

// ============================================================================
// Exports
// ============================================================================

module.exports = {
  // Constants
  CONSTANTS,
  OPERATION_TYPES,

  // Classes
  BinaryReader,
  BlockLogReader,
  DltBlockLogReader,

  // Factory
  createBlockLogReader,

  // Utilities
  numFromId,
  getBlockNum,
  blockIdToHex,

  // Deserializers
  readRipemd160,
  readSha256,
  readCompactSignature,
  readTimePointSec,
  readBlockHeader,
  readSignedBlockHeader,
  readTransaction,
  readSignedTransaction,
  readSignedBlock,

  // Operation deserializers
  readAsset,
  readPublicKey,
  publicKeyToString,
  base58Encode,
  readAccountName,
  readAuthority,
  readBeneficiaryRoute,
  readOperation,
  readTransferOperation,
  readAccountCreateOperation,
  readWitnessUpdateOperation,
  readAwardOperation,
  readTransferToVestingOperation,
  readDelegateVestingSharesOperation,
  readCreateInviteOperation,
  readCustomOperation,
  readAuthorRewardOperation,
  readWitnessRewardOperation,
  readAccountUpdateOperation,
  readWithdrawVestingOperation,
  OPERATION_READERS
};
