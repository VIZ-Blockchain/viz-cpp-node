# Block Log Tools

Node.js scripts in `programs/util/block-log/` read block files offline: the full `block_log`, a DLT node's `dlt_block_log`, and the range files of the [block archive plugin](../plugins/block-archive.md). No dependencies — Node.js 18+ is enough. The scripts use the `.cjs` extension because the repository root is an ES module.

| File | Purpose |
|------|---------|
| `block-archive.cjs` | CLI: archive summary, single block, search, export to JSONL |
| `block-log-viewer.cjs` | Interactive terminal viewer of one block file |
| `block-log-reader.cjs` | Parser library used by both (`BlockLogReader`, `DltBlockLogReader`, `readSignedBlock`) |
| `op-layouts.json` | Field layouts of operations 64+ |
| `gen-op-layouts.cjs` | Regenerates `op-layouts.json` from viz-js-lib |

---

## `block-archive.cjs`

```
node block-archive.cjs <command> <target> [options]
```

`<target>` is an archive directory, a node data dir with `dlt_block_log`, or a single `.log` / `dlt_block_log` file.

### `info`

Lists range files, their block ranges and block counts, and reports gaps:

```
$ node block-archive.cjs info /var/lib/vizd/block-archive
blocks-0083792402-0083792499.log  83792402-83792499  98 blocks
blocks-0083792500-0083792599.log  83792500-83792599  100 blocks
…
total 83792402-83792999
```

A `GAP a-b` line means blocks `a..b` are missing (see [Errors](../plugins/block-archive.md#errors)).

### `get`

Prints one block as JSON:

```bash
node block-archive.cjs get /var/lib/vizd/block-archive 83792500
```

### `search`

Prints one JSON line per matching operation:

```bash
node block-archive.cjs search /var/lib/vizd/block-archive --op=pm_place_bet,transfer --account=alice --from=83790000 --to=83799999
```

```json
{"block":83792517,"timestamp":"2026-09-28T21:14:03.000Z","tx":0,"op_in_tx":0,"type":"transfer","virtual":false,"data":{"from":"alice","to":"bob","amount":{…},"memo":"…"}}
```

### `export`

Same filters, written to a file or stdout as JSONL. Operations by default; `--blocks` exports whole blocks (with filters, only blocks that contain a matching operation):

```bash
node block-archive.cjs export /var/lib/vizd/block-archive --from=83790000 --to=83799999 --out=ops.jsonl
node block-archive.cjs export /var/lib/vizd/block-archive --blocks --out=blocks.jsonl
```

The number of written records goes to stderr, so stdout stays clean for pipes.

### Filters

| Option | Meaning |
|--------|---------|
| `--from=N`, `--to=N` | Block range (default: everything in the target) |
| `--op=a,b` | Operation names without the `_operation` suffix |
| `--account=name` | Any string field of the operation equal to the name (`from`, `to`, `account`, `author`, `creator`, …) |
| `--text=s` | Substring of the operation JSON |
| `--out=file` | Output file for `search` / `export` |
| `--blocks` | `export` whole blocks instead of operations |

Blocks are read sequentially and only the files overlapping `--from/--to` are opened, so narrow the range on large archives. Reference speed: about 4 500 blocks per second (50 000 testnet blocks in 11 s).

---

## `block-log-viewer.cjs`

Interactive viewer for a single file:

```bash
node block-log-viewer.cjs /var/lib/vizd/blockchain/block_log
node block-log-viewer.cjs /var/lib/vizd/blockchain/dlt_block_log --dlt
node block-log-viewer.cjs /var/lib/vizd/block-archive/blocks-0083790000-0083799999.log --dlt
```

Range files of the archive use the DLT layout, so open them with `--dlt`.

---

## Operation coverage

Operations 0–63 are decoded by hand-written readers. Operations 64+ (Prediction Markets, agent access, `set_reward_sharing`, …) are decoded from `op-layouts.json`, generated from viz-js-lib serializers that are byte-verified against the node. After a new operation is added to the node and to viz-js-lib, regenerate the file:

```bash
node programs/util/block-log/gen-op-layouts.cjs ../viz-js-lib/src/auth/serializer/src/operations.js
```

An unknown operation id makes the whole block unreadable — the byte stream cannot be skipped safely — so keep `op-layouts.json` in sync with the node.

Block header fields follow the node's JSON: `validator`, `validator_signature`.
