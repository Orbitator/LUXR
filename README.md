# ONE CLICK FM-1

Eigene Firmware für den M-VAVE FM-1: der Generator von ONE CLICK SYNTH auf dem Gerät. Ein Knopfdruck erzeugt einen
ganzen Song – Tonart, Akkordfolge, vier Sounds und vier Spuren (Bass, Akkorde, Lead, Drums), die zueinander passen.

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
| Tasten | spielen die gewählte Spur; im STEP-Editor / bei Step-Aufnahme: Noten an den Cursor | weiße F3–B3: Mute Spur 1–4, C4–F4: Solo; schwarze: Direktfunktionen (unten) | |

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
| GLO › MIXER | Pegel der 4 Spuren | Pan der 4 Spuren |
| SAVE › SOUND / SONG | Platz (16 Sounds / 4 Songs), Speichern/Laden – OCT+ führt aus | |
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

**SHIFT + schwarze Tasten** (GLO länger halten zeigt die Übersicht im Display):

| F#3 | G#3 | A#3 | C#4 | D#4 | F#4 | G#4 | A#4 | C#5 | D#5 | F#5 |
|---|---|---|---|---|---|---|---|---|---|---|
| Undo | Redo | Copy | Paste | Clear | Note ◂ | Note ▸ | neuer Part | neuer Sound | Metronom an/aus | Tap Tempo |

*Note ◂ / ▸* verschiebt im STEP-Editor die Note am Cursor samt ihren Ties um einen Step, an der Nachbarnote vorbei
(Drums: den Schlag der gewählten Lane); der Cursor geht mit. Auf anderen Seiten verschiebt es den ganzen Part.

**Werkzeuge (SEQ halten + weiße Taste)**, F3 bis G5: SONG (ONE CLICK), TRACK, SOUND, PART, KEY, COOK, CLEAR, REVERSE,
SHIFT ◂, SHIFT ▸, RHYTHM, NOTES, OSC, FILTER, ENV, OCTAVE (die letzten sechs erneuern nur diesen Teil, so stark wie
MUTATE es sagt). Schwarze Tasten: COPY, PASTE, DOUBLE (Pattern verdoppeln).

**Instrument-Typen** (ARP halten + weiße Taste, oder SHIFT + ALGORITHM): Bass, Chords, Pad, Atmo, Piano, Guitar, Lead,
Pluck, Brass, Flute, Drums, Kick, Snare, Perc – jede Spur kann jeder Typ sein.

USB-MIDI: Kanal 1–4 spielt Spur 1–4 (oder im Menü: alle Kanäle die gewählte Spur), Drums nach General-MIDI-Noten.

Rettungswege (aus Felucca übernommen, nicht entfernen): **OCT− und OCT+ 5 s halten** → UBOOT;
zwei Abstürze in den ersten 30 s nach dem Einschalten → UBOOT; USB-SysEx `F0 22 24 35 7D F7` → UBOOT;
das M-UPGRADE-Update über USB.

## Klang

Sieben eigene Engines in Festkomma (der FM-1 hat keine FPU): Subtraktiv (2 Osc + Sub + Noise, SVF),
Acid (TB-303-artig, Sättigung, Accent/Slide), FM (3 Operatoren, 3 Algorithmen), Supersaw (bis 7 Sägezähne),
Additiv (bis 8 Teiltöne, Orgel/Risset-Glocke/Marimba), Noise (gefiltertes Rauschen, Tiefpass/Bandpass/Hochpass) und
Drums (8 synthetisierte Instrumente). Pro Sound: Amp- und Filter-ADSR, LFO, Dual-Stimme, Pitch/Fine.
Dazu Delay (1/8 bis 3/4, Feedback) und Reverb (Größe, Dämpfung) und ein Metronom.

Speicher: 16 Sounds, 4 Songs und die Einstellungen liegen im Flash bei 0x97000–0x9D000 (der Bereich, den Felucca
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
