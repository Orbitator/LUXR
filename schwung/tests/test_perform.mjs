// SPDX-License-Identifier: GPL-3.0-only
// LUXR for Ableton Move (Schwung): PERFORM (schwung/src/perform.js) against a pretend canvas host.
//   node schwung/tests/test_perform.mjs build/schwung/out/ui_hierarchy.json
// Every key it can turn must be one the plugin serves (ui_hierarchy, written by test_plugin); knobs, SHIFT, the jog,
// its click and the pads do what the header says; nothing is drawn off the 128 x 64 screen.
import fs from "node:fs";
import vm from "node:vm";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const hier = JSON.parse(fs.readFileSync(process.argv[2] || "build/schwung/out/ui_hierarchy.json", "utf8"));
const known = new Map();                         // key -> its param (min, max, options)
for (const lvl of Object.values(hier.levels))      // (a child level: its keys for every instance, by its template)
    for (const p of lvl.params || []) if (p.key) {
        if (lvl.child_key_template)
            for (let i = 0; i < lvl.child_count; i++)
                known.set(lvl.child_key_template.replace("{index}", String(i + (lvl.child_index_base || 0))).replace("{key}", p.key), p);
        else known.set(p.key, p);
    }
known.set("ui_voice", { key: "ui_voice" });

let fails = 0;
const check = (c, m) => { if (!c) { fails++; console.log("FAIL", m); } };

const vals = new Map();                          // the plugin, pretended: every key at its default
for (const [k, p] of known)                      // (the plugin's hierarchy has no defaults: a value mid-range)
    vals.set(k, String(p.default ?? (p.type === "int" ? Math.round((p.min + p.max) / 2) : 0)));
vals.set("k_vol", "100");
vals.set("ui_voice", "0:0");
const sets = [];
let shift = false, offscreen = 0, reads = 0;
const ctx = {
    width: 128, height: 64, state: {},
    clear() {}, setPixel() {}, drawLine() {},
    fillRect(x, y, w, h) { if (x < 0 || y < 0 || x + w > 128 || y + h > 64) offscreen++; },
    drawRect(x, y, w, h) { if (x < 0 || y < 0 || x + w > 128 || y + h > 64) offscreen++; },
    print(x, y, t) { if (x < 0 || y < 0 || y + 7 > 64 || x + this.measureText(t) > 129) { offscreen++; console.log("  off:", x, y, t); } },
    measureText(t) { return String(t).length * 5; },
    shiftHeld: () => shift,
    getParam(k) { reads++; check(known.has(k), "read of an unknown key " + k); return vals.get(k) ?? null; },
    setParam(k, v) { check(known.has(k), "write of an unknown key " + k); sets.push([k, v]); vals.set(k, v); return true; },
    now: () => 0, random: () => 0.5,
};
const sandbox = { globalThis: {} };
sandbox.globalThis = sandbox;
vm.runInNewContext(fs.readFileSync(path.join(here, "../src/perform.js"), "utf8"), sandbox);
const ov = sandbox.canvas_overlay;
check(ov && typeof ov.onMidi === "function" && typeof ov.draw === "function" && ov.wantsPads, "the overlay");
const midi = (...d) => ov.onMidi(ctx, { source: 0, data: d });
const last = () => sets[sets.length - 1] || [];

ov.onOpen(ctx, {});
check(reads <= 20, "onOpen reads " + reads + " values (each a round trip)");
ov.draw(ctx);
midi(0xB0, 71, 3);                               // knob 1 +3: the kick's PITCH 14 -> 17
check(last()[0] === "s1_p1" && last()[1] === String(+vals.get("s1_p1")), "knob 1: s1_p1 " + last());
shift = true;
midi(0xB0, 71, 1);                               // SHIFT + knob 1: SAMPLE
check(last()[0] === "s1_smp", "SHIFT + knob 1: s1_smp " + last());
ov.draw(ctx);
shift = false;
midi(0xB0, 78, 127);                             // knob 8 -1: DRIVE
check(last()[0] === "s1_p8", "knob 8: s1_p8 " + last());
midi(0xB0, 14, 1);                               // jog: the snare
check(vals.get("ui_voice") === "snd2", "jog: the snare is focused");
midi(0xB0, 72, 2);
check(last()[0] === "s2_p2", "knob 2 on the snare: " + last());
midi(0xB0, 14, 127); midi(0xB0, 14, 127);        // jog back twice: the cymbal (it wraps)
check(vals.get("ui_voice") === "snd8", "jog wraps to the cymbal: " + vals.get("ui_voice"));
midi(0x90, 69, 100);                             // pad 2 of the rack: the snare
check(vals.get("ui_voice") === "snd2", "a pad hit focuses its sound");
midi(0x90, 76 + 2, 100);                         // pad 7 (second row, third): the rim
check(vals.get("ui_voice") === "snd7", "pad 7: the rim " + vals.get("ui_voice"));
midi(0x90, 84 + 0, 100);                         // pad 9: the kick, accented
check(vals.get("ui_voice") === "snd1", "pad 9: the kick " + vals.get("ui_voice"));
midi(0x90, 72, 100);                             // the right half: nothing
check(vals.get("ui_voice") === "snd1", "the right half of the pads changes nothing");
midi(0xB0, 3, 127);                              // click: MIX
midi(0xB0, 73, 5);
check(last()[0] === "s1_dly", "MIX knob 3: delay send " + last());
shift = true; midi(0xB0, 71, 1); shift = false;
check(last()[0] === "s1_l1dest", "MIX SHIFT knob 1: LFO 1 dest " + last());
midi(0x90, 3, 127);                              // knob 4 touched
ov.draw(ctx);
midi(0x80, 3, 0);
midi(0xB0, 3, 127);                              // click: KIT
midi(0xB0, 71, 120);                             // VOLUME -8
check(last()[0] === "k_vol" && last()[1] === "92", "KIT knob 1: volume 92 " + last());
shift = true; midi(0xB0, 78, 1); ov.draw(ctx); shift = false;
check(last()[0] === "k_clim" && last()[1] === "1", "KIT SHIFT knob 8: the limiter on " + last());
midi(0xB0, 78, 5); shift = true; midi(0xB0, 78, 5); shift = false;
check(vals.get("k_clim") === "1", "a value stays in its range");
midi(0xB0, 3, 127);                              // click: SOUND again
// every key PERFORM can reach, every sound, both rows, all layers
for (let s = 0; s < 8; s++) {
    midi(0x90, 68 + (s >> 2) * 8 + (s & 3), 100);
    for (let l = 0; l < 3; l++) {
        for (const sh of [false, true]) { shift = sh; for (let k = 0; k < 8; k++) { midi(0xB0, 71 + k, 1); midi(0xB0, 71 + k, 127); } ov.draw(ctx); }
        midi(0xB0, 3, 127);
    }
}
shift = false;
check(offscreen === 0, offscreen + " drawings off the screen");
check(new Set(sets.map((s) => s[0])).size >= 8 * 32 + 16, "keys reached: " + new Set(sets.map((s) => s[0])).size);
{ const r = new Set(sets.map((s) => s[0])); const miss = [...known.keys()].filter((k) => /^(s\d_(p\d|smp|slvl|spitch|sdec|sstart|stone|sbend|syn|level|pan|dly|rev|drv|atk|gate|rel|l\d\w+))$/.test(k) && !r.has(k)); if (miss.length) console.log("  not reached:", miss.slice(0, 12).join(" ")); }
console.log(fails ? `test_perform: ${fails} failed` : `test_perform: ok (${new Set(sets.map((s) => s[0])).size} keys)`);
process.exit(fails ? 1 : 0);
