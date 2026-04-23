#!/usr/bin/env node
/**
 * VIZ Block Log Viewer - Interactive terminal UI
 * No external dependencies. Uses block-log-reader.js for parsing.
 *
 * Usage: node block-log-viewer.js <block_log_path> [--dlt] [--reader=<path>]
 *
 * The viewer will look for block-log-reader.js in this order:
 *   1. --reader=<path> CLI option
 *   2. Same directory as this script
 *   3. BLOCK_LOG_READER environment variable
 */

const fs = require('fs');
const path = require('path');
const readline = require('readline');

// Resolve block-log-reader module from multiple locations
function resolveReaderModule() {
  const cliReader = process.argv.find(a => a.startsWith('--reader='));
  if (cliReader) {
    const p = cliReader.split('=')[1];
    if (fs.existsSync(p)) return p;
    console.error(`--reader path not found: ${p}`);
    process.exit(1);
  }

  const candidates = [
    path.join(__dirname, 'block-log-reader'),           // same dir as this script
    process.env.BLOCK_LOG_READER,                       // env var
  ].filter(Boolean);

  for (const candidate of candidates) {
    try {
      require.resolve(candidate);
      return candidate;
    } catch (e) { /* not found, try next */ }
  }

  console.error('Cannot find block-log-reader.js');
  console.error('Place it in the same directory as this script, or use:');
  console.error('  node block-log-viewer.js <block_log> --reader=/path/to/block-log-reader.js');
  console.error('  set BLOCK_LOG_READER=/path/to/block-log-reader.js');
  process.exit(1);
}

const readerModule = resolveReaderModule();
const {
  createBlockLogReader,
  getBlockNum,
  blockIdToHex,
  OPERATION_TYPES,
  CONSTANTS,
  BinaryReader,
  readSignedBlockHeader,
  readSignedBlock
} = require(readerModule);

// ============================================================================
// State
// ============================================================================

let reader = null;
let currentBlockNum = 0;
let startBlock = 1;
let endBlock = 0;
let scanning = false;
let lastSearch = null; // { type: 's'|'S'|'e', term: string }

// Bitmask: 1 bit per block, 1 = has non-free ops, 0 = empty/virtual-only
let bitmask = null;       // Buffer or null
let bitmaskStart = 0;     // start_block_num stored in bitmask
let bitmaskEnd = 0;       // end_block_num stored in bitmask

// ============================================================================
// Helpers
// ============================================================================

function isNonFreeOp(op) {
  return !op.isVirtual;
}

/**
 * Check if a Buffer contains an ASCII string (case-insensitive).
 * Searches raw bytes without any string conversion or object allocation.
 */
function bufIncludesAscii(buf, searchLower) {
  const len = searchLower.length;
  const bufLen = buf.length;
  outer:
  for (let i = 0; i <= bufLen - len; i++) {
    for (let j = 0; j < len; j++) {
      let c = buf[i + j];
      // Convert to lowercase: A-Z (0x41-0x5A) -> a-z (0x61-0x7A)
      if (c >= 0x41 && c <= 0x5A) c += 0x20;
      if (c !== searchLower.charCodeAt(j)) continue outer;
    }
    return true;
  }
  return false;
}

function hasNonFreeOps(block) {
  for (const tx of block.transactions) {
    for (const op of tx.operations) {
      if (isNonFreeOp(op)) return true;
    }
  }
  return false;
}

function collectOps(block) {
  const ops = [];
  for (const tx of block.transactions) {
    for (const op of tx.operations) {
      ops.push(op);
    }
  }
  return ops;
}

function formatTimestamp(d) {
  return d.toISOString().replace('T', ' ').replace('.000Z', ' UTC');
}

function hashHex(h) {
  return h ? Buffer.from(h).toString('hex') : '(null)';
}

function shortenHex(hex, len = 16) {
  if (!hex || hex.length <= len * 2) return hex;
  return hex.slice(0, len) + '...' + hex.slice(-len);
}

// ============================================================================
// Bitmask (block_log.bitmask)
// ============================================================================
//
// File format:
//   [8 bytes] start_block_num  (uint64 LE)
//   [8 bytes] end_block_num    (uint64 LE)
//   [N bytes] bit array, 1 bit per block (bit i = block startBlock+i)
//             bit=1 means block has non-free (non-virtual) operations
//             Total bytes = ceil((end - start + 1) / 8)
//
// Saved alongside block_log as block_log.bitmask
// ============================================================================

function bitmaskPath() {
  return reader.dataPath + '.bitmask';
}

/**
 * Set bit for blockNum (1 = has non-free ops)
 */
function bitmaskSet(buf, start, blockNum) {
  const idx = blockNum - start;
  const byteIdx = idx >> 3;
  const bitIdx = idx & 7;
  buf[byteIdx] |= (1 << bitIdx);
}

/**
 * Get bit for blockNum. Returns 0 or 1. If no bitmask loaded, returns -1.
 */
function bitmaskGet(blockNum) {
  if (!bitmask) return -1;
  if (blockNum < bitmaskStart || blockNum > bitmaskEnd) return -1;
  const idx = blockNum - bitmaskStart;
  const byteIdx = idx >> 3;
  const bitIdx = idx & 7;
  return (bitmask[byteIdx] >> bitIdx) & 1;
}

/**
 * Check if bitmask is loaded and valid for current range
 */
function bitmaskValid() {
  return bitmask && bitmaskStart === startBlock && bitmaskEnd === endBlock;
}

/**
 * Load bitmask from file. Returns true if loaded and valid.
 */
