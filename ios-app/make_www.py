#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ios-app/www for the iPad app: the emulator page (emu/build_lx.sh -> build/emu/luxr-emu.html) with Capacitor's
runtime in front of its script (the page finds the native plugin LuxrMidi through it), and the preset backup.

  make_www.py ../build/emu/luxr-emu.html www
"""
import shutil
import sys
from pathlib import Path

page, out = Path(sys.argv[1]), Path(sys.argv[2])
here = Path(__file__).resolve().parent
cap = (here / "node_modules" / "@capacitor" / "core" / "dist" / "capacitor.js").read_text(encoding="utf-8")
html = page.read_text(encoding="utf-8")
assert html.count("<body>") == 1, "the page has no single <body>"
html = html.replace("<body>", "<body>\n<script>" + cap.replace("</script", "<\\/script") + "</script>", 1)
out.mkdir(parents=True, exist_ok=True)
(out / "index.html").write_text(html, encoding="utf-8")
for f in page.parent.glob("*.luxr"):
    shutil.copy(f, out / f.name)
print(f"www: {out / 'index.html'} ({len(html) // 1024} KB)")
