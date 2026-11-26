#!/usr/bin/env bash
# Builds Mundi Fusion into build/mundi-fusion/MundiFusion.lua from Lekmap v6.3 (the Lekmap folder of the LEKMOD
# clone) and src/, then runs the tests. generator/stage.py ships it inside LEKMOD for Bazaar as a map script.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
V63="$HOME/repos/civ5-dll/lekmod/Lekmap/LekmapPangaeaFractalv6.3.lua"
OUT="$REPO/build/mundi-fusion/MundiFusion.lua"
[ -f "$V63" ] || { echo "Lekmap v6.3 not found at $V63" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
python3 "$HERE/build_fusion.py" "$V63" "$HERE/src/pulled_apart_block.lua" "$OUT"
FUSION_OUT="$OUT" luajit -e 'local f, e = loadfile(os.getenv("FUSION_OUT")); if not f then print("syntax error: " .. e); os.exit(1) end'
cd "$HERE"
for seed in 1 2 3 7 11; do luajit tests/test_block.lua "$seed" > /dev/null; done
echo "built $OUT ($(shasum "$OUT" | cut -c1-12)); syntax ok; tests pass on 5 seeds"
