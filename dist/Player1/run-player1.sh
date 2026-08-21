#!/usr/bin/env bash
set -euo pipefail

PLAYER_DIR="$(readlink -f "$(dirname "$0")")"
APPDIR="$PLAYER_DIR/AppDir"
DATA="$PLAYER_DIR/Data"
ROM="$DATA/ROMs/dayto2pe.zip"

export LD_LIBRARY_PATH="$APPDIR/usr/lib"

cd "$DATA"

exec "$APPDIR/usr/bin/supermodel" \
  -crosshair-style=vector \
  -crosshairs=0 \
  -net \
  -window \
  -show-fps \
  -res=496,384 \
  -no-gpu-thread \
  -no-dsb \
  "$ROM" \
  2>&1 | tee "$DATA/Logs/launch.log"
