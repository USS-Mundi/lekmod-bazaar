#!/usr/bin/env bash
# Builds and runs the map page tests natively (C++03, as the game's compiler requires), after checking the pages' .inc
# is current.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
python3 "$ROOT/tools/gen_map_pages.py" --check
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/map_pages_test.cpp" "$ROOT/MCPServer/src/McpMapPages.cpp" -o "$OUT/map_pages_test"
"$OUT/map_pages_test"
