#!/usr/bin/env bash
# Builds and runs the chronicle tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/chronicle_test.cpp" "$ROOT/MCPServer/src/McpChronicle.cpp" -o "$OUT/chronicle_test"
"$OUT/chronicle_test"
