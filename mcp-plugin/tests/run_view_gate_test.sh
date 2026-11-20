#!/usr/bin/env bash
# Builds and runs the turn view gate tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/view_gate_test.cpp" "$ROOT/MCPServer/src/McpViewGate.cpp" -o "$OUT/view_gate_test"
"$OUT/view_gate_test"
