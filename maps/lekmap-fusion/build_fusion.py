"""Builds LekmapFusion.lua: Lekmap v6.3 with six start-picker lines and its mountain cut-off changed, then the Fusion block (build.sh runs it).
Usage: python3 build_fusion.py <LekmapPangaeaFractalv6.3.lua> <pulled_apart_block.lua> <out>"""
import re, sys
src, block, out = sys.argv[1:4]
s = open(src, encoding="latin-1").read()
def once(pattern, repl, literal=True):
    global s
    found = s.count(pattern) if literal else len(re.findall(pattern, s))
    if found != 1:
        sys.exit(f"build_fusion: expected exactly one match, found {found}: {pattern!r}")
    s = s.replace(pattern, repl) if literal else re.sub(pattern, repl, s)
once("plotMult = plotMult*0.003;", "plotMult = plotMult*PA_INLAND_NEXT_TO_COAST; -- Lekmap Fusion (was 0.003)")
once("plotMult = plotMult*0.01;", "plotMult = plotMult*PA_INLAND_THREE_FROM_COAST; -- Lekmap Fusion (was 0.01)")
once(r"(if \(self\.plotDataIsCoastal\[i\] == true\) and \(isCoastal ~= true\) then[ \t]*\r?\n[ \t]*)plotMult = -1\.0",
     r"\1plotMult = plotMult*PA_INLAND_ON_COAST -- Lekmap Fusion (was -1.0: never)", literal=False)
once("if plot:GetArea() ~= iBiggestAreaID then",
     "if not PA_StartAllowed(plot, i, iAreaID, iBiggestAreaID, region_number, startPlotIndices) then -- Lekmap Fusion (was: off the biggest landmass)")
once("if beta_inland_prox_penalty == 2 and (isCoastal ~= true) then",
     "if beta_inland_prox_penalty == 2 and PA_INLAND_PROX_PENALTY and (isCoastal ~= true) then -- Lekmap Fusion")
once("startPlotIndices[region_number] = iBest;",
     "startPlotIndices[region_number] = PA_PickStart(iBest, scoreBest, region_number, iWestX, iSouthY, iWidth, iHeight, iAreaID, iBiggestAreaID, startPlotIndices); -- Lekmap Fusion")
once("local mountains = 95 - adjustment - extra_mountains;",
     "local mountains = PA_MountainPercentile(95 - adjustment - extra_mountains); -- Lekmap Fusion (Mountains option)")
assert s.count("-- Lekmap Fusion") == 7
open(out, "w", encoding="latin-1").write(s + open(block, encoding="latin-1").read())
print(f"build_fusion: wrote {out}")
