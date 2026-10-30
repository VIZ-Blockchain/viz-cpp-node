#!/bin/bash

export HOME="/var/lib/vizd"

VIZD="/usr/local/bin/vizd"

chown -R vizd:vizd $HOME

ARGS=""

if [[ ! -z "$VIZD_WITNESS_NAME" ]]; then
    ARGS+=" --witness=\"$VIZD_WITNESS_NAME\""
fi

if [[ ! -z "$VIZD_PRIVATE_KEY" ]]; then
    ARGS+=" --private-key=$VIZD_PRIVATE_KEY"
fi

# Config: ONE copy, shared with the mounted source through a symlink.
#
# node_admin applies edits atomically: canonicalize -> mkstemp next to the target -> fsync ->
# rename. `canonical` resolves symlinks, so a write through $HOME/config.ini lands in
# /etc/vizd/config.ini, i.e. straight in the mounted host source. Two conditions must hold:
#   * /etc/vizd is mounted as a DIRECTORY (rename over a file mount point fails with EBUSY);
#   * that directory is writable by the node user `vizd` (mkstemp lives next to the target).
# A regular file left by the old `cp` would shadow the link, so it is replaced once.
if [ -e "$HOME/config.ini" ] && [ ! -L "$HOME/config.ini" ]; then
    rm -f "$HOME/config.ini"
fi
ln -sfn /etc/vizd/config.ini "$HOME/config.ini"
chown -h vizd:vizd "$HOME/config.ini"

# Say it out loud at startup instead of leaving the operator to discover it from a rejected
# admin.config.apply: without write access here the node cannot create its temp file.
if ! chpst -uvizd test -w /etc/vizd; then
    echo "vizd.sh: WARNING: /etc/vizd is not writable by user vizd (uid $(id -u vizd)) — admin config apply will fail with EACCES. Chown the mounted config directory to that uid." >&2
fi

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