function bitmaskLoad() {
  const p = bitmaskPath();
  if (!fs.existsSync(p)) return false;
  try {
    const fd = fs.openSync(p, 'r');
    const stat = fs.fstatSync(fd);
    if (stat.size < 16) { fs.closeSync(fd); return false; }

    const hdr = Buffer.allocUnsafe(16);
    fs.readSync(fd, hdr, 0, 16, 0);
    const s = Number(hdr.readBigUInt64LE(0));
    const e = Number(hdr.readBigUInt64LE(8));

    const expectedBits = e - s + 1;
    const expectedBytes = Math.ceil(expectedBits / 8);
    if (stat.size < 16 + expectedBytes) { fs.closeSync(fd); return false; }

    const bits = Buffer.allocUnsafe(expectedBytes);
    fs.readSync(fd, bits, 0, expectedBytes, 16);
    fs.closeSync(fd);

    bitmask = bits;
    bitmaskStart = s;
    bitmaskEnd = e;

    if (s === startBlock && e === endBlock) {
      return true; // fully valid
    }
    console.log(`  Bitmask range #${s}-#${e} differs from log #${startBlock}-#${endBlock}, will rescan.`);
    return false;
  } catch (e) {
    return false;
  }
}

/**
 * Scan all blocks, build bitmask, save to file.
 * Uses lightweight tx-count check (no full block deserialization) with
 * batch I/O for the index file and progressive bitmask writes.
 * Virtual operations are NOT stored in block_log, so tx_count > 0 = has ops.
 */
function bitmaskScan() {
  if (scanning) { console.log('  Already scanning.'); return; }
  scanning = true;
  const total = endBlock - startBlock + 1;
  const byteLen = Math.ceil(total / 8);
  const BATCH_SIZE = 50000;

  // Prepare file
  const p = bitmaskPath();
  const hdr = Buffer.allocUnsafe(16);
  hdr.writeBigUInt64LE(BigInt(startBlock), 0);
  hdr.writeBigUInt64LE(BigInt(endBlock), 8);

  // Write header + empty bitmask placeholder
  const fd = fs.openSync(p, 'w');
  fs.writeSync(fd, hdr, 0, 16, 0);
  const zeroChunk = Buffer.alloc(Math.min(byteLen, 65536));
  for (let off = 0; off < byteLen; off += zeroChunk.length) {
    const writeLen = Math.min(zeroChunk.length, byteLen - off);
    fs.writeSync(fd, zeroChunk, 0, writeLen, 16 + off);
  }

  console.log(`  Scanning ${total} blocks for non-free operations...`);
  console.log(`  Bitmask file: ${path.basename(p)} (${((16 + byteLen) / 1024).toFixed(1)} KB)`);
  console.log(`  Batch size: ${BATCH_SIZE} blocks (lightweight: header + tx count only)`);

  // Process in batches
  let currentNum = startBlock;
  let nonFreeCount = 0;
  let lastPct = -1;

  function processBatch() {
    const batchEnd = Math.min(currentNum + BATCH_SIZE - 1, endBlock);
    const batchCount = batchEnd - currentNum + 1;
    const batchBits = Buffer.alloc(Math.ceil(batchCount / 8));

    // Read index positions in bulk (one I/O for the whole batch + 1 extra for size boundary)
    const positions = reader.readBlockPosBatch(currentNum, batchCount + 1);
    if (positions.length === 0) {
      currentNum = batchEnd + 1;
      setImmediate(processBatch);
      return;
    }

    for (let i = 0; i < batchCount; i++) {
      const num = currentNum + i;
      const startPos = Number(positions[i]);
      const endPos = (i + 1 < positions.length) ? Number(positions[i + 1]) : (Number(reader.dataSize) - 8);
      const blockSize = endPos - startPos;

      if (blockSize <= 0 || blockSize > CONSTANTS.CHAIN_BLOCK_SIZE + 16) continue;

      try {
        // Read exact block bytes and parse only header + tx count
        const buffer = reader._readData(BigInt(startPos), blockSize);
        const br = new BinaryReader(buffer);
        readSignedBlockHeader(br); // skip header
        const txCount = Number(br.readVarint());

        if (txCount > 0) {
          const byteIdx = Math.floor(i / 8);
          const bitOffset = i % 8;
          batchBits[byteIdx] |= (1 << bitOffset);
          nonFreeCount++;
        }
      } catch (e) {
        // Skip blocks that fail to parse
      }
    }

    // Write this batch's bits to the file
    const batchFileOffset = Math.floor((currentNum - startBlock) / 8);
    fs.writeSync(fd, batchBits, 0, batchBits.length, 16 + batchFileOffset);

    // Progress
    const processed = batchEnd - startBlock + 1;
    const pct = Math.floor((processed / total) * 100);
    if (pct !== lastPct) {
      lastPct = pct;
      const memMB = Math.round(process.memoryUsage().heapUsed / 1024 / 1024);
      process.stdout.write(`\r  Scanning... ${pct}% (#${batchEnd}, ${nonFreeCount} with ops, ${memMB} MB heap)      `);
    }

    currentNum = batchEnd + 1;
    if (currentNum <= endBlock) {
      setImmediate(processBatch);
    } else {
      // Done — load the completed bitmask
      fs.closeSync(fd);

      bitmaskLoad();

      const empty = total - nonFreeCount;
      const sizeKB = ((16 + byteLen) / 1024).toFixed(1);
      console.log(`\r  Done. ${nonFreeCount} with ops, ${empty} empty. Saved ${sizeKB} KB to ${path.basename(p)}      `);
      scanning = false;
      showPrompt();
    }
  }

  processBatch();
}

/**
 * Find next block with non-free ops using bitmask (fast, no deserialization)
 * Returns block number or 0 if none found
 */
