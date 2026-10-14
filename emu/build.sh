#!/bin/sh
# ONE CLICK FM-1 emulator: emu/emu_web.c -> WebAssembly, then one self-contained page build/emu/oneclick-emu.html
# (the wasm inlined as base64, the AudioWorklet from a Blob). Needs clang with the wasm32 target and wasm-ld.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/gen build/emu
python3 tools/gen_tables.py build/gen/oc_tables.h
clang --target=wasm32 -O2 -std=gnu99 -ffreestanding -nostdlib -fno-builtin -Wall -Wno-unused-function \
    -Ibuild/gen -Ifirmware/src -Wl,--no-entry -Wl,--export-dynamic -Wl,--strip-all -Wl,-z,stack-size=262144 \
    -o build/emu/oneclick.wasm emu/emu_web.c
python3 emu/inline.py emu/emu.html build/emu/oneclick.wasm build/emu
echo "emu: build/emu/oneclick-emu.html ($(wc -c < build/emu/oneclick.wasm | tr -d ' ') B wasm)"
