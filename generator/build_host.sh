#!/usr/bin/env bash
# Build the LEKMOD host DLL - LEKMOD's game core with Bazaar's plugin loader generated into it - under the build
# wine prefix, with Visual C++ 2008 driven directly. Run after run_lekmod.py --generate.
#
# Settings follow LEKMOD's Release|Win32 project configuration plus what Bazaar's generator adds to the engine
# build: its host sources and hook table compiled without the precompiled header, the export define, the two
# layout defines, /bigobj and the two warning suppressions. /Z7 stands in for /Zi: the PDB server VS2008 uses for
# parallel /Zi builds is not reliable under wine, and the link still writes a PDB. The manifest the linker writes stays
# beside the DLL: mt.exe embeds nothing under wine and sometimes never exits.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)

PREFIX=~/repos/civ5-dll/.winebuild
WINE=~/Library/Application\ Support/com.franke.Whisky/Libraries/Wine/bin/wine64
MIRROR=$REPO/build/civgen/src/CvGameCoreDLL_Expansion2
ORIG=~/repos/civ5-dll/lekmod/LEKMOD_DLL/CvGameCoreDLL_Expansion2
HOST=~/repos/civ5-dll/bazaar/host
LEK=$PREFIX/drive_c/lek
LOG=$REPO/build/host-build.log

[ -f "$MIRROR/CivHookTable.cpp" ] || { echo "no generated mirror at $MIRROR; run run_lekmod.py --generate first" >&2; exit 1; }
mkdir -p "$LEK/obj" "$LEK/out"
rm -f "$LEK"/obj/*.obj "$LEK"/obj/*.pch

win() { printf 'Z:%s' "$1" | sed 's|/|\\|g'; }
M=$(win "$MIRROR"); O=$(win "$ORIG"); H=$(win "$HOST")

# Response files, CRLF, one quoted path per line.
{ find "$MIRROR" -maxdepth 1 -name '*.cpp' ! -name '_precompile.cpp' ! -name 'CivHookTable.cpp'; find "$MIRROR/Lua" -name '*.cpp'; } \
  | sort | while read -r f; do printf '"%s"\r\n' "$(win "$f")"; done > "$LEK/engine.rsp"
{ ls "$HOST"/*.cpp; echo "$MIRROR/CivHookTable.cpp"; } \
  | while read -r f; do printf '"%s"\r\n' "$(win "$f")"; done > "$LEK/host.rsp"
echo "engine sources: $(wc -l < "$LEK/engine.rsp" | tr -d ' ')  host sources: $(wc -l < "$LEK/host.rsp" | tr -d ' ')"

cat <<EOF | sed 's/$/\r/' > "$LEK/build.bat"
@echo off
set PATH=C:\\vc9\\bin;C:\\vc9\\ide;C:\\sdk6\\bin;%PATH%
set INCLUDE=C:\\vc9\\include;C:\\sdk6\\Include
set LIB=C:\\vc9\\lib;C:\\sdk6\\Lib
set OBJ=C:\\lek\\obj
set OUT=C:\\lek\\out
set CFLAGS=/nologo /c /Ox /Ob2 /Oi /Oy /EHsc /MD /GS- /Gy /fp:fast /W3 /Z7 /bigobj /wd4251 /wd4275
set DEFS=/DFXS_IS_DLL /DWIN32 /DNDEBUG /D_WINDOWS /D_USRDLL /DCVGAMECOREDLL_EXPORTS /DFINAL_RELEASE /D_CRT_SECURE_NO_WARNINGS /D_MBCS /D_SECURE_SCL=0 /D_HAS_ITERATOR_DEBUGGING=0 /D"CIV_HOST_API=__declspec(dllexport)"
set INC=/I"$M" /I"$H" /I"$O" /I"$O\\CvWorldBuilderMap\\include" /I"$O\\CvGameCoreDLLUtil\\include" /I"$O\\CvLocalization\\include" /I"$O\\CvGameDatabase\\include" /I"$O\\FirePlace\\include" /I"$O\\FirePlace\\include\\FireWorks" /I"$O\\ThirdPartyLibs\\Lua51\\include"
set LIBPATHS=/LIBPATH:"$O\\CvWorldBuilderMap\\lib" /LIBPATH:"$O\\CvGameCoreDLLUtil\\lib" /LIBPATH:"$O\\CvLocalization\\lib" /LIBPATH:"$O\\CvGameDatabase\\lib" /LIBPATH:"$O\\FirePlace\\lib" /LIBPATH:"$O\\ThirdPartyLibs\\Lua51\\lib"
set LIBS=CvWorldBuilderMapWin32.lib CvGameCoreDLLUtilWin32.lib CvLocalizationWin32.lib CvGameDatabaseWin32.lib FireWorksWin32.lib FLuaWin32.lib lua51_Win32.lib winmm.lib dbghelp.lib

echo === 1/4 precompiled header
cl %CFLAGS% %DEFS% %INC% /Yc"CvGameCoreDLLPCH.h" /Fp%OBJ%\\engine.pch /Fo%OBJ%\\ "$M\\_precompile.cpp"
if errorlevel 1 exit /b 1
echo === 2/4 engine
cl %CFLAGS% %DEFS% %INC% /MP /Yu"CvGameCoreDLLPCH.h" /Fp%OBJ%\\engine.pch /Fo%OBJ%\\ @C:\\lek\\engine.rsp
if errorlevel 1 exit /b 2
echo === 3/4 host and hook table
cl %CFLAGS% %DEFS% %INC% /MP /Fo%OBJ%\\ @C:\\lek\\host.rsp
if errorlevel 1 exit /b 3
echo === 4/4 link
link /nologo /DLL /OUT:%OUT%\\CvGameCore_Expansion2.dll /IMPLIB:%OUT%\\CvGameCore_Expansion2.lib /DEF:"$M\\CvGameCoreDLL.def" /DEBUG /PDB:%OUT%\\CvGameCore_Expansion2.pdb /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MACHINE:X86 %LIBPATHS% %OBJ%\\*.obj %LIBS%
if errorlevel 1 exit /b 4
rem The PDB server link starts stays alive afterwards and, under wine, keeps this cmd from exiting.
taskkill /F /IM mspdbsrv.exe >nul 2>&1
echo === BUILD OK
EOF

export WINEPREFIX=$PREFIX WINEDEBUG=-all
echo "building; log -> $LOG"
"$WINE" cmd /c 'C:\lek\build.bat' 2>&1 | grep -vE 'mvk-info|^\s+(VK_|GPU|Metal|pipelineCache|model:|type:|vendorID|deviceID|supports|Read-Write)' > "$LOG" || true
if ! grep -q '=== BUILD OK' "$LOG"; then
  echo "build failed; see $LOG" >&2
  grep -nE 'error [A-Z]+[0-9]+|fatal' "$LOG" | head -12 >&2
  exit 1
fi
tail -n 3 "$LOG"
ls -la "$LEK/out/" 2>/dev/null || true
