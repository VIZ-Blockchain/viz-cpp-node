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

  // Asset symbols
  TOKEN_SYMBOL: BigInt('0x0000000000000003'),
  SHARES_SYMBOL: BigInt('0x0000000000000004')
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
   */
  readVector(itemReader) {
    const count = Number(this.readVarint());
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
 * Read block_header
 */
function readBlockHeader(reader) {
  return {
    previous: readRipemd160(reader),
    timestamp: readTimePointSec(reader),
    witness: reader.readString(),
    transaction_merkle_root: readRipemd160(reader),
    extensions: reader.readVector(() => {
      // Block header extensions are static variants
      // For simplicity, return raw bytes
      const typeIndex = Number(reader.readVarint());
      // Read extension content based on type
      // Most common extensions have known sizes
      // For now, we'll need to know extension types
      return { typeIndex };
    })
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
 * Read asset (int64 amount + uint64 symbol)
 */
function readAsset(reader) {
  const amount = reader.readInt64LE();
  const symbol = reader.readUint64LE();
  let symbolName = 'UNKNOWN';
  if (symbol === CONSTANTS.TOKEN_SYMBOL) symbolName = 'VIZ';
  else if (symbol === CONSTANTS.SHARES_SYMBOL) symbolName = 'SHARES';
  return { amount: Number(amount), symbol: symbolName, rawSymbol: symbol };
}

/**
 * Read public_key_type (33 bytes compressed)
 */
function readPublicKey(reader) {
  return reader.readBytes(33);
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

// Operation deserializer registry
const OPERATION_READERS = {
  2: readTransferOperation,
  3: readTransferToVestingOperation,
  4: readWithdrawVestingOperation,
  5: readAccountUpdateOperation,
  6: readWitnessUpdateOperation,
  10: readCustomOperation,
  19: readDelegateVestingSharesOperation,
  20: readAccountCreateOperation,
  26: readAuthorRewardOperation,
  42: readWitnessRewardOperation,
  43: readCreateInviteOperation,
  47: readAwardOperation
};

/**
 * Read operation (static_variant)
 * Fully deserializes known operations, returns raw data for unknown ones
 */
function readOperation(reader) {
  const typeIndex = Number(reader.readVarint());
  const typeInfo = OPERATION_TYPES[typeIndex] || { name: 'unknown_operation', isVirtual: false };
  const opReader = OPERATION_READERS[typeIndex];

  let data;
  if (opReader) {
    data = opReader(reader);
  } else {
    // Unknown operation - skip by reading until next known structure
    // This is a placeholder - full implementation needs all operation schemas
    data = { _raw: 'unknown operation type' };
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
  const header = readSignedBlockHeader(reader);
  return {
    ...header,
    transactions: reader.readVector(readSignedTransaction)
  };
}

// ============================================================================
// Block Number Utilities
// ============================================================================

/**
 * Extract block number from block_id_type (ripemd160 hash)
 * The block number is stored in the first 4 bytes as little-endian uint32
 */
function numFromId(blockId) {
  if (!blockId || blockId.length < 4) return 0;
  return blockId.readUInt32LE(0);
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

    // Read head block to cache block number
    if (this.dataSize > BigInt(CONSTANTS.MIN_VALID_FILE_SIZE)) {
      this._headBlock = this.readHead();
      this._headBlockNum = getBlockNum(this._headBlock);
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
   * @param {number} blockNum - Block number (1-indexed)
   * @returns {bigint} Position in data file
   */
  getBlockPos(blockNum) {
    if (blockNum < 1 || blockNum > this._headBlockNum) {
      return CONSTANTS.NPOS;
    }

    const indexOffset = BigInt(blockNum - 1) * 8n;
    return this._readIndexUint64(indexOffset);
  }

  /**
   * Read block by number
   * @param {number} blockNum - Block number
   * @returns {object|null} Signed block or null if not found
   */
  readBlockByNum(blockNum) {
    const pos = this.getBlockPos(blockNum);
    if (pos === CONSTANTS.NPOS) return null;

    const result = this.readBlock(pos);
    return result.block;
  }

  /**
   * Get head block number
   */
  getHeadBlockNum() {
    return this._headBlockNum;
  }

  /**
   * Get start block number (always 1 for standard block_log)
   */
  getStartBlockNum() {
    return 1;
  }

  /**
   * Get total number of blocks
   */
  getNumBlocks() {
    return this._headBlockNum;
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
