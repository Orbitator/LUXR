# Herkunft und Lizenz

ONE CLICK FM-1 steht als Ganzes unter **GPL-3.0-only** (siehe `LICENSE`).

## Von Felucca übernommen (GPL-3.0-only, © 2026 Leo Kuroshita / Hügelton Instruments)

Die Plattformschicht stammt aus [Felucca](https://github.com/hugelton/Felucca) und trägt dessen Copyright-Kopf.
Wo eine Datei geändert wurde, steht im Kopf ein Vermerk in eckigen Klammern (`[ONE CLICK: ...]`).

| Datei | Stand |
|---|---|
| `firmware/hal/*`, `firmware/crt0.S`, `firmware/app.ld` | unverändert |
| `firmware/loader/*` | Identität `ota-FM-1_810` statt `ota-FM-1_900` |
| `firmware/src/usb.c` | USB-Namen „ONE CLICK“ |
| `firmware/src/ota.c`, `lcd.c`, `libc.c`, `storage_hw.c` | unverändert |
| `firmware/src/ota_hw.c` | Fortschrittsanzeige mit eigener Grafik |
| `firmware/src/panel.c` | auf die Tastenzuordnung gekürzt |
| `firmware/src/audio.c`, `master.c`, `oc_main.c` | gekürzt / angepasst (Boot, Hauptschleife, Rettungswege erhalten) |
| `tools/build.py`, `fm1pkg_make.py`, `fm1_install.py`, `get_toolchain.sh`, `lz4blk.py`, `size_fns.py` | `build.py` angepasst, Rest unverändert |

## Eigene Teile

`firmware/src/oc_*.c`, `oc_core.h`, `oneclick.c`, `tools/gen_tables.py`, `host/`, `tests/`, `companion/`.

## Dritte

- JieLi AC79 SDK (Apache-2.0): drei Dateien (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`) landen beim Bauen im
  Paket, nicht im Quellbaum. `LICENSES/Apache-2.0.txt` gehört zu jedem Paket.
- Die 5x7-Schrift in `oc_font.c` ist der verbreitete gemeinfreie Glyphensatz.
