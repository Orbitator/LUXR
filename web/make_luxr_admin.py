#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LUXR's update page: web/admin.html made LUXR's -> build/web/luxr-update.html (self-contained, as make_admin.py).

The installer only: the page loads no firmware of its own (luxr.fwsc carries the LXR samples' licence and is not
published); the viewer picks the file. It accepts LUXR packages (FM-1_87x), names LUXR on the device and leaves out
ONE CLICK's samples and info queries. The official V15 and the emergency help stay as they are."""
import re, pathlib
r = pathlib.Path(__file__).resolve().parent
def strip(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)
lib = strip((r / "vendor/fm1pkg.js").read_text()) + "\n" + strip((r / "vendor/fm1ota.js").read_text())
html = (r / "admin.html").read_text()

EDITS = [   # (what, replacement): each must occur exactly once
    ("<title>ONE CLICK Admin</title>", "<title>LUXR Update</title>"),
    ("<h1>ONE CLICK <span>Admin</span></h1>", "<h1>LUXR <span>Update</span></h1>"),
    ("Firmware-Update und Wartung für den M-VAVE FM-1 über USB.",
     "LUXR, die Drum-Machine-Firmware für den M-VAVE FM-1, per USB installieren."),
    ("<small>oneclick.fwsc</small>", "<small>luxr.fwsc</small>"),
    ("<li>Paket wählen, gebaut mit <code>tools/build.py</code> (Kennung FM-1_8xx).</li>",
     "<li><code>luxr.fwsc</code> wählen: aus dem Build auf GitHub (Actions › build › Artifacts › luxr-fwsc, entpacken) "
     "oder gebaut mit <code>tools/build.py --app lxr</code> (Kennung FM-1_87x).</li>"),
    ("Songs, Sounds und Samples des Geräts habe ich bei Bedarf gesichert (Songs/Sounds bleiben beim Update erhalten, "
     "solange das Speicherformat gleich ist).",
     "Mir ist klar: Projekte im Gerät bleiben bei einem LUXR-Update erhalten; beim Wechsel von oder zu ONE CLICK gehen "
     "die Daten der anderen Firmware verloren."),
    ('<section aria-labelledby="h-smp">', '<section aria-labelledby="h-smp" hidden>'),
    ("4 · Zurück zur Original-Firmware", "3 · Zurück zur Original-Firmware"),
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
     'Für die Original-Firmware Abschnitt 3 nutzen.`);'),
    ("  if (!/^https?:$/.test(location.protocol)) return;\n",
     "  return;   /* (LUXR: no firmware next to the page, the viewer picks luxr.fwsc) */\n"),
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
