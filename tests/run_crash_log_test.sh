#!/usr/bin/env bash
# Builds and runs the crash log's game-free tests natively (C++03, as the game's compiler requires).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" "$ROOT/tests/crash_log_test.cpp" -o "$OUT/crash_log_test"
"$OUT/crash_log_test"
