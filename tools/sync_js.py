#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/oc_lib.js into the companion app and the emulator page, between /* @oc-lib-begin */ and /* @oc-lib-end */."""
import pathlib, re, sys
root = pathlib.Path(__file__).resolve().parent.parent
lib = (root / "tools/oc_lib.js").read_text()
for f in ("companion/index.html", "emu/emu.html", "web/admin.html"):
    p = root / f
    s = p.read_text()
    n = re.subn(r"/\* @oc-lib-begin \*/.*?/\* @oc-lib-end \*/",
                lambda m: "/* @oc-lib-begin */\n" + lib + "/* @oc-lib-end */", s, flags=re.S)
    if not n[1]:
        sys.exit(f"{f}: no @oc-lib markers")
    p.write_text(n[0])
