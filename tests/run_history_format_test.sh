#!/usr/bin/env bash
# Builds and runs the history record's pure tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/history_format_test.cpp" "$ROOT/MCPServer/src/McpHistoryFormat.cpp" -o "$OUT/history_format_test"
"$OUT/history_format_test"