function bitmaskFindNext(fromBlock) {
  if (!bitmaskValid()) return 0;
  for (let num = fromBlock; num <= bitmaskEnd; num++) {
    if (bitmaskGet(num) === 1) return num;
  }
  return 0;
}

/**
 * Find prev block with non-free ops using bitmask
 */
function bitmaskFindPrev(fromBlock) {
  if (!bitmaskValid()) return 0;
  for (let num = fromBlock; num >= bitmaskStart; num--) {
    if (bitmaskGet(num) === 1) return num;
  }
  return 0;
}

// ============================================================================
// Display
// ============================================================================

function showBlock(block) {
  const num = getBlockNum(block);
  const ops = collectOps(block);
  const nonFree = ops.filter(isNonFreeOp);
  const virtual = ops.filter(o => o.isVirtual);

  console.log('');
  console.log('='.repeat(72));
  console.log(`  Block #${num}`);
  console.log('='.repeat(72));
  console.log(`  Timestamp : ${formatTimestamp(block.timestamp)}`);
  console.log(`  Witness   : ${block.witness}`);
  console.log(`  Previous  : ${shortenHex(hashHex(block.previous))}`);
  console.log(`  Tx Merkle : ${shortenHex(hashHex(block.transaction_merkle_root))}`);
  console.log(`  Signature : ${shortenHex(hashHex(block.witness_signature))}`);
  console.log(`  Tx count  : ${block.transactions.length}`);
  if (block.extensions && block.extensions.length > 0) {
    for (const ext of block.extensions) {
      if (ext.name === 'version') {
        console.log(`  Extension : version = ${ext.data.version}`);
      } else if (ext.name === 'hardfork_version_vote') {
        console.log(`  Extension : hardfork_version_vote = ${ext.data.hf_version} at ${ext.data.hf_time}`);
      } else {
        console.log(`  Extension : ${ext.name} ${JSON.stringify(ext.data)}`);
      }
    }
  }
  console.log(`  Ops total : ${ops.length} (non-free: ${nonFree.length}, virtual: ${virtual.length})`);
  console.log('-'.repeat(72));
}

function showOps(block, filter) {
  const ops = collectOps(block);
  const filtered = filter ? ops.filter(o => o.typeName.includes(filter)) : ops;

  if (filtered.length === 0) {
    if (filter) {
      console.log(`  No operations matching "${filter}" in this block.`);
    } else {
      console.log('  (no operations)');
    }
    return;
  }

  for (let i = 0; i < filtered.length; i++) {
    const op = filtered[i];
    const tag = op.isVirtual ? '[V]' : '   ';
    console.log(`  ${tag} [${i}] ${op.typeName}`);
    try {
      const json = JSON.stringify(op.data, (key, val) => {
        if (val && val.type === 'Buffer') return `<buffer ${val.data.length}B>`;
        if (Buffer.isBuffer(val)) return `<buffer ${val.length}B>`;
        if (typeof val === 'bigint') return val.toString();
        return val;
      }, 2);
      for (const line of json.split('\n')) {
        console.log('      ' + line);
      }
    } catch (e) {
      console.log('      (serialization error)');
    }
    console.log('');
  }
}

function showHelp() {
  console.log('');
  console.log('Commands:');
  console.log('  f          - First block');
  console.log('  l          - Last block');
  console.log('  n          - Next block');
  console.log('  p          - Previous block');
  console.log('  N          - Next block with non-free operations (uses bitmask)');
  console.log('  P          - Prev block with non-free operations (uses bitmask)');
  console.log('  g <num>    - Go to block #num');
  console.log('  o          - Show operations in current block');
  console.log('  o <name>   - Show operations matching name');
  console.log('  s <name>   - Search forward for block containing operation name');
  console.log('  S <string> - Search forward for string in op JSON (incl. virtual)');
  console.log('              Prefix with = for exact match: S =\"V\" finds V but not VIZ');
  console.log('  R <string> - Fast raw ASCII byte search (no UTF-8/emoji)');
  console.log('  e <string> - Export all ops containing string to search_export_<ts>.json');
  console.log('              Prefix with = for exact match: e =\"V\" exports only exact V matches');
  console.log('  c          - Continue last search (s/S/e)');
  console.log('  scan       - Scan all blocks, build & save bitmask for fast nav');
  console.log('  i          - Show block info (header only)');
  console.log('  hex        - Show raw block data in hex');
  console.log('  h          - This help');
  console.log('  q          - Quit');
  const bm = bitmaskValid() ? `LOADED (${endBlock - startBlock + 1} blocks)` : 'not loaded';
  console.log(`  Bitmask   : ${bm}`);
  console.log('');
}

function showPrompt() {
  const pct = endBlock > startBlock ? Math.round(((currentBlockNum - startBlock) / (endBlock - startBlock)) * 100) : 0;
  process.stdout.write(`[${currentBlockNum}/${endBlock}] ${pct}% > `);
}

// ============================================================================
// Navigation
// ============================================================================

/**
 * Safely read a block by number. Returns the block or null.
 * Logs errors but does NOT throw — for use in scan loops.
 */
function safeReadBlock(num) {
  try {
    return reader.readBlockByNum(num);
  } catch (e) {
    return null; // skip blocks that fail to deserialize
  }
}

function showBlockHeaderOnly(header, blockNum, errorMsg) {
  console.log('');
  console.log('='.repeat(72));
  console.log(`  Block #${blockNum} (HEADER ONLY - deserialization failed)`);
  console.log('='.repeat(72));
  if (header) {
    console.log(`  Timestamp : ${formatTimestamp(header.timestamp)}`);
    console.log(`  Witness   : ${header.witness}`);
    console.log(`  Previous  : ${shortenHex(hashHex(header.previous))}`);
    console.log(`  Tx Merkle : ${shortenHex(hashHex(header.transaction_merkle_root))}`);
    console.log(`  Signature : ${shortenHex(hashHex(header.witness_signature))}`);
    console.log(`  Block Num : ${header._blockNum} (from previous)`);
    console.log(`  File Pos  : ${header._position}`);
  } else {
    console.log('  (header also could not be read)');
  }
  console.log('-'.repeat(72));
  console.log(`  Error: ${errorMsg}`);
  console.log('-'.repeat(72));
}

