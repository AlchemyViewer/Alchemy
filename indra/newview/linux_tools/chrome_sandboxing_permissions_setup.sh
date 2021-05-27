#!/usr/bin/env bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
SANDBOX_BIN="$SCRIPT_DIR/../bin/llplugin/chrome-sandbox"

chown root:root $SANDBOX_BIN
chmod 4755 $SANDBOX_BIN
