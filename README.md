# LUXR

Eine Firmware: eine Drum-Machine für den M-VAVE FM-1 mit **acht Sounds** im Stil der
Sonic Potions LXR (die Klangarchitektur der LXR-Web-Drums bzw. der LXR-Engine von ONE CLICK, hier eine Stimme pro
Spur) und einem **Sample-Layer mit den originalen LXR-Transienten-Samples**. Sie nutzt dieselbe Plattform wie
ONE CLICK (Felucca: Update über USB, UBOOT-Rettung, Display, Speicher), nur die App ist neu (`firmware/src/lx_*.c`).
Dieses Repository ist aus [ONE CLICK FM-1](https://github.com/Orbitator/oneclick-fm1) hervorgegangen; dessen Anleitung
steht in [README-ONECLICK.md](README-ONECLICK.md) (dort auch die Plattform, der Emulator und die Companion-App).

Status: **v0.1, auf dem Rechner getestet (tests/test_lx.c), noch nie auf einem FM-1 gelaufen.** Neu (Speicherformat
LXP7): Parameter-Locks mit Motion-Rec, NUDGE (Microtiming), FILL, Tempo pro Spur, Songmodus, Master-Filter, MIDI-Clock
In/Out, Notenausgabe pro Sound und CC-Learn – siehe [Performance und MIDI](#performance-und-midi).

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

**Sample-Layer** (EDIT dreimal): **44 Transienten pro Sound** zur Wahl:
- **16 eigene** des Sounds (BD 01 … BD 16, SD 01 … usw.), beim Bauen synthetisiert (`tools/gen_own_transients.py`);
- **16 im Stil der Klassiker** (808 A–B, 909 A–B, 606, 707 A–B, DMX A–B, Linn LM-1 A–B, LinnDrum LM-2 A–B, SDS6, SDS7,
  SDS8), beim Bauen nach deren Bauweise synthetisiert, keine Aufnahmen der Geräte (`tools/gen_machine_transients.py`,
  22,05 kHz 8 Bit wie bei 707/DMX/Linn; das LM-1 hatte keine Becken: dort ein Cabasa);
- die **12 Original-LXR-Transienten** (CLK, CK2, TIK, KIK, RIM, DRP, HAT, CLP, KK2, SNR, TOM, SP2); dazu SNAP und OFF.

Seite SAMPLE: Sample, Level, Pitch (25 … 175 %), Decay (FULL = ganzes Sample); Seite SAMPLE 2 (EDIT nochmal): Start
(Einsprungpunkt, davor grau), Tone (LP = Tiefpass, HP = Hochpass), Bend (Pitch-Hüllkurve des Samples: + von oben herab,
− von unten herauf), Synth (Pegel des Synth-Layers – auf 0 spielt nur das Sample). Wie an der LXR wird das Sample vor
der Amp-Hüllkurve eingemischt. Jeder Werks- und jeder gewürfelte Sound spielt immer ein Transienten-Sample.
Das Drehen an Knöpfen spielt den Sound nicht an (anspielen: die weiße Taste des Sounds auf einer Seite außer SEQ).

Werkssounds und gewürfelte Sounds haben Decay 50 und keine Sends; die Hi-Hats stehen im Panorama auf L23 (CH) und R23 (OH),
alle anderen in der Mitte.

**Pro Sound außerdem** (wie in der Web-App): Volume, Pan, Sends für Delay, Reverb und Drive (EDIT dreimal: SOUND MIX) und
**zwei LFOs** (LFO, nochmal: LFO 2) mit Ziel (Pitch, Filter, Decay, Level, Pan, Sample), Welle (Sinus, Dreieck, Säge,
Rechteck, S&H), Rate im Tempo (1/16 … 8 Takte, ab dem Takt, in dem PLAY startete) und bipolarer Tiefe (−64 … +63).

## Bedienung

**GLO lange halten (ab 250 ms) = SHIFT.** Wird GLO zusammen mit einem anderen Bedienelement gedrückt, gilt SHIFT
sofort. GLO kurz tippen öffnet den MIXER, nochmal tippen schaltet auf dessen Seite 2 (PAN), ein drittes Mal auf den
COMPRESSOR, dann wieder zum MIXER.
Eine Seiten-Taste nochmal tippen zeigt ihre nächste Seite (Punkte unten rechts).

| Bedienelement | normal | mit SHIFT |
|---|---|---|
| **SELECT** | Sound 1–8 wählen | Pattern wählen |
| **ALGORITHM** | würfelt einen neuen Sound für den gewählten (immer mit Transienten-Sample) und spielt ihn an | **das nächste / vorige Sample des Sample-Layers** (alle 44 der Reihe nach, der Synth-Layer bleibt; das Display zeigt den Namen); **auf der SEQ-Seite:** rechts ein Zufalls-Pattern für die Spur, links die Spur verdoppeln |
| **PRESETS** | die gespeicherten Patches durchblättern, jeder wird sofort geladen – so, wie KNOB 3 (LOADS) auf der STORE-Seite steht (Standard: Kit + Patterns) | |
| **KNOB 1–4** | die vier Karten der Seite (Reihe A) | Reihe B |
| **PLAY** | Start / Stop | von vorn |
| **REC** | tippen: Aufnahme scharf / aus · **halten: QUANTIZE** | ERASE scharf (gehaltene Pads löschen ihre Steps) |
| **OCT− / OCT+** | SEQ: Takt zurück / vor · MIXER: Sounds 1–4 / 5–8 · STORE: zurück / speichern · sonst: Sound zurück / vor | SEQ: Takt entfernen / Takt anhängen (Kopie des letzten) |
| **OCT− + OCT+ 5 s** | UBOOT (Rettungsweg, unverändert aus Felucca) | |

**Seiten** (Taste tippen, nochmal tippen = nächste Seite):

| Taste | Seite | Knöpfe 1–4 | mit SHIFT |
|---|---|---|---|
| HOME | KIT | Tempo, Swing, Tune (alle), Decay (alle) | MIDI-Kanal, Theme, Version |
| HOME (2.) | **MIDI** | Clock (INT / EXT), Clock Out (aus / an), Ausgabekanal des Sounds (OFF, 1–16), seine Note | MIDI-IN-Kanal |
| HOME (3.) | **CC LEARN** | LEARN (OFF, ON, CLEAR), Eintrag (blättert durch die Liste aller Zuordnungen, die das Bild zeigt), DELETE (ein Dreh löscht den gewählten Eintrag) | |
| HOME (halten) | **SYSTEM** (ohne Kopfzeile, oben das LUXR-Logo) | Version, belegte Plätze und freier Speicher, Audio-Last, Akku in % und Volt (kalibriert: voll ≈ 4,15 V beim Spielen), mit USB „CHARGING“, Restlaufzeit (gemessen am Abfall seit dem letzten Laden, ab 5 min und 1 %), unter 10 % „BATTERY LOW“, About | |
| SEQ | SEQUENCER | Länge der Spur, Velocity des letzten Steps, Rotate, Euclid | **PAT LEN: jeder Dreh verdoppelt die Patternlänge (zurück: halbiert)**, **COPY x2: Noten in die neuen Takte kopieren (an/aus)**, Accent des letzten Steps, **SPEED der Spur** (1/8X, 1/4X, 1/2X, 3/4X, 1X, 3/2X, 2X) |
| SEQ (2.) | **LIVE PADS** | Pad-Velocity, Quantize-Raster, -Richtung, Metronom | |
| SEQ (3.) | **TRACKS** | Spur, Mute, Solo, Spur löschen | alle an, Pattern löschen |
| EDIT | SYNTH 1-4 | Synth-Parameter 1–4 | Parameter 5–8 |
| EDIT (2.) | SYNTH 5-8 | Synth-Parameter 5–8 (z. B. Tone, Snap, Click, Drive) | Parameter 1–4 |
| EDIT (3.) | SAMPLE LAYER | Sample, Level, Pitch, Decay | Synth-Pegel |
| ENV | **ENVELOPE** | Attack (OFF … 252 ms), Decay (der Decay des Synth-Layers), Release, Gate (OFF oder ¼ … 16 Steps: danach übernimmt das Release den Decay) – alles lockbar | |
| EDIT (4.) | SAMPLE 2 | Start, Tone, Bend, Synth-Pegel | |
| EDIT (5.) | SOUND MIX | Volume, Pan, Delay-Send, Reverb-Send | Drive-Send |
| LFO / LFO (2.) | LFO 1 / LFO 2 | Ziel, Welle, Rate (im Tempo), Tiefe (bipolar) | |
| FX | FX SENDS (des Sounds, im Kit) | Drive-Send, Delay-Send, Reverb-Send, Volume | |
| FX (2.) | DRIVE (global, im Kit) | Modus (SOFT, HARD, FOLD), Stärke, Ton, Pegel | |
| FX (3.) | DELAY (global, im Kit) | **Modus** (MONO, STEREO: links und rechts je eine Zeit, PINGPONG: die Wiederholungen wechseln die Seite), Zeit (1/8 … 3/4; in STEREO die linke), Zeit rechts (STEREO), Feedback | |
| FX (4.) | DELAY 2 | Ton, Pegel | |
| FX (5.) | REVERB (global, im Kit) | Größe (Nachhallzeit; 0 = kein Nachhall), Dämpfung, Pegel, **Pre-Delay** (OFF … 180 ms) – ein Plate-Hall nach Dattorro | |
| FX (6.) | **MASTER** | Filter (links der Mitte Tiefpass, rechts Hochpass, Mitte aus), Resonanz – per CC anlernbar, nicht gespeichert | |
| SCL | PATTERNS | Pattern (spielend: wechselt am Pattern-Ende), Chain (spielt N Patterns nacheinander), **Copy** (ein Dreh kopiert das angezeigte Pattern), **Paste** (ein Dreh fügt es ins angezeigte Pattern ein). **Weiße Tasten:** Taste k = Pattern k; eine Taste halten und eine zweite drücken = Chain vom kleineren bis zum größeren (2 + 5: 2-3-4-5) | |
| SCL (2.) | **SONG** | Zeile (1–32), ihr Pattern, ihre Wiederholungen (1–16), Song an / aus. **Weiße Tasten:** nacheinander gedrückt ergeben sie die Zeilen des Songs (die erste Taste nach dem Öffnen der Seite beginnt einen neuen Song, SONG geht an) | Anzahl Zeilen, am Ende: LOOP / STOP |
| ARP | NOTE REPEAT | Rate, Velocity, Vel-Modus (fest, steigend, fallend, zufällig), Latch | Humanize |
| GLO (tippen) | MIXER, Seite 1 | Lautstärke der vier Sounds der Bank; Meter = Pegel am Ausgang (mit Pan und Master-Lautstärke), oben = 0 dB; darüber blinkt 2 s ein roter Punkt | Pan der vier Sounds |
| GLO (nochmal tippen) | MIXER, Seite 2: PAN | Pan der vier Sounds der Bank (L64 … C … R63) | Lautstärke |
| GLO (dritte Seite) | **COMPRESSOR** | THRESH (0 … −40 dB), RATIO (1:1 … 10:1, LIMIT), ATTACK (0,5–50 ms), RELEASE (20–1000 ms) | MAKEUP (0 … +24 dB), LIMITER an/aus |
| SAVE | STORE | **Weiße Tasten wählen den Platz (die 16 der Gruppe, in der KNOB 1 steht: 1–16, 17–32, 33–36; heller hinterlegt), OCT+ speichert sofort (auch auf NEW PATCH).** Platz 1–36, **was gespeichert wird** (KIT+PAT, nur KIT, nur PATTERN – OCT+ speichert), **was geladen wird** (KIT+PAT, nur KIT, nur PATTERN), **LOAD** (ein Dreh lädt den Platz; ein leerer Platz gibt ein neues, leeres Patch) | |
| SAVE (2.) | **NEW PATCH** | Länge (1–64 Steps), Speed (1/8X … 2X), Tempo, **CREATE** (ein Dreh legt das neue Patch an: 16 leere Patterns, Wiedergabe gestoppt, noch nicht gespeichert) | Kit: FACTORY (Werkssounds und FX) oder KEEP (das aktuelle bleibt) |
| REC (halten) | **QUANTIZE** | Raster (1/16, 1/8, 1/4), Richtung (zum nächsten Rasterpunkt / zum vorherigen), Metronom (aus, bei Aufnahme, an) | |

**Weiße Tasten (16).** Auf der **SEQUENCER-Seite** sind sie die 16 Steps des angezeigten Takts der gewählten Spur
(LED = Step an, die laufende Position blinkt invertiert). Kurz tippen: ein leerer Step wird gesetzt (still, er klingt erst
beim Abspielen), ein gesetzter beim Loslassen gelöscht. **Lang halten (ab 350 ms) oder halten und einen Knopf drehen:
STEP EDIT** – KNOB 1 Velocity; KNOB 2 Probability (6 … 100 %: wie oft der Step spielt) und weiter rechts die
**Bedingungen 1:2, 2:2, 1:4 … 4:4, 1:8 … 8:8** (A:B = der Step spielt im A-ten von je B Durchläufen der Spur, ab PLAY
gezählt: 1:2 im 1., 3., 5. …, 2:2 im 2., 4. …), dann **FILL** (spielt nur, solange F#5 gehalten wird) und **!FILL**
(nur ohne FILL); KNOB 3 Pan nur für diesen Step (SOUND = der Pan des Sounds, sonst
L48 L32 L16 C R16 R32 R48); KNOB 4 **Substeps** (OFF, 2 … 6: so viele Schläge gleichmäßig über den Step verteilt).
Mehrere gehaltene Steps werden zusammen bearbeitet. Im Raster sind Steps mit Probability oder Bedingung blasser,
Unter dem Raster zeigt je ein Balken einen Takt der Spur: rot der angezeigte, hell der laufende. Substeps teilen die Zelle, ein eigenes Pan ist ein Punkt, ein Step mit Locks hat eine Kerbe oben links, ein
verschobener (NUDGE) sitzt etwas früher oder später in seiner Spalte. (Accents setzt der Accent-Modus, SHIFT + C#5.)
**Auf allen anderen Seiten** spielen F3–C4 die Sounds 1–8 (D4–G5 dieselben mit Accent), mit REC + PLAY aufgenommen; die
LEDs leuchten beim Anschlag. Auf den Seiten eines Sounds (SYNTH, SAMPLE, SOUND MIX, LFO, FX SENDS) wählt der Anschlag den
Sound auch aus: das Display zeigt den gespielten. Auf NOTE REPEAT sind es die Repeat-Pads. Mit SHIFT überall: F3–C4 Mute,
D4–G5 Solo.

**LIVE PADS (SEQ zweimal).** Alle acht Sounds gleichzeitig spielbar: F3–C4 sind Sound 1–8, D4–G5 dieselben mit Accent.
Mit REC + PLAY wird jeder Schlag aufgenommen, auf das Raster aus QUANTIZE (REC halten) gelegt.

**TRACKS (SEQ dreimal).** Die acht Spuren mit Notenzahl, Mute (M) und Solo (S). KNOB 1 wählt die Spur, KNOB 2 Mute,
KNOB 3 Solo, KNOB 4 gedreht löscht die Spur. Zeile B (GLO halten): KNOB 2 alle an (Mute und Solo aus), KNOB 4 löscht das
ganze Pattern. Alles Gelöschte holt A#4 (UNDO) zurück.

**Schwarze Tasten (11): Funktionen.**

| Taste | normal | mit SHIFT |
|---|---|---|
| F#3 | **SNAPSHOT 1** halten: der gespeicherte Sound-Zustand (Kit, alle Sounds, FX, Master-Filter, Mute und Solo) klingt, solange gehalten; loslassen: der Sound des Patches, wie er geladen bzw. zuletzt gespeichert wurde | Snapshot 1 speichern (der aktuelle Zustand) |
| G#3 | **SNAPSHOT 2** (wie F#3) | Snapshot 2 speichern |
| A#3 | Mute des Sounds | alle wieder an |
| C#4 | Solo des Sounds | Solo aus |
| D#4 | **RANDOM MUTE**: schaltet zufällig 1–6 der anderen Sounds stumm (der gewählte bleibt an; jedes Tippen ein neuer Satz); **lang halten: alle wieder an** | neuer (gewürfelter) Sound, ohne Anspielen |
| F#4 | Takt kopieren | ganzes Pattern kopieren |
| G#4 | Takt einfügen | ganzes Pattern einfügen |
| A#4 | **UNDO** | **REDO** |
| C#5 | **P-LOCK** an / aus (mit gehaltenen Steps: deren Locks löschen) | Accent-Modus (weiße Tasten setzen Accents) |
| D#5 | **NUDGE** an / aus (mit gehaltenen Steps: zurück aufs Raster) | Follow (die Anzeige folgt dem laufenden Takt) |
| F#5 | **FILL**, solange gehalten | Tap-Tempo |

Spur verdoppeln und Zufalls-Pattern liegen jetzt auf SHIFT + ALGORITHM auf der SEQ-Seite, das Metronom auf den Seiten
LIVE PADS und QUANTIZE (KNOB 4 bzw. 3).

**Note Repeat (ARP).** ARP tippen schaltet Note Repeat ein (nochmal tippen: aus), ARP halten nur solange gehalten.
Dann sind die weißen Tasten F3–C4 die **Pads der acht Sounds**, D4–G5 wählen die **Rate** direkt
(Single, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T). Ein gehaltenes Pad wiederholt im Raster der Rate (beim Spielen
auf dem Takt-Raster). LATCH: ein Pad läuft weiter, bis es nochmal gedrückt wird. Mit REC + PLAY wird jeder Schlag
quantisiert geschrieben; mit ERASE (SHIFT + REC) löscht ein gehaltenes Pad die Schläge seines Sounds.

**Patches.** Ein Patch ist ein Kit (die acht Sounds samt LFOs und Sends, MIDI-Ausgabekanal und -Note, Tune, Decay,
Drive/Delay/Reverb) und seine 16 Patterns (jede Spur bis 64 Steps, eigene Länge und SPEED, Velocity/Probability/Pan/
Substeps/NUDGE je Step, bis 128 Parameter-Locks je Pattern; Tempo, Swing, Chain, Mute/Solo, der Song).
**36 Plätze** (je 8 KiB), also bis zu 36 Kits und 576 Patterns. **Beim Einschalten wird Patch 1 geladen** (Kit und Patterns), falls belegt. SAVE speichert beides oder nur Kit bzw. nur Patterns (der andere
Teil bleibt, wie er auf dem Platz war); PRESETS lädt sie, ebenso KNOB 4 auf der STORE-Seite – beide nach der Auswahl von KNOB 3 (LOADS).

**Updates behalten die Patches.** Ein Firmware-Update schreibt nur das Programm, nicht den Speicher. Ältere
Speicherformate von LUXR (seit v0.1) liest die Firmware weiter: beim ersten Start nach dem Update zieht sie die bisherigen
8 Plätze ins neue Format um („PATCHES KEPT“). Zusätzlich gibt es auf der Update-Seite **„3 · Backup“**: alle belegten
Plätze als `.luxr`-Datei herunterladen und wieder zurückspielen (auch ältere Backups, sie werden umgewandelt); beim
Installieren zieht die Seite auf Wunsch vorher ein Backup und spielt es danach zurück. Das geht ab dieser LUXR-Version –
die Firmware davor kennt die Backup-Befehle noch nicht.

**Speicherformat LXPA** (Hüllkurve, Pre-Delay; LXP1–9 werden gelesen). Die Patterns werden gepackt gespeichert (je Spur ein Bit pro
gesetztem Step, dann nur deren Werte; Steps mit Extras zusätzlich deren Werte), damit Locks, NUDGE, SPEED und der Song in die
8 KiB eines Platzes passen – ein normal gefülltes Patch braucht 1–4 KiB. Beim ersten Start nach dem Update zieht die
Firmware die bisherigen 14 Plätze (je 20 KiB) in das neue Raster um („PATCHES MOVED: n“), Platznummern bleiben gleich. Patches älterer Formate (LXP1–6) werden beim
Laden umgewandelt und beim nächsten Speichern als LXP7 geschrieben; Backups bleiben in beide Richtungen nutzbar. Passt
ein Patch nicht mehr (nur bei sehr vielen Locks in sehr vollen Patterns), meldet SAVE „PATCH TOO FULL“ und schreibt
nichts. **Achtung:** Eine ältere LUXR-Firmware kann LXP7-Patches nicht lesen – vor einem Downgrade ein Backup ziehen.

**MIDI IN** (USB): General-MIDI-Drum-Noten spielen die Sounds (36 Kick, 38/40 Snare, 39 Clap, 42/44 CH, 46 OH,
41–50 Tom, 37 Rim, 49/51/57 Cymbal), C4–G4 die Sounds 1–8. Kanal im KIT (SHIFT) oder auf der MIDI-Seite, Standard: alle.

## Performance und MIDI

**P-LOCK (C#5).** Solange P-LOCK an ist, sind die weißen Tasten auf *jeder* Seite die 16 Steps des angezeigten Takts
der gewählten Spur (OCT−/OCT+ blättert die Takte). Einen gesetzten Step halten und an einem Knopf der Seite drehen setzt
einen **Parameter-Lock**: nur dieser Step spielt mit dem Wert, der Sound selbst bleibt. Mehrere gehaltene Steps bekommen
den Lock gemeinsam. Lockbar sind die Parameter eines Sounds: Synth 1–8, Sample-Layer (Sample, Level, Pitch, Decay,
Start, Tone, Bend, Synth-Pegel), Volume, Pan und die drei Sends. Während ein Step gehalten wird, zeigt die Karte seinen
Wert (hervorgehoben, wenn gelockt). Step(s) halten und C#5 drücken löscht ihre Locks. Bis 128 Locks je Pattern
(„LOCKS FULL“). **Motion-Rec:** Bei P-LOCK + REC + PLAY schreibt jeder Dreh an einem lockbaren Knopf einen Lock auf den
gerade laufenden Step der Spur, wenn dort ein Schlag liegt; ohne P-LOCK verändert der Knopf wie bisher den Sound.

**NUDGE (D#5).** Wie P-LOCK sind die weißen Tasten die Steps. Step(s) halten: KNOB 1 verschiebt sie um −15 … +15
Zweiunddreißigstel des Steps (KNOB 2–4: Velocity, Probability/Bedingung, Substeps). Frühe Schläge werden aus dem Step
davor gespielt (nicht über das Pattern-Ende hinweg). Step(s) halten und D#5 drücken: zurück aufs Raster.

**FILL (F#5).** Solange F#5 gehalten wird, spielen Steps mit der Bedingung FILL, und Steps mit !FILL schweigen.

**SPEED.** Jede Spur läuft mit ihrem Tempo (SEQ, SHIFT, KNOB 4): 1/8X … 2X. Das Pattern dauert so lange wie seine
längste Spur (Länge × Tempo); ein 16-Step-Hat auf 2X läuft zweimal pro Takt.

**SONG (SCL zweimal).** Bis 32 Zeilen aus Pattern und Wiederholungen. Mit SONG an startet PLAY bei Zeile 1 und spielt die
Zeilen der Reihe nach; am Ende von vorn (LOOP) oder Stopp (STOP). Ein von Hand vorgemerktes Pattern hat Vorrang, danach
geht der Song weiter.

**MASTER-Filter (FX sechsmal).** Ein Knopf, links Tiefpass, rechts Hochpass, Mitte aus; dazu Resonanz. Gedacht für
Übergänge und per CC zu steuern; er wird nicht gespeichert und startet aus.

**MIDI-Clock (HOME zweimal).** CLOCK INT: LUXR ist Master; mit CLK OUT sendet es 24 Clocks pro Viertel (auch im Stand),
START beim Start und STOP beim Halt. CLOCK EXT: LUXR folgt START / CONTINUE / STOP und den Ticks eines anderen Geräts
(ein Sechzehntel je 6 Ticks); das Tempo im Display kommt aus der Clock, Swing ist dabei aus. PLAY wartet ohne Clock auf
den nächsten Tick („WAITING FOR CLOCK“). Mit CLK OUT wird die empfangene Clock weitergereicht. Die MIDI-Seite zeigt,
ob eine Clock ankommt.

**Notenausgabe.** Jeder Sound hat einen Ausgabekanal (OFF, 1–16) und eine Note (Standard: GM, 36 Kick, 38 Snare, 39
Clap, 42 CH, 46 OH, 45 Tom, 37 Rim, 49 Cymbal); gespeichert mit dem Kit. Gesendet werden die Schläge des Sequencers, der
Pads und des Note Repeat (nicht die von MIDI IN), mit Velocity (Accent +20) und einer Länge von einem halben Sechzehntel.
Stumme Spuren senden nicht.

**Master-Kompressor und Limiter (GLO dreimal).** Der Kompressor sitzt hinter dem Master-Filter und vor der Lautstärke,
beide Seiten gemeinsam (das Panorama bleibt). 0 dB entspricht dem vollen Ausgang bei Standard-Lautstärke. Die Grafik zeigt
die Kennlinie, den aktuellen Pegel darauf (roter Punkt) und die Absenkung von Kompressor und Limiter. Der LIMITER sitzt ganz
am Ende und hält die Spitzen unter dem Soft-Clip des Ausgangs (er kennt jeden Block, bevor er klingt: kein Überschwingen).
Beides wird mit dem Kit gespeichert; ab Werk ist der Kompressor 1:1 (aus) und der Limiter aus – der Klang bleibt wie bisher.

**Tonal spielen (MIDI IN).** Hat ein Sound als einziger einen bestimmten MIDI-Kanal (HOME 2., MIDI CH), spielen Noten auf
diesem Kanal genau diesen Sound tonal: C3 (Note 60) ist seine eigene Tonhöhe, jede Taste ein Halbton, drei Oktaven nach
oben und unten (Synth- und Sample-Layer). Teilen sich mehrere Sounds einen Kanal, oder ist es der globale MIDI-Kanal
(HOME, MIDI CH), gilt wie bisher die Drum-Map (GM-Noten bzw. C3–G3 für Sound 1–8). Mit REC aufgenommene Schläge werden
ohne Tonhöhe gespeichert.

**CC-Learn.** HOME dreimal (Seite CC LEARN), KNOB 1 auf ON. Dann an irgendeinem Knopf drehen („… SEND CC“) und am externen
Controller einen Regler bewegen: Kanal und CC sind diesem Parameter zugeordnet (bei Sound-Parametern dem Sound, der
gerade gewählt war, auch Mixer-Streifen gehen). LEARN bleibt an für weitere Zuordnungen; OFF beendet, CLEAR löscht alle.
Bis 32 Zuordnungen, global gespeichert (überleben Patchwechsel und Updates). Nicht anlernbar sind Speicher-, Step- und
Einstellungsparameter.

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
