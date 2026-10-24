// node emu/lx_emu_test.mjs build/emu/luxr.wasm: boot, screen, sound, the backup protocol, a .luxr import
import { readFileSync } from "fs";
const bytes = readFileSync(process.argv[2]);
async function boot() {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const ex = instance.exports; ex.emu_master(760); ex.emu_init(7); return ex;
}
const fail = (m) => { console.log("FAIL", m); process.exitCode = 1; };
const pack7 = (b) => { const o = []; for (let i = 0; i < b.length; i += 7) { const c = b.slice(i, i + 7); let h = 0; c.forEach((x, k) => h |= ((x >> 7) & 1) << k); o.push(h, ...c.map((x) => x & 127)); } return o; };
const unpack7 = (d) => { const o = []; for (let i = 0; i < d.length;) { const hi = d[i++]; for (let k = 0; k < 7 && i < d.length; k++) o.push(d[i++] | (((hi >> k) & 1) << 7)); } return o; };
const lx4 = (v) => [v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255];
function ask(ex, cmd, data) {
  const f = [0x7D, 0x4F, 0x43, cmd, ...pack7(data)];
  new Uint8Array(ex.memory.buffer, ex.emu_in_buf(), f.length).set(f);
  ex.emu_sysex(f.length);
  const n = ex.emu_out_take(); const p = new Uint8Array(ex.memory.buffer, ex.emu_out_buf(), n).slice();
  return unpack7(p.slice(5, -1));
}
const run = (ex, ms) => { for (let i = 0; i < ms * 44100 / 1000 / 128; i++) ex.emu_render(128); };

const A = await boot(); run(A, 1500);
const scr = new Uint16Array(A.memory.buffer, A.emu_screen(), 57600);
if (new Set(scr).size < 4) fail("screen blank");
A.emu_buttons(1 << 10); run(A, 60); A.emu_buttons(0); run(A, 2000);         // PLAY
let pk = 0; for (let i = 0; i < 400; i++) { A.emu_render(128); const l = new Float32Array(A.memory.buffer, A.emu_out_l(), 128); for (const x of l) pk = Math.max(pk, Math.abs(x)); }
if (!(pk > 0.01)) fail("no sound " + pk); else console.log("peak", pk.toFixed(3), "playing", A.emu_playing());
A.emu_buttons(1 << 10); run(A, 60); A.emu_buttons(0); run(A, 100);
A.emu_set_bpm(97); A.emu_save(0); A.emu_set_bpm(133); A.emu_save(4);
// the backup as the admin page takes it
const inf = ask(A, 0x40, []); const z = inf.indexOf(0, 4), n = inf[z + 1], size = (inf[z + 2] | inf[z + 3] << 8 | inf[z + 4] << 16) >>> 0;
const used = []; for (let s = 0; s < n; s++) if ((inf[z + 6 + (s >> 3)] >> (s & 7)) & 1) used.push(s);
console.log("slots", n, "size", size, "used", used.join(","));
const parts = [..."LUXRBK01"].map((c) => c.charCodeAt(0)); parts.push(used.length);
for (const s of used) {
  const b = new Uint8Array(size);
  for (let off = 0; off < size; off += 256) { const r = ask(A, 0x42, [s, ...lx4(off)]); b.set(r.slice(5, 5 + Math.min(256, size - off)), off); }
  parts.push(s, ...lx4(size), ...b);
}
const file = Uint8Array.from(parts);
// import into a fresh device
const B = await boot(); run(B, 300);
let o = 9;
for (let k = 0; k < file[8]; k++) {
  const s = file[o], len = (file[o + 1] | file[o + 2] << 8 | file[o + 3] << 16 | file[o + 4] << 24) >>> 0;
  new Uint8Array(B.memory.buffer, B.emu_stage(), len).set(file.subarray(o + 5, o + 5 + len));
  const c = B.emu_import(s, len); if (c) fail(`import slot ${s}: ${c}`);
  o += 5 + len;
}
B.emu_load(4); run(B, 100); if (B.emu_bpm() !== 133) fail("slot 5 bpm " + B.emu_bpm());
B.emu_load(0); run(B, 100); if (B.emu_bpm() !== 97) fail("slot 1 bpm " + B.emu_bpm());
const B2 = await boot(); new Uint8Array(B2.memory.buffer, B2.emu_store(), B2.emu_store_len()).set(new Uint8Array(B.memory.buffer, B.emu_store(), B.emu_store_len()));
B2.emu_init(3); if (B2.emu_bpm() !== 97) fail("after a restart: patch 1 bpm " + B2.emu_bpm());
console.log(process.exitCode ? "emu test: FAILED" : "emu test: ok");
