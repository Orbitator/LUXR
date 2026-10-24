#!/bin/sh
# LUXR FM-1 emulator: emu/lx_emu_web.c -> WebAssembly, then one self-contained page build/emu/luxr-emu.html
# (the wasm inlined as base64, the AudioWorklet from a Blob). Needs clang with the wasm32 target and wasm-ld.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/gen build/emu
python3 tools/build.py --app lxr --gen >/dev/null
[ build/gen/ui_hd.h -nt emu/gen_hd.py ] || python3 emu/gen_hd.py build/gen/ui_hd.h
python3 emu/mk_gfx_hd.py firmware/src/gfx.c build/emu/gfx_hd.c
clang --target=wasm32 -O2 -std=gnu99 -ffreestanding -nostdlib -fno-builtin -Wall -Wno-unused-function \
    -DOC_VERSION="\"${LUXR_VERSION:-WEB}\"" \
    -Ibuild/gen -Ibuild/emu -Iemu -Ifirmware/src -Wl,--no-entry -Wl,--export-dynamic -Wl,--strip-all -Wl,-z,stack-size=262144 \
    -o build/emu/luxr.wasm emu/lx_emu_web.c
python3 emu/inline_lx.py emu/luxr_emu.html build/emu/luxr.wasm build/emu web/emu/luxr-backup-20261024-1221.luxr
cp web/emu/luxr-backup-20261024-1221.luxr build/emu/
echo "emu: build/emu/luxr-emu.html ($(wc -c < build/emu/luxr.wasm | tr -d ' ') B wasm)"
