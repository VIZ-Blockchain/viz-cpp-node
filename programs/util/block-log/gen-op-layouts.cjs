#!/usr/bin/env node
// Regenerates op-layouts.json from viz-js-lib's serializer source, whose operation
// layouts are byte-verified against the node. Run after adding an operation:
//   node gen-op-layouts.cjs <viz-js-lib>/src/auth/serializer/src/operations.js
// The node itself is the source of truth for which of them are virtual:
//   grep "public virtual_operation" libraries/protocol/include/graphene/protocol/*.hpp
const fs = require('fs');
const path = require('path');
const FROM = 64; // 0..63 have hand-written readers in block-log-reader.cjs
const src = fs.readFileSync(process.argv[2], 'utf8');
const i = src.lastIndexOf('operation.st_operations = [');
const order = src.slice(i + 27, src.indexOf('];', i)).replace(/\/\/.*$/mg, '').split(',').map(x => x.trim()).filter(Boolean);

function body(name) {
  const m = new RegExp('const ' + name + ' = new Serializer\\(\\s*"[a-z_0-9]+",\\s*\\{').exec(src);
  if (!m) throw new Error('no serializer for ' + name);
  let depth = 1, j = m.index + m[0].length;
  const start = j;
  for (; depth; j++) {
    if (src[j] === '{') depth++;
    if (src[j] === '}') depth--;
  }
  return src.slice(start, j - 1);
}

function fields(b) {
  const out = [];
  let depth = 0, cur = '';
  for (const ch of b.replace(/\/\/.*$/mg, '')) {
    if ('([{'.includes(ch)) depth++;
    if (')]}'.includes(ch)) depth--;
    if (ch === ',' && !depth) { out.push(cur); cur = ''; } else cur += ch;
  }
  out.push(cur);
  return out.map(x => x.trim()).filter(Boolean).map(l => {
    const k = l.indexOf(':');
    return [l.slice(0, k).trim(), l.slice(k + 1).replace(/\s+/g, '').replace(/types\.nosort\(([^)]*)\)/, '$1')];
  });
}

const out = {};
order.forEach((name, id) => {
  if (id >= FROM) out[id] = { name, fields: fields(body(name)) };
});
fs.writeFileSync(path.join(__dirname, 'op-layouts.json'), JSON.stringify(out, null, 1) + '\n');
console.log(`op-layouts.json: ops ${FROM}..${order.length - 1}`);
