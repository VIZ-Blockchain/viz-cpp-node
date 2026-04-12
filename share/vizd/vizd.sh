#!/bin/bash

export HOME="/var/lib/vizd"

VIZD="/usr/local/bin/vizd"

chown -R vizd:vizd $HOME

# seed nodes come from documentation/seednodes which is
# installed by docker into /etc/vizd/seednodes
SEED_NODES="$(cat /etc/vizd/seednodes | awk -F' ' '{print $1}')"

ARGS=""

# if user did not pass in any desired
# seed nodes, use the ones above:
if [[ -z "$VIZD_SEED_NODES" ]]; then
    for NODE in $SEED_NODES ; do
        ARGS+=" --p2p-seed-node=$NODE"
    done
fi

# if user did pass in desired seed nodes, use
# the ones the user specified:
if [[ ! -z "$VIZD_SEED_NODES" ]]; then
    for NODE in $VIZD_SEED_NODES ; do
        ARGS+=" --p2p-seed-node=$NODE"
    done
fi

if [[ ! -z "$VIZD_WITNESS_NAME" ]]; then
    ARGS+=" --witness=\"$VIZD_WITNESS_NAME\""
fi

if [[ ! -z "$VIZD_PRIVATE_KEY" ]]; then
    ARGS+=" --private-key=$VIZD_PRIVATE_KEY"
fi

# overwrite local config with image one
cp /etc/vizd/config.ini $HOME/config.ini

chown vizd:vizd $HOME/config.ini

if [[ ! -d $HOME/blockchain ]]; then
    if [[ -e /var/cache/vizd/blocks.tbz2 ]]; then
        # init with blockchain cached in image
        ARGS+=" --replay-blockchain"
        mkdir -p $HOME/blockchain/database
        cd $HOME/blockchain/database
        tar xvjpf /var/cache/vizd/blocks.tbz2
        chown -R vizd:vizd $HOME/blockchain
    fi
fi

# without --data-dir it uses cwd as datadir(!)
# who knows what else it dumps into current dir
cd $HOME

# slow down restart loop if flapping
sleep 1

if [[ ! -z "$VIZD_RPC_ENDPOINT" ]]; then
    RPC_ENDPOINT=$VIZD_RPC_ENDPOINT
else
    RPC_ENDPOINT="0.0.0.0:8090"
fi

if [[ ! -z "$VIZD_P2P_ENDPOINT" ]]; then
    P2P_ENDPOINT=$VIZD_P2P_ENDPOINT
else
    P2P_ENDPOINT="0.0.0.0:2001"
fi

# If --create-snapshot is in VIZD_EXTRA_OPTS, run vizd directly (not via exec)
# so we can shut down the container after snapshot creation instead of runit restarting it.
if echo "$VIZD_EXTRA_OPTS" | grep -q "\-\-create-snapshot"; then
    chpst -uvizd \
        $VIZD \
            --rpc-endpoint=${RPC_ENDPOINT} \
            --p2p-endpoint=${P2P_ENDPOINT} \
            --data-dir=$HOME \
            $ARGS \
            $VIZD_EXTRA_OPTS \
            2>&1
    echo "Snapshot created. Stopping container..."
    kill -TERM 1
    exit 0
fi

exec chpst -uvizd \
    $VIZD \
        --rpc-endpoint=${RPC_ENDPOINT} \
        --p2p-endpoint=${P2P_ENDPOINT} \
        --data-dir=$HOME \
        $ARGS \
        $VIZD_EXTRA_OPTS \
        2>&1
