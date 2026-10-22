#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LUXR: sixteen transient samples of each sound's own, synthesized (no recordings): beater clicks for the kick, snaps
for the snare, bursts for the clap, ticks and sizzles for the hats, stick pings for the tom, wood for the rim, metal
for the cymbal. Signed 8 bit, 44.1 kHz, 2205 samples (50 ms) as the LXR's, the same seed each build.

  tools/gen_own_transients.py OUT.h      -> OWN_TRANS[8][16][2205]"""
import math
import random
import sys
from pathlib import Path

FS, LEN, N = 44100, 2205, 16
TRACKS = ["BD", "SD", "CP", "CH", "OH", "TM", "RS", "CY"]


def env(i, tau_ms):
    return math.exp(-i / (tau_ms * FS / 1000))


class Biquad:                                       # RBJ band-pass (constant peak gain) / high-pass
    def __init__(self, kind, fc, q):
        w = 2 * math.pi * min(fc, FS * 0.45) / FS
        a = math.sin(w) / (2 * q)
        c = math.cos(w)
        if kind == "bp":
            b0, b1, b2 = a, 0, -a
        else:
            b0, b1, b2 = (1 + c) / 2, -(1 + c), (1 + c) / 2
        a0 = 1 + a
        self.k = (b0 / a0, b1 / a0, b2 / a0, -2 * c / a0, (1 - a) / a0)
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0

    def __call__(self, x):
        b0, b1, b2, a1, a2 = self.k
        y = b0 * x + b1 * self.x1 + b2 * self.x2 - a1 * self.y1 - a2 * self.y2
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        return y


def kick(r):                                        # a beater: a fast falling sine and a click of noise
    f0, f1, sw = r.uniform(1200, 5000), r.uniform(50, 160), r.uniform(1.5, 14)
    tau, ntau, nmix = r.uniform(6, 28), r.uniform(0.4, 3), r.uniform(0.1, 0.8)
    lp, ph, out = 0.0, 0.0, []
    for i in range(LEN):
        f = f1 + (f0 - f1) * math.exp(-i / (sw * FS / 1000))
        ph += 2 * math.pi * f / FS
        lp += (r.uniform(-1, 1) - lp) * 0.35
        out.append(math.sin(ph) * env(i, tau) + nmix * lp * env(i, ntau))
    return out


def snare(r):                                       # a snap: band-passed noise over a short ping
    bp = Biquad("bp", r.uniform(1500, 6500), r.uniform(0.6, 3))
    tau, ptau, pf, pm = r.uniform(4, 30), r.uniform(5, 25), r.uniform(170, 360), r.uniform(0, 0.7)
    return [bp(r.uniform(-1, 1)) * 2.5 * env(i, tau) + pm * math.sin(2 * math.pi * pf * i / FS) * env(i, ptau)
            for i in range(LEN)]


def clap(r):                                        # hands: bursts of band-passed noise, the last one longer
    bp = Biquad("bp", r.uniform(800, 2600), r.uniform(0.8, 2.5))
    n, gap, tail = r.randint(2, 5), r.uniform(3.5, 11), r.uniform(8, 35)
    starts = [round(k * gap * FS / 1000 * r.uniform(0.85, 1.15)) for k in range(n)]
    out = []
    for i in range(LEN):
        a = 0.0
        for k, s in enumerate(starts):
            if i >= s:
                a = max(a, env(i - s, tail if k == n - 1 else r.uniform(1.2, 3)))
        out.append(bp(r.uniform(-1, 1)) * 3 * a)
    return out


def hat(r, open_):                                  # sizzle: six detuned squares and noise, high-passed
    hp, hp2 = Biquad("hp", r.uniform(5000, 9500), 0.7), Biquad("hp", r.uniform(4000, 8000), 0.7)
    base = r.uniform(250, 600)
    ratios = [1, 1.342, 1.2312, 1.6532, 1.9523, 2.1523]
    tau, mix = (r.uniform(12, 45) if open_ else r.uniform(1.2, 10)), r.uniform(0.2, 0.9)
    out = []
    for i in range(LEN):
        sq = sum(1 if math.sin(2 * math.pi * base * q * i / FS) > 0 else -1 for q in ratios) / 6
        out.append(hp2(hp(sq * (1 - mix) + r.uniform(-1, 1) * mix)) * 1.8 * env(i, tau))
    return out


def tom(r):                                         # a stick on a head: a falling ping and a little noise
    f0, drop, tau, nm = r.uniform(250, 1300), r.uniform(0.3, 0.8), r.uniform(8, 40), r.uniform(0.05, 0.5)
    ph, out = 0.0, []
    for i in range(LEN):
        f = f0 * (drop + (1 - drop) * math.exp(-i / (0.008 * FS)))
        ph += 2 * math.pi * f / FS
        out.append(math.sin(ph) * env(i, tau) + nm * r.uniform(-1, 1) * env(i, 1.5))
    return out


def rim(r):                                         # wood: a resonant band struck by a click
    bp = Biquad("bp", r.uniform(900, 3800), r.uniform(5, 22))
    bp2 = Biquad("bp", r.uniform(400, 1200), r.uniform(3, 10))
    tau, out = r.uniform(4, 22), []
    for i in range(LEN):
        x = (1.0 if i < 3 else 0.0) + r.uniform(-1, 1) * env(i, 0.6) * 0.5
        out.append((bp(x) * 6 + bp2(x) * 3) * env(i, tau * 2))
    return out


def cymbal(r):                                      # metal: an FM burst with ringing ratios, high-passed noise
    hp = Biquad("hp", r.uniform(3000, 7000), 0.7)
    fc, ratio, idx = r.uniform(2500, 8000), r.uniform(1.37, 3.71), r.uniform(2, 9)
    tau, nm = r.uniform(12, 48), r.uniform(0.1, 0.6)
    out = []
    for i in range(LEN):
        t = i / FS
        x = math.sin(2 * math.pi * fc * t + idx * env(i, tau) * math.sin(2 * math.pi * fc * ratio * t))
        out.append(hp(x * (1 - nm) + r.uniform(-1, 1) * nm) * 1.6 * env(i, tau))
    return out


GEN = [kick, snare, clap, lambda r: hat(r, False), lambda r: hat(r, True), tom, rim, cymbal]


def to8(x):
    pk = max(1e-9, max(abs(v) for v in x))
    out = []
    for i, v in enumerate(x):
        if i > LEN - 300:                            # (a fade over the last 7 ms: no step at the end)
            v *= (LEN - i) / 300
        out.append(max(-127, min(127, round(v / pk * 120))))
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    L = ["/* generated by tools/gen_own_transients.py: do not edit (GPL-3.0-only, synthesized, no recordings) */",
         "#pragma once", "#include <stdint.h>", f"#define OWN_TRANS_N {N}", f"#define OWN_TRANS_LEN {LEN}",
         f"static const int8_t OWN_TRANS[{len(GEN)}][OWN_TRANS_N][OWN_TRANS_LEN] = {{"]
    for t, g in enumerate(GEN):
        L.append("{")
        for k in range(N):
            s = to8(g(random.Random(1000 * t + k + 7)))
            L.append("{")
            for i in range(0, LEN, 32):
                L.append(",".join(str(v) for v in s[i:i + 32]) + ",")
            L.append("},")
        L.append("},")
    L.append("};")
    Path(sys.argv[1]).write_text("\n".join(L) + "\n")
    print(f"own transients: {len(GEN)} sounds x {N} x {LEN} B -> {sys.argv[1]}")


if __name__ == "__main__":
    main()
