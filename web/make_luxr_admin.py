#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LUXR's update page: web/admin.html made LUXR's -> build/web/luxr-update.html (self-contained, as make_admin.py).

As ONE CLICK's page: luxr.fwsc of the same build lies next to it (GitHub Pages) and is loaded at once; another file
can still be picked. It accepts LUXR packages (FM-1_87x), names LUXR on the device and leaves out ONE CLICK's
samples and info queries. Section 4 backs up the patches over USB (LUXR's frames 7D 'O' 'C' 40..47, lx_ui.c) to a
.luxr file and restores one; an update backs them up before and restores them after, when the device can. The official V15 and the emergency help stay as they are."""
import re, pathlib
r = pathlib.Path(__file__).resolve().parent
def strip(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)
lib = strip((r / "vendor/fm1pkg.js").read_text()) + "\n" + strip((r / "vendor/fm1ota.js").read_text())
html = (r / "admin.html").read_text()

BACKUP_HTML = """  <section aria-labelledby="h-bk">
    <h2 id="h-bk">3 · Backup <small>Kits und Patterns</small></h2>
    <p class="hint">Liest alle belegten Speicherplätze (Kit + 16 Patterns je Platz) über USB aus und speichert sie als
    <code>.luxr</code>-Datei; „Zurückspielen“ schreibt eine solche Datei zurück (auch Backups älterer LUXR-Versionen, sie werden
    umgewandelt). Geht mit LUXR ab der Version, die diese Seite mitbringt.</p>
    <div class="row"><button id="bkSave" disabled>Backup herunterladen</button></div>
    <p class="hint" id="bkNote">Erst oben unter „1 · Gerät“ auf „Verbinden“ drücken.</p>
    <input id="bkFile" type="file" accept=".luxr">
    <div class="row"><button id="bkLoad" disabled>Backup zurückspielen</button></div>
    <label class="check"><input type="checkbox" id="bkAuto" checked> <span>Beim Installieren vorher ein Backup ziehen
    (als Datei gespeichert) und es danach zurückspielen.</span></label>
  </section>

"""
BACKUP_JS = r"""/* ---- 3: LUXR's backup: its patches over USB (7D 'O' 'C' 40..47, lx_ui.c), a .luxr file: "LUXRBK01", the count, then
 * each slot: its number, its length (4, LE), its bytes as stored */
let lxDev = null, preBackup = null, bkPicked = null;
const lx4 = (v) => [v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255];
const lxr4 = (b, i) => (b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24) >>> 0;
async function lxAsk(link, cmd, data, ms = 2500) {
  link.drain(); link.send([0xF0, 0x7D, 0x4F, 0x43, cmd, ...OC.pack7(data), 0xF7]);
  const end = Date.now() + ms;
  while (Date.now() < end) {
    const p = await link.read(end - Date.now());
    if (p && p[1] === 0x7D && p[2] === 0x4F && p[3] === 0x43 && p[4] === cmd + 1) return OC.unpack7(p.slice(5, -1));
  }
  return null;
}
async function lxInfo(link) {
  const r = await lxAsk(link, 0x40, [], 1500);
  if (!r || String.fromCharCode(...r.slice(0, 4)) !== "LUXR") return null;
  const z = r.indexOf(0, 4), n = r[z + 1], size = lxr4(r, z + 2);   // then the slots in use: a bit each, (n + 7) / 8 bytes
  const slots = []; for (let s = 0; s < n; s++) if ((r[z + 6 + (s >> 3)] >> (s & 7)) & 1) slots.push(s);
  return { ver: "LUXR " + String.fromCharCode(...r.slice(4, z)), n, size, slots };
}
async function lxBackup(link) {
  const inf = await lxInfo(link); if (!inf) return null;
  const slots = [];
  for (const s of inf.slots) {
    const b = new Uint8Array(inf.size);
    for (let off = 0; off < inf.size; off += 256) {
      const r = await lxAsk(link, 0x42, [s, ...lx4(off)]);
      if (!r || r.length < 5) throw new Error(`Platz ${s + 1}: keine Antwort`);
      b.set(r.slice(5, 5 + Math.min(256, inf.size - off)), off);
    }
    slots.push({ s, bytes: b });
    status(`Backup: Platz ${s + 1} gelesen …`); log(`backup slot ${s + 1}: ${b.length} B`);
  }
  return { ver: inf.ver, slots };
}
async function lxRestore(link, bk) {
  for (const { s, bytes } of bk.slots) {
    for (let off = 0; off < bytes.length; off += 256) {
      const r = await lxAsk(link, 0x44, [s, ...lx4(off), ...bytes.slice(off, off + 256)]);
      if (!r || r[0] !== 0) throw new Error(`Platz ${s + 1}: Übertragung abgelehnt`);
    }
    const c = await lxAsk(link, 0x46, [s, ...lx4(bytes.length)], 5000);
    if (!c || c[0] !== 0) throw new Error(`Platz ${s + 1}: ${c && c[0] === 4 ? "Flash-Fehler" : "kein gültiger Patch"}`);
    status(`Zurückspielen: Platz ${s + 1} geschrieben …`); log(`restore slot ${s + 1}`);
  }
}
function bkBytes(bk) {
  const parts = [..."LUXRBK01"].map((c) => c.charCodeAt(0)); parts.push(bk.slots.length);
  let n = parts.length; for (const x of bk.slots) n += 5 + x.bytes.length;
  const out = new Uint8Array(n); out.set(parts, 0); let o = parts.length;
  for (const x of bk.slots) { out[o++] = x.s; out.set(lx4(x.bytes.length), o); o += 4; out.set(x.bytes, o); o += x.bytes.length; }
  return out;
}
function bkParse(u8) {
  if (String.fromCharCode(...u8.slice(0, 8)) !== "LUXRBK01") throw new Error("Keine LUXR-Backup-Datei.");
  const slots = []; let o = 9;
  for (let k = 0; k < u8[8]; k++) { const s = u8[o], len = lxr4(u8, o + 1); slots.push({ s, bytes: u8.slice(o + 5, o + 5 + len) }); o += 5 + len; }
  return { slots };
}
function bkDownload(bk) {
  const d = new Date(), p = (x) => String(x).padStart(2, "0");
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([bkBytes(bk)], { type: "application/octet-stream" }));
  a.download = `luxr-backup-${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}.luxr`;
  document.body.appendChild(a); a.click(); a.remove();
  status(`Backup: ${bk.slots.length} Patches gespeichert (${a.download}).`, "ok");
}
$("bkSave").onclick = async () => {
  if (!dev || !lxDev) return;
  lock(true);
  try { const bk = await lxBackup(dev.link); if (bk) bkDownload(bk); else status("Das Gerät kennt das Backup nicht (LUXR vorher aktualisieren).", "warn"); }
  catch (e) { status("Backup: " + e.message, "err"); }
  lock(false);
};
$("bkFile").onchange = async () => {
  const f = $("bkFile").files[0]; bkPicked = null;
  if (f) try { bkPicked = bkParse(new Uint8Array(await f.arrayBuffer())); status(`Backup-Datei: ${bkPicked.slots.length} Patches.`, "ok"); }
  catch (e) { status(e.message, "err"); }
  lock(false);
};
$("bkLoad").onclick = async () => {
  if (!dev || !lxDev || !bkPicked) return;
  lock(true);
  try { await lxRestore(dev.link, bkPicked); status(`${bkPicked.slots.length} Patches zurückgespielt.`, "ok"); lxDev = await lxInfo(dev.link); }
  catch (e) { status("Zurückspielen: " + e.message, "err"); }
  lock(false);
};

