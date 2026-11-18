#!/usr/bin/env bash
# Shared packaging helpers. Source this file, then call the functions.
# usage: pkg_finish <outdir> <platform-label> <builddir> <srcdir>

pkg_finish() {
    local out=$1 label=$2 build=$3 src=$4 exe=""
    case "$(uname -s)" in MINGW*|MSYS*) exe=".exe" ;; esac

    cp "$src/COPYING" "$out/COPYING"
    cp "$src/COPYING.LIB" "$out/COPYING.LIB" 2>/dev/null || true
    mkdir -p "$out/share/qemu"
    if [ -d "$build/pc-bios/keymaps" ]; then
        cp -R "$build/pc-bios/keymaps" "$out/share/qemu/keymaps"
    fi

    local version commit cfg
    version=$("$out/bin/qemu-system-aarch64$exe" --version | head -1)
    commit=$(git -C "$src" rev-parse HEAD)
    cfg=$(grep -E "^exec " "$build/config.status" | head -1 | sed "s#'\.\./configure'#configure#")
    cat > "$out/README-lobster.txt" <<README
LobsterOS QEMU (Raspberry Pi 5 'raspi5b' + lobster-* paravirtual devices)
Platform : $label
Version  : $version
Commit   : $commit
Source   : https://github.com/jlaportebot/lobster-qemu
Configure: $cfg

Layout
  bin/qemu-system-aarch64$exe   the emulator
  bin/qemu-img$exe              disk image tool
  lib/ (or DLLs next to the exes on Windows)
                                bundled runtime libraries
  share/qemu/keymaps            keymap data (for -k with VNC)
  COPYING                       GPL-2.0 licence text

Licence: GPL-2.0-or-later (see COPYING). Corresponding source is the commit above.
README
}