function goTo(num) {
  num = Math.max(startBlock, Math.min(endBlock, num));
  let block;
  try {
    block = reader.readBlockByNum(num);
  } catch (e) {
    // Full deserialization failed — try header-only
    const header = reader.readBlockHeaderByNum(num);
    showBlockHeaderOnly(header, num, e.message);
    currentBlockNum = num;
    return;
  }
  if (!block) {
    // Block number out of index range
    console.log(`Block #${num} is not in the index (out of range).`);
    return;
  }
  currentBlockNum = num;
  showBlock(block);
}

function goNext() {
  if (currentBlockNum >= endBlock) { console.log('Already at last block.'); return; }
  goTo(currentBlockNum + 1);
}

function goPrev() {
  if (currentBlockNum <= startBlock) { console.log('Already at first block.'); return; }
  goTo(currentBlockNum - 1);
}

function goFirst() {
  goTo(startBlock);
}

function goLast() {
  goTo(endBlock);
}

function goNextWithOps() {
  // Fast path: use bitmask to skip empty blocks without deserialization
  if (bitmaskValid()) {
    const next = bitmaskFindNext(currentBlockNum + 1);
    if (next === 0) {
      console.log('  No more blocks with non-free operations forward.');
      return;
    }
    // Only read & deserialize the one block we found
    currentBlockNum = next;
    try {
      const block = reader.readBlockByNum(next);
      if (block) showBlock(block);
      else showBlockHeaderOnly(reader.readBlockHeaderByNum(next), next, 'Block returned null');
    } catch (e) {
      showBlockHeaderOnly(reader.readBlockHeaderByNum(next), next, e.message);
    }
    return;
  }

  // Slow fallback: scan by reading each block
  for (let num = currentBlockNum + 1; num <= endBlock; num++) {
    const block = safeReadBlock(num);
    if (block && hasNonFreeOps(block)) {
      currentBlockNum = num;
      showBlock(block);
      return;
    }
    if ((num - currentBlockNum) % 1000 === 0) {
      process.stdout.write(`\r  Scanning... #${num}      `);
    }
  }
  console.log('\r  No more blocks with non-free operations forward.      ');
}

function goPrevWithOps() {
  // Fast path: use bitmask
  if (bitmaskValid()) {
    const prev = bitmaskFindPrev(currentBlockNum - 1);
    if (prev === 0) {
      console.log('  No more blocks with non-free operations backward.');
      return;
    }
    currentBlockNum = prev;
    try {
      const block = reader.readBlockByNum(prev);
      if (block) showBlock(block);
      else showBlockHeaderOnly(reader.readBlockHeaderByNum(prev), prev, 'Block returned null');
    } catch (e) {
      showBlockHeaderOnly(reader.readBlockHeaderByNum(prev), prev, e.message);
    }
    return;
  }

  // Slow fallback
  for (let num = currentBlockNum - 1; num >= startBlock; num--) {
    const block = safeReadBlock(num);
    if (block && hasNonFreeOps(block)) {
      currentBlockNum = num;
      showBlock(block);
      return;
    }
    if ((currentBlockNum - num) % 1000 === 0) {
      process.stdout.write(`\r  Scanning... #${num}      `);
    }
  }
  console.log('\r  No more blocks with non-free operations backward.      ');
}

/**
 * Run a search function in batches to avoid OOM.
 * The checkFn receives (blockNum) and should return false to skip this block,
 * or true to proceed with full deserialization via matchFn.
 * matchFn receives (blockNum, block) and should return truthy to stop (the match).
 * Calls onProgress(currentNum) for status updates.
 */
function batchSearch(checkFn, matchFn, onProgress, onDone) {
  if (scanning) { console.log('  Already scanning.'); return; }
  scanning = true;
  const BATCH_SIZE = 10000;
  let currentNum = currentBlockNum + 1;

  function processBatch() {
    const batchEnd = Math.min(currentNum + BATCH_SIZE - 1, endBlock);

    for (let num = currentNum; num <= batchEnd; num++) {
      // Lightweight pre-check: skip empty blocks
      if (!checkFn(num)) continue;

      // Full deserialize only blocks that pass the check
      const block = safeReadBlock(num);
      if (block) {
        const result = matchFn(num, block);
        if (result) {
          currentBlockNum = num;
          scanning = false;
          onDone(null);
          return;
        }
      }
      if ((num - currentNum) % 2000 === 0 && num > currentNum) {
        onProgress(num);
      }
    }

    onProgress(batchEnd);
    currentNum = batchEnd + 1;
    if (currentNum <= endBlock) {
      setImmediate(processBatch);
    } else {
      scanning = false;
      onDone('not found');
    }
  }

  processBatch();
}

function searchOpForward(name) {
  lastSearch = { type: 's', term: name };
  console.log(`  Searching for operation "${name}" forward from #${currentBlockNum + 1}...`);

  batchSearch(
    (num) => {
      // Skip blocks with no transactions (bitmask only — no per-block I/O)
      if (bitmaskValid()) return bitmaskGet(num) === 1;
      return true; // without bitmask, can't skip
    },
    (num, block) => {
      const ops = collectOps(block);
      if (ops.some(o => o.typeName.includes(name))) {
        showBlock(block);
        showOps(block, name);
        return true;
      }
      return false;
    },
    (num) => process.stdout.write(`\r  Scanning... #${num}      `),
    (err) => {
      if (err === 'not found') console.log('\r  No matching operations found.      ');
      else console.log('\r  No more matching operations.      ');
      showPrompt();
    }
  );
}

