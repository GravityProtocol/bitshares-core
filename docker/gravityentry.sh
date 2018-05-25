#!/bin/bash
GRAVITYD="/usr/local/bin/witness_node"

# For blockchain download
VERSION=`cat /etc/gravity/version`

## Supported Environmental Variables
#
#   * $GRAVITYD_SEED_NODES
#   * $GRAVITYD_RPC_ENDPOINT
#   * $GRAVITYD_PLUGINS
#   * $GRAVITYD_REPLAY
#   * $GRAVITYD_RESYNC
#   * $GRAVITYD_P2P_ENDPOINT
#   * $GRAVITYD_WITNESS_ID
#   * $GRAVITYD_PRIVATE_KEY
#   * $GRAVITYD_TRACK_ACCOUNTS
#   * $GRAVITYD_PARTIAL_OPERATIONS
#   * $GRAVITYD_MAX_OPS_PER_ACCOUNT
#   * $GRAVITYD_ES_NODE_URL
#   * $GRAVITYD_TRUSTED_NODE
#

ARGS=""
# Translate environmental variables
if [[ ! -z "$GRAVITYD_SEED_NODES" ]]; then
    for NODE in $GRAVITYD_SEED_NODES ; do
        ARGS+=" --seed-node=$NODE"
    done
fi
if [[ ! -z "$GRAVITYD_RPC_ENDPOINT" ]]; then
    ARGS+=" --rpc-endpoint=${GRAVITYD_RPC_ENDPOINT}"
fi

if [[ ! -z "$GRAVITYD_PLUGINS" ]]; then
    ARGS+=" --plugins=\"${GRAVITYD_PLUGINS}\""
fi

if [[ ! -z "$GRAVITYD_REPLAY" ]]; then
    ARGS+=" --replay-blockchain"
fi

if [[ ! -z "$GRAVITYD_RESYNC" ]]; then
    ARGS+=" --resync-blockchain"
fi

if [[ ! -z "$GRAVITYD_P2P_ENDPOINT" ]]; then
    ARGS+=" --p2p-endpoint=${GRAVITYD_P2P_ENDPOINT}"
fi

if [[ ! -z "$GRAVITYD_WITNESS_ID" ]]; then
    ARGS+=" --witness-id=$GRAVITYD_WITNESS_ID"
fi

if [[ ! -z "$GRAVITYD_PRIVATE_KEY" ]]; then
    ARGS+=" --private-key=$GRAVITYD_PRIVATE_KEY"
fi

if [[ ! -z "$GRAVITYD_TRACK_ACCOUNTS" ]]; then
    for ACCOUNT in $GRAVITYD_TRACK_ACCOUNTS ; do
        ARGS+=" --track-account=$ACCOUNT"
    done
fi

if [[ ! -z "$GRAVITYD_PARTIAL_OPERATIONS" ]]; then
    ARGS+=" --partial-operations=${GRAVITYD_PARTIAL_OPERATIONS}"
fi

if [[ ! -z "$GRAVITYD_MAX_OPS_PER_ACCOUNT" ]]; then
    ARGS+=" --max-ops-per-account=${GRAVITYD_MAX_OPS_PER_ACCOUNT}"
fi

if [[ ! -z "$GRAVITYD_ES_NODE_URL" ]]; then
    ARGS+=" --elasticsearch-node-url=${GRAVITYD_ES_NODE_URL}"
fi

if [[ ! -z "$GRAVITYD_TRUSTED_NODE" ]]; then
    ARGS+=" --trusted-node=${GRAVITYD_TRUSTED_NODE}"
fi

## Link the gravity config file into home
## This link has been created in Dockerfile, already
[ ! -f /var/lib/gravity/config.ini ] && cp -v /etc/gravity/config.ini /var/lib/gravity

$GRAVITYD --data-dir ${HOME} ${ARGS} ${GRAVITYD_ARGS}
