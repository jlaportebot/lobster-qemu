#!/usr/bin/env bash
# Smoke test for a QEMU binary directory: machine list, qemu-img, real boot.
# usage: smoke.sh <bindir> <workdir>
set -uo pipefail

BINDIR=$(cd "$1" && pwd)
WORKDIR=$2
BOOT_TIMEOUT=${BOOT_TIMEOUT:-240}
EXE=""
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;; esac
QEMU="$BINDIR/qemu-system-aarch64$EXE"
QIMG="$BINDIR/qemu-img$EXE"
KERNEL_URL=${KERNEL_URL:-https://github.com/jlaportebot/lobster-os/releases/download/kernel-nightly/kernel8.img}
DTB_URL=${DTB_URL:-https://raw.githubusercontent.com/jlaportebot/pi5-emulator/master/bcm2712-rpi-5-b.dtb}

mkdir -p "$WORKDIR"
cd "$WORKDIR" || exit 1

echo "== machines"
machines=$("$QEMU" -M help) || { echo "FAIL: -M help"; exit 1; }
for m in raspi5b raspi5b-2g raspi5b-8g raspi5b-16g; do
    if ! echo "$machines" | grep -qE "^$m( |$)"; then
        echo "FAIL: machine $m missing"; echo "$machines"; exit 1
    fi
    echo "ok $m"
done

echo "== qemu-img"
"$QIMG" --version || { echo "FAIL: qemu-img"; exit 1; }
"$QIMG" create -f qcow2 t.qcow2 64M >/dev/null || { echo "FAIL: qemu-img create"; exit 1; }

echo "== boot"
[ -s kernel8.img ] || curl -fsSL --retry 5 -o kernel8.img "$KERNEL_URL" || exit 1
[ -s bcm2712-rpi-5-b.dtb ] || curl -fsSL --retry 5 -o bcm2712-rpi-5-b.dtb "$DTB_URL" || exit 1
rm -f serial.log
"$QEMU" -M raspi5b,graphics=on -m 4G -kernel kernel8.img \
    -dtb bcm2712-rpi-5-b.dtb -display none -serial file:serial.log \
    -monitor none > qemu.stdout 2> qemu.stderr &
QPID=$!
rc=1
for ((i = 0; i < BOOT_TIMEOUT; i++)); do
    if grep -q "desktop-ready" serial.log 2>/dev/null; then rc=0; break; fi
    if ! kill -0 "$QPID" 2>/dev/null; then echo "qemu exited early"; break; fi
    sleep 1
done
kill "$QPID" 2>/dev/null
[ -n "$EXE" ] && taskkill //F //IM "qemu-system-aarch64.exe" >/dev/null 2>&1
wait "$QPID" 2>/dev/null
if [ $rc -eq 0 ]; then
    echo "PASS: desktop-ready after ~${i}s"
else
    echo "FAIL: no desktop-ready within ${BOOT_TIMEOUT}s"
    tail -20 serial.log qemu.stderr 2>/dev/null
fi
exit $rc
