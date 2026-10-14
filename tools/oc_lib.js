/* ONE CLICK FM-1: the browser side shared by the companion app and the emulator page (tools/sync_js.py copies it
   into both between the @oc-lib markers): the dump, MIDI export, sample import (decode, 22 kHz mono, mu-law) and
   the sample upload with its answers. Protocol: firmware/src/oc_proto.c. */
const OC = (() => {
  const TYPE = ["BASS", "CHORDS", "PAD", "ATMO", "PIANO", "GUITAR", "LEAD", "PLUCK", "BRASS", "FLUTE", "DRUMS", "KICK", "SNARE", "PERC", "LOOP"];
  const ENG = ["SUBTRACT", "ACID", "FM", "SUPERSAW", "ADDITIVE", "NOISE", "DRUMS", "SAMPLE"];
  const isDrum = (ty) => ty >= 10 && ty <= 13, isLoop = (ty) => ty === 14;
  function pack7(bytes) {
    const o = [];
    for (let i = 0; i < bytes.length; i += 7) {
      let hi = 0; const h = o.length; o.push(0);
      const m = Math.min(7, bytes.length - i);
      for (let k = 0; k < m; k++) { hi |= ((bytes[i + k] >> 7) & 1) << k; o.push(bytes[i + k] & 0x7f); }
      o[h] = hi;
    }
    return o;
  }
  function unpack7(d) {
    const o = [];
    for (let i = 0; i < d.length;) { const hi = d[i++]; for (let k = 0; k < 7 && i < d.length; k++) o.push(d[i++] | (((hi >> k) & 1) << 7)); }
    return o;
  }
  /* the dump (11): version 1 (4 tracks) and 2 (the track count in it) */
  function parseDump(raw) {
    const v = raw[0]; if (v !== 1 && v !== 2) return null;
    const ps = raw[1], ss = raw[2]; let n = 3; const nt = v === 2 ? raw[n++] : 4;
    const s = { root: raw[n++], scale: raw[n++], prog: raw.slice(n, n + 4), bpm: raw[n + 4], playing: raw[n + 5], tracks: [], transpose: 0, swing: 0 };
    n += 6;
    for (let t = 0; t < nt; t++) {
      const patch = raw.slice(n, n + ps); n += ps;
      const len = raw[n++], gate = raw[n++], steps = [];
      for (let i = 0; i < 64; i++, n += ss) { const b = raw.slice(n, n + ss); steps.push({ note: b.slice(0, 4), n: b[4], time: b[5], flags: b[6], vel: b[7], hit: b[8], acc: b[9] }); }
      s.tracks.push({ engine: patch[0], patch, len, gate, steps, type: [0, 1, 6, 10, 11, 14][t] ?? 0, octave: 0 });
    }
    if (raw.length >= n + 2 * nt + 2) {
      for (let t = 0; t < nt; t++) { s.tracks[t].type = raw[n + t]; s.tracks[t].octave = (raw[n + nt + t] << 24) >> 24; }
      s.transpose = (raw[n + 2 * nt] << 24) >> 24; s.swing = raw[n + 2 * nt + 1];
    }
    return s;
  }
  /* MIDI export: 4 bars, Format 1, 480 PPQN, a track a part; drums on channel 10, the loop's slices as notes from C2 */
  function toMidi(s, steps = 64) {
    const PPQ = 480, S16 = PPQ / 4, DRUM = [36, 38, 39, 42, 46, 45, 37, 56];
    const vlq = (v) => { const b = [v & 0x7f]; while ((v >>= 7)) b.unshift((v & 0x7f) | 0x80); return b; };
    const u32 = (v) => [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
    const text = (type, str) => [0xff, type, ...vlq(str.length), ...Array.from(str, (c) => c.charCodeAt(0) & 0x7f)];
    const chunk = (ev) => { ev.sort((a, b) => a[0] - b[0] || a[1] - b[1]); const d = []; let last = 0;
      for (const [t, , bytes] of ev) { d.push(...vlq(t - last), ...bytes); last = t; } d.push(0, 0xff, 0x2f, 0);
      return [0x4d, 0x54, 0x72, 0x6b, ...u32(d.length), ...d]; };
    const swing = (g) => g * S16 + (g & 1 ? Math.round(S16 * (s.swing || 0) / 100) : 0);
    const us = Math.round(60000000 / (s.bpm || 120));
    const chunks = [chunk([[0, 0, text(3, "ONE CLICK FM-1")], [0, 0, [0xff, 0x51, 3, (us >> 16) & 255, (us >> 8) & 255, us & 255]], [0, 0, [0xff, 0x58, 4, 4, 2, 24, 8]]])];
    s.tracks.forEach((tr, t) => {
      const drum = isDrum(tr.type), loop = isLoop(tr.type), ch = drum ? 9 : t, len = Math.max(1, tr.len);
      const ev = [[0, 0, text(3, `${t + 1} ${TYPE[tr.type] || "TRACK"}`)]];
      const shift = drum || loop ? 0 : (s.transpose || 0) + 12 * (tr.octave || 0);
      for (let g = 0; g < steps; g++) {
        const st = tr.steps[g % len];
        if (st.time !== 1) continue;
        const on = swing(g), vel = Math.max(1, Math.min(127, (st.vel || 100) + ((st.flags & 1) ? 15 : 0)));
        if (drum) {
          for (let l = 0; l < 8; l++) if ((st.hit >> l) & 1) {
            const v = (st.acc >> l) & 1 ? 127 : Math.min(st.vel || 100, 120);
            ev.push([on, 1, [0x90 | ch, DRUM[l], v]], [on + S16 / 2, 0, [0x80 | ch, DRUM[l], 0]]);
          }
          continue;
        }
        if (loop) { const n = 36 + (st.note[0] & 63); ev.push([on, 1, [0x90 | ch, n, vel]], [on + S16, 0, [0x80 | ch, n, 0]]); continue; }
        let k = 1; while (k < 16 && k < len && tr.steps[(g + k) % len].time === 2) k++;
        const slide = (st.flags & 2) && tr.steps[(g + k) % len].time === 1;
        const off = swing(g + k - 1) + (slide ? S16 : Math.max(10, Math.round(S16 * tr.gate / 100)));
        for (let i = 0; i < Math.min(4, st.n); i++) {
          const n = Math.max(0, Math.min(127, st.note[i] + shift));
          ev.push([on, 1, [0x90 | ch, n, vel]], [Math.min(off, steps * S16), 0, [0x80 | ch, n, 0]]);
        }
      }
      chunks.push(chunk(ev));
    });
    return new Uint8Array([0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 1, 0, chunks.length, (PPQ >> 8) & 255, PPQ & 255, ...chunks.flat()]);
  }
  /* samples: any file the browser decodes, to 22050 Hz mono, normalised, mu-law. A one-shot loses its silent end and
     stays under 3 s; a loop keeps its length, its bars (1, 2, 4) guessed from the tempo unless given */
  const RATE = 22050;
  function ulaw(x) {
    let v = Math.max(-32635, Math.min(32635, Math.round(x * 32767)));
    const sign = v < 0 ? 0x80 : 0; if (v < 0) v = -v;
    v += 0x84; let e = 7;
    for (let m = 0x4000; e > 0 && !(v & m); e--, m >>= 1);
    return ~(sign | (e << 4) | ((v >> (e + 3)) & 15)) & 0xff;
  }
  async function prepSample(file, kind, bpm, bars) {
    const buf = await file.arrayBuffer();
    const dec = await new OfflineAudioContext(1, 1, RATE).decodeAudioData(buf);
    const n = Math.max(32, Math.ceil(dec.duration * RATE));
    const off = new OfflineAudioContext(1, n, RATE), src = off.createBufferSource();
    src.buffer = dec; src.connect(off.destination); src.start();
    let d = (await off.startRendering()).getChannelData(0);
    let pk = 0; for (const x of d) pk = Math.max(pk, Math.abs(x));
    const g = pk > 0 ? 0.95 / pk : 1;
    if (kind === 0) {                             // one-shot: no silent tail, at most 3 s
      let e = d.length; while (e > 64 && Math.abs(d[e - 1]) * g < 0.003) e--;
      d = d.subarray(0, Math.min(e, 3 * RATE));
    } else if (!bars) {
      const b = d.length / RATE * (bpm || 120) / 240;
      bars = [1, 2, 4].reduce((a, c) => Math.abs(Math.log2(c / b)) < Math.abs(Math.log2(a / b)) ? c : a, 1);
    }
    const out = new Uint8Array(d.length);
    for (let i = 0; i < d.length; i++) out[i] = ulaw(d[i] * g);
    return { data: out, rate: RATE, bars: kind ? bars : 1, secs: d.length / RATE };
  }
  /* the upload: frames out through send(body) (body without F0 / F7), each answered 2F cmd code; feed answers in
     through answer(d) (the whole F0 .. F7 message) */
  const ERR = ["", "kein Platz mehr", "ungültige Anfrage", "falsche Reihenfolge", "Flash-Fehler"];
  function uploader(send) {
    let wait = null;
    const ask = (body, cmd) => new Promise((res, rej) => {
      wait = { cmd, res, rej, t: setTimeout(() => { wait = null; rej(new Error("keine Antwort vom Gerät")); }, 6000) };
      send(body);
    });
    function answer(d) {
      if (d[4] === 0x2F && wait && d[5] === wait.cmd) {
        const w = wait; wait = null; clearTimeout(w.t);
        if (d[6]) w.rej(new Error(ERR[d[6]] || "Fehler " + d[6])); else w.res();
        return true;
      }
      return false;
    }
    async function upload(slot, kind, name, smp, target, progress) {
      const len = smp.data.length, nm = Array.from(name.toUpperCase().slice(0, 11), (c) => c.charCodeAt(0) & 0x7f);
      const head = [slot, kind, 60, smp.bars, RATE & 255, RATE >> 8, len & 255, (len >> 8) & 255, (len >> 16) & 255, len >>> 24, target ?? 0x7F];
      while (nm.length < 12) nm.push(0);
      await ask([0x7D, 0x4F, 0x43, 0x20, ...pack7([...head, ...nm])], 0x20);
      for (let o = 0; o < len; o += 256) {
        const c = smp.data.subarray(o, Math.min(len, o + 256));
        await ask([0x7D, 0x4F, 0x43, 0x21, ...pack7([o & 255, (o >> 8) & 255, (o >> 16) & 255, o >>> 24, ...c])], 0x21);
        if (progress && (o & 4095) === 0) progress(o / len);
      }
      await ask([0x7D, 0x4F, 0x43, 0x22], 0x22);
      if (progress) progress(1);
    }
    const clear = (slot) => ask([0x7D, 0x4F, 0x43, 0x23, slot ?? 0x7F], 0x23);
    return { answer, upload, clear };
  }
  function parseSmpList(raw) {                    // 25: free, area, 8 slots
    const u32 = (i) => raw[i] | raw[i + 1] << 8 | raw[i + 2] << 16 | (raw[i + 3] << 24 >>> 0);
    const slots = [];
    for (let k = 0, i = 8; k < 8; k++, i += 22)
      slots.push({ used: raw[i], kind: raw[i + 1], root: raw[i + 2], bars: raw[i + 3], rate: raw[i + 4] | raw[i + 5] << 8, len: u32(i + 6),
                   name: String.fromCharCode(...raw.slice(i + 10, i + 22).filter((c) => c)) });
    return { free: u32(0), area: u32(4), slots };
  }
  return { TYPE, ENG, isDrum, isLoop, pack7, unpack7, parseDump, toMidi, prepSample, uploader, parseSmpList, RATE };
})();
