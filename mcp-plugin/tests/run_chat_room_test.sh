#!/usr/bin/env bash
# Builds and runs the Message Center chat tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/chat_room_test.cpp" "$ROOT/MCPServer/src/McpChatRoom.cpp" "$ROOT/MCPServer/src/McpFiles.cpp" -o "$OUT/chat_room_test"
"$OUT/chat_room_test"
