#!/usr/bin/env bash
# Run inside the MSYS2 UCRT64 shell.
# usage: package-windows.sh <builddir> <srcdir> <outdir> <label>
set -euo pipefail
BUILD=$(cd "$1" && pwd); SRC=$(cd "$2" && pwd); OUT=$3; LABEL=$4
. "$SRC/.github/scripts/package-common.sh"

mkdir -p "$OUT/bin"
cp "$BUILD/qemu-system-aarch64.exe" "$BUILD/qemu-img.exe" "$OUT/bin/"
strip --strip-unneeded "$OUT/bin/"*.exe || true

# ntldd -R walks the import tree; keep only DLLs that live in the MSYS2
# ucrt64 prefix (Windows system DLLs come from the host).
for exe in "$OUT"/bin/*.exe; do
    ntldd -R "$exe" | awk '/=>/ {print $3}'
done | sort -u | tr '\\' '/' | grep -i "/ucrt64/" | while read -r dll; do
    cp "$(cygpath -u "$dll")" "$OUT/bin/"
done
pkg_finish "$OUT" "$LABEL" "$BUILD" "$SRC"
echo "bundled DLLs:"; ls "$OUT/bin"/*.dll | wc -l
