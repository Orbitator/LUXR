#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# LUXR for Ableton Move (Schwung): the module. From the repository's root:
#   schwung/scripts/build.sh            -> build/schwung/dist/luxr/ and build/schwung/luxr-module.tar.gz (aarch64)
#   NATIVE=1 schwung/scripts/build.sh   -> build/schwung/native/dsp.so for this computer (the tests; EXTRA_CFLAGS: e.g.
#                                          the sanitizers)
# 1. the tables and samples (tools/build.py --gen, as the firmware), the preset kits from the backup the emulator starts
#    with (schwung/tools/gen_kits.c, on this computer); 2. the plugin: luxr_engine.c once for each of the LXE_SLOTS
#    engines, the transients once, the entry (luxr_plugin.c) - with aarch64-linux-gnu-gcc, else in Docker.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$PWD
OUT=build/schwung
BACKUP=${LUXR_KITS:-web/emu/luxr-backup-20261024-1221.luxr}
SLOTS=6
mkdir -p "$OUT" build/gen

if [ ! -f "/.dockerenv" ] || [ -z "${LUXR_IN_DOCKER:-}" ]; then
    python3 tools/build.py --app lxr --gen >/dev/null
    cc -O1 -w -std=gnu99 -Ibuild/gen -Ifirmware/src -o "$OUT/gen_kits" schwung/tools/gen_kits.c -lm
    "$OUT/gen_kits" "$BACKUP" build/gen/luxr_kits.h
fi

compile() {   # $1 the compiler, $2 the output, then flags
    local cc=$1 so=$2 obj
    shift 2
    obj=$(dirname "$so")/obj
    mkdir -p "$obj"
    local CF=(-O2 -fPIC -fvisibility=hidden -std=gnu99 -Wall -Wno-unused-function -Wno-unused-variable
              -Ibuild/gen -Ifirmware/src -Ischwung/dsp "$@" ${EXTRA_CFLAGS:-})
    for k in $(seq 0 $((SLOTS - 1))); do
        "$cc" "${CF[@]}" -DLXE_SLOT="$k" -c schwung/dsp/luxr_engine.c -o "$obj/engine$k.o"
    done
    "$cc" "${CF[@]}" -c schwung/dsp/luxr_tables.c -o "$obj/tables.o"
    "$cc" "${CF[@]}" -c schwung/dsp/luxr_plugin.c -o "$obj/plugin.o"
    "$cc" -shared ${EXTRA_CFLAGS:-} -o "$so" "$obj"/engine*.o "$obj/tables.o" "$obj/plugin.o" -lm
}

if [ -n "${NATIVE:-}" ]; then
    mkdir -p "$OUT/native"
    compile "${CC:-cc}" "$OUT/native/dsp.so" -g
    echo "native: $OUT/native/dsp.so"
    exit 0
fi

CROSS=${CROSS_PREFIX:-aarch64-linux-gnu-}gcc
if ! command -v "$CROSS" >/dev/null; then
    if [ -n "${LUXR_IN_DOCKER:-}" ]; then echo "no $CROSS in the container" >&2; exit 1; fi
    echo "no $CROSS here: building in Docker (schwung/scripts/Dockerfile)"
    docker build -q -t luxr-schwung-builder -f schwung/scripts/Dockerfile schwung/scripts >/dev/null
    docker run --rm -e LUXR_IN_DOCKER=1 -v "$ROOT:/build" -u "$(id -u):$(id -g)" -w /build luxr-schwung-builder \
        ./schwung/scripts/build.sh
    exit 0
fi

DIST=$OUT/dist/luxr
rm -rf "$OUT/dist" "$OUT/aarch64"
mkdir -p "$DIST" "$OUT/aarch64"
compile "$CROSS" "$OUT/aarch64/dsp.so" -march=armv8-a -mtune=cortex-a72 -DNDEBUG
"${CROSS_PREFIX:-aarch64-linux-gnu-}strip" --strip-unneeded "$OUT/aarch64/dsp.so" 2>/dev/null || true
cp "$OUT/aarch64/dsp.so" "$DIST/dsp.so"
chmod 0755 "$DIST/dsp.so"
cp schwung/src/module.json schwung/src/help.json "$DIST/"
cp LICENSE "$DIST/LICENSE"
cp schwung/LICENSE-SCHWUNG "$DIST/LICENSE-SCHWUNG"
cp schwung/NOTICE-LXR.txt "$DIST/NOTICE-LXR.txt"
(cd "$OUT/dist" && tar -czf ../luxr-module.tar.gz luxr/)
echo "built: $OUT/luxr-module.tar.gz ($(wc -c < "$OUT/luxr-module.tar.gz" | tr -d ' ') B)"
