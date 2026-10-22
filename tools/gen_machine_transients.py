#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LUXR: sixteen transients of each sound in the manner of the classic drum machines - Roland TR-808, TR-909, TR-606,
TR-707, Oberheim DMX, Simmons SDS-6, SDS-7, SDS-8. Synthesized after how those machines make their sounds (the 808's
bridged-T resonators and six square oscillators, the 909's swept kick and noise snare, the 606's thin analog voices,
the 707's and the DMX's 8-bit PCM grit, the Simmons' swept oscillator and filtered noise); no recordings of them.
Signed 8 bit at 22.05 kHz (the 707 and the DMX played at about that), 1024 samples (46 ms).

  tools/gen_machine_transients.py OUT.h      -> MACH_TRANS[8][16][1024], MACH_NAME[16]"""
import math
import random
import sys
from pathlib import Path

FS, LEN = 22050, 1024
NAMES = ["808 A", "808 B", "808 C", "909 A", "909 B", "909 C", "606 A", "606 B",
         "707 A", "707 B", "DMX A", "DMX B", "SDS6", "SDS7 A", "SDS7 B", "SDS8"]
MACHINE = ["808", "808", "808", "909", "909", "909", "606", "606",
           "707", "707", "DMX", "DMX", "SDS6", "SDS7", "SDS7", "SDS8"]
BD, SD, CP, CH, OH, TM, RS, CY = range(8)


def env(i, tau_ms):
    return math.exp(-i / max(1e-3, tau_ms * FS / 1000))


class Biquad:                                       # RBJ band-pass (constant peak gain) / high-pass / low-pass
    def __init__(self, kind, fc, q):
        w = 2 * math.pi * min(fc, FS * 0.45) / FS
        a, c = math.sin(w) / (2 * q), math.cos(w)
        b = {"bp": (a, 0, -a), "hp": ((1 + c) / 2, -(1 + c), (1 + c) / 2), "lp": ((1 - c) / 2, 1 - c, (1 - c) / 2)}[kind]
        a0 = 1 + a
        self.k = (b[0] / a0, b[1] / a0, b[2] / a0, -2 * c / a0, (1 - a) / a0)
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0

    def __call__(self, x):
        b0, b1, b2, a1, a2 = self.k
        y = b0 * x + b1 * self.x1 + b2 * self.x2 - a1 * self.y1 - a2 * self.y2
        self.x2, self.x1, self.y2, self.y1 = self.x1, x, self.y1, y
        return y


def sweep(f0, f1, ms, tau, r, click=0.0, ctau=0.5, wave="sine"):   # a pitched body falling from f0 to f1
    ph, out, lp = 0.0, [], 0.0
    for i in range(LEN):
        f = f1 + (f0 - f1) * env(i, ms)
        ph += 2 * math.pi * f / FS
        x = math.sin(ph) if wave == "sine" else (2 / math.pi) * math.asin(math.sin(ph))
        lp += (r.uniform(-1, 1) - lp) * 0.5
        out.append(x * env(i, tau) + click * lp * env(i, ctau))
    return out


def resonators(freqs, tau, r, noise=0.0, ntau=1.0, hp=0.0):   # bridged-T: rings struck at once, plus noise
    h = Biquad("hp", hp, 0.7) if hp else None
    out = []
    for i in range(LEN):
        x = sum(a * math.sin(2 * math.pi * f * i / FS) for f, a in freqs) * env(i, tau)
        n = r.uniform(-1, 1) * noise * env(i, ntau)
        out.append(x + (h(n) if h else n))
    return out


def noise_snare(fc, q, tau, tones, ttau, r, mix=0.6, hp=0.0):    # tones under band-passed noise (909 / 707 style)
    bp, h = Biquad("bp", fc, q), Biquad("hp", hp, 0.7) if hp else None
    out = []
    for i in range(LEN):
        n = bp(r.uniform(-1, 1)) * 2.5
        n = h(n) if h else n
        t = sum(math.sin(2 * math.pi * f * i / FS) for f in tones) / max(1, len(tones))
        out.append(mix * n * env(i, tau) + (1 - mix) * t * env(i, ttau))
    return out


def clap(n, gap, tail, fc, q, r):                   # bursts, then the tail (808 / 909 / 707 hands)
    bp = Biquad("bp", fc, q)
    starts = [round(k * gap * FS / 1000) for k in range(n)]
    out = []
    for i in range(LEN):
        a = 0.0
        for k, s0 in enumerate(starts):
            if i >= s0:
                a = max(a, env(i - s0, tail if k == n - 1 else 1.6))
        out.append(bp(r.uniform(-1, 1)) * 3 * a)
    return out


def squares(freqs, bpf, tau, r, hp=6000, noise=0.0):   # the 808 / 606 metal: six square oscillators, band-passed
    bp, h = Biquad("bp", bpf, 1.2), Biquad("hp", hp, 0.7)
    out = []
    for i in range(LEN):
        x = sum(1 if math.sin(2 * math.pi * f * i / FS) > 0 else -1 for f in freqs) / len(freqs)
        out.append(h(bp(x + r.uniform(-1, 1) * noise)) * 3 * env(i, tau))
    return out


def hiss(hp, tau, r, bp=0.0, q=1.0):                # sampled metal: high-passed noise (909 / 707 / DMX hats)
    h, b = Biquad("hp", hp, 0.7), Biquad("bp", bp, q) if bp else None
    out = []
    for i in range(LEN):
        x = h(r.uniform(-1, 1))
        out.append((b(x) * 2 + x * 0.5 if b else x) * env(i, tau))
    return out


def simmons(f0, f1, ms, tau, nmix, nfc, r, wave="tri"):   # the Simmons pew: a swept oscillator and swept noise
    ph, lp, out = 0.0, 0.0, []
    for i in range(LEN):
        f = f1 + (f0 - f1) * env(i, ms)
        ph += 2 * math.pi * f / FS
        x = (2 / math.pi) * math.asin(math.sin(ph)) if wave == "tri" else math.sin(ph)
        k = 1 - math.exp(-2 * math.pi * max(200, nfc * (0.3 + 0.7 * env(i, ms * 2))) / FS)   # (its low-pass closes)
        lp += (r.uniform(-1, 1) - lp) * k
        out.append(x * (1 - nmix) * env(i, tau) + nmix * lp * 2.5 * env(i, tau * 0.7))
    return out


def lofi(x, bits, lp_hz):                           # PCM grit: a low-pass and fewer bits (707, DMX, the 909's hats)
    lp, out, q = Biquad("lp", lp_hz, 0.7), [], 2 ** (bits - 1)
    pk = max(1e-9, max(abs(v) for v in x))
    for v in x:
        out.append(round(lp(v / pk) * q) / q)
    return out


def model(m, t, r):
    j = lambda lo, hi: r.uniform(lo, hi)            # (the variants' spread)
    if m == "808":
        return [sweep(j(90, 120), j(48, 58), j(8, 20), j(40, 90), r, click=j(0.05, 0.2)),
                resonators([(j(230, 245), 1), (j(470, 485), 0.6)], j(14, 30), r, noise=j(0.7, 1.2), ntau=j(20, 45), hp=1800),
                clap(4, j(9, 11), j(25, 45), j(900, 1100), 1.4, r),
                squares([205.3, 304.4, 369.6, 522.7, 540, 800], j(7000, 8500), j(4, 12), r),
                squares([205.3, 304.4, 369.6, 522.7, 540, 800], j(7000, 8500), j(30, 60), r),
                resonators([(j(100, 210), 1)], j(30, 70), r, noise=j(0.1, 0.25), ntau=8),
                resonators([(j(1650, 1750), 1), (j(450, 470), 0.8)], j(5, 10), r, noise=0.1, ntau=1),
                squares([205.3, 304.4, 369.6, 522.7, 540, 800], j(3200, 3600), j(40, 80), r, hp=3000)][t]
    if m == "909":
        return [sweep(j(280, 420), j(50, 60), j(4, 9), j(35, 70), r, click=j(0.4, 0.9), ctau=j(0.6, 1.5), wave="tri"),
                noise_snare(j(3000, 5000), 0.8, j(18, 35), [j(175, 190), j(320, 340)], j(10, 20), r, mix=j(0.5, 0.7)),
                clap(4, j(7, 9), j(30, 50), j(1100, 1400), 1.6, r),
                lofi(hiss(7000, j(5, 12), r), 6, 10000),
                lofi(hiss(6500, j(35, 70), r), 6, 10000),
                sweep(j(280, 520), j(120, 260), j(10, 25), j(40, 90), r, click=0.2),
                resonators([(j(480, 520), 1), (j(1680, 1720), 0.7)], j(4, 8), r, noise=0.3, ntau=0.8),
                lofi(hiss(5000, j(60, 100), r, bp=7000, q=0.8), 6, 10000)][t]
    if m == "606":
        return [sweep(j(110, 150), j(70, 90), j(5, 10), j(20, 45), r, click=0.1),
                noise_snare(j(5000, 7000), 0.7, j(12, 25), [j(290, 330)], j(8, 14), r, mix=0.75, hp=2000),
                clap(3, j(8, 12), j(15, 30), j(1500, 2000), 1.4, r),
                squares([245, 306, 365, 415, 437, 619], j(8500, 9500), j(2, 6), r, hp=7500),
                squares([245, 306, 365, 415, 437, 619], j(8500, 9500), j(20, 45), r, hp=7500),
                resonators([(j(150, 280), 1)], j(20, 45), r, noise=0.15, ntau=5),
                resonators([(j(1900, 2100), 1)], j(3, 6), r, noise=0.2, ntau=0.6),
                squares([245, 306, 365, 415, 437, 619], j(4000, 5000), j(40, 70), r, hp=3500)][t]
    if m in ("707", "DMX"):
        bits, lp = (7, 9000) if m == "707" else (8, 8000)
        body = [sweep(j(150, 220), j(55, 70), j(6, 12), j(45, 90) if m == "DMX" else j(30, 60), r, click=j(0.3, 0.6)),
                noise_snare(j(2500, 4000), 0.9, j(25, 45) if m == "DMX" else j(18, 30), [j(180, 220)], j(15, 25), r,
                            mix=j(0.55, 0.75)),
                clap(int(j(3, 5.99)), j(6, 10), j(35, 60), j(1000, 1600), 1.2, r),
                hiss(j(6000, 8000), j(4, 10), r, bp=j(8000, 9000), q=1.2),
                hiss(j(5500, 7500), j(40, 90), r, bp=j(7500, 9000), q=1.0),
                sweep(j(250, 450), j(110, 220), j(12, 30), j(50, 100), r, click=0.3),
                resonators([(j(700, 900), 1), (j(1900, 2300), 0.8)], j(4, 9), r, noise=0.3, ntau=1),
                hiss(j(3500, 5000), j(80, 140), r, bp=j(5500, 7000), q=0.6)][t]
        return lofi(body, bits, lp)
    # Simmons: SDS-6 the big sweep, SDS-7 brighter with more noise, SDS-8 punchy and short
    big, nz, ln = (1.6, 0.25, 1.0) if m == "SDS6" else (1.0, 0.45, 0.9) if m == "SDS7" else (0.8, 0.3, 0.6)
    return [simmons(j(180, 260) * big, j(45, 60), j(10, 25) * big, j(60, 120) * ln, nz * 0.5, 2000, r, "sine"),
            simmons(j(450, 650), j(160, 220), j(8, 15), j(25, 50) * ln, nz + 0.25, 4000, r),
            simmons(j(1200, 1600), j(700, 900), j(3, 6), j(20, 35) * ln, 0.9, 2500, r),
            simmons(j(7000, 9000), j(5000, 6000), 2, j(4, 10) * ln, 0.95, 8000, r),
            simmons(j(7000, 9000), j(4500, 5500), 4, j(30, 60) * ln, 0.95, 7000, r),
            simmons(j(500, 900) * big, j(90, 180), j(15, 40) * big, j(60, 120) * ln, nz, 1500, r),
            simmons(j(1500, 2500), j(900, 1300), j(2, 4), j(5, 10), nz, 3000, r),
            simmons(j(5000, 7000), j(3000, 4000), j(10, 20), j(50, 100) * ln, 0.85, 6000, r)][t]


def to8(x):
    pk = max(1e-9, max(abs(v) for v in x))
    out = []
    for i, v in enumerate(x):
        if i > LEN - 150:                            # (a fade over the last 7 ms: no step at the end)
            v *= (LEN - i) / 150
        out.append(max(-127, min(127, round(v / pk * 120))))
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    L = ["/* generated by tools/gen_machine_transients.py: do not edit (GPL-3.0-only, synthesized after the classic",
         " * drum machines, no recordings of them) */", "#pragma once", "#include <stdint.h>",
         "#define MACH_TRANS_N 16", f"#define MACH_TRANS_LEN {LEN}   /* at 22.05 kHz */",
         "static const char *const MACH_NAME[MACH_TRANS_N] = {" + ", ".join(f'"{n}"' for n in NAMES) + "};",
         f"static const int8_t MACH_TRANS[8][MACH_TRANS_N][MACH_TRANS_LEN] = {{"]
    for t in range(8):
        L.append("{")
        for k in range(16):
            s = to8(model(MACHINE[k], t, random.Random(5000 + 97 * t + k)))
            L.append("{")
            for i in range(0, LEN, 32):
                L.append(",".join(str(v) for v in s[i:i + 32]) + ",")
            L.append("},")
        L.append("},")
    L.append("};")
    Path(sys.argv[1]).write_text("\n".join(L) + "\n")
    print(f"machine transients: 8 sounds x 16 x {LEN} B (22.05 kHz) -> {sys.argv[1]}")


if __name__ == "__main__":
    main()
