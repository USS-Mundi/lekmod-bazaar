#!/usr/bin/env bash
# Drives the Message Center window script against stand-ins for the game (LuaJIT).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
luajit "$ROOT/tests/message_center_ui_test.lua" "$ROOT/MCPServer/mod/MessageCenter.lua"
