# Transaction and block ids

VIZ inherited its id types from Graphene (BitShares, then Steem). In the source they are declared
as `fc::ripemd160`, and older documentation and libraries call them "ripemd160 ids". That is only
the name of the 20-byte container they are stored in: **neither id is a ripemd160 digest.**
The node declares them through the alias `id160_type` to say so.

| Value | How it is computed | Size |
|---|---|---|
| `transaction_id` | first 20 bytes of `sha256(packed transaction)`, signatures **not** included | 20 bytes |
| `block_id` | first 20 bytes of `sha224(packed block header)`, then bytes 0–3 replaced by the block number (big-endian) | 20 bytes |
| `transaction_merkle_root` | `ripemd160` of the root of a sha256 tree over the signed transactions | 20 bytes |

The merkle root is the one value that really is a ripemd160.

## Consequences for clients

* To compute a transaction id, serialize the transaction **without** `signatures`, take sha256 and
  keep the first 20 bytes. Do not run ripemd160 — the result would never match the node.
* The block number can be read straight from a block id: it is the first 4 bytes, big-endian.
  `ref_block_num` / `ref_block_prefix` (TaPoS) use the low 16 bits of that number and bytes 4–7 of
  the block id.
* The ids are 20 bytes in every API answer, in the block log and in account history. Changing the
  hash (to a real ripemd160 or to a full 32-byte sha256) would be a hardfork that splits history in
  two id formats, for no security gain: 160 bits is plenty for an identifier, and signatures are
  made over the full sha256 digest, not over the id.

Source: `transaction::id()` in `libraries/protocol/transaction.cpp`, `signed_block_header::id()`
and `signed_block::calculate_merkle_root()` in `libraries/protocol/block.cpp`, the types in
`libraries/protocol/include/graphene/protocol/types.hpp`.
