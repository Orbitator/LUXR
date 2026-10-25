# LUXR als iPad-App – ohne App Store

Die iPad-App ist der LUXR-Emulator (`emu/`), verpackt mit [Capacitor](https://capacitorjs.com) wie Hapax Web, plus ein
nativer **CoreMIDI**-Baustein (`ios/App/App/LuxrMidiPlugin.swift`). Damit geht auf dem iPad, was Safari fehlt:

* MIDI über **USB**, **Bluetooth** und **Netzwerk** (rtpMIDI): Noten, MIDI-Clock (Start/Stop) ein und aus
* **Backup exportieren** über das iOS-Teilen-Menü (in „Dateien“ sichern, AirDrop …)
* Ton auch bei eingeschaltetem Stummschalter, Vollbild

Die Patches liegen in der App und bleiben erhalten, auch wenn die Signatur abläuft.

## Die App-Datei holen

Jeder Push auf `main`, der den Emulator oder `ios-app/` ändert, baut die App (Workflow `.github/workflows/ipad.yml`,
dauert etwa 5 Minuten) und legt sie hier ab:

**https://github.com/Orbitator/LUXR/releases/download/ipad/LUXR.ipa**

Die Datei ist **unsigniert**. Apple erlaubt die Installation außerhalb des App Stores nur signiert:

| | Weg | Kosten | Mac nötig | Gültigkeit |
|---|---|---|---|---|
| **A** | SideStore / AltStore mit kostenloser Apple-ID | 0 € | nein | 7 Tage, Verlängerung per Tipp |
| **B** | Xcode mit kostenloser Apple-ID | 0 € | ja | 7 Tage |
| **C** | Apple-Developer-Account (TestFlight / Ad-hoc) | 99 $/Jahr | nein | 1 Jahr |

## Weg A: SideStore (kein Mac nötig)

Einmalig SideStore nach **https://docs.sidestore.io** einrichten (Entwicklermodus am iPad einschalten, dem Profil
unter *Einstellungen → Allgemein → VPN & Geräteverwaltung* vertrauen). Dann:

1. `LUXR.ipa` aufs iPad laden (Safari → Link oben, landet in „Dateien → Downloads“).
2. SideStore → **My Apps** → **+** → `LUXR.ipa` wählen.
3. Alle 7 Tage: SideStore → **Refresh All**. Eine neue Version installierst du genauso, die Patches bleiben.

## Weg B: Xcode (Mac)

```bash
sh emu/build_lx.sh          # die Emulator-Seite (clang mit wasm32, Python mit Pillow)
cd ios-app && npm install && npm run sync && npm run open
```

In Xcode: **App** → *Signing & Capabilities* → *Team* = deine Apple-ID; ggf. die Bundle-ID `de.orbitator.luxr`
ändern; iPad anschließen, **▶ Run**.

## In der App

* Einschalten: aufs Display tippen (wie im Browser).
* MIDI: unter „Computer keyboard & MIDI“ Eingang („all inputs“) und Ausgang wählen; **Bluetooth MIDI …** öffnet den
  iOS-Dialog für Bluetooth-Geräte.
* Backup: „Import backup“ wählt eine `.luxr`-Datei aus „Dateien“; „Export backup“ öffnet das Teilen-Menü.
