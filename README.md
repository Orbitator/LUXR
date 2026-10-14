# ONE CLICK FM-1

Eigene Firmware für den M-VAVE FM-1: der Generator von ONE CLICK SYNTH auf dem Gerät. Ein Knopfdruck erzeugt einen
ganzen Song – Tonart, Akkordfolge, vier Sounds und vier Spuren (Bass, Akkorde, Lead, Drums), die zueinander passen.

Status: **v0.1, auf dem Rechner getestet, noch nie auf einem FM-1 gelaufen.**

## Bedienung

| Bedienelement | Funktion |
|---|---|
| **REC** | ONE CLICK: neuer Song |
| **ARP** | gewählte Spur neu (Sound und Pattern) |
| **EDIT** | nur Sound der Spur neu |
| **SEQ** | nur Pattern der Spur neu |
| **SCL** | neue Tonart und Akkorde (Sounds bleiben) |
| **HOME** | Undo / Redo der letzten Generierung |
| **GLO** | Spur stumm / laut |
| **PLAY** | Start / Stop |
| **FX** | Swing 0 / 12 / 25 % |
| **OCT− / OCT+** | Oktave der Tasten |
| **SELECT** | Tempo |
| **ALGORITHM / PRESETS** | Spur wählen |
| **KNOB 1–4** | Cutoff, Resonanz, Decay, Lautstärke der Spur (live) |
| Tasten | spielen die gewählte Spur (Drums: weiße Tasten F3–C4 = Instrumente) |
| USB-MIDI Kanal 1–4 | spielt Spur 1–4 |

Rettungswege (aus Felucca übernommen, nicht entfernen): **OCT− und OCT+ 5 s halten** → UBOOT;
zwei Abstürze in den ersten 30 s nach dem Einschalten → UBOOT; USB-SysEx `F0 22 24 35 7D F7` → UBOOT;
das M-UPGRADE-Update über USB.

## Klang

Sechs eigene Engines in Festkomma (der FM-1 hat keine FPU): Subtraktiv (2 Osc + Sub + Noise, SVF),
Acid (TB-303-artig, Sättigung, Accent/Slide), FM (3 Operatoren, 3 Algorithmen), Supersaw (bis 7 Sägezähne),
Additiv (bis 8 Teiltöne, Orgel/Risset-Glocke/Marimba) und Drums (8 synthetisierte Instrumente).
Dazu Delay (punktierte Achtel) und Reverb. 8 Stimmen gesamt.

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
WebAssembly mit nachgebautem Bedienfeld, eine Datei, die direkt im Browser läuft. Der Ton ist bit-gleich zu einem
nativen Clang-Build derselben Quellen (`emu/emu_test.mjs`). Nicht simuliert werden der JieLi-Prozessor und seine
Rechenzeit, Booten, Update, UBOOT und Flash; ob die Engines auf dem Gerät schnell genug sind, zeigt erst die Hardware.

Hinweis: Der Generator ruft `rr()` mehrfach in einer Argumentliste auf. Deren Auswertungsreihenfolge ist in C nicht
festgelegt, deshalb erzeugt ein GCC-Build aus demselben Startwert andere Songs als ein Clang-Build (Gerät, Emulator).

## Companion-App

`companion/index.html` (Chrome/Edge, Web MIDI): ONE CLICK und alles Weitere vom Browser aus, Pattern-Ansicht und
-Bearbeitung. Protokoll: `firmware/src/oc_proto.c`.
