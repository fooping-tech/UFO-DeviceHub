#!/bin/sh
# Renders the M5Dial screen on this PC: run.sh OUT_DIR  ->  OUT_DIR/sheet.png
set -e
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:?usage: run.sh OUT_DIR}
mkdir -p "$out"
c++ -std=c++17 -Wall -I "$root/firmware" -I "$root/shared" "$root/tests/tools/scene_preview/main.cpp" \
  "$root/firmware/m5dial/dial_scene.cpp" -o "$out/scene_preview"
"$out/scene_preview" "$out"
uv run --no-project --with pillow python "$root/tests/tools/scene_preview/sheet.py" "$out" "$out/sheet.png"
echo "$out/sheet.png"
