# LUXR für Ableton Move (Schwung)

Die Klangerzeugung von LUXR als **Sound Generator für [Schwung](https://github.com/charlesvestal/schwung)** auf dem
Ableton Move: die acht Drum-Stimmen (Synth-Layer nach der Sonic Potions LXR plus Sample-Layer), die Kit-FX (Drive,
Delay, Reverb), Master-Filter, Kompressor und Limiter – derselbe Festkomma-Code wie in der Firmware
(`firmware/src/lx_synth.c`, unverändert eingebunden). Der Sequencer und das Panel von LUXR bleiben auf dem FM-1: auf dem
Move sequenziert der Move, LUXR klingt.

Status: **v0.3.0, auf dem Rechner und unter QEMU (aarch64) getestet, noch nie auf einem Move gelaufen.**

## Installieren

Voraussetzung: Schwung ist auf dem Move installiert.

- **Schwung Manager › Install Custom Module**, GitHub-Repo `https://github.com/Orbitator/LUXR` – der Manager liest
  `release.json` im Repo und lädt `luxr-module.tar.gz` aus dem Release „schwung“ (das der Workflow
  `.github/workflows/schwung.yml` bei jedem Push auf `main` neu baut). Dafür muss das Repo öffentlich sein.
- oder `luxr-module.tar.gz` (Actions › schwung › Artifacts, bzw. das Release) im Manager hochladen,
- oder per SSH: `schwung/scripts/build.sh && schwung/scripts/install.sh` (nach
  `/data/UserData/schwung/modules/sound_generators/luxr`).

Dann im Shadow-Modus auf einer Spur **LUXR** als Synth laden – am besten auf einer Drum-Spur.

## Spielen

| Note Map (Kit-Seite) | Noten |
|---|---|
| **Move** (Standard) | Pads 1–8 (Noten 36–43): Kick, Snare, Clap, Cl Hat, Op Hat, Tom, Rim, Cymbal; Pads 9–16 (44–51): dieselben mit Accent. Velocity über 110 gibt ebenfalls Accent. |
| **GM** | General-MIDI-Drums wie LUXRs MIDI IN (36 Kick, 38/40 Snare, 39 Clap, 42/44 CH, 46 OH, 41–50 Tom, 37 Rim, 49/51/57 Cymbal) |
| **Tonal** | jede Note spielt den Sound **Tonal Snd**, C3 (60) ist seine eigene Tonhöhe, ein Halbton je Taste (±3 Oktaven) |

Die geschlossene Hi-Hat würgt die offene ab (wie auf LUXR). Note-Off bedeutet nichts – die Länge macht **Gate** (in
Vierteln eines 16tels, 0 = aus) und **Release** des Sounds.

## Die 8 Drehknöpfe und Shift

Schwung gibt einem Modul die Knöpfe auf zwei Wegen: im **Knopf-Grid** des Hosts (Seiten zu je 8, Jog blättert,
Shift + Knopf = Feineinstellung – Shift gehört dort dem Host) und auf einem eigenen **Vollbild-Screen**, auf dem das
Modul Knöpfe, Jog-Rad, Jog-Klick und Pads selbst bekommt und abfragen darf, ob Shift gehalten wird. LUXR nutzt beides.

**Im Grid** sind die Seiten so geordnet, dass Knopf *n* überall dasselbe Gewicht hat:

| Seite | Knopf 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| LUXR (die erste) | Volume | Tune | Decay | Filter | Reso | Drive | Delay-Return | Reverb-Return |
| Sound, Seite 1 (Synth) | LUXRs EDIT 1 | 2 | 3 | 4 | EDIT 5 (auf LUXR mit SHIFT) | 6 | 7 | 8 |
| Sound, Seite 2 (Sample) | Sample | Pegel | Pitch | Decay | Start | Tone | Bend | Synth-Pegel |
| Sound, Seite 3 (Mix/Env) | Level | Pan | Delay | Reverb | Drive | Attack | Gate | Release |
| Sound, Seite 4 (LFOs) | LFO 1 Ziel | Welle | Rate | Tiefe | LFO 2 Ziel | Welle | Rate | Tiefe |
| Sound, Seite 5 | WAV-Datei | Length (50 ms / Whole) | Variation | | | | | |
| Drive & Reverb | Drv Mode | Drv Gain | Drv Tone | Drv Return | Size | Damp | Pre-Delay | Rev Return |
| Delay | Mode | Zeit | Zeit R | Feedback | Tone | Return | | |
| Comp & Limiter | Threshold | Ratio | Attack | Release | Makeup | Limiter | Ceiling | Lim Release |
| Setup | Output | Note Map | Tonal Snd | | Pre-Clip | Auto Level | Target | |

Der Synth-Layer liegt wie auf dem FM-1: Knöpfe 1–4 sind LUXRs Reihe A, 5–8 die Reihe B, die man dort mit SHIFT
(GLO halten) erreicht – z. B. Kick: Pitch, Decay, P.Env, P.Dec | Wave, FM, Click, Drive.

**PERFORM** (auf der LUXR-Seite die Zelle *Perform* anklicken) bringt LUXRs Doppelbelegung zurück: die 8 Knöpfe
spielen eine Reihe, **mit Shift die zweite**.

| | ohne Shift (Reihe A) | mit Shift (Reihe B) |
|---|---|---|
| Ebene **SOUND** | der Synth-Layer des Sounds (EDIT 1–8) | der Sample-Layer (Sample, Pegel, Pitch, Decay, Start, Tone, Bend, Synth) |
| Ebene **MIX** | Level, Pan, Delay, Reverb, Drive, Attack, Gate, Release | LFO 1 (Ziel, Welle, Rate, Tiefe), LFO 2 |
| Ebene **KIT** | Volume, Tune, Decay, Filter, Reso, Drive, Delay-Return, Reverb-Return | Delay-Zeit, Feedback, Size, Damp, Threshold, Ratio, Makeup, Limiter |

| Bedienung | Wirkung |
|---|---|
| Jog drehen | Sound wählen (Kick … Cymbal) |
| Jog klicken | nächste Ebene: SOUND › MIX › KIT |
| Pad anschlagen | wählt seinen Sound (er klingt trotzdem) |
| Knopf berühren | Name und Wert unten im Display |
| Shift + Jog, Back | zurück ins Grid (gehört dem Host) |

Der gewählte Sound ist auch der, dem das Grid folgt (`focus_param` `ui_voice`).

## Seiten

- **Preset-Browser**: *Factory* und die Kits der 13 Preset-Patches (dieselben, mit denen der Emulator startet:
  `web/emu/luxr-backup-*.luxr`, beim Bauen von `schwung/tools/gen_kits.c` mit dem Firmware-Code ausgelesen).
- **LUXR (Root)**: Perform, dann Volume, Tune (±24 Halbtöne), Decay (alle Sounds), Master-Filter (− Tiefpass,
  + Hochpass), Resonanz, Drive, Delay- und Reverb-Return; Output, Note Map und Tonal Snd stehen unter **Setup**.
- **Kick … Cymbal** (je ein „Voice“ mit seiner Note): die 8 Synth-Parameter nach Art des Sounds (z. B. Kick: Pitch,
  Decay, P.Env, P.Dec, Wave, FM, Click, Drive), dann der Sample-Layer (Sample – Off, Snap, die 12 LXR-Transienten, 16
  eigene, 16 nach 808/909/606/707/DMX/LM-1/LM-2/SDS –, Pegel, Pitch, Decay, Start, Tone, Bend, Synth-Pegel), Level,
  Pan, die Sends Delay/Reverb/Drive, Attack/Gate/Release, zwei LFOs (Ziel, Welle, Rate im Tempo, Tiefe) und
  **Variation** (würfelt den Sound neu wie PRESETS auf LUXR; 0 = der Werks-Sound).
- **WAV-Layer** (Sound, Seite 5): statt einer eingebauten Transiente spielt der Sample-Layer eine eigene WAV-Datei.
  *WAV File* öffnet den Dateibrowser von Schwung (Start: `UserLibrary/Samples` des Move); eine gewählte Datei stellt
  *Sample* auf **WAV**. **Length** wählt, wie viel davon klingt:
  - **50 ms** – nur der Anfang, als Transiente wie die eingebauten (die sind alle 50 ms lang, die Maschinen-Samples
    46 ms): mit 6 ms Ausblendung am Ende, innerhalb der Hüllkurve des Sounds, also zusammen mit dem Synth-Layer.
  - **Whole** – die ganze Datei bis zu ihrem Ende, an der Hüllkurve des Synth-Layers vorbei (nur Attack und das
    Abwürgen der offenen Hi-Hat wirken); Smp Decay unter 127 lässt sie trotzdem ausklingen.

  Pitch (0,25–1,75 × und die Halbtöne der Tonal-Map), Smp Decay, Start, Tone, Bend und Smp Lvl gelten für beides.
  Gelesen werden PCM 8/16/24/32 Bit, 32-Bit-Float und WAVE_FORMAT_EXTENSIBLE, mono oder mehrkanalig (zu mono gemischt),
  in ihrer eigenen Abtastrate, bis 2^20 Frames (23,7 s bei 44,1 kHz; länger wird abgeschnitten). Der Pfad steht im
  Zustand. Geladen wird in einem eigenen Thread (SCHED_OTHER, Kerne 0–2) und per Zeigertausch übergeben – im
  Audio-Thread passiert kein Datei-Zugriff und keine Allokation, wie Schwung es verlangt. In der Firmware des FM-1 ist
  das abgeschaltet (`#ifdef LX_FILE_LAYER` in `lx_synth.c`, dort ändert sich nichts).
- **Drive & Reverb**, **Delay** (1/8 … 3/4 im Tempo des Move; Mono/Stereo/Ping-Pong), **Comp & Limiter**, **Setup**.

Tempo und Takt kommen vom Move (`get_bpm`, `get_beat_position`): Delay-Zeiten, Gate und die LFOs laufen mit, die LFOs
phasenstarr zum Takt, solange der Move spielt. Alles steht im Zustand (`state`), den Schwung mit dem Set und den
User-Presets speichert; die Zahl-Parameter (200) sind als `chain_params` automatisierbar und von Scenes sperrbar.

Bis zu **sechs Instanzen** gleichzeitig, jede eine eigene Maschine (der Firmware-Code hält seinen Zustand in Statics;
`luxr_engine.c` wird deshalb sechsmal übersetzt). Eine siebte meldet „all engines in use“.

## Bauen und testen

```
schwung/scripts/build.sh                 # -> build/schwung/luxr-module.tar.gz (aarch64-linux-gnu-gcc, sonst Docker)
schwung/scripts/install.sh               # aufs Gerät (ableton@move.local; MOVE_HOST=…)
NATIVE=1 schwung/scripts/build.sh        # dsp.so für diesen Rechner
cc -Ischwung/dsp -o build/schwung/test_plugin schwung/tests/test_plugin.c -ldl -lm
build/schwung/test_plugin build/schwung/native/dsp.so build/schwung/out
```

Der Test lädt das Plugin wie Schwung (`dlopen`, `move_plugin_init_v2`), spielt alle Sounds, einen Groove und jedes Kit
(`out/luxr_move.wav`), prüft Accent, Note Maps, Enums per Index und Name, den Zustand (hin und zurück), getrennte
Instanzen, die Grenze von sechs, und misst die Rechenzeit (auf einem x86-Rechner ~40 µs je Block von 128 Samples, also
gut 1 % Echtzeit). Die aarch64-Version liefert unter QEMU dieselben Samples bitgenau.

Bauen braucht dasselbe wie `tools/build.py --gen` (Python mit Pillow; die LXR-Transienten holt
`tools/gen_lxr_transients.py` aus github.com/SonicPotions/LXR).

## Dateien

| | |
|---|---|
| `dsp/luxr_engine.c` | eine Maschine: `lx_core.h` + `lx_synth.c` der Firmware, Parameter, MIDI, Zustand, Hierarchie |
| `dsp/luxr_plugin.c` | Plugin API v2: jede Instanz bekommt eine der sechs Maschinen |
| `dsp/luxr_tables.c` | die Transienten, einmal für alle Maschinen |
| `dsp/luxr_wav.c` | WAV-Dateien lesen, im eigenen Thread, Übergabe an den Audio-Thread |
| `dsp/plugin_api_v1.h` | aus Schwung (MIT, `LICENSE-SCHWUNG`) |
| `src/module.json`, `src/help.json` | das Modul, die Hilfe auf dem Move |
| `src/perform.js` | PERFORM: die Knöpfe mit Reihe A / Shift-Reihe B, Jog, Pads |
| `tools/gen_kits.c` | die Preset-Kits aus einem `.luxr`-Backup (`LUXR_KITS=…` wählt ein anderes) |
| `tests/test_plugin.c`, `tests/test_perform.mjs` | die Tests (Plugin; PERFORM gegen einen nachgebauten Canvas-Host) |

## Lizenz

Code: GPL-3.0 wie das Repository. **Der Sample-Layer enthält die Transienten der Sonic Potions LXR** (© 2013 Julian
Schmidt), deren Lizenz kommerzielle Nutzung verbietet und nicht GPL-verträglich ist – ein damit gebautes Modul ist
nur für den **persönlichen, nicht-kommerziellen Gebrauch** (`NOTICE-LXR.txt`, liegt im Modul bei). Deshalb ist es auch
nicht für den offiziellen Schwung-Katalog gedacht, sondern wird aus diesem Repo installiert.
