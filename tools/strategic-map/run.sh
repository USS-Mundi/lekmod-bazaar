#!/usr/bin/env bash
# Builds the harness with the game's compiler under Wine and draws a fixture: run.sh <fixture> <out.png>
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../MCPServer/src" && pwd)
PREFIX=~/repos/civ5-dll/.winebuild
WINE=~/Library/Application\ Support/com.franke.Whisky/Libraries/Wine/bin/wine64
OUT=$PREFIX/drive_c/lek/out/harness
mkdir -p "$OUT"
win() { printf 'Z:%s' "$1" | sed 's|/|\\|g'; }
cat <<BAT | sed 's/$/\r/' > "$PREFIX/drive_c/lek/harness.bat"
@echo off
set PATH=C:\\vc9\\bin;C:\\vc9\\ide;C:\\sdk6\\bin;%PATH%
set INCLUDE=C:\\vc9\\include;C:\\sdk6\\Include
set LIB=C:\\vc9\\lib;C:\\sdk6\\Lib
cl /nologo /EHsc /MT /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /I"$(win "$SRC")" "$(win "$HERE/harness.cpp")" "$(win "$SRC/McpMapDraw.cpp")" /Fo"C:\\lek\\out\\harness\\\\" /Fe"C:\\lek\\out\\harness\\strategic.exe" /link /SUBSYSTEM:CONSOLE gdiplus.lib ole32.lib shell32.lib
if errorlevel 1 exit /b 1
echo === BUILD OK
BAT
export WINEPREFIX=$PREFIX WINEDEBUG=-all
LOG="$OUT/build.log"
"$WINE" cmd /c 'C:\lek\harness.bat' > "$LOG" 2>&1 || true
grep -q '=== BUILD OK' "$LOG" || { grep -E 'error|warning' "$LOG" | head -20 >&2; exit 1; }
"$WINE" "$OUT/strategic.exe" "$(win "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")")" "$(win "$(cd "$(dirname "$2")" && pwd)/$(basename "$2")")" 2>&1 | grep -vE 'mvk-info|^\s+(VK_|GPU|Metal)' || true
