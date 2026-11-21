#!/usr/bin/env bash
# Builds and runs the native tests of the grades and the day's glide.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
clang++ -std=c++98 -Wall -o "$OUT/core" "$HERE/test_core.cpp" "$HERE/../src/CgLooks.cpp"
"$OUT/core"
