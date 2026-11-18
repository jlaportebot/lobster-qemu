#!/usr/bin/env bash
# usage: package-linux.sh <builddir> <srcdir> <outdir> <label>
set -euo pipefail
BUILD=$(cd "$1" && pwd); SRC=$(cd "$2" && pwd); OUT=$3; LABEL=$4
. "$SRC/.github/scripts/package-common.sh"

# System libraries that must come from the host (glibc family) or depend on
# the host GPU driver stack; everything else is bundled.
EXCLUDE='^(linux-vdso|ld-linux.*|libc|libm|libdl|librt|libpthread|libresolv|libutil|libnsl|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libgbm|libdrm|libvulkan)\.so'

mkdir -p "$OUT/bin" "$OUT/lib"
cp "$BUILD/qemu-system-aarch64" "$BUILD/qemu-img" "$OUT/bin/"
strip --strip-unneeded "$OUT/bin/"* || true

for exe in "$OUT"/bin/*; do
    ldd "$exe" | awk '/=> \// {print $3}'
done | sort -u | while read -r lib; do
    name=$(basename "$lib")
    if [[ "$name" =~ $EXCLUDE ]]; then continue; fi
    cp -L "$lib" "$OUT/lib/$name"
done
chmod u+w "$OUT"/lib/*
for exe in "$OUT"/bin/*; do patchelf --set-rpath '$ORIGIN/../lib' "$exe"; done
for lib in "$OUT"/lib/*; do patchelf --set-rpath '$ORIGIN' "$lib"; done

pkg_finish "$OUT" "$LABEL" "$BUILD" "$SRC"
echo "bundled libs:"; ls "$OUT/lib" | wc -l
