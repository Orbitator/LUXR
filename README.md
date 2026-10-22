# ONE CLICK FM-1

Eigene Firmware für den M-VAVE FM-1: der Generator von ONE CLICK SYNTH auf dem Gerät. Ein Knopfdruck erzeugt einen
ganzen Song – Tonart, Akkordfolge und sechs Spuren, die zueinander passen: Bass, Akkorde, Lead, Drums und – wie im
X-Generator – eine Kick- und eine Loop-Spur, die mit eigenen Samples spielen können.

Status: **v0.2, auf dem Rechner und im Emulator getestet, noch nie auf einem FM-1 gelaufen.**

## Bedienung

Nach Felucca-Art: eine Taste **tippen** öffnet ihre Seite (beim Loslassen; nochmal tippen = nächste Unterseite, Punkte
unten rechts), **halten** ruft ihre Haltefunktion auf. **KNOB 1–4** sind die vier Karten der Seite. **GLO halten = SHIFT**:
die Karten zeigen die zweite Reihe (B), und viele Bedienelemente bekommen eine zweite Funktion.

| Bedienelement | tippen / drehen | mit SHIFT (GLO halten) | halten |
|---|---|---|---|
| **SELECT** | Tempo | Swing | |
| **ALGORITHM** | Spur wählen | Instrument-Typ der Spur | |
| **PRESETS** | Sound-Verlauf (zurück) / neuer Sound; im STEP-Editor der Cursor; auf SAVE der Platz; im Menü die Zeile | Engine | |
| **KNOB 1–4** | Reihe A der Seite | Reihe B der Seite | |
| **PLAY** | Start / Stop | von vorn | |
| **REC** | Aufnahme scharf / aus | Overdub ↔ Replace | ONE CLICK: neuer Song |
| **OCT− / OCT+** | Oktave der Tasten (beide: 0); auf SAVE/Menü: OCT+ ausführen, OCT− zurück | Transponieren ±1 | beide 5 s: UBOOT |
| **SAVE** | Seite SOUND · SONG · UNDO | Redo | Undo |
| **HOME** | Seite HOME | | MENÜ |
| **SEQ** | Seite STEP · EUCLID · SEQUENCE | | Werkzeuge auf den Tasten |
| **EDIT** | Seiten der Engine · FILTER · VOICE | | weiße Taste = Engine |
| **ARP** | Seite GENERATOR | | weiße Taste = Instrument-Typ |
| **SCL** | Seite SCALE | | Taste = Grundton |
| **GLO** | Seite MIXER | (ist SHIFT) | |
| **FX · ENV · LFO** | ihre Seiten | | |
| Tasten | spielen die gewählte Spur; im STEP-Editor / bei Step-Aufnahme: Noten an den Cursor | weiße F3–D4: Mute Spur 1–6, E4–B4: Solo; schwarze: Direktfunktionen (unten) | |

Die Seiten (Reihe A | Reihe B mit SHIFT):

| Seite | Reihe A | Reihe B |
|---|---|---|
| HOME | Cutoff, Reso, Decay, Pegel (live, nicht im Sound) | Pitch, Fine, Oktave der Spur, Transpose (Drums: Tune, Decay, Kit) |
| EDIT › OSC / ACID / FM / SAW / ADD / NOISE / KIT | die Parameter der Engine | weitere Parameter der Engine |
| EDIT › FILTER | Cutoff, Reso, Env-Menge, Keytrack | Filter-ADSR |
| EDIT › VOICE | Mono/Poly, Glide, Dual (Unison, Oktave hoch/tief, Quinte), Dual-Pegel | Pitch, Fine, Oktave |
| ENV | Amp-ADSR | Filter-ADSR |
| LFO | Rate, Tiefe, Ziel (Cutoff, Pitch, Amp, Pan), Welle (Sinus, Dreieck, Säge, Rechteck, S&H) | Pitch, Fine, Glide, Keytrack |
| FX | Pegel, Pan, Delay-Send, Hall-Send | Delayzeit (1/8 … 3/4), Feedback, Hallgröße, Dämpfung |
| SCL | Grundton, Skala, Akkordfolge, Oktave der Tasten | Transpose, Swing |
| SEQ › STEP | Note, Velocity, Länge (Ties), Step-Art (Pause/Note/Tie) | Accent, Slide, Akkord (einzeln, Terz, Dreiklang, Septakkord) |
| SEQ › STEP (Drums) | Lane, Schlag, Velocity, Accent | |
| SEQ › EUCLID | Länge, Schläge, Rotation, Lane (Drums) | |
| SEQ › SEQUENCE | Länge (1–64), Gate, Swing, Verschieben | SEQ an/aus, MASTER, Oktave, Mutate |
| ARP › GENERATOR | Dichte, Variation, Stil, Lage | Mutate, Instrument-Typ |
| GLO › MIX 1-4 / MIX 5-6 | Pegel der Spuren | Pan der Spuren |
| SAVE › SOUND / SONG / SAMPLES | Platz (16 Sounds / 4 Songs / 8 Samples), Speichern/Laden bzw. Zuweisen/Löschen – OCT+ führt aus | |
| MENÜ (HOME halten) | Transpose, Rec-Modus, Metronom (aus / bei Aufnahme / an), MIDI IN (Kanal 1–4 = Spuren / alle Kanäle auf die gewählte Spur), Theme (10 Paletten), Swing, Version | |

