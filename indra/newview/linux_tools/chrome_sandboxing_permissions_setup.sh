#!/usr/bin/env sh

SCRIPT_DIR=$(dirname "$0")
SANDBOX_BIN="$SCRIPT_DIR/../bin/llplugin/chrome-sandbox"

chown root:root "$SANDBOX_BIN"
chmod 4755 "$SANDBOX_BIN"
