/* SPDX-License-Identifier: GPL-3.0-only
 * LUXR for Ableton Move (Schwung): PERFORM - the eight knobs as on LUXR, a row and its SHIFT row.
 *
 * A fullscreen canvas (root page > Perform). Schwung hands a canvas the knobs, the jog wheel, the jog click (it is
 * "enterable") and, asked for, the pads; Shift stays the host's, but a canvas may ask whether it is held.
 *
 *   KNOB 1-8          row A of the layer            SHIFT + KNOB 1-8   row B of the layer
 *   JOG turn          the sound (Kick .. Cymbal)    JOG click          the next layer: SOUND > MIX > KIT
 *   a PAD hit         that sound (it still plays)   touching a KNOB    its name and value at the bottom
 *   SHIFT + JOG       out (the host's)              BACK               out
 *
 *   layer   row A (knobs)                                      row B (SHIFT + knobs)
 *   SOUND   the synth layer: LUXR's EDIT 1-4 and 5-8           the sample layer: SAMPLE LEVEL PITCH DECAY START TONE
 *                                                              BEND SYNTH
 *   MIX     LEVEL PAN DELAY REVERB DRIVE ATTACK GATE RELEASE    LFO 1 (DEST WAVE RATE AMOUNT), LFO 2
 *   KIT     VOLUME TUNE DECAY FILTER RESO DRIVE DLY-RET          DLY-TIME FEEDBACK SIZE DAMP THRESH RATIO MAKEUP LIMITER
 *           REV-RET
 */