**Eigene Noten**

- *STEP-Editor* (SEQ › STEP): PRESETS bewegt den Cursor, die Tasten schreiben Note oder Akkord (gleichzeitig gedrückte
  Tasten) an den Cursor, die Knöpfe ändern Note, Velocity, Länge, Accent, Slide, Akkordform. Drums: die 16 weißen Tasten
  sind die 16 Steps des Takts, die schwarzen Tasten wählen die Lane; die Tasten-LEDs zeigen das Pattern.
- *Step-Aufnahme*: REC tippen, während gestoppt – jede Taste (oder jeder Akkord) wird an den Cursor geschrieben, danach
  springt er einen Step weiter; PRESETS vorwärts lässt eine Pause.
- *Live-Aufnahme*: REC tippen, dann PLAY (oder umgekehrt) – Tasten und USB-MIDI werden in die gewählte Spur
  aufgenommen, auf den nächsten 16tel quantisiert, gehaltene Noten als Ties. **Overdub** fügt hinzu, **Replace** (SHIFT +
  REC) leert jeden Step, den der Durchlauf erreicht. Metronom im Menü.
- Jede Aufnahme und jeder Eingriff kann rückgängig gemacht werden (SAVE halten; 15 Stufen, SHIFT + SAVE = Redo).

**SHIFT + schwarze Tasten** – F#3 Undo und G#3 Redo immer, die anderen neun je nach Seite (GLO länger halten zeigt
die aktuelle Belegung im Display):

| Seite | A#3 | C#4 | D#4 | F#4 | G#4 | A#4 | C#5 | D#5 | F#5 |
|---|---|---|---|---|---|---|---|---|---|
| HOME, SAVE, Menü | Copy | Paste | Clear | Part ◂ | Part ▸ | neuer Part | neuer Sound | Metronom | Tap Tempo |
| SEQ › STEP | Step kopieren | Step einfügen | Step leeren | Note ◂ | Note ▸ | Insert | Delete | Oktave − | Oktave + |
| SEQ › EUCLID / SEQUENCE | Copy | Paste | Clear | Part ◂ | Part ▸ | Reverse | Double | Half | Rhythm |
| EDIT, ENV, LFO, FX | Sound kopieren | Sound einfügen | neuer Sound | Osc | Filter | Env | Sound ◂ | Sound ▸ | Live-Regler 0 |
| SCL | neue Tonart | Akkorde ◂ | Akkorde ▸ | Grundton − | Grundton + | Skala ◂ | Skala ▸ | Transp. − | Transp. + |
| ARP (Generator) | neuer Part | neuer Track | neuer Sound | Rhythm | Notes | Octave | Cook | Stil ◂ | Stil ▸ |
| GLO (Mixer) | alle an | Solo | Mute | Pegel − | Pegel + | Pan L | Pan C | Pan R | Metronom |

*Note ◂ / ▸* verschiebt im STEP-Editor die Note am Cursor samt ihren Ties um einen Step, an der Nachbarnote vorbei
(Drums: den Schlag der gewählten Lane); der Cursor geht mit. *Insert / Delete* fügen am Cursor eine Pause ein bzw.
entfernen den Step (der Rest rückt nach).

