/* lxr-drums.js — six drum voices in the manner of the Sonic Potions LXR, for the Web Audio API.
 * The same voices, parameters and ranges as ONE CLICK FM-1's LXR engine (firmware/src/oc_synth.c), written for
 * Web Audio nodes: it plays in an AudioContext and renders in an OfflineAudioContext (WAV export) alike.
 * No LXR code is used (its licence forbids commercial use); this is a new implementation of the published voice design.
 * SPDX-License-Identifier: GPL-3.0-only
 *
 *   const drums = new LxrDrums(ctx, destinationNode);       // a kit is rolled at start
 *   drums.trigger("BD", time, 1.0);                          // lanes: BD SD CP CH OH TM RS CB (or 0..7)
 *   drums.set("KICK", "DECAY", 90);                          // voice, parameter, 0..127
 *   drums.randomize();                                       // a new kit
 *   const json = drums.toJSON(); drums.load(json);           // presets
 *   drums.output.gain.value = 0.8;                           // the kit's level (a GainNode)
 */
(function (root) {
  "use strict";
  const LANES = ["BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"];
  const VOICES = ["KICK", "TOM", "PERC", "SNARE", "CYMBAL", "HIHAT"];
  const KIND = [0, 0, 0, 1, 2, 3];                           // drum, drum, drum, snare, cymbal, hihat
  const VOICE_OF_LANE = [0, 3, 3, 5, 5, 1, 2, 4];
  const PARAMS = [
    ["PITCH", "DECAY", "P.ENV", "P.DECAY", "WAVE", "FM", "CLICK", "DRIVE"],
    ["PITCH", "DECAY", "NOISE", "N.DECAY", "TONE", "SNAP", "CLICK", "DRIVE"],
    ["PITCH", "DECAY", "RATIO", "FM", "FILTER", "RESO", "NOISE", "DRIVE"],
    ["PITCH", "CLOSED", "OPEN", "FM", "FILTER", "NOISE", "CLICK", "DRIVE"],
  ];
  const WAVES = ["sine", "triangle", "sawtooth", "square", "noise"];
  const RANGES = [                                            // the kit generator: lo, hi per voice and parameter
    [[8, 22], [55, 100], [40, 110], [20, 50], [0, 0], [0, 30], [20, 90], [0, 60]],
    [[30, 60], [30, 70], [20, 70], [25, 60], [0, 25], [0, 40], [0, 50], [0, 30]],
    [[60, 110], [5, 35], [0, 90], [5, 40], [0, 127], [0, 90], [20, 100], [0, 70]],
    [[30, 80], [25, 60], [50, 110], [40, 80], [40, 110], [20, 90], [10, 70], [0, 50]],
    [[30, 90], [50, 95], [10, 110], [20, 100], [30, 100], [10, 70], [0, 50], [0, 40]],
    [[50, 110], [5, 30], [35, 70], [10, 90], [50, 120], [20, 90], [0, 50], [0, 30]],
  ];
  // the firmware's time scale: index 0..127 -> 1 ms .. 12 s (to -60 dB); a time constant for setTargetAtTime
  const t60 = (idx) => 0.001 * Math.pow(12000, Math.max(0, Math.min(127, idx)) / 127);
  const tau = (idx) => t60(idx) / 6.9;
  const mtof = (n) => 440 * Math.pow(2, (n - 69) / 12);
  const svfHz = (idx) => 25 * Math.pow(16000 / 25, Math.max(0, Math.min(255, idx)) / 255);
  const noiseOf = new WeakMap(), curveOf = new Map();

  function noiseBuffer(ctx) {
    let b = noiseOf.get(ctx);
    if (!b) {
      b = ctx.createBuffer(1, ctx.sampleRate, ctx.sampleRate);
      const d = b.getChannelData(0);
      let x = 0x1234567;
      for (let i = 0; i < d.length; i++) { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; d[i] = ((x >>> 0) / 4294967296) * 2 - 1; }
      noiseOf.set(ctx, b);
    }
    return b;
  }
  function driveCurve(amount) {                               // tanh, harder with DRIVE
    const k = Math.round(amount);
    if (!curveOf.has(k)) {
      const n = 1024, c = new Float32Array(n), g = 1 + k / 16;
      for (let i = 0; i < n; i++) { const x = (i / (n - 1)) * 2 - 1; c[i] = Math.tanh(x * g * 1.5) / Math.tanh(g * 1.5); }
      curveOf.set(k, c);
    }
    return curveOf.get(k);
  }

  class LxrDrums {
    constructor(ctx, destination, kit) {
      this.ctx = ctx;
      this.output = ctx.createGain();
      this.output.gain.value = 0.8;
      if (destination) this.output.connect(destination);
      this.tune = 0;                                          // semitones, -24..24
      this.decay = 64;                                        // all decays, 64 = as set
      this.voices = VOICES.map(() => new Array(8).fill(0));
      this._open = null;                                      // the open hat ringing (the closed one chokes it)
      if (kit) this.load(kit); else this.randomize();
    }
    static get LANES() { return LANES.slice(); }
    static get VOICES() { return VOICES.slice(); }
    static paramsOf(voice) { return PARAMS[KIND[LxrDrums._v(voice)]].slice(); }
    static _v(voice) { const i = typeof voice === "number" ? voice : VOICES.indexOf(String(voice).toUpperCase()); if (i < 0 || i > 5) throw new Error("LxrDrums: no voice " + voice); return i; }
    static _l(lane) { const i = typeof lane === "number" ? lane : LANES.indexOf(String(lane).toUpperCase()); if (i < 0 || i > 7) throw new Error("LxrDrums: no lane " + lane); return i; }

    set(voice, param, value) {
      const v = LxrDrums._v(voice), names = PARAMS[KIND[v]];
      const k = typeof param === "number" ? param : names.indexOf(String(param).toUpperCase());
      if (k < 0 || k > 7) throw new Error("LxrDrums: no parameter " + param);
      this.voices[v][k] = Math.max(0, Math.min(127, Math.round(value)));
    }
    get(voice, param) {
      const v = LxrDrums._v(voice), k = typeof param === "number" ? param : PARAMS[KIND[v]].indexOf(String(param).toUpperCase());
      return this.voices[v][k];
    }
    randomize(rnd = Math.random) {
      const rr = (lo, hi) => lo + Math.floor(rnd() * (hi - lo + 1));
      this.voices = RANGES.map((r) => r.map(([lo, hi]) => rr(lo, hi)));
      this.voices[0][4] = 0;                                  // the kick: a sine
      if (rnd() < 0.3) this.voices[1][4] = 26;                // a triangle tom now and then
      return this;
    }
    toJSON() { return { type: "lxr-kit", version: 1, tune: this.tune, decay: this.decay, voices: this.voices.map((p) => p.slice()) }; }
    load(kit) {
      if (!kit || !Array.isArray(kit.voices) || kit.voices.length !== 6) throw new Error("LxrDrums: not a kit");
      this.voices = kit.voices.map((p) => p.slice(0, 8).map((x) => Math.max(0, Math.min(127, x | 0))));
      this.tune = kit.tune | 0; this.decay = kit.decay || 64;
      return this;
    }

    /* a hit on lane (BD SD CP CH OH TM RS CB or 0..7) at time t (seconds, ctx time), velocity 0..1, accent adds a bit */
    trigger(lane, t = this.ctx.currentTime, velocity = 0.8, accent = false) {
      const L = LxrDrums._l(lane), v = VOICE_OF_LANE[L], kind = KIND[v], p = this.voices[v], ctx = this.ctx;
      const vel = Math.min(1.2, velocity + (accent ? 0.25 : 0));
      const dk = 64 / Math.max(8, this.decay);                // the kit's DECAY
      const amp = ctx.createGain(), drv = ctx.createWaveShaper();
      drv.curve = driveCurve(p[7]); drv.oversample = "2x";
      amp.connect(this.output);
      let src = amp, end;
      if (p[7] > 0) { drv.connect(amp); src = drv; }
      const env = (g, peak, tc, at = t) => { g.gain.setValueAtTime(0, at); g.gain.linearRampToValueAtTime(peak, at + 0.0015); g.gain.setTargetAtTime(0, at + 0.0015, tc); };
      const osc = (type, f) => { const o = ctx.createOscillator(); o.type = type; o.frequency.setValueAtTime(f, t); return o; };
      const noise = () => { const n = ctx.createBufferSource(); n.buffer = noiseBuffer(ctx); n.loop = true; n.loopStart = Math.random() * 0.5; return n; };
      const nodes = [];
      const click = (level) => {                             // a short noisy transient
        if (level <= 0) return;
        const n = noise(), g = ctx.createGain();
        g.gain.setValueAtTime(level / 127 * 0.9, t); g.gain.setTargetAtTime(0, t, tau(12));
        n.connect(g); g.connect(src); n.start(t); n.stop(t + 0.06); nodes.push(n);
      };
      if (kind === 0) {                                       // DRUM: osc with pitch env and FM, click, drive
        const f0 = mtof(24 + p[0] * 60 / 127 + this.tune), dec = tau(25 + p[1] * 85 / 127) * dk;
        const wave = WAVES[Math.min(4, Math.floor(p[4] * 5 / 128))];
        let o;
        if (wave === "noise") {
          o = noise();
          const bp = ctx.createBiquadFilter(); bp.type = "bandpass"; bp.Q.value = 1.5;
          bp.frequency.setValueAtTime(f0 * (1 + p[2] / 16), t); bp.frequency.setTargetAtTime(f0 * 4, t, tau(15 + p[3] * 60 / 127));
          o.connect(bp); bp.connect(src);
        } else {
          o = osc(wave, f0 * (1 + 8 * p[2] / 127));
          o.frequency.setTargetAtTime(f0, t, tau(15 + p[3] * 60 / 127));
          if (p[5] > 0) {                                    // FM: a sine an octave up into the frequency
            const m = osc("sine", f0 * 2), mg = ctx.createGain();
            mg.gain.value = f0 * p[5] / 127 * 6;
            m.connect(mg); mg.connect(o.frequency); m.start(t); nodes.push(m);
          }
          o.connect(src);
        }
        end = dec * 7;
        env(amp, vel * 0.9, dec);
        o.start(t); nodes.push(o);
        click(p[6]);
      } else if (kind === 1) {                                // SNARE (and the clap): tone with snap, noise above TONE
        const f0 = mtof(43 + p[0] * 26 / 127 + this.tune), tdec = tau(25 + p[1] * 85 / 127) * dk;
        const ndec = tau(30 + p[3] * 70 / 127) * dk, cut = svfHz(60 + p[4] * 1.5);
        const n = noise(), hp = ctx.createBiquadFilter(), bp = ctx.createBiquadFilter(), ng = ctx.createGain();
        hp.type = "highpass"; hp.frequency.value = cut; bp.type = "bandpass"; bp.frequency.value = cut; bp.Q.value = 0.9;
        n.connect(hp); n.connect(bp); hp.connect(ng); bp.connect(ng); ng.connect(src);
        if (L === 2) {                                        // CP: three bursts 10 ms apart, then the tail
          ng.gain.setValueAtTime(0, t);
          for (let b = 0; b < 3; b++) { ng.gain.setValueAtTime(vel * 0.45, t + b * 0.01); ng.gain.setValueAtTime(vel * 0.09, t + b * 0.01 + 0.0036); }
          ng.gain.setValueAtTime(vel * 0.45, t + 0.03); ng.gain.setTargetAtTime(0, t + 0.03, ndec);
          amp.gain.value = 1; end = 0.03 + ndec * 7;
        } else {
          const o = osc("sine", f0 * (1 + 2 * p[5] / 127)), tg = ctx.createGain();
          o.frequency.setTargetAtTime(f0, t, tau(25));
          env(tg, vel * (1 - p[2] / 254), tdec);
          o.connect(tg); tg.connect(src); o.start(t); nodes.push(o);
          env(ng, vel * p[2] / 64 * 0.32, ndec);
          amp.gain.value = 1; end = Math.max(tdec, ndec) * 7;
          click(p[6]);
        }
        n.start(t); nodes.push(n);
      } else {                                                // CYMBAL / HIHAT: three squares, FM'd, high-passed, noise
        const hat = kind === 3;
        const f0 = mtof(60 + p[0] * 40 / 127 + this.tune);
        const ratio = hat ? 1.71 : 1 + p[2] / 64, fm = p[3];
        const a = osc("square", f0), b = osc("square", f0 * ratio), c = osc("square", f0 * 1.36);
        const fmg = ctx.createGain(); fmg.gain.value = f0 * fm / 127 * 3;
        b.connect(fmg); fmg.connect(a.frequency); fmg.connect(c.frequency);
        const mix = ctx.createGain(); mix.gain.value = 0.3;
        a.connect(mix); b.connect(mix); c.connect(mix);
        const n = noise(), ng = ctx.createGain(); ng.gain.value = (hat ? p[5] : p[6]) / 127 * 0.7;
        n.connect(ng); ng.connect(mix);
        const hp = ctx.createBiquadFilter(); hp.type = "highpass";
        hp.frequency.value = svfHz(120 + p[4]); hp.Q.value = 0.5 + (hat ? 20 : p[5]) / 127 * 8;
        mix.connect(hp); hp.connect(src);
        const idx = hat ? (L === 4 ? 55 + p[2] * 45 / 127 : 30 + p[1] * 40 / 127) : 80 + p[1] * 40 / 127;
        const dec = tau(idx) * dk;
        env(amp, vel * 0.8, dec); end = dec * 7;
        if (hat) {
          if (L === 3 && this._open) { const o = this._open; o.gain.cancelScheduledValues(t); o.gain.setTargetAtTime(0, t, 0.001); this._open = null; }
          if (L === 4) this._open = amp;
          click(p[6]);
        }
        [a, b, c, n].forEach((x) => { x.start(t); nodes.push(x); });
      }
      const stop = t + Math.min(12, end + 0.05);
      nodes.forEach((x) => x.stop(stop));
      nodes[0].onended = () => { amp.disconnect(); if (this._open === amp) this._open = null; };
      return { lane: LANES[L], voice: VOICES[v], stop };
    }
  }
  LxrDrums.version = "1.0";
  if (typeof module === "object" && module.exports) module.exports = LxrDrums;
  else root.LxrDrums = LxrDrums;
})(typeof globalThis !== "undefined" ? globalThis : this);
