#!/usr/bin/env bash
# Drives SecondLeaderScripts.lua against stand-ins for the game and LEKMOD's installed civ scripts (LuaJIT).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
LEK=${LEKMOD_LUA:-"$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/Program Files (x86)/Steam/steamapps/common/Sid Meier's Civilization V/Assets/DLC/MP_MODSPACK/Lua"}
[ -f "$LEK/Civilizations/Lekmod_uae.lua" ] || { echo "no LEKMOD civ scripts at $LEK; set LEKMOD_LUA" >&2; exit 1; }
luajit "$HERE/scripts_test.lua" "$HERE/../mod/SecondLeaderScripts.lua" "$LEK"