**Werkzeuge (SEQ halten + weiße Taste)**, F3 bis G5: SONG (ONE CLICK), TRACK, SOUND, PART, KEY, COOK, CLEAR, REVERSE,
SHIFT ◂, SHIFT ▸, RHYTHM, NOTES, OSC, FILTER, ENV, OCTAVE (die letzten sechs erneuern nur diesen Teil, so stark wie
MUTATE es sagt). Schwarze Tasten: COPY, PASTE, DOUBLE (Pattern verdoppeln).

**Instrument-Typen** (ARP halten + weiße Taste, oder SHIFT + ALGORITHM): Bass, Chords, Pad, Atmo, Piano, Guitar, Lead,
Pluck, Brass, Flute, Drums, Kick, Snare, Perc, Loop – jede Spur kann jeder Typ sein.

**LXR-Drums (wahlweise)**

Auf Drum- und Kick-Spuren ist neben den eingebauten Drums und dem Sample die Engine **LXR** wählbar (EDIT halten +
zweite weiße Taste, oder SHIFT + PRESETS): sechs Drum-Stimmen nach der Klangarchitektur der Sonic Potions LXR, hier
neu in Festkomma geschrieben (kein LXR-Code: dessen Lizenz verbietet kommerzielle Nutzung und verträgt sich nicht mit der
GPL). EDIT blättert durch die sechs Stimmen, je 8 Parameter (4 + 4 mit SHIFT):

| Stimme (Lanes) | Parameter |
|---|---|
| KICK (BD), TOM (TM), PERC (RS) | Pitch, Decay, Pitch-Env, Pitch-Decay, Wave (Sinus, Dreieck, Säge, Rechteck, Rauschen), FM, Click, Drive |
| SNARE (SD, CP als Clap-Bursts) | Pitch, Decay, Noise, Noise-Decay, Tone, Snap, Click, Drive |
| CYMBAL (CB) | Pitch, Decay, Ratio, FM, Filter (Hochpass), Reso, Noise, Drive |
| HIHAT (CH, OH: die geschlossene stoppt die offene) | Pitch, Closed-Decay, Open-Decay, FM, Filter, Noise, Click, Drive |

ENV zeigt bei LXR das Kit (Tune, Decay aller Stimmen, Pegel, Pan, Sends). ONE CLICK würfelt bei gewählter LXR ein neues
LXR-Kit. Rechenzeit: etwa doppelt so viel wie die eingebauten Drums (tests/cpu_bench.c).

**Kick- und Loop-Spur, Samples**

- Spur 5 (KICK) spielt die Kick: die eingebaute Synth-Kick oder einen One-Shot. Spur 6 (LOOP) spielt einen Loop, in
  16 Slices pro Takt geschnitten; jeder Step spielt einen Slice, so folgt der Loop dem Tempo ohne Tonhöhenänderung
  (wie in der Web-App). Der Generator macht daraus Varianten (STRAIGHT, SHUFFLE, STUTTER, HALFTIME, Dichte).
- Samples kommen über die Companion-App (oder im Emulator) auf das Gerät: Datei wählen (WAV, MP3 …), One-Shot oder
  Loop, Platz 1–8, „zuweisen“ → die Datei wird im Browser auf 22 kHz mono, 8 Bit (µ-law) gewandelt, per USB-SysEx in den
  Flash geschrieben und der Spur gegeben. Platz insgesamt ca. 11 s (der freie Teil des Flashs neben der App).
- Am Gerät: EDIT auf Spur 5/6 zeigt das Sample (Wellenform, Slices) mit SAMPLE (Platz), START, TUNE, Pegel, Filter;
  SAVE › SAMPLES weist ein Sample der gewählten Spur zu, löscht es oder alle.
- Im STEP-Editor der Loop-Spur ist die Note der Slice (S1 …); die weißen Tasten spielen bzw. setzen die Slices.
- Eine Sample-Engine für die Synth-Spuren (chromatisch spielen) gibt es bewusst nicht: dafür ist der Speicher zu klein.

USB-MIDI: Kanal 1–6 spielt Spur 1–6 (Loop: ab C2 die Slices) (oder im Menü: alle Kanäle die gewählte Spur), Drums nach General-MIDI-Noten.

Rettungswege (aus Felucca übernommen, nicht entfernen): **OCT− und OCT+ 5 s halten** → UBOOT;
zwei Abstürze in den ersten 30 s nach dem Einschalten → UBOOT; USB-SysEx `F0 22 24 35 7D F7` → UBOOT;
das M-UPGRADE-Update über USB.

## Klang

