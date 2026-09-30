#!/usr/bin/env node
/**
 * Reads the block_archive plugin output: <dir>/blocks-<first>-<last>.log (+ .log.index),
 * dlt_block_log layout, one file per range. Also accepts a single dlt_block_log or range file.
 *
 *   node block-archive.cjs info   <dir>
 *   node block-archive.cjs get    <dir> <block>
 *   node block-archive.cjs search <dir> [filters]   one JSON line per matching operation
 *   node block-archive.cjs export <dir> [filters] [--blocks] [--out=file.jsonl]
 *   node block-archive.cjs merge  <dir> --out=<data>/blockchain/dlt_block_log [--from=N] [--to=N]
 *
 * merge glues range files into one dlt_block_log (<out> + <out>.index, the names vizd opens) byte for byte,
 * rebasing offsets, so a stopped node can serve the history to peers. Refuses gaps and
 * existing output files. It does not rebuild state: that still comes from a snapshot/seeds.
 *
 * Filters: --from=N --to=N --op=pm_place_bet[,transfer] --account=alice --text=substring
 * --account matches any string field equal to the name (account, from, to, author, ...).
 * export writes operations by default, --blocks writes whole blocks (filters then select blocks).
 * No dependencies besides block-log-reader.cjs.
 */
const fs = require('fs');
const path = require('path');
const { DltBlockLogReader } = require('./block-log-reader.cjs');

const RANGE_RE = /^blocks-(\d{10})-(\d{10})\.log$/;

function listFiles(target) {
  const st = fs.statSync(target);
  if (st.isFile()) {
    const r = new DltBlockLogReader();
    r.open(target);
    const f = { path: target, first: r.getStartBlockNum(), last: r._headBlockNum };
    r.close();
    return [f];
  }
  const files = fs.readdirSync(target).map(n => RANGE_RE.exec(n)).filter(Boolean)
    .map(m => ({ path: path.join(target, m[0]), first: Number(m[1]), last: Number(m[2]) }))
    .sort((a, b) => a.first - b.first);
  if (!files.length && fs.existsSync(path.join(target, 'dlt_block_log'))) {
    return listFiles(path.join(target, 'dlt_block_log'));
  }
  return files;
}

function args(argv) {
  const o = { _: [] };
  for (const a of argv) {
    const m = /^--([a-z]+)(?:=(.*))?$/.exec(a);
    if (m) o[m[1]] = m[2] === undefined ? true : m[2]; else o._.push(a);
  }
  return o;
}

function json(v) {
  return JSON.stringify(v, function (k, x) {
    if (typeof x === 'bigint') return x.toString();
    // Buffer.toJSON runs before the replacer, so raw bytes arrive as {type:'Buffer'}
    if (x && x.type === 'Buffer' && Array.isArray(x.data)) return Buffer.from(x.data).toString('hex');
    return x;
  });
}

function opName(op) {
  return op.typeName.replace(/_operation$/, '');
}

function mentions(v, name) {
  if (typeof v === 'string') return v === name;
  if (v && typeof v === 'object') return Object.values(v).some(x => mentions(x, name));
  return false;
}

function* blocks(files, from, to) {
  for (const f of files) {
    if (f.last < from || f.first > to) continue;
    const r = new DltBlockLogReader();
    r.open(f.path);
    try {
      for (let n = Math.max(from, f.first); n <= Math.min(to, f.last); n++) {
        const b = r.readBlockByNum(n);
        if (b) yield [n, b];
      }
    } finally {
      r.close();
    }
  }
}

function* ops(b) {
  for (let t = 0; t < b.transactions.length; t++) {
    const tx = b.transactions[t];
    for (let i = 0; i < tx.operations.length; i++) yield [t, i, tx.operations[i]];
  }
}

function matcher(o) {
  const types = o.op ? new Set(String(o.op).split(',')) : null;
  return op => (!types || types.has(opName(op)))
    && (!o.account || mentions(op.data, o.account))
    && (!o.text || json(op.data).includes(o.text));
}

function u64(n) {
  const b = Buffer.alloc(8);
  b.writeBigUInt64LE(BigInt(n));
  return b;
}

