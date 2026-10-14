#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""web/lxr-web-drums.html with web/lxr-drums.js inlined -> build/web/lxr-web-drums.html"""
import pathlib
r = pathlib.Path(__file__).resolve().parent
out = r.parent / "build/web"; out.mkdir(parents=True, exist_ok=True)
page = (r / "lxr-web-drums.html").read_text().replace("/* @lxr-module */", (r / "lxr-drums.js").read_text())
(out / "lxr-web-drums.html").write_text(page)
print(out / "lxr-web-drums.html")