(function () {
    "use strict";
    const KNOB_CC = 71, JOG_CC = 14, CLICK_CC = 3, PAD0 = 68;
    const SND = ["KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "TOM", "RIM", "CYMBAL"];
    const KIND = [0, 1, 2, 3, 4, 0, 0, 5];       /* (lx_core.h TRK_KIND) */
    const SYN = [                                /* (lx_synth.c LX_LABEL, short) */
        ["PITCH", "DECAY", "P.ENV", "P.DEC", "WAVE", "FM", "CLICK", "DRIVE"],
        ["PITCH", "DECAY", "NOISE", "N.DEC", "TONE", "SNAP", "CLICK", "DRIVE"],
        ["TONE", "DECAY", "SPRD", "BURST", "COLOR", "RESO", "CLICK", "DRIVE"],
        ["PITCH", "DECAY", "FM", "FILT", "NOISE", "CLICK", "RESO", "DRIVE"],
        ["PITCH", "DECAY", "FM", "FILT", "NOISE", "CLICK", "RESO", "DRIVE"],
        ["PITCH", "DECAY", "RATIO", "FM", "FILT", "RESO", "NOISE", "DRIVE"],
    ];
    const LXR = ["CLK", "CK2", "TIK", "KIK", "RIM", "DRP", "HAT", "CLP", "KK2", "SNR", "TOM", "SP2"];
    const MACH = ["808A", "808B", "909A", "909B", "606", "707A", "707B", "DMXA", "DMXB", "LM1A", "LM1B", "LM2A", "LM2B",
                  "SDS6", "SDS7", "SDS8"];
    const SMP = ["OFF", "SNAP"].concat(LXR, Array.from({ length: 16 }, (_, i) => "OWN" + (i + 1)), MACH, ["WAV"]);
    const O = {
        dest: ["OFF", "PITCH", "FILT", "DECAY", "LEVEL", "PAN", "SMP"],
        wave: ["SINE", "TRI", "SAW", "SQR", "S&H"],
        rate: ["1/16", "1/8", "1/4", "1/2", "1BAR", "2BAR", "4BAR", "8BAR"],
        div: ["1/8", "3/16", "1/4", "3/8", "1/2", "3/4"],
        ratio: ["1:1", "1.5", "2:1", "3:1", "4:1", "6:1", "10:1", "LIM"],
        onoff: ["OFF", "ON"],
    };
    /* a knob: key (s_ prefixed: the sound's), label, min, max, options */
    const P = (k, l, min, max, opt) => ({ k, l, min, max, opt });
    const SOUND_B = [P("s_smp", "SMP", 0, 46, SMP), P("s_slvl", "S.LVL", 0, 127), P("s_spitch", "S.PIT", 0, 127),
                     P("s_sdec", "S.DEC", 0, 127), P("s_sstart", "START", 0, 127), P("s_stone", "S.TON", -64, 63),
                     P("s_sbend", "BEND", -64, 63), P("s_syn", "SYNTH", 0, 127)];
    const MIX_A = [P("s_level", "LEVEL", 0, 127), P("s_pan", "PAN", -64, 63), P("s_dly", "DELAY", 0, 127),
                   P("s_rev", "REVRB", 0, 127), P("s_drv", "DRIVE", 0, 127), P("s_atk", "ATK", 0, 127),
                   P("s_gate", "GATE", 0, 64), P("s_rel", "REL", 0, 127)];
    const MIX_B = [P("s_l1dest", "L1>", 0, 6, O.dest), P("s_l1wave", "L1~", 0, 4, O.wave), P("s_l1rate", "L1 T", 0, 7, O.rate),
                   P("s_l1amt", "L1 A", -64, 63), P("s_l2dest", "L2>", 0, 6, O.dest), P("s_l2wave", "L2~", 0, 4, O.wave),
                   P("s_l2rate", "L2 T", 0, 7, O.rate), P("s_l2amt", "L2 A", -64, 63)];
    const KIT_A = [P("k_vol", "VOL", 0, 127), P("k_tune", "TUNE", -24, 24), P("k_decay", "DECAY", 8, 127),
                   P("k_mflt", "FILT", -64, 63), P("k_mres", "RESO", 0, 127), P("k_drv_amt", "DRIVE", 0, 127),
                   P("k_dly_lvl", "DLY", 0, 127), P("k_rev_lvl", "REV", 0, 127)];
    const KIT_B = [P("k_dly_div", "TIME", 0, 5, O.div), P("k_dly_fb", "FDBK", 0, 127), P("k_rev_size", "SIZE", 0, 127),
                   P("k_rev_damp", "DAMP", 0, 127), P("k_cthr", "THRSH", 0, 40), P("k_crat", "RATIO", 0, 7, O.ratio),
                   P("k_cmak", "MAKUP", 0, 24), P("k_clim", "LIMIT", 0, 1, O.onoff)];
    const LAYERS = ["SOUND", "MIX", "KIT"];

    function row(st, shift) {                    /* the eight knobs now: [{key, l, min, max, opt}] */
        const s = st.snd;
        let r;
        if (st.layer === 0)
            r = shift ? SOUND_B : SYN[KIND[s]].map((l, i) => P("s_p" + (i + 1), l, 0, 127));
        else if (st.layer === 1)
            r = shift ? MIX_B : MIX_A;
        else
            r = shift ? KIT_B : KIT_A;
        return r.map((p) => Object.assign({ key: p.k.replace(/^s_/, "s" + (s + 1) + "_") }, p));
    }
    function num(v, d) {
        const n = parseInt(v, 10);
        return Number.isFinite(n) ? n : d;
    }
    function fetchRows(ctx) {                    /* both rows' values (onMidi / onOpen only: a read is a round trip) */
        const st = ctx.state;
        for (const p of row(st, false).concat(row(st, true)))
            if (!(p.key in st.vals)) st.vals[p.key] = num(ctx.getParam(p.key), p.min);
    }
    function text(p, v) {
        if (v === undefined) return "-";
        if (p.opt) return p.opt[Math.max(0, Math.min(p.opt.length - 1, v))];
        return String(v);
    }
    function rel(v) { return v < 64 ? v : v - 128; }   /* (Move's encoders: 1..63 up, 65..127 down) */
    function padSound(st, note) {                /* a pad (68..99) of the drum rack's 4 x 4 (the left half) -> a sound */
        const i = note - PAD0, r = i >> 3, c = i & 7;
        if (i < 0 || i >= 32 || c >= 4) return -1;
        const idx = r * 4 + c;                   /* (drum pad 1..16: notes 36..51) */
        if (st.map === 1) {
            const GM = { 35: 0, 36: 0, 38: 1, 40: 1, 39: 2, 42: 3, 44: 3, 46: 4, 41: 5, 43: 5, 45: 5, 47: 5, 48: 5,
                         50: 5, 37: 6, 49: 7, 51: 7 };
            const t = GM[36 + idx];
            return t === undefined ? -1 : t;
        }
        return st.map === 2 ? -1 : idx % 8;
    }
    function setSound(ctx, s) {
        const st = ctx.state;
        st.snd = ((s % 8) + 8) % 8;
        ctx.setParam("ui_voice", "snd" + (st.snd + 1));   /* (the knob grid follows: focus_param) */
        fetchRows(ctx);
    }

    globalThis.canvas_overlay = {
        wantsPads: true,
        onOpen(ctx) {
            const st = ctx.state;
            st.vals = {};                        /* (a kit may have been loaded meanwhile: read afresh) */
            st.layer = st.layer || 0;
            st.touch = -1;
            const v = String(ctx.getParam("ui_voice") || "snd1");
            st.snd = Math.max(0, Math.min(7, num(v.replace(/^snd/, ""), 1) - 1));
            st.map = num(ctx.getParam("k_map"), 0);
            fetchRows(ctx);
        },
        onMidi(ctx, msg) {
            const st = ctx.state, d = msg && msg.data;
            if (!d || d.length < 3) return;
            const type = d[0] & 0xF0;
            if (type === 0xB0) {
                const cc = d[1], v = d[2];
                if (cc >= KNOB_CC && cc < KNOB_CC + 8) {
                    const p = row(st, ctx.shiftHeld())[cc - KNOB_CC];
                    let cur = p.key in st.vals ? st.vals[p.key] : num(ctx.getParam(p.key), p.min);
                    const n = Math.max(p.min, Math.min(p.max, cur + rel(v)));
                    st.touch = cc - KNOB_CC;
                    if (n !== cur) {
                        st.vals[p.key] = n;
                        ctx.setParam(p.key, String(n));
                        if (p.key === "k_map") st.map = n;
                    }
                } else if (cc === JOG_CC) {
                    setSound(ctx, st.snd + (rel(d[2]) > 0 ? 1 : -1));
                } else if (cc === CLICK_CC && v > 0) {
                    st.layer = (st.layer + 1) % LAYERS.length;
                    fetchRows(ctx);
                }
            } else if (type === 0x90 && d[2] > 0) {
                if (d[1] < 8) {
                    st.touch = d[1];                  /* a knob touched */
                } else if (d[1] >= PAD0 && d[1] < PAD0 + 32) {
                    const s = padSound(st, d[1]);
                    if (s >= 0 && s !== st.snd) setSound(ctx, s);
                }
            } else if ((type === 0x80 || (type === 0x90 && d[2] === 0)) && d[1] < 8 && st.touch === d[1]) {
                st.touch = -1;
            }
        },
        handleBack() { return false; },
        draw(ctx) {
            const st = ctx.state, shift = ctx.shiftHeld();
            if (!st.vals) return;
            const r = row(st, shift), W = ctx.width;
            ctx.clear();
            ctx.print(0, 0, (st.layer === 2 ? "KIT" : SND[st.snd]) + " " + LAYERS[st.layer], 1);
            if (shift) {
                const w = ctx.measureText("SHIFT") + 4;
                ctx.fillRect(W - w, 0, w, 9, 1);
                ctx.print(W - w + 2, 1, "SHIFT", 0);
            }
            ctx.fillRect(0, 9, W, 1, 1);
            for (let i = 0; i < 8; i++) {
                const p = r[i], x = (i % 4) * 32, y = 12 + (i >> 2) * 22, v = st.vals[p.key];
                ctx.print(x + 1, y, p.l.slice(0, 5), 1);
                ctx.print(x + 1, y + 9, text(p, v).slice(0, 5), 1);
                if (!p.opt && v !== undefined) {   /* a bar for a number */
                    const f = (v - p.min) / (p.max - p.min);
                    ctx.fillRect(x + 1, y + 18, Math.max(1, Math.round(29 * f)), 1, 1);
                }
                if (i === st.touch) ctx.drawRect(x, y - 2, 31, 22, 1);
            }
            ctx.fillRect(0, 55, W, 1, 1);
            ctx.print(0, 57, st.touch >= 0 ? "K" + (st.touch + 1) + " " + r[st.touch].l + " " +
                      text(r[st.touch], st.vals[r[st.touch].key]) : "JOG SOUND  CLICK LAYER", 1);
        },
    };
})();