// ============================================================================
// String Search in Operation JSON (including virtual ops)
// ============================================================================

/**
 * Parse search term: strip surrounding quotes, detect = prefix for exact match.
 * Returns { term: string, exact: boolean }
 * Examples:
 *   '="V"' → { term: 'V', exact: true }
 *   '=V'    → { term: 'V', exact: true }
 *   '"V"'  → { term: 'V', exact: false }
 *   'VIZ'   → { term: 'VIZ', exact: false }
 */
function parseSearch(str) {
  if (!str) return { term: str, exact: false };
  let exact = false;
  if (str.startsWith('=')) {
    exact = true;
    str = str.slice(1);
  }
  if ((str.startsWith('"') && str.endsWith('"')) ||
      (str.startsWith("'") && str.endsWith("'"))) {
    str = str.slice(1, -1);
  }
  return { term: str, exact };
}

/**
 * Recursively walk an object looking for a string value matching searchStr.
 * Zero-allocation: no JSON.stringify, just walks existing objects.
 * If exact=true, uses === instead of .includes() for string values.
 */
function deepIncludes(obj, searchStr, exact) {
  if (obj === null || obj === undefined) return false;
  if (typeof obj === 'string') return exact ? (obj === searchStr) : obj.includes(searchStr);
  if (typeof obj === 'number' || typeof obj === 'boolean') return false;
  if (typeof obj === 'bigint') return exact ? (obj.toString() === searchStr) : obj.toString().includes(searchStr);
  if (Buffer.isBuffer(obj)) return false; // skip binary
  if (Array.isArray(obj)) {
    for (let i = 0; i < obj.length; i++) {
      if (deepIncludes(obj[i], searchStr, exact)) return true;
    }
    return false;
  }
  if (typeof obj === 'object') {
    for (const key of Object.keys(obj)) {
      if (exact ? (key === searchStr) : key.includes(searchStr)) return true;
      if (deepIncludes(obj[key], searchStr, exact)) return true;
    }
  }
  return false;
}

/**
 * Check if any operation in a block contains the search string.
 * Uses recursive walk instead of JSON.stringify to avoid large temp strings.
 * Returns matching operations array (empty if none).
 */
function findOpsByString(block, searchStr, exact) {
  const ops = collectOps(block);
  const matched = [];
  for (const op of ops) {
    if (op.typeName && (exact ? op.typeName === searchStr : op.typeName.includes(searchStr))) {
      matched.push(op);
      continue;
    }
    if (deepIncludes(op.data, searchStr, exact)) {
      matched.push(op);
    }
  }
  return matched;
}

/**
 * Deserialize a block from an existing raw buffer (no re-read, no 1MB over-allocate).
 * Returns the block object or null on error.
 */
function deserializeFromRaw(rawBuf) {
  try {
    const br = new BinaryReader(rawBuf);
    const block = readSignedBlock(br);
    return block;
  } catch (e) {
    console.error(`  deserializeFromRaw ERROR: ${e.message} (bufLen=${rawBuf.length})`);
    return null;
  }
}

/**
 * Fast raw ASCII byte search forward — no deserialization, just scans raw block bytes.
 * Only finds ASCII strings (English, digits). Use S for UTF-8/emoji search.
 */
function searchRawForward(str) {
  lastSearch = { type: 'R', term: str };
  if (scanning) { console.log('  Already scanning.'); return; }
  scanning = true;
  const YIELD_EVERY = 200; // raw scan is lighter, can do more per yield
  let currentNum = currentBlockNum + 1;

  console.log(`  Raw searching for ASCII "${str}" forward from #${currentNum}...`);

  function processChunk() {
    const chunkEnd = Math.min(currentNum + YIELD_EVERY - 1, endBlock);

    const positions = reader.readBlockPosBatch(currentNum, chunkEnd - currentNum + 2);
    if (positions.length === 0) {
      currentNum = chunkEnd + 1;
      setImmediate(processChunk);
      return;
    }

    for (let num = currentNum; num <= chunkEnd; num++) {
      const i = num - currentNum;
      const startPos = Number(positions[i]);
      const endPos = (i + 1 < positions.length) ? Number(positions[i + 1]) : (Number(reader.dataSize) - 8);
      const blockSize = endPos - startPos;
      if (blockSize <= 0 || blockSize > CONSTANTS.CHAIN_BLOCK_SIZE + 16) continue;

      let rawBuf = null;
      try {
        rawBuf = reader._readData(BigInt(startPos), blockSize);
        if (bufIncludesAscii(rawBuf, str)) {
          // Found — deserialize to show context
          const block = deserializeFromRaw(rawBuf);
          rawBuf = null;
          if (block) {
            currentBlockNum = num;
            showBlock(block);
            showOps(block);
          } else {
            currentBlockNum = num;
            console.log(`  Block #${num} contains "${str}" in raw bytes (could not deserialize).`);
          }
          scanning = false;
          showPrompt();
          return;
        }
      } catch (e) { /* skip */ }
      rawBuf = null; // free buffer
    }

    process.stdout.write(`\r  Scanning... #${chunkEnd}      `);
    currentNum = chunkEnd + 1;
    if (currentNum <= endBlock) {
      setImmediate(processChunk);
    } else {
      console.log('\r  No blocks containing ASCII "' + str + '" found.      ');
      scanning = false;
      showPrompt();
    }
  }

  processChunk();
}

