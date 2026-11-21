#!/usr/bin/env bash
# Builds the Time of Day plugin (C:\lek\out\cg\ColorGrade.dll in the build prefix) against the LEKMOD host, after its
# tests; generator/stage.py packs it into LEKMOD for Bazaar. Needs the host built first (docs/BUILD.md).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
"$HERE/tests/run_core.sh"
luajit "$HERE/tests/ui_test.lua" "$HERE/mod/TimeOfDay.lua" | tail -1 | grep -q "passed" || { echo "the Time of Day panel test failed" >&2; exit 1; }
PREFIX=~/repos/civ5-dll/.winebuild
WINE=~/Library/Application\ Support/com.franke.Whisky/Libraries/Wine/bin/wine64
MIRROR=$REPO/build/civgen/src/CvGameCoreDLL_Expansion2
SDKH=$REPO/build/civgen/sdk
ORIG=~/repos/civ5-dll/lekmod/LEKMOD_DLL/CvGameCoreDLL_Expansion2
HOST=~/repos/civ5-dll/bazaar/host
LEK=$PREFIX/drive_c/lek
LOG=$REPO/build/color-grade-build.log
STAGE=$REPO/build/color-grade
D3D=ColorGradeD3D.cpp
[ -f "$SDKH/CivHooks.h" ] || { echo "no $SDKH/CivHooks.h; generate first" >&2; exit 1; }
[ -f "$LEK/out/CvGameCore_Expansion2.lib" ] || { echo "no host import library; build the host first" >&2; exit 1; }
mkdir -p "$LEK/sobj" "$LEK/out/cg" "$LEK/sinc"; rm -f "$LEK"/sobj/*.obj
printf '#pragma once\r\n#define PLUGIN_VERSION "1"\r\n' > "$LEK/sinc/PluginVersion.h"
win() { printf 'Z:%s' "$1" | sed 's|/|\\|g'; }
M=$(win "$MIRROR"); K=$(win "$SDKH"); O=$(win "$ORIG"); H=$(win "$HOST"); S=$(win "$HERE/src")
cat <<BAT | sed 's/$/\r/' > "$LEK/cg.bat"
@echo off
set PATH=C:\\vc9\\bin;C:\\vc9\\ide;C:\\sdk6\\bin;%PATH%
set INCLUDE=C:\\vc9\\include;C:\\sdk6\\Include
set LIB=C:\\vc9\\lib;C:\\sdk6\\Lib
set CFLAGS=/nologo /c /Ox /Ob2 /Oi /Oy /EHsc /MD /GS- /Gy /fp:fast /W3 /Z7 /bigobj /wd4251 /wd4275
set DEFS=/D"CIV_HOST_API=__declspec(dllimport)" /D_SECURE_SCL=0 /D_HAS_ITERATOR_DEBUGGING=0 /DFXS_IS_DLL /DWIN32 /DNDEBUG /D_WINDOWS /D_USRDLL /DFINAL_RELEASE /D_CRT_SECURE_NO_WARNINGS /D_MBCS
set INC=/I"$K" /I"$H" /I"$M" /IC:\\lek\\sinc /I"$O\\CvWorldBuilderMap\\include" /I"$O\\CvGameCoreDLLUtil\\include" /I"$O\\CvLocalization\\include" /I"$O\\CvGameDatabase\\include" /I"$O\\FirePlace\\include" /I"$O\\FirePlace\\include\\FireWorks" /I"$O\\ThirdPartyLibs\\Lua51\\include"
set LIBPATHS=/LIBPATH:C:\\lek\\out /LIBPATH:"$O\\CvWorldBuilderMap\\lib" /LIBPATH:"$O\\CvGameCoreDLLUtil\\lib" /LIBPATH:"$O\\CvLocalization\\lib" /LIBPATH:"$O\\CvGameDatabase\\lib" /LIBPATH:"$O\\FirePlace\\lib" /LIBPATH:"$O\\ThirdPartyLibs\\Lua51\\lib"
set LIBS=CvGameCore_Expansion2.lib CvWorldBuilderMapWin32.lib CvGameCoreDLLUtilWin32.lib CvLocalizationWin32.lib CvGameDatabaseWin32.lib FireWorksWin32.lib FLuaWin32.lib lua51_Win32.lib winmm.lib user32.lib ws2_32.lib
echo === 1/2 compile
cl %CFLAGS% %DEFS% %INC% /FoC:\\lek\\sobj\\ "$S\\ColorGradePlugin.cpp" "$S\\$D3D" "$S\\CgLooks.cpp"
if errorlevel 1 exit /b 1
echo === 2/2 link
link /nologo /DLL /DEBUG /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MACHINE:X86 /NODEFAULTLIB:libcmt %LIBPATHS% /OUT:C:\\lek\\out\\cg\\ColorGrade.dll /PDB:C:\\lek\\out\\cg\\ColorGrade.pdb C:\\lek\\sobj\\*.obj %LIBS%
if errorlevel 1 exit /b 2
taskkill /F /IM mspdbsrv.exe >nul 2>&1
echo === BUILD OK
BAT
export WINEPREFIX=$PREFIX WINEDEBUG=-all
"$WINE" cmd /c 'C:\lek\cg.bat' 2>&1 | grep -vE 'mvk-info|^\s+(VK_|GPU|Metal|pipelineCache|model:|type:|vendorID|deviceID|supports|Read-Write)' > "$LOG" || true
if ! grep -q '=== BUILD OK' "$LOG"; then
  echo "build failed; see $LOG" >&2
  grep -nE 'error [A-Z]+[0-9]+|fatal' "$LOG" | head -12 >&2
  exit 1
fi
grep -nE 'ColorGrade.*warning' "$LOG" || true
echo "built $LEK/out/cg/ColorGrade.dll"
