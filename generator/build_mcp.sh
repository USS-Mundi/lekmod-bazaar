#!/usr/bin/env bash
# Build the MCP Server plugin against the LEKMOD host: its mirrored headers (which carry the export markers),
# the generated CivHooks.h, Bazaar's CivPluginApi.h and the host's import library. Settings follow Bazaar's
# CivPlugin.vsprops, which the plugin's project inherits, plus the one library its project adds (ws2_32);
# gdiplus, ole32 and shell32 come in by #pragma comment from the sources.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=~/repos/civ5-dll/.winebuild
WINE=~/Library/Application\ Support/com.franke.Whisky/Libraries/Wine/bin/wine64
MIRROR=$REPO/build/civgen/src/CvGameCoreDLL_Expansion2
SDKH=$REPO/build/civgen/sdk
ORIG=~/repos/civ5-dll/lekmod/LEKMOD_DLL/CvGameCoreDLL_Expansion2
HOST=~/repos/civ5-dll/bazaar/host
SRC=~/repos/civ5-dll/mcp-plugin/MCPServer/src
COMPAT=$REPO/plugin-compat
LEK=$PREFIX/drive_c/lek
LOG=$REPO/build/mcp-build.log
[ -f "$SDKH/CivHooks.h" ] || { echo "no $SDKH/CivHooks.h; generate first" >&2; exit 1; }
[ -f "$LEK/out/CvGameCore_Expansion2.lib" ] || { echo "no host import library; build the host first" >&2; exit 1; }
mkdir -p "$LEK/pobj" "$LEK/out/plugin" "$LEK/pinc"; rm -f "$LEK"/pobj/*.obj
# The release script would write this from VERSION; the sources may include it.
printf '#pragma once\r\n#define PLUGIN_VERSION "%s"\r\n' "$(sed -n 's/^release //p' "$SRC/../../VERSION" | tr -d '\r')" > "$LEK/pinc/PluginVersion.h"
win() { printf 'Z:%s' "$1" | sed 's|/|\\|g'; }
M=$(win "$MIRROR"); K=$(win "$SDKH"); O=$(win "$ORIG"); H=$(win "$HOST"); S=$(win "$SRC"); C=$(win "$COMPAT")
ls "$SRC"/*.cpp | sort | while read -r f; do printf '"%s"\r\n' "$(win "$f")"; done > "$LEK/mcp.rsp"
printf '"%s"\r\n' "$(win "$COMPAT/McpLekmodCompat.cpp")" >> "$LEK/mcp.rsp"
echo "plugin sources: $(wc -l < "$LEK/mcp.rsp" | tr -d ' ')"
cat <<EOF | sed 's/$/\r/' > "$LEK/mcp.bat"
@echo off
set PATH=C:\\vc9\\bin;C:\\vc9\\ide;C:\\sdk6\\bin;%PATH%
set INCLUDE=C:\\vc9\\include;C:\\sdk6\\Include
set LIB=C:\\vc9\\lib;C:\\sdk6\\Lib
set POBJ=C:\\lek\\pobj
set CFLAGS=/nologo /c /Ox /Ob2 /Oi /Oy /EHsc /MD /GS- /Gy /fp:fast /W3 /Z7 /MP /bigobj /wd4251 /wd4275 /DMCP_LEKMOD /FI"$C\\McpLekmodCompat.h"
set DEFS=/D"CIV_HOST_API=__declspec(dllimport)" /D_SECURE_SCL=0 /D_HAS_ITERATOR_DEBUGGING=0 /DFXS_IS_DLL /DWIN32 /DNDEBUG /D_WINDOWS /D_USRDLL /DFINAL_RELEASE /D_CRT_SECURE_NO_WARNINGS /D_MBCS
set INC=/I"$K" /I"$H" /I"$M" /I"$S" /I"$C" /IC:\\lek\\pinc /I"$O\\CvWorldBuilderMap\\include" /I"$O\\CvGameCoreDLLUtil\\include" /I"$O\\CvLocalization\\include" /I"$O\\CvGameDatabase\\include" /I"$O\\FirePlace\\include" /I"$O\\FirePlace\\include\\FireWorks" /I"$O\\ThirdPartyLibs\\Lua51\\include"
set LIBPATHS=/LIBPATH:C:\\lek\\out /LIBPATH:"$O\\CvWorldBuilderMap\\lib" /LIBPATH:"$O\\CvGameCoreDLLUtil\\lib" /LIBPATH:"$O\\CvLocalization\\lib" /LIBPATH:"$O\\CvGameDatabase\\lib" /LIBPATH:"$O\\FirePlace\\lib" /LIBPATH:"$O\\ThirdPartyLibs\\Lua51\\lib"
set LIBS=CvGameCore_Expansion2.lib CvWorldBuilderMapWin32.lib CvGameCoreDLLUtilWin32.lib CvLocalizationWin32.lib CvGameDatabaseWin32.lib FireWorksWin32.lib FLuaWin32.lib lua51_Win32.lib winmm.lib ws2_32.lib
echo === 1/2 compile
cl %CFLAGS% %DEFS% %INC% /Fo%POBJ%\\ @C:\\lek\\mcp.rsp
if errorlevel 1 exit /b 1
echo === 2/2 link
link /nologo /DLL /DEBUG /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MACHINE:X86 /NODEFAULTLIB:libcmt %LIBPATHS% /OUT:C:\\lek\\out\\plugin\\MCPServer.dll /PDB:C:\\lek\\out\\plugin\\MCPServer.pdb %POBJ%\\*.obj %LIBS%
if errorlevel 1 exit /b 2
rem The PDB server link starts stays alive afterwards and, under wine, keeps this cmd from exiting.
taskkill /F /IM mspdbsrv.exe >nul 2>&1
echo === BUILD OK
EOF
export WINEPREFIX=$PREFIX WINEDEBUG=-all
"$WINE" cmd /c 'C:\lek\mcp.bat' 2>&1 | grep -vE 'mvk-info|^\s+(VK_|GPU|Metal|pipelineCache|model:|type:|vendorID|deviceID|supports|Read-Write)' > "$LOG" || true
if ! grep -q '=== BUILD OK' "$LOG"; then
  echo "build failed; see $LOG" >&2
  grep -nE 'error [A-Z]+[0-9]+|fatal' "$LOG" | head -12 >&2
  exit 1
fi
tail -n 2 "$LOG"
