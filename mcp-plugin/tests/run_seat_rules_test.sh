#!/usr/bin/env bash
# Builds and runs the seat rules tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/seat_rules_test.cpp" "$ROOT/MCPServer/src/McpSeatRules.cpp" -o "$OUT/seat_rules_test"
"$OUT/seat_rules_test"
