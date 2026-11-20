#!/usr/bin/env bash
# Builds and runs the city-state rules tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/city_state_rules_test.cpp" "$ROOT/MCPServer/src/McpCityStateRules.cpp" -o "$OUT/city_state_rules_test"
"$OUT/city_state_rules_test"
