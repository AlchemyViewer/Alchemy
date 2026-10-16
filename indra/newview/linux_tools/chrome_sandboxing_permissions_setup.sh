#!/usr/bin/env sh

# Makes CEF's sandbox helper setuid root, which the web browser takes as its
# cue to sandbox its renderers. Run as root.

set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this as root, with sudo." >&2
    exit 1
fi

SCRIPT_DIR=$(dirname -- "$(readlink -f -- "$0")")
SANDBOX_BIN="$SCRIPT_DIR/../bin/llplugin/chrome-sandbox"

chown root:root "$SANDBOX_BIN"
chmod 4755 "$SANDBOX_BIN"
