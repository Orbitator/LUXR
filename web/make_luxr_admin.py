#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LUXR's update page: web/admin.html made LUXR's -> build/web/luxr-update.html (self-contained, as make_admin.py).

As ONE CLICK's page: luxr.fwsc of the same build lies next to it (GitHub Pages) and is loaded at once; another file
can still be picked. It accepts LUXR packages (FM-1_87x), names LUXR on the device and leaves out ONE CLICK's
samples and info queries. Section 3 backs up the patches over USB (LUXR's frames 7D 'O' 'C' 40..47, lx_ui.c) to a
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
  const z = r.indexOf(0, 4), n = r[z + 1], size = lxr4(r, z + 2), used = r[z + 6] | r[z + 7] << 8;
  const slots = []; for (let s = 0; s < n; s++) if ((used >> s) & 1) slots.push(s);
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
    ('<section aria-labelledby="h-smp">', '<section aria-labelledby="h-smp" hidden>'),
    ('<section class="danger" aria-labelledby="h-back">', BACKUP_HTML + '<section class="danger" aria-labelledby="h-back">'),
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
for a, b in EDITS:
    n = html.count(a)
    if n != 1:
        raise SystemExit(f"make_luxr_admin: {n} x {a[:60]!r} in web/admin.html (expected 1)")
    html = html.replace(a, b)
assert html.count("/*LIB*/") == 1
out = r.parent / "build/web"; out.mkdir(parents=True, exist_ok=True)
(out / "luxr-update.html").write_text(html.replace("/*LIB*/", lib))
print(out / "luxr-update.html")
