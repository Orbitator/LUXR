#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""web/admin.html, self-contained -> build/web/oneclick-admin.html (Felucca's fm1pkg.js and fm1ota.js inlined)."""
import re, pathlib
r = pathlib.Path(__file__).resolve().parent
def strip(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)
lib = strip((r / "vendor/fm1pkg.js").read_text()) + "\n" + strip((r / "vendor/fm1ota.js").read_text())
html = (r / "admin.html").read_text()
assert html.count("/*LIB*/") == 1
out = r.parent / "build/web"; out.mkdir(parents=True, exist_ok=True)
(out / "oneclick-admin.html").write_text(html.replace("/*LIB*/", lib))
print(out / "oneclick-admin.html")