/**
 * Search forward for next block containing string in any operation's JSON.
 * Only deserializes blocks that have operations (bitmask or tx-count filter).
 * Yields to event loop every YIELD_EVERY blocks so GC can reclaim memory.
 * Supports UTF-8, emoji, and any string in operation data.
 */
function searchStringForward(str, exact) {
  lastSearch = { type: 'S', term: str, exact: !!exact };
  if (scanning) { console.log('  Already scanning.'); return; }
  scanning = true;
  const YIELD_EVERY = 100;
  let currentNum = currentBlockNum + 1;

  console.log(`  Searching for ${exact ? 'exact' : 'substring'} "${str}" in op JSON forward from #${currentNum}...`);

  function processChunk() {
    const chunkEnd = Math.min(currentNum + YIELD_EVERY - 1, endBlock);

    // Read index positions in bulk for this chunk
    const positions = reader.readBlockPosBatch(currentNum, chunkEnd - currentNum + 2);
    if (positions.length === 0) {
      currentNum = chunkEnd + 1;
      setImmediate(processChunk);
      return;
    }

    for (let num = currentNum; num <= chunkEnd; num++) {
      const i = num - currentNum;

      // Skip blocks with no operations (bitmask only — no per-block I/O)
      if (bitmaskValid() && bitmaskGet(num) === 0) continue;

      // Read exact-size bytes and deserialize
      const startPos = Number(positions[i]);
      const endPos = (i + 1 < positions.length) ? Number(positions[i + 1]) : (Number(reader.dataSize) - 8);
      const blockSize = endPos - startPos;
      if (blockSize <= 0 || blockSize > CONSTANTS.CHAIN_BLOCK_SIZE + 16) continue;

      // Debug: log block number and size every 1000 blocks or if block is large
      if (num % 1000 === 0 || blockSize > 100000) {
        const memMB = process.memoryUsage().heapUsed / 1024 / 1024;
        process.stdout.write(`\r  #${num} size=${blockSize} heap=${memMB.toFixed(0)}MB      \n`);
      }

      let rawBuf = null;
      let block = null;
      try {
        rawBuf = reader._readData(BigInt(startPos), blockSize);
        block = deserializeFromRaw(rawBuf);
      } catch (e) { /* skip */ }
      rawBuf = null;

      if (block) {
        const matched = findOpsByString(block, str, exact);
        if (matched.length > 0) {
          currentBlockNum = num;
          showBlock(block);
          for (const op of matched) {
            const tag = op.isVirtual ? '[V]' : '   ';
            console.log(`  ${tag} ${op.typeName}`);
            try {
              const json = JSON.stringify(op.data, (key, val) => {
                if (val && val.type === 'Buffer') return `<buffer ${val.data.length}B>`;
                if (Buffer.isBuffer(val)) return `<buffer ${val.length}B>`;
                if (typeof val === 'bigint') return val.toString();
                return val;
              }, 2);
              for (const line of json.split('\n')) {
                console.log('      ' + line);
              }
            } catch (e) {
              console.log('      (serialization error)');
            }
            console.log('');
          }
          scanning = false;
          showPrompt();
          return;
        }
      }
      block = null;
    }

    process.stdout.write(`\r  Scanning... #${chunkEnd}      `);
    currentNum = chunkEnd + 1;
    if (currentNum <= endBlock) {
      setImmediate(processChunk);
    } else {
      console.log('\r  No matching operations found.      ');
      scanning = false;
      showPrompt();
    }
  }

  processChunk();
}

/**
 * JSON replacer for Buffer/bigint serialization
 */
function jsonReplacer(key, val) {
  if (val && val.type === 'Buffer' && Array.isArray(val.data)) return Buffer.from(val.data).toString('hex');
  if (Buffer.isBuffer(val)) return val.toString('hex');
  if (typeof val === 'bigint') return val.toString();
  return val;
}

/**
 * Search all blocks and export matching operations to JSON file.
 * Streams results to disk: writes each chunk's matches as JSON objects,
 * then clears them from memory. Yields every YIELD_EVERY blocks for GC.
 * Final file = "[" + obj1 + "," + obj2 + ... + "]".
 */