"""

HELP_HTML = """  <section aria-labelledby="h-map">
    <h2 id="h-map">3 · Help map <small>how LUXR is played</small></h2>
    <p class="hint">Every control of the FM-1 and what it does in LUXR. SHIFT means: hold GLO. The full manual and a
    demonstration of every function: <a href="https://holodeck-audiotools.com/LUXR" target="_blank" rel="noopener"
    style="color:inherit"><b>holodeck-audiotools.com/LUXR</b></a>.</p>
    <a href="luxr-help-map-en.jpg" target="_blank" rel="noopener"><img src="luxr-help-map-en.jpg" alt="LUXR help map: the
    FM-1 with leader lines from every control to its function" style="width:100%;height:auto;display:block;border-radius:10px"></a>
    <p class="hint">Click the map to open it full size.</p>
  </section>

"""
EDITS = [   # (what, replacement): each must occur exactly once
    ("<title>ONE CLICK Admin</title>", "<title>LUXR Update</title>"),
    ("<h1>ONE CLICK <span>Admin</span></h1>", "<h1>LUXR <span>Update</span></h1>"),
    ("Firmware-Update und Wartung für den M-VAVE FM-1 über USB.",
     "LUXR, die Drum-Machine-Firmware für den M-VAVE FM-1, per USB installieren."),
    ("<small>oneclick.fwsc</small>", "<small>luxr.fwsc</small>"),
    ("<li>Paket wählen, gebaut mit <code>tools/build.py</code> (Kennung FM-1_8xx).</li>",
     "<li>Die Firmware dieses Builds ist schon geladen; oder eine eigene <code>luxr.fwsc</code> wählen, gebaut mit "
     "<code>tools/build.py --app lxr</code> (Kennung FM-1_87x).</li>"),
    ("Songs, Sounds und Samples des Geräts habe ich bei Bedarf gesichert (Songs/Sounds bleiben beim Update erhalten, "
     "solange das Speicherformat gleich ist).",
     "Mir ist klar: Projekte im Gerät bleiben bei einem LUXR-Update erhalten; beim Wechsel von oder zu ONE CLICK gehen "
     "die Daten der anderen Firmware verloren."),
    ('<section aria-labelledby="h-smp">', '<section aria-labelledby="h-smp" hidden style="display:none">'),   # (ONE CLICK's
    # samples: not for LUXR; kept for the script's ids)
    ('<section class="danger" aria-labelledby="h-back">', HELP_HTML + BACKUP_HTML + '<section class="danger" aria-labelledby="h-back">'),
    ('/* ---- 2: the package */', BACKUP_JS + '/* ---- 2: the package */'),
    ('  $("smpRefresh").disabled = $("smpClear").disabled =',
     '  $("bkSave").disabled = on || !lxDev; $("bkLoad").disabled = on || !lxDev || !bkPicked;\n'
     '  $("smpRefresh").disabled = $("smpClear").disabled ='),
    ('    $("iStore").textContent = "–";\n',
     '    $("iStore").textContent = "–";\n'
     '    lxDev = /^FM-1_87/.test(dev.id.text) ? await lxInfo(dev.link).catch(() => null) : null;   /* (LUXR: its patches) */\n'
     '    if (lxDev) $("iStore").textContent = `${lxDev.ver}: ${lxDev.slots.length} von ${lxDev.n} Plätzen belegt`;\n'
     '    $("bkNote").textContent = lxDev ? `Bereit: ${lxDev.slots.length} belegte Plätze.` : /^FM-1_87/.test(dev.id.text) ?\n'
     '      "Die LUXR-Version auf dem Gerät kennt das Backup noch nicht: erst die Firmware installieren (Abschnitt 2), dann erneut „Verbinden“." :\n'
     '      /^ota-/.test(dev.id.text) ? "Das Gerät ist im Update-Modus: erst die Installation abschließen." :\n'
     '      "Auf dem Gerät läuft kein LUXR.";\n'),
    ('    $("devState").textContent = "nicht gefunden"; $("devState").className = "chip err";',
     '    $("devState").textContent = "nicht gefunden"; $("devState").className = "chip err";\n'
     '    lxDev = null; $("bkNote").textContent = "Kein FM-1 gefunden: siehe Fortschritt.";'),
    ('lock(true); $("bar").value = 0;\n  if (dev) { dev.link.close(); dev = null; }',
     'lock(true); $("bar").value = 0;\n'
     '  preBackup = null;\n'
     '  if (dev && lxDev && $("bkAuto").checked) {   /* (the patches first: saved as a file, restored afterwards) */\n'
     '    try { status("Backup vor dem Update …"); preBackup = await lxBackup(dev.link); if (preBackup) bkDownload(preBackup); }\n'
     '    catch (e) { log("Backup: " + (e.stack || e)); }\n'
     '  }\n'
     '  if (dev) { dev.link.close(); dev = null; }'),
    ('    setTimeout(connect, 1500);',
     '    setTimeout(async () => {\n'
     '      await connect();\n'
     '      if (preBackup && preBackup.slots.length && dev && lxDev) {\n'
     '        lock(true);\n'
     '        try { await lxRestore(dev.link, preBackup); status(`Fertig: das FM-1 meldet ${product}, ${preBackup.slots.length} Patches zurückgespielt.`, "ok"); }\n'
     '        catch (e) { status("Zurückspielen: " + e.message + " (die Backup-Datei ist gespeichert)", "err"); }\n'
     '        lock(false);\n'
     '      }\n'
     '    }, 2500);'),
    ("Was ONE CLICK im Flash gespeichert hat", "Was LUXR im Flash gespeichert hat"),
    ("ONE CLICK FM-1 (GPL-3.0) baut auf Felucca von Leo Kuroshita (@kurogedelic), Hügelton Instruments; der "
     "Installer-Code stammt aus Felucca.",
     "LUXR (GPL-3.0; die Transienten-Samples der Sonic Potions LXR, © Julian Schmidt, unter deren nicht-kommerzieller "
     "Lizenz) baut auf ONE CLICK FM-1 und Felucca von Leo Kuroshita (@kurogedelic), Hügelton Instruments; der "
     "Installer-Code stammt aus Felucca. Quelltext: "
     '<a href="https://github.com/Orbitator/LUXR" target="_blank" rel="noopener" style="color:inherit">Orbitator/LUXR</a>.'),
    ('const ident = (id) => !id ? "–" : /^FM-1_8\\d\\d$/.test(id.text) ? `ONE CLICK (${id.text})`',
     'const ident = (id) => !id ? "–" : /^FM-1_87\\d$/.test(id.text) ? `LUXR (${id.text})` : '
     '/^FM-1_8\\d\\d$/.test(id.text) ? `ONE CLICK (${id.text})`'),
    ("    if (/^FM-1_8/.test(dev.id.text)) {\n      const r = await ocRequest",
     "    if (/^FM-1_8/.test(dev.id.text) && !/^FM-1_87/.test(dev.id.text)) {   /* (ONE CLICK's info; LUXR has none) */\n"
     "      const r = await ocRequest"),
    ("      await listSlots();\n    } else if", "    } else if"),
    ('if (!/^FM-1_8\\d\\d$/.test(product)) throw new Error(`Kennung ${product}: kein ONE-CLICK-Paket (FM-1_8xx). '
     'Für die Original-Firmware Abschnitt 4 nutzen.`);',
     'if (!/^FM-1_87\\d$/.test(product)) throw new Error(`Kennung ${product}: kein LUXR-Paket (FM-1_87x). '
     'Für die Original-Firmware Abschnitt 4 nutzen.`);'),
    ('const r = await fetch("oneclick.fwsc", { cache: "no-store" });',   # the firmware next to the page
     'const r = await fetch("luxr.fwsc", { cache: "no-store" });'),
    ('await usePkg(new Uint8Array(await r.arrayBuffer()), "oneclick.fwsc" +',
     'await usePkg(new Uint8Array(await r.arrayBuffer()), "luxr.fwsc" +'),
]
# the page in English: every German text of web/admin.html and the edits above (each must occur at least once)
EN = [
    ("Erst oben unter „1 · Gerät“ auf „Verbinden“ drücken.", "First press “Connect” under “1 · Device” above."),
    ('<html lang="de">', '<html lang="en">'),
    ("LUXR, die Drum-Machine-Firmware für den M-VAVE FM-1, per USB installieren.",
     "Install LUXR, the drum machine firmware for the M-VAVE FM-1, over USB."),
    ("Läuft in Chrome oder Edge auf diesem Computer; die Dateien bleiben im Browser.",
     "Runs in Chrome or Edge on this computer; the files stay in the browser."),
    ("1 · Gerät", "1 · Device"),
    ("FM-1 per USB-Datenkabel direkt an den Computer, andere MIDI-Programme schließen.",
     "Connect the FM-1 directly to the computer with a USB data cable; close other MIDI programs."),
    (">Verbinden<", ">Connect<"),
    (">nicht verbunden<", ">not connected<"),
    ("MIDI-Port <select", "MIDI port <select"),
    (">automatisch<", ">automatic<"),
    ("<dt>Kennung</dt>", "<dt>Product ID</dt>"),
    ("<dt>MIDI-Port</dt>", "<dt>MIDI port</dt>"),
    ("<dt>Speicher</dt>", "<dt>Storage</dt>"),
    ("2 · Firmware installieren", "2 · Install the firmware"),
    ("Die Firmware dieses Builds ist schon geladen; oder eine eigene <code>luxr.fwsc</code> wählen, gebaut mit "
     "<code>tools/build.py --app lxr</code> (Kennung FM-1_87x).",
     "The firmware of this build is already loaded; or choose your own <code>luxr.fwsc</code>, built with "
     "<code>tools/build.py --app lxr</code> (product ID FM-1_87x)."),
    ("„Installieren“: das FM-1 prüft das Paket, startet den Update-Lader und schreibt es (etwa 2 Minuten).",
     "“Install”: the FM-1 checks the package, starts the update loader and writes it (about 2 minutes)."),
    ("Danach startet es neu; die Seite prüft, dass es die neue Kennung meldet.",
     "Then it restarts; the page checks that it reports the new product ID."),
    ("<dt>Datei</dt>", "<dt>File</dt>"),
    ("<dt>Größe</dt>", "<dt>Size</dt>"),
    ("<dt>Lader</dt>", "<dt>Loader</dt>"),
    ("Mir ist klar: Projekte im Gerät bleiben bei einem LUXR-Update erhalten; beim Wechsel von oder zu ONE CLICK gehen "
     "die Daten der anderen Firmware verloren.",
     "I understand: the patches on the device are kept by a LUXR update; switching from or to ONE CLICK loses the other "
     "firmware's data."),
    (">Installieren<", ">Install<"),
    (">Fortschritt<", ">Progress<"),
    (">Bereit.<", ">Ready.<"),
    ('aria-label="Protokoll"', 'aria-label="Log"'),
    ("Während des Schreibens das USB-Kabel nicht ziehen und die Seite offen lassen. Bricht es ab, bleibt das FM-1 im "
     "Update-Modus: wieder einstecken, „Installieren“ setzt fort.",
     "Do not unplug the USB cable while writing and keep the page open. If it breaks off, the FM-1 stays in update mode: "
     "plug it in again, “Install” continues."),
    ("3 · Backup <small>Kits und Patterns</small>", "4 · Backup <small>kits and patterns</small>"),
    ("""Liest alle belegten Speicherplätze (Kit + 16 Patterns je Platz) über USB aus und speichert sie als
    <code>.luxr</code>-Datei; „Zurückspielen“ schreibt eine solche Datei zurück (auch Backups älterer LUXR-Versionen, sie werden
    umgewandelt). Geht mit LUXR ab der Version, die diese Seite mitbringt.""",
     """Reads every used slot (kit + 16 patterns each) over USB and saves them as a <code>.luxr</code> file; “Restore”
    writes such a file back (backups of older LUXR versions too, they are converted). Works with LUXR from the version
    this page brings."""),
    (">Backup herunterladen<", ">Download backup<"),
    (">Backup zurückspielen<", ">Restore backup<"),
    ("""Beim Installieren vorher ein Backup ziehen
    (als Datei gespeichert) und es danach zurückspielen.""",
     """When installing, take a backup first (saved as a file) and restore it afterwards."""),
    ("4 · Zurück zur Original-Firmware", "5 · Back to the original firmware"),
    ("Die offizielle FM-1.fwsc (V15) von", "Choose the official FM-1.fwsc (V15) from"),
    (" wählen. Akzeptiert wird nur die unveränderte Datei (SHA-256 geprüft). Was LUXR im Flash gespeichert hat, bleibt dort "
     "liegen, ist aber in V15 nicht sichtbar.",
     ". Only the unchanged file is accepted (SHA-256 checked). What LUXR stored in the flash stays there but is not "
     "visible in V15."),
    (">V15 installieren<", ">Install V15<"),
    (">Notfall<", ">Emergency<"),
    ("<b>Update abgebrochen:</b> FM-1 bleibt im Update-Modus (Display „UPDATE“). Kabel neu stecken, hier „Installieren“ "
     "drücken: es geht weiter.",
     "<b>Update broken off:</b> the FM-1 stays in update mode (display “UPDATE”). Plug the cable in again and press "
     "“Install” here: it continues."),
    ("<b>Startet nicht mehr:</b> zweimal hintereinander innerhalb von 30 s abgestürzt → das FM-1 geht von selbst in UBOOT. "
     "Ebenso <b>OCT− und OCT+ 5 s halten</b>. In UBOOT nimmt M-VAVEs Updater die V15 an.",
     "<b>Does not start any more:</b> two crashes within 30 s → the FM-1 enters UBOOT by itself. So does "
     "<b>holding OCT− and OCT+ for 5 s</b>. In UBOOT, M-VAVE's updater accepts V15."),
    ("Hilft nichts davon: Wiederherstellung mit dem", "If none of this helps: recovery with the"),
    ("LUXR (GPL-3.0; die Transienten-Samples der Sonic Potions LXR, © Julian Schmidt, unter deren nicht-kommerzieller "
     "Lizenz) baut auf ONE CLICK FM-1 und Felucca von Leo Kuroshita (@kurogedelic), Hügelton Instruments; der "
     "Installer-Code stammt aus Felucca. Quelltext:",
     "LUXR (GPL-3.0; the transient samples of the Sonic Potions LXR, © Julian Schmidt, under their non-commercial "
     "licence) builds on ONE CLICK FM-1 and Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments; the installer "
     "code comes from Felucca. Source:"),
    ("M-VAVE und FM-1 sind Marken ihrer Inhaber. Benutzung auf eigene Gefahr, ohne Gewähr.",
     "M-VAVE and FM-1 are trademarks of their owners. Use at your own risk, without warranty."),
    # the script
    ('["", "kein Platz mehr", "ungültige Anfrage", "falsche Reihenfolge", "Flash-Fehler"]',
     '["", "no room left", "invalid request", "wrong order", "flash error"]'),
    ('"keine Antwort vom Gerät"', '"no answer from the device"'),
    ('"Fehler " + d[6]', '"error " + d[6]'),
    ("`kein Ausgang zu „${input.name}“`", "`no output for “${input.name}”`"),
    ("`„${input.name}“ belegt: ${e.message}`", "`“${input.name}” busy: ${e.message}`"),
    ("`„${input.name}“ antwortet nicht`", "`“${input.name}” does not answer`"),
    ('"MIDI-Eingänge: "', '"MIDI inputs: "'),
    ('"MIDI-Ausgänge: "', '"MIDI outputs: "'),
    ('`„${p.name}“ (${p.state})`', '`“${p.name}” (${p.state})`'),
    ('|| "keine"', '|| "none"'),
    ('"MIDI (SysEx) wurde nicht erlaubt: in der Adressleiste die Website-Einstellungen öffnen, MIDI erlauben, erneut versuchen."',
     '"MIDI (SysEx) was not allowed: open the site settings in the address bar, allow MIDI, try again."'),
    ('"FM-1 nicht gefunden: Datenkabel prüfen, andere MIDI-Programme schließen, erneut versuchen."',
     '"FM-1 not found: check the data cable, close other MIDI programs, try again."'),
    ('"Das verbundene Gerät ist kein FM-1."', '"The connected device is not an FM-1."'),
    ('"USB-Verbindung verloren: Kabel neu stecken und erneut „Installieren“."',
     '"USB connection lost: plug the cable in again and press “Install” again."'),
    ('"Das Gerät hat aufgehört zu antworten: Kabel neu stecken und erneut „Installieren“."',
     '"The device stopped answering: plug the cable in again and press “Install” again."'),
    ('"Der Update-Lader meldet sich nicht: Kabel neu stecken und „Installieren“ (das FM-1 bleibt im Update-Modus)."',
     '"The update loader does not answer: plug the cable in again and press “Install” (the FM-1 stays in update mode)."'),
    ('"Das FM-1 kommt nicht zurück: aus- und wieder einschalten."', '"The FM-1 does not come back: switch it off and on again."'),
    ('"Geschrieben, aber das FM-1 meldet eine andere Kennung: "', '"Written, but the FM-1 reports another product ID: "'),
    ('"Ungültige Anfrage vom Gerät: Paket und Gerät passen nicht zusammen."',
     '"Invalid request from the device: the package and the device do not match."'),
    ('"Es wird geschrieben."', '"Writing in progress."'),
    ('"Kein Web MIDI: die Seite in Chrome oder Edge öffnen."', '"No Web MIDI: open the page in Chrome or Edge."'),
    ('"MIDI ist erlaubt und Ports sind da, aber kein FM-1 hat geantwortet. Im Protokoll stehen die gefundenen Ports: den '
     'FM-1-Port unten auswählen und erneut „Verbinden“. Andere MIDI-Programme (DAW, M-VAVE-App) schließen."',
     '"MIDI is allowed and there are ports, but no FM-1 answered. The log lists the ports found: choose the FM-1 port '
     'below and press “Connect” again. Close other MIDI programs (DAW, M-VAVE app)."'),
    ('"MIDI ist erlaubt, aber der Computer sieht kein MIDI-Gerät: ein USB-Datenkabel (kein reines Ladekabel) direkt am '
     'Computer verwenden, FM-1 einschalten. Bluetooth-MIDI reicht für das Update nicht."',
     '"MIDI is allowed, but the computer sees no MIDI device: use a USB data cable (not a charge-only cable) directly on '
     'the computer, switch the FM-1 on. Bluetooth MIDI is not enough for the update."'),
    ("`${lxDev.ver}: ${lxDev.slots.length} von ${lxDev.n} Plätzen belegt`", "`${lxDev.ver}: ${lxDev.slots.length} of ${lxDev.n} slots used`"),
    ("`Bereit: ${lxDev.slots.length} belegte Plätze.`", "`Ready: ${lxDev.slots.length} slots in use.`"),
    ('"Die LUXR-Version auf dem Gerät kennt das Backup noch nicht: erst die Firmware installieren (Abschnitt 2), dann erneut „Verbinden“."',
     '"The LUXR version on the device does not know the backup yet: install the firmware first (section 2), then press “Connect” again."'),
    ('"Das Gerät ist im Update-Modus: erst die Installation abschließen."', '"The device is in update mode: finish the installation first."'),
    ('"Auf dem Gerät läuft kein LUXR."', '"The device is not running LUXR."'),
    ("`Samples: ${(ocU32(r, z + 3) / OC.RATE).toFixed(1)} s frei von ${(ocU32(r, z + 7) / OC.RATE).toFixed(1)} s`",
     "`Samples: ${(ocU32(r, z + 3) / OC.RATE).toFixed(1)} s free of ${(ocU32(r, z + 7) / OC.RATE).toFixed(1)} s`"),
    ('"Flash nicht lesbar"', '"flash not readable"'),
    ('"Das FM-1 ist im Update-Modus: Paket wählen und „Installieren“ setzt fort."',
     '"The FM-1 is in update mode: choose the package, and “Install” continues."'),
    ('"nicht gefunden"', '"not found"'),
    ('"Kein FM-1 gefunden: siehe Fortschritt."', '"No FM-1 found: see Progress."'),
    ('"Wirklich alle löschen?"', '"Really delete all?"'),
    ('"Alle Samples löschen"', '"Delete all samples"'),
    ('"Keine Antwort."', '"No answer."'),
    ('"Gelöscht."', '"Deleted."'),
    ("`Platz ${s + 1}: keine Antwort`", "`slot ${s + 1}: no answer`"),
    ("`Backup: Platz ${s + 1} gelesen …`", "`Backup: slot ${s + 1} read …`"),
    ("`Platz ${s + 1}: Übertragung abgelehnt`", "`slot ${s + 1}: transfer refused`"),
    ('`Platz ${s + 1}: ${c && c[0] === 4 ? "Flash-Fehler" : "kein gültiger Patch"}`', '`slot ${s + 1}: ${c && c[0] === 4 ? "flash error" : "not a valid patch"}`'),
    ("`Zurückspielen: Platz ${s + 1} geschrieben …`", "`Restore: slot ${s + 1} written …`"),
    ('"Keine LUXR-Backup-Datei."', '"Not a LUXR backup file."'),
    ('"Das Gerät kennt das Backup nicht (LUXR vorher aktualisieren)."', '"The device does not know the backup (update LUXR first)."'),
    ("`Backup-Datei: ${bkPicked.slots.length} Patches.`", "`Backup file: ${bkPicked.slots.length} patches.`"),
    ("`${bkPicked.slots.length} Patches zurückgespielt.`", "`${bkPicked.slots.length} patches restored.`"),
    ('"Zurückspielen: " + e.message', '"Restore: " + e.message'),
    ('"Keine gültige .fwsc-Datei."', '"Not a valid .fwsc file."'),
    ("`Kennung ${product}: kein LUXR-Paket (FM-1_87x). Für die Original-Firmware Abschnitt 4 nutzen.`",
     "`Product ID ${product}: not a LUXR package (FM-1_87x). For the original firmware use section 5.`"),
    ('"Im Paket fehlt der Update-Lader: es ist nicht mit tools/build.py gebaut."',
     '"The package has no update loader: it was not built with tools/build.py."'),
    ("`Paket ${product} geprüft.`", "`Package ${product} checked.`"),
    ('"Die Firmware dieses Builds ist schon geladen. Eine andere Datei kann trotzdem gewählt werden."',
     '"The firmware of this build is already loaded. Another file can still be chosen."'),
    ('"Gestartet: das FM-1 prüft das Paket …"', '"Started: the FM-1 checks the package …"'),
    ('"Schreibe … nicht abziehen"', '"Writing … do not unplug"'),
    ('"Update-Modus gefunden: setze fort"', '"update mode found: continuing"'),
    ("`Fertig: das FM-1 meldet ${product}.`", "`Done: the FM-1 reports ${product}.`"),
    ("`Fertig: das FM-1 meldet ${product}, ${preBackup.slots.length} Patches zurückgespielt.`",
     "`Done: the FM-1 reports ${product}, ${preBackup.slots.length} patches restored.`"),
    ('" (die Backup-Datei ist gespeichert)"', '" (the backup file is saved)"'),
    ('"Das ist nicht die offizielle FM-1.fwsc V15 (falsche Größe)."', '"This is not the official FM-1.fwsc V15 (wrong size)."'),
    ('"Sicher? Nochmal drücken"', '"Sure? Press again"'),
    ('"V15 installieren"', '"Install V15"'),
    ('"Kein Web MIDI in diesem Browser: die Seite in Chrome oder Edge öffnen."', '"No Web MIDI in this browser: open the page in Chrome or Edge."'),
]
for a, b in EDITS:
    n = html.count(a)
    if n != 1:
        raise SystemExit(f"make_luxr_admin: {n} x {a[:60]!r} in web/admin.html (expected 1)")
    html = html.replace(a, b)
for a, b in EN:
    if a not in html:
        raise SystemExit(f"make_luxr_admin: EN: {a[:60]!r} not in the page")
    html = html.replace(a, b)
assert html.count("/*LIB*/") == 1
out = r.parent / "build/web"; out.mkdir(parents=True, exist_ok=True)
(out / "luxr-update.html").write_text(html.replace("/*LIB*/", lib))
print(out / "luxr-update.html")
