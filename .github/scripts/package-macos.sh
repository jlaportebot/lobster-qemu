#!/usr/bin/env bash
# usage: package-macos.sh <builddir> <srcdir> <outdir> <label>
set -euo pipefail
BUILD=$(cd "$1" && pwd); SRC=$(cd "$2" && pwd); OUT=$3; LABEL=$4
. "$SRC/.github/scripts/package-common.sh"

mkdir -p "$OUT/bin"
cp "$BUILD/qemu-system-aarch64" "$BUILD/qemu-img" "$OUT/bin/"
strip -x "$OUT/bin/"* || true
python3 "$SRC/.github/scripts/bundle-macos.py" "$OUT"
pkg_finish "$OUT" "$LABEL" "$BUILD" "$SRC"
