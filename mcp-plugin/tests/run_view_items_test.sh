#!/usr/bin/env bash
# Builds and runs the turn-view item tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/view_items_test.cpp" "$ROOT/MCPServer/src/McpViewItems.cpp" "$ROOT/MCPServer/src/McpTurnCycle.cpp" -o "$OUT/view_items_test"
"$OUT/view_items_test"
