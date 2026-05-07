# Snapshot Pause Block Workflow

## Overview

When the snapshot plugin creates a snapshot, it **pauses P2P block processing** to
prevent concurrent database modifications.  During this pause, incoming blocks from
peers are **silently dropped** — they are not buffered.  After the pause ends, the
P2P layer detects the gap and fetches missing blocks from peers.  The witness plugin
must defer block production until the gap is filled to avoid producing competing
blocks on a stale head.

## Sequence Diagram: Snapshot Pause Lifecycle

```mermaid
sequenceDiagram
    participant SP as Snapshot Plugin
    participant P2P as P2P Layer
    participant W as Witness Plugin
    participant Peer as Remote Peer

    Note over SP: Block N applied
    SP->>SP: on_applied_block() triggers snapshot
    SP->>P2P: pause_block_processing()
    Note over P2P: _block_processing_paused = true

    Peer->>P2P: Block N+1 (from delegate)
    P2P-->>P2P: DROPPED (pause active)
    Peer->>P2P: Block N+2 (from delegate)
    P2P-->>P2P: DROPPED (pause active)

    Note over W: Production loop fires (250ms)
    W->>W: maybe_produce_block()
    Note over W: No gate blocks production<br/>if it was emergency master's turn

    SP->>SP: create_snapshot() completes
    SP->>P2P: resume_block_processing()
    Note over P2P: _block_processing_paused = false
    Note over P2P: _catchup_after_pause = true<br/>(peers are ahead)
    P2P->>Peer: gap fill / sync request
    Note over P2P: Returns immediately (async)

    Note over W: Production loop fires (250ms)
    W->>W: maybe_produce_block()
    W->>P2P: is_catching_up_after_pause()?
    P2P-->>W: true
    W-->>W: return not_synced (deferred)

    Peer->>P2P: Block N+1 (re-fetched)
    P2P->>P2P: accept_block() → applied
    Peer->>P2P: Block N+2 (re-fetched)
    P2P->>P2P: accept_block() → applied

    Note over P2P: transition_to_forward()
    Note over P2P: _catchup_after_pause = false

    Note over W: Production loop fires
    W->>P2P: is_catching_up_after_pause()?
    P2P-->>W: false
    W->>W: Normal production resumes
```

## Incoming Block Workflow (During Pause)

```mermaid
flowchart TD
    A["Block message received from peer"] --> B{"_block_processing_paused?"}
    B -->|Yes| C{"Message type?"}
    C -->|hello/hello_reply| D["Process normally"]
    C -->|block_reply, range_reply,<br/>gap_fill_reply, transaction| E["Silently drop<br/>(return true)"]
    B -->|No| F["Normal processing:<br/>accept_block() → push_block()"]
    F --> G{"Block accepted?"}
    G -->|ACCEPTED| H["on_block_applied()<br/>retransmit to fork peers"]
    G -->|FORK_DB_ONLY| I["Store in fork_db<br/>(competing fork)"]
    G -->|DEAD_FORK| J["Soft-ban peer"]
    G -->|REJECTED| K["Log + track rejections"]
```

## Witness Production Workflow (With Catchup Gate)

```mermaid
flowchart TD
    Start["maybe_produce_block()"] --> Sync{"DLT mode +<br/>chain.is_syncing()?"}
    Sync -->|Yes, not emergency master| Ret1["return not_synced"]
    Sync -->|No / emergency master| Gate{"p2p.is_catching_up_after_pause()?"}
    Gate -->|Yes| Ret2["return not_synced<br/>(defer production)"]
    Gate -->|No| HF12{"Hardfork 12 checks"}
    HF12 --> Slot{"get_slot_at_time()"}
    Slot -->|slot == 0| Ret3["return not_time_yet"]
    Slot -->|slot > 0| Witness{"Our witness scheduled?"}
    Witness -->|No| Ret4["return not_my_turn"]
    Witness -->|Yes| Stale{"scheduled_time <=<br/>head_block_time?"}
    Stale -->|Yes| Ret5["return not_time_yet<br/>(slot already filled)"]
    Stale -->|No| Fork{"Competing block in fork_db?"}
    Fork -->|Yes, weaker fork| Produce
    Fork -->|Yes, stronger fork| Ret6["return fork_collision"]
    Fork -->|No| Produce["generate_block()"]
    Produce --> Broadcast["p2p.broadcast_block()"]
    Broadcast --> Done["return produced"]
```

## Post-Pause Catchup State Machine

```mermaid
stateDiagram-v2
    [*] --> Normal: Node running
    Normal --> Paused: pause_block_processing()
    Paused --> Catchup: resume_block_processing()<br/>peers ahead → _catchup_after_pause=true
    Paused --> Normal: resume_block_processing()<br/>no gap detected
    Catchup --> SyncMode: transition_to_sync()<br/>or gap fill request
    SyncMode --> Normal: transition_to_forward()<br/>_catchup_after_pause=false
```

## Key Files

| File | Role |
|------|------|
| `libraries/network/dlt_p2p_node.cpp` | P2P block reception, pause/resume, catchup flag |
| `libraries/network/include/graphene/network/dlt_p2p_node.hpp` | `_catchup_after_pause` flag and getter |
| `plugins/p2p/p2p_plugin.cpp` | Exposes `is_catching_up_after_pause()` to other plugins |
| `plugins/p2p/include/graphene/plugins/p2p/p2p_plugin.hpp` | Public API declaration |
| `plugins/witness/witness.cpp` | Production gate that checks catchup flag |
| `plugins/snapshot/plugin.cpp` | Calls `pause/resume_block_processing()` |

## The Bug (Before Fix)

Without the catchup gate, the following race occurred on the emergency master:

1. Snapshot starts → P2P paused
2. Other delegates produce blocks N+1, N+2 → **dropped** by P2P
3. Snapshot finishes → `resume_block_processing()` requests gap fill → **returns immediately**
4. Witness production loop (250ms tick) → "My turn, fork_db empty at head+1" → **produces block N+1 with emergency key**
5. Gap fill response arrives with the real block N+1 from the delegate → **fork conflict**
6. Other nodes see two competing blocks → fork switch chaos

The fix adds a `_catchup_after_pause` flag that is set when `resume_block_processing()`
detects a gap, and cleared when `transition_to_forward()` confirms catchup.  The
witness plugin checks this flag before producing and defers if it is set.
