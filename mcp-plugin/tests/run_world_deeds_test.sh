#!/usr/bin/env bash
# Builds and runs the world deeds tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/world_deeds_test.cpp" "$ROOT/MCPServer/src/McpWorldDeeds.cpp" "$ROOT/MCPServer/src/McpSeedCompose.cpp" -o "$OUT/world_deeds_test"
"$OUT/world_deeds_test"
