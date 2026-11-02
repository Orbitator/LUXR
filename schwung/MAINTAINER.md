# LUXR for Schwung: notes for the Schwung maintainer

**Module:** `luxr` (sound generator, drum machine), version 0.4.0
**Repository:** https://github.com/Orbitator/LUXR (module sources in [`schwung/`](.))
**Install (custom module):** Schwung Manager › Install Custom Module › `https://github.com/Orbitator/LUXR`
**Download:** https://github.com/Orbitator/LUXR/releases/download/schwung/luxr-module.tar.gz (referenced by the repository's [`release.json`](../release.json))
**Author:** Orbitator (orbitator on GitHub)

The download and install links work once the module branch is merged into `main` of Orbitator/LUXR and the `schwung` workflow has published the release.

## What it is

LUXR is a custom firmware that turns the M-VAVE FM-1 into a drum machine. Its voices follow the sound architecture of the
Sonic Potions LXR, rewritten in fixed point. This module puts LUXR's sound engine on Move as a Schwung sound generator.

The engine is the firmware's own `firmware/src/lx_synth.c`, compiled in unchanged. It provides:

- eight drum voices, each with an LXR-style synth layer and a sample layer;
- kit FX (drive, tempo-synced delay, reverb), a master filter, a compressor and a limiter;
- 14 preset kits;
- a WAV sample layer per voice that plays either the file's first 50 ms or the whole file.

LUXR's own sequencer is not included; Move sequences the module.

## How it uses Schwung

| Area | What the module does |
|---|---|
| API | Plugin API v2 only (`move_plugin_init_v2`); `plugin_api_v1.h` vendored from Schwung at `f288066` (MIT, `LICENSE-SCHWUNG` ships in the tarball). |
| Instances | Up to 6, fully independent. The firmware engine keeps its state in statics, so `luxr_engine.c` is compiled once per slot. Each instance gets a free slot; a seventh `create_instance` returns NULL and logs one line. |
| Hierarchy | Served from `get_param("ui_hierarchy")`, as required for sound generators, and built once per process (both it and `chain_params` are fixed documents; a read is a memcpy). `pad_layout: "drums"`; the eight voices are ONE template child level `sound` (`child_key_template` `s{index}_{key}`, base 1, `child_notes` 36..43, `child_names`, `child_roles`, `child_press_param`), with `focus_param: "ui_voice"` answering `"<count>:<voice>"` and `focus_press_param: "ui_press"`. A press is paired with the next note within 100 ms (else the note just before it). The root level has a kit browser (`list_param`/`count_param`/`name_param`). |
| Knobs | The root page holds the kit macros. Each voice has 5 pages of 8 knobs; the synth layer follows the FM-1's row A / SHIFT-row B layout. Kit pages: Drive & Reverb, Delay, Comp & Limiter, Setup. Short names are chosen for the 5-character cells. |
| chain_params | 209 entries: the numeric kit and voice parameters plus 8 `filepath` entries. No enums (their pages own them). No `max_param`. |
| Enums | Reported as an index; set_param accepts the index or the option name. Unknown values are ignored. |
| state | A flat JSON object: every value, plus the WAV paths (escaped). It describes what the engine currently holds, so scenes and LFOs read back their base values correctly. |
| Tempo | `get_bpm` sets delay time, gate and LFO rate. `get_beat_position` phase-locks the LFOs while Move plays. Both are NULL-guarded. |
| Canvas | `perform.js` is a fullscreen, `enterable` canvas cell on the root level with `wantsPads: true`. Knobs 1–8 edit row A and Shift+knobs edit row B (read through `ctx.shiftHeld()`). The jog wheel selects the voice, the jog click changes layer, and a pad hit focuses its voice. `handleBack` returns false. It only reads and writes params in `onOpen`/`onMidi`, never on the draw path. |
| Files | Each voice has a `filepath` param (`root` `/data/UserData`, `start_path` `UserLibrary/Samples`, `.wav`). |
| Help | `help.json` with `children`, lines of 20 characters or fewer, ASCII only. |

### User kits

`Setup › Save Kit` (a `string` param) saves the state as `/data/UserData/schwung/luxr/kits/<name>.json`; `Delete Kit` (an enum with `access: "write"`) removes the loaded one. The list follows LUXR's own kits in the browser. All file work happens on the same worker thread as the WAV loader (`luxr_user.c`); the audio thread posts a request and picks up the result.

### Realtime safety

The module follows `docs/MODULES.md` "there is no control thread":

