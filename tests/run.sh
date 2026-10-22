#!/bin/sh
# ONE CLICK FM-1: everything that runs without the device.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/gen
python3 tools/build.py --gen >/dev/null
python3 tools/build.py --app lxr --gen >/dev/null     # (+ the LXR's transient samples: LXR FM-1)
CF="-std=gnu99 -Ibuild/gen -Ifirmware/src"
cc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize=shift \
   $CF -o build/test_oc tests/test_oc.c -lm
build/test_oc
# the firmware unit and the loader, type-checked for a 32-bit freestanding target (the JieLi toolchain builds them)
clang --target=i686-unknown-none-elf -ffreestanding -fsyntax-only -Wall -Wno-unused-function -Ifirmware/hal $CF firmware/src/oneclick.c
clang --target=i686-unknown-none-elf -ffreestanding -fsyntax-only -Wno-unused-function -Ifirmware/hal $CF firmware/loader/loader.c
# LXR FM-1: the app on the computer (sound, sequencer, panel, store; screenshots, demo WAV), its firmware unit type-checked
mkdir -p build/lx
cc -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare -fsanitize=address,undefined \
   -fno-sanitize=shift $CF -o build/test_lx tests/test_lx.c -lm
build/test_lx build/lx
clang --target=i686-unknown-none-elf -ffreestanding -fsyntax-only -Wall -Wno-unused-function -Ifirmware/hal $CF firmware/src/lxrfm1.c
python3 -c "import importlib.util,sys;sys.argv=['x'];s=importlib.util.spec_from_file_location('b','tools/build.py');b=importlib.util.module_from_spec(s);s.loader.exec_module(b);e=b.mmio_check();print('register access:', e or 'hal/ only');sys.exit(1 if e else 0)"
# the voice pool: audible cuts a minute over 40 songs (limit 2), and the render cost (ratios only)
clang -O2 -w $CF -o build/voices_bench tests/voices_bench.c -lm
build/voices_bench 40 8 2 | tail -2
clang -O2 -w $CF -o build/cpu_bench tests/cpu_bench.c -lm
build/cpu_bench
cc -O2 -w $CF -o build/render_demo host/render_demo.c -lm
build/render_demo build/demo.wav build/screen.ppm 1 4 16
# the emulator: wasm without imports, the same samples as a native clang build, the page in headless Chromium
emu/build.sh
clang -O2 -ffp-contract=off -w $CF -o build/emu/native_ref emu/native_ref.c
build/emu/native_ref build/emu/ref.f32
node emu/emu_test.mjs build/emu/oneclick.wasm build/emu/ref.f32
if python3 -c "import playwright" 2>/dev/null; then python3 emu/page_test.py build/emu/oneclick-emu.html; fi
echo "all ok"
