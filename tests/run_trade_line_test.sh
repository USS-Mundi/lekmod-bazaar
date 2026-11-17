#!/usr/bin/env bash
# Builds and runs the trade line tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/trade_line_test.cpp" "$ROOT/MCPServer/src/McpTradeLine.cpp" -o "$OUT/trade_line_test"
"$OUT/trade_line_test"