function merge(files, from, to, out) {
  if (!out) { console.error('merge needs --out=<data>/blockchain/dlt_block_log'); process.exit(2); }
  const logPath = out, idxPath = out + '.index';
  for (const p of [logPath, idxPath]) {
    if (fs.existsSync(p)) { console.error(p + ' exists, refusing to overwrite'); process.exit(1); }
  }
  const sel = files.filter(f => f.last >= from && f.first <= to);
  let next = from;
  for (const f of sel) {
    if (f.first > next) { console.error(`GAP ${next}-${f.first - 1}, merge stopped`); process.exit(1); }
    next = Math.min(to, f.last) + 1;
  }
  if (!sel.length || next <= to) { console.error(`archive does not cover ${from}-${to}`); process.exit(1); }
  const log = fs.openSync(logPath, 'wx'), idx = fs.openSync(idxPath, 'wx');
  fs.writeSync(idx, u64(from));
  let pos = 0, count = 0;
  for (const f of sel) {
    const src = fs.openSync(f.path, 'r');
    const ix = fs.readFileSync(f.path + '.index');
    const size = fs.fstatSync(src).size;
    const a = Math.max(from, f.first), z = Math.min(to, f.last);
    for (let n = a; n <= z; n++) {
      const i = 8 + (n - f.first) * 8;
      const start = Number(ix.readBigUInt64LE(i));
      const end = i + 8 < ix.length ? Number(ix.readBigUInt64LE(i + 8)) : size;
      const buf = Buffer.alloc(end - start - 8);
      fs.readSync(src, buf, 0, buf.length, start);
      fs.writeSync(log, buf);
      fs.writeSync(log, u64(pos));
      fs.writeSync(idx, u64(pos));
      pos += buf.length + 8;
      count++;
    }
    fs.closeSync(src);
  }
  fs.closeSync(log);
  fs.closeSync(idx);
  console.error(`merged ${count} blocks ${from}-${to} into ${logPath}`);
}

function main() {
  process.stdout.on('error', e => process.exit(e.code === 'EPIPE' ? 0 : 1));
  const o = args(process.argv.slice(2));
  const [cmd, target, num] = o._;
  if (!cmd || !target) {
    console.error(fs.readFileSync(__filename, 'utf8').split('*/')[0]);
    process.exit(2);
  }
  const files = listFiles(target);
  if (!files.length) {
    console.error('no range files in ' + target);
    process.exit(1);
  }
  const from = o.from ? Number(o.from) : files[0].first;
  const to = o.to ? Number(o.to) : files[files.length - 1].last;

  if (cmd === 'info') {
    let prev = null;
    for (const f of files) {
      if (prev !== null && f.first !== prev + 1) console.log(`GAP ${prev + 1}-${f.first - 1}`);
      console.log(`${path.basename(f.path)}  ${f.first}-${f.last}  ${f.last - f.first + 1} blocks`);
      prev = f.last;
    }
    console.log(`total ${files[0].first}-${prev}`);
    return;
  }
  if (cmd === 'get') {
    const n = Number(num);
    for (const [, b] of blocks(files, n, n)) return console.log(json(b));
    console.error(`block ${n} is not in the archive`);
    process.exit(1);
  }
  if (cmd === 'merge') return merge(files, from, to, o.out);
  if (cmd === 'search' || cmd === 'export') {
    const match = matcher(o);
    const out = o.out ? fs.createWriteStream(o.out) : process.stdout;
    let count = 0;
    for (const [n, b] of blocks(files, from, to)) {
      if (cmd === 'export' && o.blocks) {
        if ((!o.op && !o.account && !o.text) || [...ops(b)].some(([, , op]) => match(op))) {
          out.write(json({ block: n, ...b }) + '\n');
          count++;
        }
        continue;
      }
      for (const [t, i, op] of ops(b)) {
        if (!match(op)) continue;
        out.write(json({ block: n, timestamp: b.timestamp, tx: t, op_in_tx: i, type: opName(op), virtual: op.isVirtual, data: op.data }) + '\n');
        count++;
      }
    }
    if (o.out) out.end();
    console.error(`${count} ${cmd === 'export' && o.blocks ? 'blocks' : 'operations'} (${from}-${to})`);
    return;
  }
  console.error('unknown command ' + cmd);
  process.exit(2);
}

main();
