# Block Archive Plugin

`block_archive` is an **optional, non-consensus** plugin that keeps a permanent copy of every irreversible block on disk, split into fixed-size range files. It exists because a DLT node's `dlt_block_log` holds only a rolling window (`dlt-block-log-max-blocks`, 100 000 blocks by default) and drops older blocks.

The plugin does not change consensus, does not need a hardfork and does not affect block processing: nodes with and without it are fully compatible.

**Source:** [plugins/block_archive/plugin.cpp](../../plugins/block_archive/plugin.cpp)

---

## Dependencies

```
chain::plugin
```

---

## Enabling

The plugin is off by default. Add to `config.ini` (**above** the `[logger.*]` sections — lines appended at the end of the file land inside the last section and are silently ignored):

```ini
plugin = block_archive
block-archive-dir = block-archive
block-archive-range = 10000
```

| Option | Default | Description |
|--------|---------|-------------|
| `block-archive-dir` | `block-archive` | Archive directory. A relative path is resolved against the node data dir. |
| `block-archive-range` | `10000` | Blocks per file. Minimum 100. Changing it for an existing archive is not supported — use a new directory. |

On startup the plugin logs its settings and cursor:

```
block_archive: /var/lib/vizd/block-archive, range 10000, last archived block 83792999
```

---

## How It Works

1. After every applied block the plugin compares its cursor (the last archived block) with the last irreversible block (LIB).
2. Every block from `cursor + 1` up to LIB is read from the node's block log and appended to the file of its range. Only irreversible blocks are archived, so the archive never contains a block that can be undone by a fork switch.
3. Range `k` covers blocks `[k × N, k × N + N − 1]`, where `N` is `block-archive-range`. The range in progress is written to `<dir>/partial/blocks-<range start>`.
4. When the last block of a range is archived, the file is **sealed**: moved to the top directory under its final name. Files in the top directory are complete and never change again.

```
block-archive/
├── blocks-0083790000-0083799999.log         ← sealed, immutable
├── blocks-0083790000-0083799999.log.index
├── blocks-0083800000-0083809999.log
├── blocks-0083800000-0083809999.log.index
└── partial/
    ├── blocks-0083810000                    ← range in progress
    └── blocks-0083810000.index
```

### File format

Each range is a pair in the `dlt_block_log` layout (see [Block Log](../storage/block-log.md)):

- `.log` — serialized `signed_block` records, each followed by its 8-byte start offset;
- `.log.index` — an 8-byte header with the first block number, then one 8-byte offset per block.

Block `n` is read with a single seek: `offset = index[8 + (n − first) × 8]`. Numbers in file names are zero-padded to 10 digits, so an alphabetical listing is also a block order.

### First file and start point

The archive starts from the LIB at the moment the plugin is first enabled — **older blocks are not backfilled**. `<first>` in a file name is the first block actually stored, so the first file of a node started from a snapshot is shorter than `N` (for example `blocks-0083792402-0083792499`).

### Restarts

The cursor is restored from disk: the highest sealed file plus the head of the file in `partial/`. After a restart or a crash the plugin continues with the next block, without gaps or duplicates. A partial file for a range that is already sealed is removed as stale.

### Errors

The archive is a side copy and must never break the node:

- any error while writing stops archiving with an `elog` line (`block_archive: …, archiving stopped`); the node keeps running and applying blocks;
- if the next block is no longer readable (the node was offline longer than the `dlt_block_log` window, or the plugin was re-enabled after a long pause), archiving stops instead of writing a hole. It stays stopped after a restart, because the cursor is on disk. To continue, either fill the gap from another archive or move the directory away and start a new one.

Watch the node log for `archiving stopped`.

---

## Reading the Archive

Use the tools in `programs/util/block-log/` — see [Block Log Tools](../storage/block-log-tools.md):

```bash
node programs/util/block-log/block-archive.cjs info   /var/lib/vizd/block-archive
node programs/util/block-log/block-archive.cjs search /var/lib/vizd/block-archive --op=transfer --account=alice
```

Sealed files are immutable, so they can be copied, compressed, backed up or served while the node is running. Do not read or move files in `partial/`.

---

## Disk Usage

The archive grows with the chain and is never pruned by the node. Size depends on block contents: estimate it by the size of your `dlt_block_log` divided by its window and multiplied by the number of blocks you plan to keep. Old sealed files can be moved to cold storage freely — the plugin only looks at the highest sealed file and `partial/`.
