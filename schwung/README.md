# LUXR für Ableton Move (Schwung)

Die Klangerzeugung von LUXR als **Sound Generator für [Schwung](https://github.com/charlesvestal/schwung)** auf dem
Ableton Move: die acht Drum-Stimmen (Synth-Layer nach der Sonic Potions LXR plus Sample-Layer), die Kit-FX (Drive,
Delay, Reverb), Master-Filter, Kompressor und Limiter – derselbe Festkomma-Code wie in der Firmware
(`firmware/src/lx_synth.c`, unverändert eingebunden). Der Sequencer und das Panel von LUXR bleiben auf dem FM-1: auf dem
Move sequenziert der Move, LUXR klingt.

Status: **v0.5.1, auf dem Rechner und unter QEMU (aarch64) getestet, noch nie auf einem Move gelaufen.**

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
| **Move** (Standard) | Pads 1–8 (Noten 36–43): Kick, Snare, Clap, Cl Hat, Op Hat, Tom, Rim, Cymbal; die oberen acht Pads 9–16 (44–51) spielen nichts. Velocity über 110 gibt Accent. |
| **GM** | General-MIDI-Drums wie LUXRs MIDI IN (36 Kick, 38/40 Snare, 39 Clap, 42/44 CH, 46 OH, 41–50 Tom, 37 Rim, 49/51/57 Cymbal) |
| **Tonal** | jede Note spielt den Sound **Tonal Snd**, C3 (60) ist seine eigene Tonhöhe, ein Halbton je Taste (±3 Oktaven) |

**MIDI-Kanal.** Welche Noten ein Slot bekommt, entscheidet Schwung über den *Receive Channel* des Slots (Slot 1 =
Kanal 1 …; *All* = alle Kanäle). LUXR spielt bei *Setup › MIDI Ch = All* (Standard) alles, was der Slot weitergibt; auf
1–16 gestellt nur Noten dieses Kanals – so bleibt LUXR stumm für die Noten anderer Spuren, auch wenn der Slot sie
durchlässt. Jede ankommende Note steht in `/data/UserData/schwung/luxr/midi.log` – Kanal, Note, Velocity, Schwungs
Quelle und ob LUXR sie gespielt hat –, so lässt sich prüfen, auf welchem Kanal die Noten einer Spur wirklich ankommen.

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
| Sound, Seite 1 (Synth) | Pitch (EDIT 1) | Decay (2) | Syn 3 | Syn 4 | Syn 5 (auf LUXR mit SHIFT) | Syn 6 | Syn 7 | Drive (8) |
| Sound, Seite 2 (Sample) | Sample | Pegel | Pitch | Decay | Start | Tone | Bend | Synth-Pegel |
| Sound, Seite 3 (Mix/Env) | Level | Pan | Delay | Reverb | Drive | Attack | Gate | Release |
| Sound, Seite 4 (LFOs) | LFO 1 Ziel | Welle | Rate | Tiefe | LFO 2 Ziel | Welle | Rate | Tiefe |
| Sound, Seite 5 | WAV-Datei | Length (50 ms / Whole) | Variation | | | | | |
| Drive & Reverb | Drv Mode | Drv Gain | Drv Tone | Drv Return | Size | Damp | Pre-Delay | Rev Return |
| Delay | Mode | Zeit | Zeit R | Feedback | Tone | Return | | |
| Comp & Limiter | Threshold | Ratio | Attack | Release | Makeup | Limiter | Ceiling | Lim Release |
| Setup | Output | Note Map | Tonal Snd | | Pre-Clip | Auto Level | Target | |
| Setup (Zellen) | Save Kit | Delete Kit | | | | | | |

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

**Sounds per Pad.** Die acht Sounds sind *eine* Seitengruppe „Sounds“ (eine Child-Ebene in Schwung): das Grid zeigt
immer nur den Sound im Fokus, das Jog-Rad blättert also nicht mehr durch alle 40 Seiten. **Pad anschlagen** – Schwung
meldet den Fingerdruck (`ui_press`), LUXR verbindet ihn mit der Note, die das Pad gespielt hat (innerhalb 100 ms; erst
eine Note danach, sonst die davor), und das Grid springt auf die Seiten dieses Sounds; nochmal dasselbe Pad holt sie
zurück, auch wenn man inzwischen weitergeblättert hat. Vom Sequencer gespielte Noten verschieben den Fokus nicht.
Weil alle acht Sounds dieselben Seiten teilen, tragen die Synth-Knöpfe 3–7 im Grid neutrale Namen (*Syn 3* … *Syn 7*,
die Stellen von LUXRs EDIT); PERFORM zeigt jeweils die echten (Kick: P.Env, P.Dec, Wave, FM, Click …).

**Eigene Kits.** *Setup › Save Kit* öffnet die Bildschirmtastatur (vorbelegt mit dem Namen des Kits): der Name wird
gespeichert und das Kit erscheint im Preset-Browser hinter den LUXR-Presets und ist gleich ausgewählt. *Delete Kit*
löscht das geladene eigene Kit (LUXRs Presets bleiben). Gespeichert wird alles, was den Klang ausmacht (alle Sounds, FX,
Master, die WAV-Pfade) als JSON in `/data/UserData/schwung/luxr/kits/<Name>.json` – außerhalb des Moduls, ein Update
lässt sie stehen. Lesen, Schreiben und Löschen erledigt derselbe Hintergrund-Thread wie die WAVs.

**Beim Start** lädt LUXR das erste Preset (*Patch 1*), wie der FM-1 beim Einschalten; *Factory* steht jetzt am Ende
der Liste. Ein gespeichertes Set stellt natürlich seinen eigenen Klang wieder her.

## Rechenlast

Gemessen auf einem Rechner (x86, 2,1 GHz), je Block von 128 Samples (2,9 ms): ~35 µs mit allen acht Sounds auf jedem
16tel, **~2 µs in Stille**. Ein Profil (callgrind) des vollen Betriebs: die acht Stimmen ~55 %, Hall und Delay ~20 %,
die Mischung ~12 %, Kompressor, Limiter und Drive zusammen ~3 %.

Was beim **Schrauben während des Spielens** den Move überlastete, war nicht die Klangerzeugung, sondern das
Zusammenspiel mit dem Host:

- **Fehlende Parameter-Beschreibungen.** Schwung schlägt bei jeder Änderung, die Automation („Lanes“), Modulation oder
  Scenes betrifft, den Parameter in `chain_params` nach. Fehlt er dort, liest der Host **alle 250 ms die ganze Liste
  neu und zerlegt sie in rund 1 MB Strukturen – im Audio-Thread**, solange daran gedreht wird. Bis 0.3.0 fehlten alle
  Auswahl-Parameter (Sample, LFO-Ziel/-Welle/-Rate, Length, Note Map …), Variation, Attack, Gate und Release, und die
  Liste wurde dafür jedes Mal neu als JSON gebaut. Jetzt steht **jeder** Schlüssel drin: die 33 Kit-Parameter und die 34
  eines Sounds je einmal – `module.json` nennt dem Host die Vorlage `s{index}_{key}`, mit der er `s1_…`–`s8_…` darauf
  abbildet; ohne Sound-Nummer (`p2`, `level` …) meint ein Schlüssel den Sound im Fokus. 71 Einträge statt 210, fest
  gebaut, eine Abfrage kostet 0,1 µs. `schwung/tests/check_docs.py` prüft bei jedem Build, dass jeder Schlüssel jeder
  Seite so aufgelöst wird, wie der Host es tut.
- **Seitenbeschreibung fest** (`ui_hierarchy`, 17 KB): einmal gebaut statt bei jeder Abfrage (~190 µs).
- **Zustand schnell:** den `state` liest Schwung nach Änderungen (Autosave, Preset-Markierung) – 4 µs statt 31 µs.
- **Hall und Delay ruhen**, solange nichts an sie gesendet wird und sie seit 1 s still sind (länger als die
  Delay-Leitung, 0,74 s: kein Echo geht verloren). Das spart ihre ~20 %, wenn ein Kit sie nicht nutzt.
- **Stille kostet nichts:** klingt keine Stimme, ist der Ausgang still und ruhen Hall und Delay, ruht die Engine bis
  zum nächsten Anschlag.
- **Kein Knacksen beim Neuanschlag:** der laufende Schlag klingt noch ~3 ms aus statt hart abgeschnitten zu werden.
- `-O3` statt `-O2`.

- **Tempo geglättet:** Delay-Zeit, LFOs und Gate folgen dem Tempo des Move erst, wenn es sich um mehr als 0,75 BPM
  ändert. Ein Host, dessen Tempo um einen Wert pendelt (119,4 / 120,6), ließ sonst bei jedem Pendeln die Delay-Leitung
  springen (im Test 10 % des Klangs bei Patch 1) – jetzt bitgleich mit festem Tempo.
- **Stimmen enden bei −60 dB** statt −72 dB: der letzte, unhörbare Teil langer Ausklänge (offene Hi-Hat, Becken) wird
  nicht mehr gerechnet.

**Messen auf dem Move.** Auf dem Rechner ist kein Kit auffällig teurer (Patch 11: ~37 µs je Block, Patch 3/4 ~41 µs,
Factory ~16 µs). Was der Move tatsächlich braucht, zeigt LUXR jetzt selbst:

- *Setup › CPU* (und unten auf der Perform-Seite): LUXRs Anteil an der Audio-Zeit in der letzten Sekunde, Mittel und
  Spitze, z. B. `4% pk 12%` (100 % = 2,9 ms = ein ganzer Block, den sich alle Module und der Move teilen).
- `/data/UserData/schwung/luxr/cpu.log`: solange etwas klingt, eine Zeile je Sekunde – Mittel und Spitze in µs, die
  Zahl der Stimmen, das Kit. Lesen per SSH (`ssh ableton@move.local tail -50 /data/UserData/schwung/luxr/cpu.log`)
  oder per SFTP (z. B. Cyberduck). Geschrieben vom Hintergrund-Thread, nie vom Audio-Thread.

(Hall-Ruhe, Ausklingen und die WAV-Ebene stehen in `lx_synth.c` unter `#ifdef`, nur im Modul eingeschaltet – die
Firmware bleibt gleich.)

## Speichern mit dem Projekt

Alles, was den Klang ausmacht – alle acht Sounds, Kit-FX, Master, Kompressor, Limiter, die WAV-Pfade, die Nummer des
Kits – steht im `state`, den Schwung mit dem Set speichert und beim Öffnen zurückgibt, **unabhängig davon, welches
Preset gewählt war**: auch geänderte Sounds kommen so zurück, wie sie zuletzt klangen. Schwung stellt beim Öffnen auch
die Auswahl des Preset-Browsers wieder ein; dieses erste Setzen desselben Kits nach einem Zustand überschreibt den
Klang nicht. Wählt man ein Kit danach von Hand, lädt es.

## Seiten

- **Preset-Browser**: die Kits der 13 Preset-Patches, *Factory*, dann die eigenen Kits (dieselben, mit denen der Emulator startet:
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
| `dsp/luxr_user.c` | eigene Kits speichern, auflisten, laden, löschen (im selben Thread) |
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

Für den Betreiber von Schwung (englisch): [`MAINTAINER.md`](MAINTAINER.md) – Installationslink, wie das Modul die Host-API nutzt, die Lizenzfrage und ein Vorschlag für den Katalog-Eintrag.
