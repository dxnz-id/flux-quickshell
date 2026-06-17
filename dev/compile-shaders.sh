#!/bin/bash
set -e
QSB="/usr/lib/qt6/bin/qsb"
SRC_DIR="$(dirname "$0")/../shaders/src"
OUT_DIR="$(dirname "$0")/../shaders/compiled"
mkdir -p "$OUT_DIR"
for f in "$SRC_DIR"/*.frag; do
    name="$(basename "$f" .frag)"
    echo "  $name"
    "$QSB" --qt6 "$f" -o "$OUT_DIR/$name.qsb"
done
echo "Done."