Sieben eigene Engines in Festkomma (der FM-1 hat keine FPU): Subtraktiv (2 Osc + Sub + Noise, SVF),
Acid (TB-303-artig, Sättigung, Accent/Slide), FM (3 Operatoren, 3 Algorithmen), Supersaw (bis 7 Sägezähne),
Additiv (bis 8 Teiltöne, Orgel/Risset-Glocke/Marimba), Noise (gefiltertes Rauschen, Tiefpass/Bandpass/Hochpass) und
Drums (8 synthetisierte Instrumente). Pro Sound: Amp- und Filter-ADSR, LFO, Dual-Stimme, Pitch/Fine.
Dazu Delay (1/8 bis 3/4, Feedback) und Reverb (Größe, Dämpfung) und ein Metronom.

Speicher: 16 Sounds, 4 Songs, die Einstellungen und die Samples liegen im Flash bei 0x97000–0xE0000 (der Bereich, den Felucca
für seine Projekte nutzt – wer zwischen Felucca und ONE CLICK wechselt, verliert dort die Daten der anderen Firmware).

Stimmen: 8 Synth-Stimmen für Bass, Akkorde und Lead mit Reserven (Bass 1, Akkorde 4, Lead 1, zwei frei vergeben);
eine wiederholte Note übernimmt ihre eigene Stimme. Die Drums haben eine Stimme pro Instrument wie eine Drum-Machine
(die geschlossene Hi-Hat blendet die offene in 3 ms aus). `tests/voices_bench.c` zählt hörbar abgeschnittene Noten:
vorher 118 pro Minute, jetzt 0.

## Bauen

Wie bei Felucca: JieLi-Toolchain (Linux x86-64, auf dem Mac per Docker) und AC79-SDK.

```
tools/get_toolchain.sh
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/fw-AC79_AIoT_SDK python3 tools/build.py   # -> build/oneclick.fwsc
python3 tools/fm1_install.py build/oneclick.fwsc                                         # pip3 install mido python-rtmidi
```

Vor dem ersten Flashen: die Original-Firmware (FM-1 V15, „PC Firmware“ auf m-vave.com) bereitlegen.
Zurück geht es mit M-VAVEs Updater oder dem Felucca-Webinstaller („Return to official V15“).

## Tests auf dem Rechner

```
tests/run.sh      # Host-Test unter ASan/UBSan, Typprüfung des Firmware-Builds, Demo-WAV und Screenshot
```

## Emulator

`emu/build.sh` baut `build/emu/oneclick-emu.html`: die App (Klang, Sequencer, Generator, Display, USB-Protokoll) als
WebAssembly mit dem Bedienfeld wie am FM-1 (links MASTER, SELECT, PRESETS, ALGORITHM, OCT; Mitte Display; rechts
KNOB 1–4 und die zwei Tastenreihen; Halten und SHIFT funktionieren, auch über die Computer-Tastatur: ⇧ = SHIFT,
Enter = ONE CLICK, R = REC, Z = Undo). Gespeichertes bleibt im Browser. Export: MIDI-Datei (aus dem Dump) und WAV.
Der Ton ist bit-gleich zu einem nativen Clang-Build derselben Quellen (`emu/emu_test.mjs`). Nicht simuliert werden der
JieLi-Prozessor und seine Rechenzeit, Booten, Update und UBOOT; ob die Engines auf dem Gerät schnell genug sind, zeigt
erst die Hardware.

Hinweis: Der Generator ruft `rr()` mehrfach in einer Argumentliste auf. Deren Auswertungsreihenfolge ist in C nicht
festgelegt, deshalb erzeugt ein GCC-Build aus demselben Startwert andere Songs als ein Clang-Build (Gerät, Emulator).

## Companion-App

`companion/index.html` (Chrome/Edge, Web MIDI): ONE CLICK und alles Weitere vom Browser aus, Pattern-Ansicht und
-Bearbeitung. Protokoll: `firmware/src/oc_proto.c`.

## Build auf GitHub und Update-Seite

`.github/workflows/build.yml` baut bei jedem Push auf `main` die Firmware (JieLi-Toolchain + AC79-SDK) und veröffentlicht
die Admin-Seite zusammen mit `oneclick.fwsc` auf GitHub Pages (Einstellungen › Pages › Source: „GitHub Actions“).
Die Seite `https://<user>.github.io/oneclick-fm1/` lädt die Firmware dieses Builds selbst; in Chrome/Edge öffnen, FM-1 per USB verbinden, „Installieren“.
