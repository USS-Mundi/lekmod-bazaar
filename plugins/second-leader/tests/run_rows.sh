#!/usr/bin/env bash
# Drives SecondLeaderRows.lua against a stand-in GameInfo and LEKMOD's installed TechButtonInclude.lua (LuaJIT).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
LEK=${LEKMOD_LUA:-"$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/Program Files (x86)/Steam/steamapps/common/Sid Meier's Civilization V/Assets/DLC/MP_MODSPACK/Lua"}
[ -f "$LEK/UI/TechButtonInclude.lua" ] || { echo "no LEKMOD UI scripts at $LEK; set LEKMOD_LUA" >&2; exit 1; }
luajit "$HERE/rows_test.lua" "$HERE/../src/SecondLeaderRows.lua" "$LEK"