function searchExport(str, exact) {
  lastSearch = { type: 'e', term: str, exact: !!exact };
  if (scanning) { console.log('  Already scanning.'); return; }
  scanning = true;

  let matchCount = 0;
  let blockCount = 0;
  let writtenBytes = 0;
  const total = endBlock - startBlock + 1;
  let lastPct = -1;
  const YIELD_EVERY = 100;
  let currentNum = startBlock;
  let needComma = false; // whether to prepend "," before next object

  // Open export file, write opening "["
  const unixTime = Math.floor(Date.now() / 1000);
  const outPath = path.join(path.dirname(reader.dataPath), `search_export_${unixTime}.json`);
  const fd = fs.openSync(outPath, 'w');
  fs.writeSync(fd, '[\n', null, 'utf8');
  writtenBytes += 2;

  console.log(`  Exporting all operations ${exact ? 'exactly matching' : 'containing'} "${str}" from #${startBlock} to #${endBlock}...`);
  console.log(`  Output: ${path.basename(outPath)}`);

  function processChunk() {
    const chunkEnd = Math.min(currentNum + YIELD_EVERY - 1, endBlock);

    // Read index positions in bulk
    const positions = reader.readBlockPosBatch(currentNum, chunkEnd - currentNum + 2);

    for (let num = currentNum; num <= chunkEnd; num++) {
      const i = num - currentNum;

      // Skip blocks with no operations (bitmask only — no per-block I/O)
      if (bitmaskValid() && bitmaskGet(num) === 0) continue;

      const startPos = (i < positions.length) ? Number(positions[i]) : 0;
      const endPos = (i + 1 < positions.length) ? Number(positions[i + 1]) : 0;
      const blockSize = endPos - startPos;
      if (blockSize <= 0 || blockSize > CONSTANTS.CHAIN_BLOCK_SIZE + 16) continue;

      // Debug: log block number and size every 1000 blocks or if block is large
      if (num % 1000 === 0 || blockSize > 100000) {
        const memMB = process.memoryUsage().heapUsed / 1024 / 1024;
        process.stdout.write(`\r  #${num} size=${blockSize} heap=${memMB.toFixed(0)}MB      \n`);
      }

      // Read exact-size bytes and deserialize
      let rawBuf = null;
      let block = null;
      try {
        rawBuf = reader._readData(BigInt(startPos), blockSize);
        block = deserializeFromRaw(rawBuf);
      } catch (e) { /* skip */ }
      rawBuf = null;

      if (block) {
        const matched = findOpsByString(block, str, exact);
        if (matched.length > 0) {
          blockCount++;
          for (const op of matched) {
            matchCount++;
            // Flush each op immediately — don't accumulate references
            const prefix = needComma ? ',\n' : '';
            needComma = true;
            const record = {
              block: num,
              timestamp: formatTimestamp(block.timestamp),
              witness: block.witness,
              typeId: op.typeId,
              typeName: op.typeName,
              isVirtual: op.isVirtual,
              data: op.data
            };
            const json = JSON.stringify(record, jsonReplacer, 2);
            const chunk = prefix + json;
            fs.writeSync(fd, chunk, null, 'utf8');
            writtenBytes += Buffer.byteLength(chunk, 'utf8');
          }
        }
      }
      block = null;
    }

    const pct = Math.floor(((chunkEnd - startBlock) / total) * 100);
    if (pct !== lastPct && pct % 5 === 0) {
      lastPct = pct;
      const sizeMB = (writtenBytes / 1024 / 1024).toFixed(1);
      process.stdout.write(`\r  Scanning... ${pct}% (#${chunkEnd}, ${matchCount} ops in ${blockCount} blocks, ${sizeMB} MB written)      `);
    } else {
      process.stdout.write(`\r  Scanning... #${chunkEnd} (${matchCount} ops)      `);
    }

    currentNum = chunkEnd + 1;
    if (currentNum <= endBlock) {
      setImmediate(processChunk);
    } else {
      // Close the JSON array
      fs.writeSync(fd, '\n]', null, 'utf8');
      writtenBytes += 2;
      fs.closeSync(fd);

      if (matchCount === 0) {
        // Remove empty file
        fs.unlinkSync(outPath);
        console.log('\r  No matching operations found.                                          ');
      } else {
        const sizeMB = (writtenBytes / 1024 / 1024).toFixed(1);
        console.log(`\r  Done. ${matchCount} ops in ${blockCount} blocks. Saved ${sizeMB} MB to ${path.basename(outPath)}      `);
      }
      scanning = false;
      showPrompt();
    }
  }

  processChunk();
}

function showCurrentOps(filter) {
  try {
    const block = reader.readBlockByNum(currentBlockNum);
    if (!block) { console.log('No current block (deserialization failed).'); return; }
    showOps(block, filter || null);
  } catch (e) {
    console.log(`Cannot show ops: ${e.message}`);
  }
}

function showCurrentInfo() {
  try {
    const block = reader.readBlockByNum(currentBlockNum);
    if (!block) {
      const header = reader.readBlockHeaderByNum(currentBlockNum);
      showBlockHeaderOnly(header, currentBlockNum, 'Full deserialization failed');
      return;
    }
    showBlock(block);
  } catch (e) {
    const header = reader.readBlockHeaderByNum(currentBlockNum);
    showBlockHeaderOnly(header, currentBlockNum, e.message);
  }
}

function showBlockHex() {
  const raw = reader.readBlockRawData(currentBlockNum);
  if (!raw) {
    console.log(`Block #${currentBlockNum} raw data not available (out of range).`);
    return;
  }

  const pos = reader.getBlockPos(currentBlockNum);
  console.log('');
  console.log(`Raw block #${currentBlockNum}: offset ${pos}, ${raw.length} bytes`);
  console.log('='.repeat(73));

  const BYTES_PER_LINE = 16;
  for (let offset = 0; offset < raw.length; offset += BYTES_PER_LINE) {
    const slice = raw.slice(offset, Math.min(offset + BYTES_PER_LINE, raw.length));
    const hexParts = [];
    const asciiParts = [];

    for (let i = 0; i < BYTES_PER_LINE; i++) {
      if (i < slice.length) {
        hexParts.push(slice.readUInt8(i).toString(16).padStart(2, '0'));
        const c = slice.readUInt8(i);
        asciiParts.push(c >= 0x20 && c < 0x7f ? String.fromCharCode(c) : '.');
      } else {
        hexParts.push('  ');
        asciiParts.push(' ');
      }
      if (i === 7) hexParts.push(''); // extra space in the middle
    }

    const addr = offset.toString(16).padStart(8, '0');
    console.log(`  ${addr}  ${hexParts.join(' ')}  |${asciiParts.join('')}|`);
  }

  console.log('='.repeat(73));
  console.log(`  ${raw.length} bytes total`);

  // Also show as continuous hex on one line for easy copy
  const hexLine = raw.toString('hex');
  console.log(`  Hex (first 128 bytes): ${hexLine.slice(0, 256)}`);
  if (hexLine.length > 256) {
    console.log(`  ... (${hexLine.length / 2 - 128} more bytes)`);
  }
}

// ============================================================================
// Main Loop
// ============================================================================