- **Audio path.** `render_block`, `on_midi`, `set_param` and `get_param` do no file I/O, no allocation, no locks and no logging. `get_param` writes into the host's buffer with `vsnprintf` and returns -1 rather than a truncated JSON document.
- **WAV loading.** WAV files are read by one worker thread per instance, started in `create_instance`. The worker:
  - calls `sched_setscheduler(SCHED_OTHER)` and `sched_setaffinity` (cores 0–2) as its first action;
  - polls every 10 ms;
  - publishes decoded buffers through a one-slot atomic mailbox.
- **Hand-off.** At the start of a block, the audio thread swaps in the new buffer and hands the old one back through an atomic return queue. The worker frees it.
- **Instance teardown.** `destroy_instance` joins the worker and frees everything.
- **The only potentially long call on the audio thread is `lxw_request`**, which is a bounded `memcpy` of the path and an atomic increment.
- **Render cost.** About 35–40 µs per 128-frame block on x86 with all eight voices hit every 16th note and the FX on (roughly 1.3 % of real time). Not yet measured on Move.

### Tests (no hardware)

- `schwung/tests/test_plugin.c` loads `dsp.so` the way the host does (dlopen plus `move_plugin_init_v2`, with a fake `host_api_v1_t`). It checks:
  - every voice and every kit is audible;
  - accents and the note maps;
  - enums by index and by name;
  - the hierarchy, chain_params and state are valid JSON (the output files are written for inspection);
  - an exact state round-trip;
  - instance independence and the 6-instance limit;
  - WAV loading: 50 ms versus whole (by RMS), 16/24-bit and float formats, a missing file, state with escaped paths, and 40 rapid file switches.
- It runs clean under ASan/UBSan.
- The aarch64 build runs under QEMU and produces bit-identical audio to the native build.
- `schwung/tests/test_perform.mjs` runs `perform.js` against a mock canvas host. It checks that every key it reaches exists in the plugin's hierarchy and that nothing is drawn outside 128×64.
- CI: `.github/workflows/schwung.yml` (build, both tests, QEMU, tarball; the release `schwung` from `main`).

**Not yet done: a run on Move hardware.** That is the next step, and the reason this is offered as a custom module first.

## Licensing: please read before listing it in the catalog

- The module's code is **GPL-3.0**.
- The sample layer's built-in transients include the **Sonic Potions LXR's transient samples** (© 2013 Julian Schmidt). Their license **forbids commercial use** and is **not GPL-compatible**.
- They are not stored in the repository; the build fetches them. A tarball built with them is for personal, non-commercial use only, offered free of charge with full source. `NOTICE-LXR.txt` ships inside the module.

Two ways forward, and which to take is your call:

1. **Keep it out of the catalog.** It stays a custom module, installed from the repository as above. This is the default.
2. **List it in the catalog.** The entry is below. If the non-commercial sample license is a problem for the catalog, a variant without the LXR transients is possible: the sample layer would keep its own 16 synthesized transients per voice, the 16 drum-machine-style ones and user WAVs, and that build would be plain GPL-3.0. Tell me and I will add that build.

Proposed catalog entry:

```json
{
  "id": "luxr",
  "name": "LUXR",
  "description": "Eight LXR-style drum voices with sample layer (built-in transients or your own WAV, 50 ms or whole), 14 preset kits, drive, delay, reverb, master filter, compressor and limiter; a Perform page with Shift rows",
  "author": "Orbitator",
  "component_type": "sound_generator",
  "subcategory": "drum-machine",
  "tags": ["drums", "sample-based"],
  "github_repo": "Orbitator/LUXR",
  "default_branch": "main",
  "asset_name": "luxr-module.tar.gz",
  "min_host_version": "1.8.2"
}
```

`min_host_version` is set to the host version I built and read the documentation against (1.8.2). The module relies on `focus_param`, enterable canvases with `wantsPads`, and `get_beat_position`. If you know an older host that has all three, the value can be lowered.

## Open points and questions

- **Screen reader.** `CONTRIBUTING.md` asks modules for screen-reader support. The knob grid and help are host-drawn. The Perform canvas does not announce anything yet. Is there a recommended way for a canvas to announce (for example, an announce helper on the canvas `ctx`)?
- **Pad mapping in Perform.** Perform maps raw pad notes 68–99 to drum pads assuming Move's drum-rack 4×4 sits on the left half (`68 + row*8 + col`, `col < 4`). Please correct this if that assumption is wrong.
- **Canvas redraw.** The Shift indicator in Perform relies on the canvas `draw` being called regularly while Shift changes. If a canvas only redraws after input, a `fullscreen_live_ms` or similar may be needed.
- **Release asset.** The release `schwung` is a rolling prerelease that is rebuilt from `main`. `release.json` carries the version, which the CI keeps equal to `module.json`. Tagged releases are possible if you prefer them for the catalog.

## Provenance

As `CONTRIBUTING.md` asks: this module was written with **Claude Code** (Anthropic), working in the LUXR repository at the author's direction. No Grok was used.
