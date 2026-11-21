#!/usr/bin/env bash
# Builds and runs the agent launcher tests natively (C++03, as the game's compiler requires). The scripts it renders
# for a Mac run under /bin/sh with stand-ins for claude, curl and open.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++03 -Wall -Wextra -Werror -I "$ROOT/MCPServer/src" \
	"$ROOT/tests/agent_desk_test.cpp" "$ROOT/MCPServer/src/McpAgentDesk.cpp" "$ROOT/MCPServer/src/McpFiles.cpp" -o "$OUT/agent_desk_test"
"$OUT/agent_desk_test"
