#!/usr/bin/env node
/**
 * VIZ Block Log Viewer - Interactive terminal UI
 * No external dependencies. Uses block-log-reader.js for parsing.
 *
 * Usage: node block-log-viewer.js <block_log_path> [--dlt]
 */

const fs = require('fs');
const path = require('path');
const readline = require('readline');

const {
  createBlockLogReader,
  getBlockNum,
  blockIdToHex,
  OPERATION_TYPES
} = require('./block-log-reader');

// ============================================================================
// State
// ============================================================================

let reader = null;
let currentBlockNum = 0;
let startBlock = 1;
let endBlock = 0;

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
 */
function bitmaskScan() {
  const total = endBlock - startBlock + 1;
  const byteLen = Math.ceil(total / 8);
  const buf = Buffer.alloc(byteLen); // all zeros

  console.log(`  Scanning ${total} blocks for non-free operations...`);
  let nonFreeCount = 0;
  let lastPct = -1;

  for (let num = startBlock; num <= endBlock; num++) {
    const block = reader.readBlockByNum(num);
    if (block && hasNonFreeOps(block)) {
      bitmaskSet(buf, startBlock, num);
      nonFreeCount++;
    }
    const pct = Math.floor(((num - startBlock) / total) * 100);
    if (pct !== lastPct && pct % 5 === 0) {
      lastPct = pct;
      process.stdout.write(`\r  Scanning... ${pct}% (#${num}, ${nonFreeCount} with ops)      `);
    }
  }

  // Write file
  const p = bitmaskPath();
  const hdr = Buffer.allocUnsafe(16);
  hdr.writeBigUInt64LE(BigInt(startBlock), 0);
  hdr.writeBigUInt64LE(BigInt(endBlock), 8);

  const fd = fs.openSync(p, 'w');
  fs.writeSync(fd, hdr, 0, 16, 0);
  fs.writeSync(fd, buf, 0, byteLen, 16);
  fs.closeSync(fd);

  bitmask = buf;
  bitmaskStart = startBlock;
  bitmaskEnd = endBlock;

  const empty = total - nonFreeCount;
  const sizeKB = ((16 + byteLen) / 1024).toFixed(1);
  console.log(`\r  Done. ${nonFreeCount} with ops, ${empty} empty. Saved ${sizeKB} KB to ${path.basename(p)}      `);
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
  console.log('  e <string> - Export all ops containing string to search_export_<ts>.json');
  console.log('  scan       - Scan all blocks, build & save bitmask for fast nav');
  console.log('  i          - Show block info (header only)');
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

function goTo(num) {
  num = Math.max(startBlock, Math.min(endBlock, num));
  const block = reader.readBlockByNum(num);
  if (!block) {
    console.log(`Block #${num} not found.`);
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
    const block = reader.readBlockByNum(next);
    if (block) showBlock(block);
    return;
  }

  // Slow fallback: scan by reading each block
  for (let num = currentBlockNum + 1; num <= endBlock; num++) {
    const block = reader.readBlockByNum(num);
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
    const block = reader.readBlockByNum(prev);
    if (block) showBlock(block);
    return;
  }

  // Slow fallback
  for (let num = currentBlockNum - 1; num >= startBlock; num--) {
    const block = reader.readBlockByNum(num);
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

function searchOpForward(name) {
  name = name.toLowerCase();
  console.log(`  Searching for "${name}" forward from #${currentBlockNum + 1}...`);

  for (let num = currentBlockNum + 1; num <= endBlock; num++) {
    // Fast skip: if bitmask says this block is empty, no need to read it
    if (bitmaskValid() && bitmaskGet(num) === 0) continue;

    const block = reader.readBlockByNum(num);
    if (block) {
      const ops = collectOps(block);
      if (ops.some(o => o.typeName.toLowerCase().includes(name))) {
        currentBlockNum = num;
        showBlock(block);
        showOps(block, name);
        return;
      }
    }
    if ((num - currentBlockNum) % 1000 === 0) {
      process.stdout.write(`\r  Scanning... #${num}      `);
    }
  }
  console.log('\r  No matching operations found.      ');
}

// ============================================================================
// String Search in Operation JSON (including virtual ops)
// ============================================================================

/**
 * Serialize op data to a searchable JSON string (Buffers -> hex, BigInt -> string)
 */
function opToJsonString(op) {
  try {
    return JSON.stringify({ typeName: op.typeName, isVirtual: op.isVirtual, data: op.data }, (key, val) => {
      if (val && val.type === 'Buffer' && Array.isArray(val.data)) return Buffer.from(val.data).toString('hex');
      if (Buffer.isBuffer(val)) return val.toString('hex');
      if (typeof val === 'bigint') return val.toString();
      return val;
    });
  } catch (e) {
    return '';
  }
}

/**
 * Check if any operation in a block contains the search string in its JSON
 * Returns matching operations array (empty if none)
 */
function findOpsByString(block, searchStr) {
  const lower = searchStr.toLowerCase();
  const ops = collectOps(block);
  const matched = [];
  for (const op of ops) {
    const json = opToJsonString(op);
    if (json.toLowerCase().includes(lower)) {
      matched.push(op);
    }
  }
  return matched;
}

/**
 * Search forward for next block containing string in any operation's JSON
 */
function searchStringForward(str) {
  console.log(`  Searching for "${str}" in op JSON forward from #${currentBlockNum + 1}...`);

  for (let num = currentBlockNum + 1; num <= endBlock; num++) {
    // bitmask can't help here - string might be in virtual ops too
    const block = reader.readBlockByNum(num);
    if (block) {
      const matched = findOpsByString(block, str);
      if (matched.length > 0) {
        currentBlockNum = num;
        showBlock(block);
        // Show only matching ops
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
        return;
      }
    }
    if ((num - currentBlockNum) % 1000 === 0) {
      process.stdout.write(`\r  Scanning... #${num}      `);
    }
  }
  console.log('\r  No matching operations found.      ');
}

/**
 * Search all blocks and export matching operations to JSON file
 */
function searchExport(str) {
  console.log(`  Exporting all operations containing "${str}" from #${startBlock} to #${endBlock}...`);

  const results = [];
  let matchCount = 0;
  let blockCount = 0;
  const total = endBlock - startBlock + 1;
  let lastPct = -1;

  for (let num = startBlock; num <= endBlock; num++) {
    const block = reader.readBlockByNum(num);
    if (block) {
      const matched = findOpsByString(block, str);
      if (matched.length > 0) {
        blockCount++;
        for (const op of matched) {
          matchCount++;
          results.push({
            block: num,
            timestamp: formatTimestamp(block.timestamp),
            witness: block.witness,
            typeId: op.typeId,
            typeName: op.typeName,
            isVirtual: op.isVirtual,
            data: op.data
          });
        }
      }
    }
    const pct = Math.floor(((num - startBlock) / total) * 100);
    if (pct !== lastPct && pct % 5 === 0) {
      lastPct = pct;
      process.stdout.write(`\r  Scanning... ${pct}% (#${num}, ${matchCount} ops in ${blockCount} blocks)      `);
    }
  }

  if (matchCount === 0) {
    console.log('\r  No matching operations found.                                          ');
    return;
  }

  // Serialize with Buffer/bigint handling
  const json = JSON.stringify(results, (key, val) => {
    if (val && val.type === 'Buffer' && Array.isArray(val.data)) return Buffer.from(val.data).toString('hex');
    if (Buffer.isBuffer(val)) return val.toString('hex');
    if (typeof val === 'bigint') return val.toString();
    return val;
  }, 2);

  const unixTime = Math.floor(Date.now() / 1000);
  const outPath = path.join(path.dirname(reader.dataPath), `search_export_${unixTime}.json`);
  fs.writeFileSync(outPath, json, 'utf8');

  const sizeKB = (Buffer.byteLength(json, 'utf8') / 1024).toFixed(1);
  console.log(`\r  Done. ${matchCount} ops in ${blockCount} blocks. Saved ${sizeKB} KB to ${path.basename(outPath)}      `);
}

function showCurrentOps(filter) {
  const block = reader.readBlockByNum(currentBlockNum);
  if (!block) { console.log('No current block.'); return; }
  showOps(block, filter || null);
}

function showCurrentInfo() {
  const block = reader.readBlockByNum(currentBlockNum);
  if (!block) { console.log('No current block.'); return; }
  showBlock(block);
}

// ============================================================================
// Main Loop
// ============================================================================

function main() {
  const args = process.argv.slice(2);
  if (args.length < 1) {
    console.log('Usage: node block-log-viewer.js <block_log_path> [--dlt]');
    console.log('');
    console.log('Options:');
    console.log('  --dlt    Use DLT (rolling) block log reader');
    process.exit(1);
  }

  const dataPath = args.find(a => !a.startsWith('--'));
  const isDlt = args.includes('--dlt');

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
    console.log('Block log appears empty (no head block found).');
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
        const str = parts.slice(1).join(' ');
        if (!str) { console.log('Usage: S <string>'); break; }
        searchStringForward(str);
        break;
      }
      case 'e': {
        const str = parts.slice(1).join(' ');
        if (!str) { console.log('Usage: e <string>'); break; }
        searchExport(str);
        break;
      }
      case 'i': showCurrentInfo(); break;
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
