const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const cli = process.env.ARCHIVE_CLI || path.resolve(__dirname, '../../programs/util/block-log/block-archive.cjs');
function u64(n) { const b=Buffer.alloc(8); b.writeBigUInt64LE(BigInt(n)); return b; }
function range(dir, first, last) {
  const log=[], index=[u64(first)];
  for(let n=first;n<=last;n++) {
    const offset=(n-first)*12, row=Buffer.alloc(4); row.writeUInt32LE(n);
    log.push(row,u64(offset)); index.push(u64(offset));
  }
  const file=path.join(dir,`blocks-${String(first).padStart(10,'0')}-${String(last).padStart(10,'0')}.log`);
  fs.writeFileSync(file,Buffer.concat(log)); fs.writeFileSync(file+'.index',Buffer.concat(index));
}
function fixture(fn) {
  const dir=fs.mkdtempSync(path.join(process.env.TMPDIR||os.tmpdir(),'viz-archive-'));
  try { fn(dir,path.join(dir,'merged')); } finally { fs.rmSync(dir,{recursive:true,force:true}); }
}
function merge(dir,out,extra=[]) { return spawnSync(process.execPath,[cli,'merge',dir,'--out='+out,...extra],{encoding:'utf8'}); }
function verify(out,first,last) {
  const log=fs.readFileSync(out), ix=fs.readFileSync(out+'.index');
  assert.equal(log.length,(last-first+1)*12); assert.equal(ix.length,8+(last-first+1)*8);
  assert.equal(Number(ix.readBigUInt64LE(0)),first);
  for(let n=first;n<=last;n++) { const p=(n-first)*12;
    assert.equal(log.readUInt32LE(p),n); assert.equal(Number(log.readBigUInt64LE(p+4)),p);
    assert.equal(Number(ix.readBigUInt64LE(8+(n-first)*8)),p);
  }
}
test('nested overlap cannot rewind either cursor',()=>fixture((d,o)=>{
  range(d,1,10); range(d,2,3); range(d,4,12);
  const r=merge(d,o,['--from=1','--to=12']); assert.equal(r.status,0,r.stderr); verify(o,1,12);
}));
test('fully covered tail keeps default upper bound',()=>fixture((d,o)=>{
  range(d,1,12); range(d,2,3);
  const r=merge(d,o); assert.equal(r.status,0,r.stderr); verify(o,1,12);
}));
test('equal starts and clipped interval are merged once',()=>fixture((d,o)=>{
  range(d,1,4); range(d,1,10); range(d,4,12);
  const r=merge(d,o,['--from=3','--to=9']); assert.equal(r.status,0,r.stderr); verify(o,3,9);
}));
test('real gap fails before creating output',()=>fixture((d,o)=>{
  range(d,1,3); range(d,5,7);
  const r=merge(d,o,['--to=7']); assert.notEqual(r.status,0); assert.match(r.stderr,/GAP/); assert.equal(fs.existsSync(o),false);
}));
test('existing output is preserved',()=>fixture((d,o)=>{
  range(d,1,3); fs.writeFileSync(o,'keep');
  const r=merge(d,o); assert.notEqual(r.status,0); assert.match(r.stderr,/exists, refusing/);
  assert.equal(fs.readFileSync(o,'utf8'),'keep');
}));
