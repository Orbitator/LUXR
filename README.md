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

**Sample-Layer** (ENV, oder EDIT zweimal): SAMPLE (OFF, SNAP, CLK, CK2, TIK, KIK, RIM, DRP, HAT, CLP, KK2, SNR, TOM,
SP2 – die zwölf Transienten der LXR), LEVEL, PITCH (25 … 175 % wie an der LXR), DECAY (FULL = ganzes Sample);
mit SHIFT: SYNTH (Pegel des Synth-Layers – auf 0 spielt nur das Sample). Wie an der LXR wird das Sample vor der
Amp-Hüllkurve eingemischt; SNAP ist der Pitch-Snap der LXR (LEVEL = Stärke, PITCH = Tempo).

## Bedienung

**GLO lange halten (ab 250 ms) = SHIFT.** Wird GLO zusammen mit einem anderen Bedienelement gedrückt, gilt SHIFT
sofort. GLO kurz tippen öffnet den MIXER.

| Bedienelement | normal | mit SHIFT |
|---|---|---|
| **SELECT** | Tempo | Swing |
| **ALGORITHM** | Sound 1–8 wählen | Pattern wählen |
| **PRESETS** | Presets des Sounds (0 = Werkssound, 1–99 gewürfelt, spielt ihn an) | Sample des Sample-Layers |
| **KNOB 1–4** | die vier Karten der Seite (Reihe A) | Reihe B |
| **PLAY** | Start / Stop | von vorn |
| **REC** | Aufnahme scharf / aus | ERASE scharf (gehaltene Pads löschen ihre Steps) |
| **OCT− / OCT+** | SEQ: Takt zurück / vor · MIXER: Sounds 1–4 / 5–8 · STORE: zurück / ausführen · sonst: Sound zurück / vor | SEQ: Takt entfernen / Takt anhängen (Kopie des letzten) |
| **OCT− + OCT+ 5 s** | UBOOT (Rettungsweg, unverändert aus Felucca) | |

**Seiten** (Taste tippen):

| Taste | Seite | Knöpfe 1–4 | mit SHIFT |
|---|---|---|---|
| HOME | KIT | Tempo, Swing, Tune (alle), Decay (alle) | Metronom, MIDI-Kanal, Theme, Version |
| SEQ | SEQUENCER | Länge der Spur, Velocity des letzten Steps, Rotate, Euclid | Länge aller Spuren, Accent des letzten Steps |
| EDIT | SYNTH LAYER (nochmal: SAMPLE LAYER) | Parameter 1–4 | Parameter 5–8 |
| ENV | SAMPLE LAYER | Sample, Level, Pitch, Decay | Synth-Pegel |
| LFO | LFO des Sounds | Rate, Tiefe, Ziel (Pitch, Filter, Level, Pan), Welle | |
| FX | FX | Level, Pan, Delay-Send, Reverb-Send | Delayzeit, Feedback, Hallgröße, Dämpfung |
| SCL | PATTERNS | Pattern (spielend: wechselt am Pattern-Ende), Chain (spielt N Patterns nacheinander) | |
| ARP | NOTE REPEAT | Rate, Velocity, Vel-Modus (fest, steigend, fallend, zufällig), Latch | Humanize |
| GLO (tippen) | MIXER | Lautstärke der vier Sounds der Bank | Pan der vier Sounds |
| SAVE | STORE | Platz 1–8, Aktion (Save, Load, Load Kit) – OCT+ führt aus | |

**Weiße Tasten (16): Steps setzen.** Sie sind die 16 Steps des angezeigten Takts der gewählten Spur (LED = Step an,
die laufende Position blinkt invertiert). Ist der Step neu und das Gerät steht, klingt er an.
Mit SHIFT: F3–C4 schalten Sound 1–8 stumm, D4–G5 solo.

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
| F#5 | Tap-Tempo | Metronom an / aus |

**Note Repeat (ARP).** ARP tippen schaltet Note Repeat ein (nochmal tippen: aus), ARP halten nur solange gehalten.
Dann sind die weißen Tasten F3–C4 die **Pads der acht Sounds**, D4–G5 wählen die **Rate** direkt
(Single, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T). Ein gehaltenes Pad wiederholt im Raster der Rate (beim Spielen
auf dem Takt-Raster). LATCH: ein Pad läuft weiter, bis es nochmal gedrückt wird. Mit REC + PLAY wird jeder Schlag in
den nächsten Step geschrieben; mit ERASE (SHIFT + REC) löscht ein gehaltenes Pad die Schläge seines Sounds.

**Patterns.** 16 Patterns, jede Spur bis 64 Steps (4 Takte), jede Spur mit eigener Länge (Polymetrik); ein Pattern
endet nach seiner längsten Spur. STORE speichert alles (16 Patterns, Kit, Tempo, FX) auf einen von 8 Plätzen.

**MIDI IN** (USB): General-MIDI-Drum-Noten spielen die Sounds (36 Kick, 38/40 Snare, 39 Clap, 42/44 CH, 46 OH,
41–50 Tom, 37 Rim, 49/51/57 Cymbal), C4–G4 die Sounds 1–8. Kanal im KIT (SHIFT), Standard: alle.

## Bauen

```
JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/fw-AC79_AIoT_SDK python3 tools/build.py --app lxr   # -> build/luxr.fwsc
python3 tools/fm1_install.py build/luxr.fwsc
```

`tools/gen_lxr_transients.py` holt dabei die Transienten aus dem LXR-Quelltext (`$LXR_SRC`, sonst ein Klon von
github.com/SonicPotions/LXR nach `build/ext/LXR`) und schreibt `build/gen/lxr_transients.h`. Kennung FM-1_870 (Release
X.Y: FM-1_87Y) – die Update-Seite / Firmware-Werkstatt nimmt sie wie ONE CLICK an (FM-1_8xx). Auf GitHub baut der
Workflow (`.github/workflows/build.yml`) sie bei jedem Push auf `main` und legt sie als Actions-Artefakt
`luxr-fwsc` ab (Actions › build › der Lauf › Artifacts).

Tests auf dem Rechner: `tests/run.sh` (oder nur `cc … tests/test_lx.c && build/test_lx build/lx`: Klang, Sequencer,
Note Repeat, Aufnahme, Erase, Patterns, Speicher, UBOOT-Countdown; Screenshots der Seiten und `lxr_demo.wav`).

Speicher: derselbe Bereich wie ONE CLICK (0x97000–0xE0000). Wer zwischen ONE CLICK und LUXR wechselt, verliert die
dort gespeicherten Daten der anderen Firmware (sie erkennt fremde Daten und ignoriert sie).

## Lizenz – bitte lesen

Der Code dieser Firmware ist GPL-3.0 wie der Rest des Repositorys. **Die Transienten-Samples sind es nicht:** sie stammen
aus der Firmware der Sonic Potions LXR (© 2013 Julian Schmidt), deren Lizenz die kommerzielle Nutzung verbietet und mit
der GPL nicht verträglich ist. Deshalb liegen sie nicht im Repository, sondern werden beim Bauen geholt. Ein damit
gebautes `luxr.fwsc` ist nur für den **persönlichen, nicht-kommerziellen Gebrauch** – nicht verkaufen und nicht als
GPL-Firmware weitergeben. Ohne Samples (Sample-Layer auf OFF/SNAP) wäre die Firmware reines GPL; das ist hier nicht
vorgesehen, weil der Sample-Layer ausdrücklich die Original-Samples nutzen soll.