function main() {
  const args = process.argv.slice(2);
  if (args.length < 1) {
    console.log('Usage: node block-log-viewer.js <path> [--dlt] [--reader=<path>]');
    console.log('');
    console.log('  <path> can be:');
    console.log('    - Path to block_log or dlt_block_log file directly');
    console.log('    - Path to directory containing block_log / dlt_block_log');
    console.log('');
    console.log('Options:');
    console.log('  --dlt              Use DLT (rolling) block log reader');
    console.log('  --reader=<path>    Path to block-log-reader.js module');
    process.exit(1);
  }

  let dataPath = args.find(a => !a.startsWith('--'));
  let isDlt = args.includes('--dlt');

  // If path is a directory, auto-detect block_log or dlt_block_log inside it
  if (fs.existsSync(dataPath) && fs.statSync(dataPath).isDirectory()) {
    const dltPath = path.join(dataPath, 'dlt_block_log');
    const stdPath = path.join(dataPath, 'block_log');

    if (!isDlt && fs.existsSync(dltPath) && !fs.existsSync(stdPath)) {
      // Only dlt_block_log exists — auto-switch to DLT mode
      dataPath = dltPath;
      isDlt = true;
    } else if (isDlt) {
      if (!fs.existsSync(dltPath)) {
        console.log(`dlt_block_log not found in: ${dataPath}`);
        process.exit(1);
      }
      dataPath = dltPath;
    } else {
      if (!fs.existsSync(stdPath)) {
        console.log(`block_log not found in: ${dataPath}`);
        process.exit(1);
      }
      dataPath = stdPath;
    }
  }

  if (!fs.existsSync(dataPath)) {
    console.log(`File not found: ${dataPath}`);
    process.exit(1);
  }

  try {
    reader = createBlockLogReader(dataPath, undefined, isDlt);
  } catch (e) {
    console.log(`Failed to open block log: ${e.message}`);
    process.exit(1);
  }

  startBlock = reader.getStartBlockNum();
  endBlock = reader.getHeadBlockNum();

  if (endBlock === 0) {
    console.log('Block log appears empty (index has no entries).');
    reader.close();
    process.exit(1);
  }

  console.log('');
  console.log(`VIZ Block Log Viewer`);
  console.log(`  File     : ${dataPath}`);
  console.log(`  Type     : ${isDlt ? 'DLT (rolling)' : 'Standard'}`);
  console.log(`  Blocks   : #${startBlock} - #${endBlock} (${endBlock - startBlock + 1} total)`);

  // Try to load bitmask
  const bmLoaded = bitmaskLoad();
  if (bmLoaded) {
    console.log(`  Bitmask  : LOADED (${path.basename(bitmaskPath())})`);
  } else if (bitmask) {
    console.log(`  Bitmask  : outdated, run 'scan' to rebuild`);
  } else {
    console.log(`  Bitmask  : not found, run 'scan' to build`);
  }
  console.log('');

  // Show first block
  goTo(startBlock);
  showHelp();

  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    prompt: ''
  });

  const onLine = (line) => {
    if (scanning) return; // ignore input during scan
    const trimmed = line.trim();
    if (!trimmed) { showPrompt(); return; }

    const parts = trimmed.split(/\s+/);
    const cmd = parts[0];

    switch (cmd) {
      case 'f': goFirst(); break;
      case 'l': goLast(); break;
      case 'n': goNext(); break;
      case 'p': goPrev(); break;
      case 'N': goNextWithOps(); break;
      case 'P': goPrevWithOps(); break;
      case 'scan': bitmaskScan(); break;
      case 'g': {
        const num = parseInt(parts[1], 10);
        if (isNaN(num)) { console.log('Usage: g <block_number>'); break; }
        goTo(num);
        break;
      }
      case 'o': {
        showCurrentOps(parts.slice(1).join(' ') || null);
        break;
      }
      case 's': {
        const name = parts.slice(1).join(' ');
        if (!name) { console.log('Usage: s <operation_name>'); break; }
        searchOpForward(name);
        break;
      }
      case 'S': {
        const { term, exact } = parseSearch(parts.slice(1).join(' '));
        if (!term) { console.log('Usage: S <string>  (prefix with = for exact match, e.g. S ="V")'); break; }
        searchStringForward(term, exact);
        break;
      }
      case 'R': {
        const { term } = parseSearch(parts.slice(1).join(' '));
        if (!term) { console.log('Usage: R <string> (ASCII only)'); break; }
        searchRawForward(term);
        break;
      }
      case 'e': {
        const { term, exact } = parseSearch(parts.slice(1).join(' '));
        if (!term) { console.log('Usage: e <string>  (prefix with = for exact match, e.g. e ="V")'); break; }
        searchExport(term, exact);
        break;
      }
      case 'c': {
        if (!lastSearch) { console.log('No previous search. Use s, S, R, or e first.'); break; }
        if (lastSearch.type === 's') searchOpForward(lastSearch.term);
        else if (lastSearch.type === 'S') searchStringForward(lastSearch.term, lastSearch.exact);
        else if (lastSearch.type === 'R') searchRawForward(lastSearch.term);
        else if (lastSearch.type === 'e') searchExport(lastSearch.term, lastSearch.exact);
        break;
      }
      case 'i': showCurrentInfo(); break;
      case 'hex': showBlockHex(); break;
      case 'h': showHelp(); break;
      case 'q':
        console.log('Bye.');
        reader.close();
        process.exit(0);
        break;
      default:
        // Allow raw number input to jump to block
        const maybeNum = parseInt(cmd, 10);
        if (!isNaN(maybeNum) && parts.length === 1) {
          goTo(maybeNum);
        } else {
          console.log(`Unknown command: ${cmd}. Type h for help.`);
        }
    }
    showPrompt();
  };

  rl.on('line', onLine);
  showPrompt();
}

main();
