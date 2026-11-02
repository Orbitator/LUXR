#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# LUXR for Ableton Move: the module built by schwung/scripts/build.sh onto a Move with Schwung (over SSH, as
# ableton@move.local; MOVE_HOST overrides). Restart Schwung on the Move afterwards (or reload the slot).
set -euo pipefail
cd "$(dirname "$0")/../.."
HOST=${MOVE_HOST:-ableton@move.local}
TARGET=/data/UserData/schwung/modules/sound_generators/luxr
test -d build/schwung/dist/luxr || { echo "build/schwung/dist/luxr missing: run schwung/scripts/build.sh first" >&2; exit 1; }
ssh "$HOST" "mkdir -p $TARGET"
scp -r build/schwung/dist/luxr/. "$HOST:$TARGET/"
ssh "$HOST" "chmod -R a+rw $TARGET"
echo "installed to $HOST:$TARGET"
