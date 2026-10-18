# lobster-qemu — Custom QEMU fork for LobsterOS / Raspberry Pi 5 (BCM2712) emulation

This repo carries the **custom QEMU device code** that turns stock QEMU 10.0.8
into the LobsterOS Pi 5 emulator. It is maintained as an overlay against a
plain QEMU checkout — the custom machine, its 27 new device files, and a patch
series for the stock files that needed modification.

## What it adds

- **`raspi5b` machine** (`hw/arm/raspi5b.c`, `bcm2712.c`, `bcm2712_peripherals.c`,
  `bcm2712_rp1.c`, …) — a BCM2712 SoC model: 4× Cortex-A76, GIC-400,
  VC-peripheral bus at 0x7c000000, SCB bus at 0x107c000000, EMMC2, RP1 I/O
  controller, DTB fixups for bare-metal kernels.
- **`lobster-serial`** (`hw/misc/lobster_serial.c`) — bulk console TX window at
  `0x7d00a000`: guest writes up to 2048 payload bytes + one TXLEN register
  write, and the device forwards the whole chunk through the PL011 chardev in
  a single host call. Cuts console-heavy guest workloads by ~2x on TCG.
- **`lobster-net`** (`hw/misc/lobster_net.c`) — MMIO HTTP proxy + cross C
  compiler bridge (host curl / aarch64-linux-gnu-gcc) at `0x7d009000`,
  512KB response buffer, Bearer-auth support.
- **`lobster-input`** (`hw/misc/lobster_input.c`) — mouse/keyboard event ring
  for the graphical desktop at `0x7d008000`.
- **Display stack** (`hw/display/bcm2712_*.c`) — BCM2712 framebuffer, HDMI,
  pixelvalve, MOP/MOPLET compositor models for the GUI.

## Device map (guest view)

| Range | Device |
|---|---|
| `0x7d001000` | PL011 UART0 (console) |
| `0x7d008000` | lobster-input (mouse/kbd ring) |
| `0x7d009000` | lobster-net (HTTP proxy + C compiler) |
| `0x7d00a000` | lobster-serial (bulk TX window, magic `LBUF`) |
| `0x7c003000` | BCM2835 system timer (SPI 64–67) |
| `0x107fff9000` | GIC-400 distributor |
| `0x107fffa000` | GIC-400 CPU interface |

## Build (on a Pi 5 host — ~35 min, 4 jobs)

```bash
# 1. Start from a stock QEMU 10.0.x tree
git clone https://gitlab.com/qemu-project/qemu.git
cd qemu && git checkout v10.0.8

# 2. Overlay this repo's custom files
cp -r lobster-qemu/custom/* .

# 3. Apply the stock-file modifications
git apply lobster-qemu/lobster_qemu_mods.patch

# 4. Configure optimized (LTO hits a GCC aarch64 stringop warning; -O3 is enough)
mkdir build-opt && cd build-opt
../configure --target-list=aarch64-softmmu -Doptimization=3 -Dwerror=false \
  -Dc_args="-Wno-error=maybe-uninitialized -Wno-error=stringop-overflow -Wno-error=uninitialized"
ninja -j4 qemu-system-aarch64
```

## Run LobsterOS

```bash
./qemu-system-aarch64 -M raspi5b -m 4G -cpu cortex-a76 \
  -kernel <path>/kernel8.img -dtb bcm2712-rpi-5-b.dtb \
  -serial stdio -display none
```

## Performance notes (measured on Pi 5, TCG)

- `-O3` optimized build vs stock `-O0 -g` debug build: **1.5x** on
  serial-bound guest workloads (`seq 1 50000`: 21.8s → 14.3s).
- MTTCG (`-accel tcg,thread=multi`) and `tb-size` tuning: no measurable effect
  for this single-vCPU-bound kernel workload.
- The single biggest console win is **guest-side batching**: with the
  lobster-serial window + 2KB batch buffers in the guest, end-to-end
  `seq 1 50000` drops to **12.4s (1.76x vs the original stack)**.

## Verified kernel/IRQ bring-up facts

- The BCM2712 GIC model routes systmr IRQs to **SPI 64–67**; the kernel must
  add `+32` when addressing GICv2 distributor registers for SPI N (they are
  indexed by full interrupt ID: SGI 0–15, PPI 16–31, SPI 32+).
- The PL011 TX FIFO flag is never asserted in QEMU, so guest TX polls are
  always ready — per-byte output costs exactly 2 MMIO ops with no backpressure.
