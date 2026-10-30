# Node configuration templates

Starting points for `config.ini`. Copy one into your data directory and edit it;
every option is described in [docs/node/configuration.md](../../../docs/node/configuration.md).

| Template | Use it for |
|----------|-----------|
| `config.ini` | Public mainnet node: P2P on 2001, RPC on 0.0.0.0:8090/8091, all API plugins, history and snapshots. The production Docker image ships this file. |
| `config_witness.ini` | Validator (block producer): RPC bound to 127.0.0.1, adds `validator_guard`. Set `validator` and `private-key`. See [validator-node.md](../../../docs/node/validator-node.md). |
| `config_testnet.ini` | Test network with `testnet_plugin` and stale production enabled. The testnet Docker image ships this file. |
| `config_stock_exchange.ini` | Exchange or service node that only follows the chain and serves history: RPC bound to 127.0.0.1, no committee/invite/subscription APIs. |
| `config_debug.ini` | Local single-node chain with stale production and a well-known test key. Never use it on mainnet. |
