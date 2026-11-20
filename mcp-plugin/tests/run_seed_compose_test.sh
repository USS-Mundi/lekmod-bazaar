#!/usr/bin/env bash
# Builds and runs the page dealer tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" -I "$ROOT/tests" \
	"$ROOT/tests/seed_compose_test.cpp" "$ROOT/MCPServer/src/McpSeedCompose.cpp" -o "$OUT/seed_compose_test"
"$OUT/seed_compose_test"
