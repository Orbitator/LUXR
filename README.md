# LUXR

Eine Firmware: eine Drum-Machine für den M-VAVE FM-1 mit **acht Sounds** im Stil der
Sonic Potions LXR (die Klangarchitektur der LXR-Web-Drums bzw. der LXR-Engine von ONE CLICK, hier eine Stimme pro
Spur) und einem **Sample-Layer mit den originalen LXR-Transienten-Samples**. Sie nutzt dieselbe Plattform wie
ONE CLICK (Felucca: Update über USB, UBOOT-Rettung, Display, Speicher), nur die App ist neu (`firmware/src/lx_*.c`).
Dieses Repository ist aus [ONE CLICK FM-1](https://github.com/Orbitator/oneclick-fm1) hervorgegangen; dessen Anleitung
steht in [README-ONECLICK.md](README-ONECLICK.md) (dort auch die Plattform, der Emulator und die Companion-App).

Status: **v0.1, auf dem Rechner getestet (tests/test_lx.c), noch nie auf einem FM-1 gelaufen.**

## Die acht Sounds

| # | Sound | Synth-Layer (EDIT, Knöpfe 1–4 / mit SHIFT 5–8) |
|---|---|---|
| 1 | KICK | Pitch, Decay, Pitch-Env, Pitch-Decay · Wave (Sinus … Rauschen), FM, Click, Drive |
| 2 | SNARE | Pitch, Decay, Noise, Noise-Decay · Tone, Snap, Click, Drive |
| 3 | CLAP | Tone, Decay, Spread, Bursts · Color, Reso, Click, Drive |
| 4 | CL HAT | Pitch, Decay, FM, Filter · Noise, Click, Reso, Drive (stoppt die offene Hi-Hat) |
| 5 | OP HAT | wie CL HAT, längere Decays |
| 6 | TOM | wie KICK |
| 7 | RIM | wie KICK (kurz, hoch) |
| 8 | CYMBAL | Pitch, Decay, Ratio, FM · Filter, Reso, Noise, Drive |

**Sample-Layer** (EDIT dreimal, oder ENV): **44 Transienten pro Sound** zur Wahl:
- **16 eigene** des Sounds (BD 01 … BD 16, SD 01 … usw.), beim Bauen synthetisiert (`tools/gen_own_transients.py`);
- **16 im Stil der Klassiker** (808 A–B, 909 A–B, 606, 707 A–B, DMX A–B, Linn LM-1 A–B, LinnDrum LM-2 A–B, SDS6, SDS7,
  SDS8), beim Bauen nach deren Bauweise synthetisiert, keine Aufnahmen der Geräte (`tools/gen_machine_transients.py`,
  22,05 kHz 8 Bit wie bei 707/DMX/Linn; das LM-1 hatte keine Becken: dort ein Cabasa);
- die **12 Original-LXR-Transienten** (CLK, CK2, TIK, KIK, RIM, DRP, HAT, CLP, KK2, SNR, TOM, SP2); dazu SNAP und OFF.

Seite SAMPLE: Sample, Level, Pitch (25 … 175 %), Decay (FULL = ganzes Sample); Seite SAMPLE 2 (EDIT nochmal): Start
(Einsprungpunkt, davor grau), Tone (LP = Tiefpass, HP = Hochpass), Bend (Pitch-Hüllkurve des Samples: + von oben herab,
− von unten herauf), Synth (Pegel des Synth-Layers – auf 0 spielt nur das Sample). Wie an der LXR wird das Sample vor
der Amp-Hüllkurve eingemischt. Jeder Werks- und jeder gewürfelte Sound spielt immer ein Transienten-Sample.
Das Drehen an Knöpfen spielt den Sound nicht an (anspielen: weiße Taste des Sounds oder D#4).

**Pro Sound außerdem** (wie in der Web-App): Volume, Pan, Sends für Delay, Reverb und Drive (EDIT dreimal: SOUND MIX) und
**zwei LFOs** (LFO, nochmal: LFO 2) mit Ziel (Pitch, Filter, Decay, Level, Pan, Sample), Welle (Sinus, Dreieck, Säge,
Rechteck, S&H), Rate im Tempo (1/16 … 8 Takte, ab dem Takt, in dem PLAY startete) und bipolarer Tiefe (−64 … +63).

## Bedienung

**GLO lange halten (ab 250 ms) = SHIFT.** Wird GLO zusammen mit einem anderen Bedienelement gedrückt, gilt SHIFT
sofort. GLO kurz tippen öffnet den MIXER, nochmal tippen schaltet auf dessen Seite 2 (PAN) und zurück.
Eine Seiten-Taste nochmal tippen zeigt ihre nächste Seite (Punkte unten rechts).

| Bedienelement | normal | mit SHIFT |
|---|---|---|
| **SELECT** | Sound 1–8 wählen | Pattern wählen |
| **ALGORITHM** | würfelt einen neuen Sound für den gewählten (immer mit Transienten-Sample) und spielt ihn an | Tempo |
| **PRESETS** | die gespeicherten Patches (Kit + Patterns) durchblättern, jeder wird sofort geladen | nur die Patterns des Patches |
| **KNOB 1–4** | die vier Karten der Seite (Reihe A) | Reihe B |
| **PLAY** | Start / Stop | von vorn |
| **REC** | tippen: Aufnahme scharf / aus · **halten: QUANTIZE** | ERASE scharf (gehaltene Pads löschen ihre Steps) |
| **OCT− / OCT+** | SEQ: Takt zurück / vor · MIXER: Sounds 1–4 / 5–8 · STORE: zurück / speichern · sonst: Sound zurück / vor | SEQ: Takt entfernen / Takt anhängen (Kopie des letzten) |
| **OCT− + OCT+ 5 s** | UBOOT (Rettungsweg, unverändert aus Felucca) | |

**Seiten** (Taste tippen, nochmal tippen = nächste Seite):

| Taste | Seite | Knöpfe 1–4 | mit SHIFT |
|---|---|---|---|
| HOME | KIT | Tempo, Swing, Tune (alle), Decay (alle) | MIDI-Kanal, Theme, Version |
| SEQ | SEQUENCER | Länge der Spur, Velocity des letzten Steps, Rotate, Euclid | **PAT LEN: jeder Dreh verdoppelt die Patternlänge (zurück: halbiert)**, **COPY x2: Noten in die neuen Takte kopieren (an/aus)**, Accent des letzten Steps |
| SEQ (2.) | **LIVE PADS** | Pad-Velocity, Quantize-Raster, -Richtung, Metronom | |
| EDIT | SYNTH 1-4 | Synth-Parameter 1–4 | Parameter 5–8 |
| EDIT (2.) | SYNTH 5-8 | Synth-Parameter 5–8 (z. B. Tone, Snap, Click, Drive) | Parameter 1–4 |
| EDIT (3.) / ENV | SAMPLE LAYER | Sample, Level, Pitch, Decay | Synth-Pegel |
| EDIT (4.) | SAMPLE 2 | Start, Tone, Bend, Synth-Pegel | |
| EDIT (5.) | SOUND MIX | Volume, Pan, Delay-Send, Reverb-Send | Drive-Send |
| LFO / LFO (2.) | LFO 1 / LFO 2 | Ziel, Welle, Rate (im Tempo), Tiefe (bipolar) | |
| FX | FX SENDS (des Sounds, im Kit) | Drive-Send, Delay-Send, Reverb-Send, Volume | |
| FX (2.) | DRIVE (global, im Kit) | Modus (SOFT, HARD, FOLD), Stärke, Ton, Pegel | |
| FX (3.) | DELAY (global, im Kit) | Zeit (1/8 … 3/4), Feedback, Ton, Pegel | |
| FX (4.) | REVERB (global, im Kit) | Größe, Dämpfung, Pegel | |
| SCL | PATTERNS | Pattern (spielend: wechselt am Pattern-Ende), Chain (spielt N Patterns nacheinander), **Copy** (ein Dreh kopiert das angezeigte Pattern), **Paste** (ein Dreh fügt es ins angezeigte Pattern ein) | |
| ARP | NOTE REPEAT | Rate, Velocity, Vel-Modus (fest, steigend, fallend, zufällig), Latch | Humanize |
| GLO (tippen) | MIXER, Seite 1 | Lautstärke der vier Sounds der Bank | Pan der vier Sounds |
| GLO (nochmal tippen) | MIXER, Seite 2: PAN | Pan der vier Sounds der Bank (L64 … C … R63) | Lautstärke |
| SAVE | STORE | Platz 1–8, **was gespeichert wird** (KIT+PAT, nur KIT, nur PATTERN – OCT+ speichert), **was geladen wird** (KIT+PAT, nur KIT, nur PATTERN), **LOAD** (ein Dreh lädt den Platz) | |
| REC (halten) | **QUANTIZE** | Raster (1/16, 1/8, 1/4), Richtung (zum nächsten Rasterpunkt / zum vorherigen), Metronom (aus, bei Aufnahme, an) | |

**Weiße Tasten (16).** Auf der **SEQUENCER-Seite** sind sie die 16 Steps des angezeigten Takts der gewählten Spur
(LED = Step an, die laufende Position blinkt invertiert). Kurz tippen: ein leerer Step wird gesetzt (und klingt an, wenn
das Gerät steht), ein gesetzter beim Loslassen gelöscht. **Lang halten (ab 350 ms) oder halten und einen Knopf drehen:
STEP EDIT** – KNOB 1 Velocity, KNOB 2 Probability (6 … 100 %: wie oft der Step spielt), KNOB 3 Pan nur für diesen Step
(SOUND = der Pan des Sounds, L56 … C … R56), KNOB 4 Accent; mehrere gehaltene Steps werden zusammen bearbeitet. Im Raster
sind Steps mit Probability blasser, solche mit eigenem Pan haben einen Punkt.
**Auf allen anderen Seiten** spielen F3–C4 die Sounds 1–8 (D4–G5 dieselben mit Accent), mit REC + PLAY aufgenommen; die
LEDs leuchten beim Anschlag. Auf den Seiten eines Sounds (SYNTH, SAMPLE, SOUND MIX, LFO, FX SENDS) wählt der Anschlag den
Sound auch aus: das Display zeigt den gespielten. Auf NOTE REPEAT sind es die Repeat-Pads. Mit SHIFT überall: F3–C4 Mute,
D4–G5 Solo.

**LIVE PADS (SEQ zweimal).** Alle acht Sounds gleichzeitig spielbar: F3–C4 sind Sound 1–8, D4–G5 dieselben mit Accent.
Mit REC + PLAY wird jeder Schlag aufgenommen, auf das Raster aus QUANTIZE (REC halten) gelegt.

**Schwarze Tasten (11): Funktionen.**

| Taste | normal | mit SHIFT |
|---|---|---|
| F#3 | Sound zurück | Undo |
| G#3 | Sound vor | Redo |
| A#3 | Mute des Sounds | alle wieder an |
| C#4 | Solo des Sounds | Solo aus |
| D#4 | Sound anspielen | neuer (gewürfelter) Sound |
| F#4 | Takt kopieren | ganzes Pattern kopieren |
| G#4 | Takt einfügen | ganzes Pattern einfügen |
| A#4 | Spur löschen | ganzes Pattern löschen |
| C#5 | Accent-Modus (weiße Tasten setzen Accents) | Spur verdoppeln (Länge × 2) |
| D#5 | Follow (die Anzeige folgt dem laufenden Takt) | Zufalls-Pattern für die Spur |
| F#5 | Tap-Tempo | Metronom: aus → bei Aufnahme → an |

**Note Repeat (ARP).** ARP tippen schaltet Note Repeat ein (nochmal tippen: aus), ARP halten nur solange gehalten.
Dann sind die weißen Tasten F3–C4 die **Pads der acht Sounds**, D4–G5 wählen die **Rate** direkt
(Single, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T). Ein gehaltenes Pad wiederholt im Raster der Rate (beim Spielen
auf dem Takt-Raster). LATCH: ein Pad läuft weiter, bis es nochmal gedrückt wird. Mit REC + PLAY wird jeder Schlag
quantisiert geschrieben; mit ERASE (SHIFT + REC) löscht ein gehaltenes Pad die Schläge seines Sounds.

**Patches.** Ein Patch ist ein Kit (die acht Sounds samt LFOs und Sends, Tune, Decay, Drive/Delay/Reverb) und seine
16 Patterns (jede Spur bis 64 Steps, eigene Länge, Velocity/Probability/Pan je Step; Tempo, Swing, Chain, Mute/Solo).
**14 Plätze**, also bis zu 14 Kits und 224 Patterns. SAVE speichert beides oder nur Kit bzw. nur Patterns (der andere
Teil bleibt, wie er auf dem Platz war); PRESETS lädt sie (mit SHIFT: nur die Patterns), ebenso KNOB 4 auf der STORE-Seite.

**Updates behalten die Patches.** Ein Firmware-Update schreibt nur das Programm, nicht den Speicher. Ältere
Speicherformate von LUXR (seit v0.1) liest die Firmware weiter: beim ersten Start nach dem Update zieht sie die bisherigen
8 Plätze ins neue Format um („PATCHES KEPT“). Zusätzlich gibt es auf der Update-Seite **„3 · Backup“**: alle belegten
Plätze als `.luxr`-Datei herunterladen und wieder zurückspielen (auch ältere Backups, sie werden umgewandelt); beim
Installieren zieht die Seite auf Wunsch vorher ein Backup und spielt es danach zurück. Das geht ab dieser LUXR-Version –
die Firmware davor kennt die Backup-Befehle noch nicht.

**MIDI IN** (USB): General-MIDI-Drum-Noten spielen die Sounds (36 Kick, 38/40 Snare, 39 Clap, 42/44 CH, 46 OH,
41–50 Tom, 37 Rim, 49/51/57 Cymbal), C4–G4 die Sounds 1–8. Kanal im KIT (SHIFT), Standard: alle.

## Bauen

```
JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/fw-AC79_AIoT_SDK python3 tools/build.py --app lxr   # -> build/luxr.fwsc
python3 tools/fm1_install.py build/luxr.fwsc
```

`tools/gen_own_transients.py` erzeugt die eigenen Transienten (GPL, synthetisiert), `tools/gen_lxr_transients.py` holt
die Original-Transienten aus dem LXR-Quelltext (`$LXR_SRC`, sonst ein Klon von
github.com/SonicPotions/LXR nach `build/ext/LXR`) und schreibt `build/gen/lxr_transients.h`. Kennung FM-1_870 (Release
X.Y: FM-1_87Y) – die Update-Seite / Firmware-Werkstatt nimmt sie wie ONE CLICK an (FM-1_8xx). Auf GitHub baut der
Workflow (`.github/workflows/build.yml`) sie bei jedem Push auf `main` und legt sie als Actions-Artefakt
`luxr-fwsc` ab (Actions › build › der Lauf › Artifacts).

**Installieren:** die Update-Seite **https://orbitator.github.io/LUXR/** in Chrome oder Edge öffnen, FM-1 per USB
verbinden, „Verbinden“ (Chrome fragt nach der MIDI-Erlaubnis), „Installieren“. Die Firmware des letzten Builds liegt
neben der Seite und ist schon geladen (wie bei ONE CLICK); eine eigene `luxr.fwsc` kann trotzdem gewählt werden. Im Claude-Artefakt „Firmware-Werkstatt“ geht
das nicht: Artefakt-Seiten bekommen kein Web MIDI.

Tests auf dem Rechner: `tests/run.sh` (oder nur `cc … tests/test_lx.c && build/test_lx build/lx`: Klang, Sequencer,
Note Repeat, Aufnahme, Erase, Patterns, Speicher, UBOOT-Countdown; Screenshots der Seiten und `lxr_demo.wav`).

Speicher: derselbe Bereich wie ONE CLICK (0x97000–0xE0000). Wer zwischen ONE CLICK und LUXR wechselt, verliert die
dort gespeicherten Daten der anderen Firmware (sie erkennt fremde Daten und ignoriert sie).

## Lizenz – bitte lesen

Der Code dieser Firmware ist GPL-3.0 wie der Rest des Repositorys. **Die Transienten-Samples sind es nicht:** sie stammen
aus der Firmware der Sonic Potions LXR (© 2013 Julian Schmidt), deren Lizenz die kommerzielle Nutzung verbietet und mit
der GPL nicht verträglich ist. Deshalb liegen sie nicht im Repository, sondern werden beim Bauen geholt. Ein damit
gebautes `luxr.fwsc` ist nur für den **persönlichen, nicht-kommerziellen Gebrauch** – nicht verkaufen und nicht als
reine GPL-Firmware weitergeben. Die Update-Seite auf GitHub Pages bietet sie kostenlos und zusammen mit dem vollständigen
Quelltext an (das verlangt die LXR-Lizenz); der Hinweis gilt auch für jede dort geladene Kopie. Ohne Samples (Sample-Layer auf OFF/SNAP) wäre die Firmware reines GPL; das ist hier nicht
vorgesehen, weil der Sample-Layer ausdrücklich die Original-Samples nutzen soll.
