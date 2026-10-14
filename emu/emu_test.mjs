// ONE CLICK FM-1 emulator check (node): the wasm runs without imports, plays, draws, answers the companion,
// and renders the same samples as the native build (emu/native_ref.c) for the same gestures.
import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]);
const { instance } = await WebAssembly.instantiate(wasm, {});
const e = instance.exports, mem = () => e.memory.buffer;
let fails = 0;
const check = (c, m) => { if (!c) { fails++; console.log("FAIL", m); } };
check(WebAssembly.Module.imports(new WebAssembly.Module(wasm)).length === 0, "imports");
const B_PLAY = 10, B_REC = 11;
e.emu_init(11);
const out = [];
function run(ms, rec) {
  const n = Math.round(ms * 44.1);
  for (let k = 0; k < n; k += 128) {
    const m = Math.min(128, n - k); e.emu_render(m);
    const l = new Float32Array(mem(), e.emu_out_l(), m), r = new Float32Array(mem(), e.emu_out_r(), m);
    if (rec) for (let i = 0; i < m; i++) out.push(l[i], r[i]);
  }
}
function press(b) { e.emu_buttons(1 << b); run(20); e.emu_buttons(0); run(20); }
run(200); press(B_PLAY); run(3000, true); press(B_REC); run(3000, true);
let pk = 0, nan = 0; for (const x of out) { if (!Number.isFinite(x)) nan++; pk = Math.max(pk, Math.abs(x)); }
check(nan === 0 && pk > 0.05 && pk < 1, `audio peak ${pk} nan ${nan}`);
check(e.emu_playing() === 1, "playing");
const scr = new Uint16Array(mem(), e.emu_screen(), 240 * 240); let lit = 0; for (const p of scr) lit += p !== 0;
check(lit > 20000, `screen ${lit}`);
check(new Uint8Array(mem(), e.emu_leds_btn(), 14)[B_PLAY] === 1, "PLAY led");
// the companion: dump request in, 0x11 answer out
new Uint8Array(mem(), e.emu_in_buf(), 4).set([0x7D, 0x4F, 0x43, 0x10]);
check(e.emu_sysex(4) === 1, "sysex taken");
const n = e.emu_out_take(); const o = new Uint8Array(mem(), e.emu_out_buf(), n);
check(n > 2000 && o[0] === 0xF0 && o[4] === 0x11 && o[n - 1] === 0xF7, `dump ${n}`);
// the same as the native build
if (process.argv[3]) {
  const ref = new Float32Array(fs.readFileSync(process.argv[3]).buffer.slice(0));
  let diff = 0; for (let i = 0; i < Math.min(ref.length, out.length); i++) diff = Math.max(diff, Math.abs(ref[i] - out[i]));
  check(ref.length === out.length && diff === 0, `native vs wasm: ${ref.length} / ${out.length} samples, max diff ${diff}`);
}
console.log(`emu_test: ${fails ? "FAIL" : "ok"} (peak ${pk.toFixed(3)}, screen ${lit} px, dump ${n} B)`);
process.exit(fails ? 1 : 0);
