#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""ios-app/www for the iPad app: the emulator page (emu/build_lx.sh -> build/emu/luxr-emu.html) and the preset backup.
The page talks to the native plugin LuxrMidi through the bridge Capacitor injects (Capacitor.nativePromise /
addListener); Capacitor's JavaScript core is not added (without a bundler it is not needed, as in XGenerator).

  make_www.py ../build/emu/luxr-emu.html www
"""
import shutil
import sys
from pathlib import Path

page, out = Path(sys.argv[1]), Path(sys.argv[2])
html = page.read_text(encoding="utf-8")
# the app: no zoom (a double tap or a pinch zoomed the whole instrument)
vp = '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">'
assert html.count(vp) == 1, "the page's viewport meta changed"
html = html.replace(vp, '<meta name="viewport" content="width=device-width, initial-scale=1, minimum-scale=1, maximum-scale=1, '
                        'user-scalable=no, viewport-fit=cover">\n<style>html, body { touch-action: manipulation; }</style>\n'
                        "<script>['gesturestart', 'gesturechange', 'dblclick'].forEach(function (n) { document.addEventListener(n, "
                        "function (e) { e.preventDefault(); }, { passive: false }); });</script>")
out.mkdir(parents=True, exist_ok=True)
(out / "index.html").write_text(html, encoding="utf-8")
for f in page.parent.glob("*.luxr"):
    shutil.copy(f, out / f.name)
print(f"www: {out / 'index.html'} ({len(html) // 1024} KB)")
