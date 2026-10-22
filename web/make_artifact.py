#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The admin page as a claude.ai artifact, with a built firmware inside:
   web/make_artifact.py FWSC BUILD.json -> build/web/fm1-firmware-artifact.html"""
import base64, json, re, sys, pathlib
r = pathlib.Path(__file__).resolve().parent
fw = pathlib.Path(sys.argv[1]).read_bytes()
info = json.loads(pathlib.Path(sys.argv[2]).read_text())
html = (r.parent / "build/web/oneclick-admin.html").read_text()
title = "<title>FM-1 Firmware-Werkstatt</title>\n<meta name=\"claude:viewport-fit-sides\" content=\"cover\">"
links = "\n".join(re.findall(r"<link [^>]+>", html))
style = re.search(r"<style>.*?</style>", html, re.S).group(0)
body = re.search(r"<body>(.*)</body>", html, re.S).group(1)
forge = (r / "artifact_forge.html").read_text()
body = body.replace('  <section aria-labelledby="h-dev">', forge + '\n  <section aria-labelledby="h-dev">', 1)
body = body.replace("<h1>ONE CLICK <span>Admin</span></h1>", "<h1>FM-1 <span>Firmware-Werkstatt</span></h1>", 1)
body = body.replace("Firmware-Update und Wartung für den M-VAVE FM-1 über USB. Läuft in Chrome oder Edge auf diesem Computer; die Dateien bleiben im Browser.",
  "ONE CLICK als Firmware für den M-VAVE FM-1: gebaut auf GitHub, hier geprüft und per USB-MIDI auf das Gerät übertragen.", 1)
for i, n in (("1 · Gerät", "2 · Gerät"), ("2 · Firmware installieren", "3 · Übertragen"), ("3 · Samples", "4 · Samples"), ("4 · Zurück", "5 · Zurück")):
    body = body.replace(i, n, 1)
embed = "<script>const EMBED = " + json.dumps({**info, "size": len(fw), "b64": base64.b64encode(fw).decode()}) + ";</script>\n"
tail = "<script>\n" + (r / "artifact_forge.js").read_text() + "</script>\n"
out = r.parent / "build/web/fm1-firmware-artifact.html"
out.write_text(title + "\n" + links + "\n" + style + "\n" + embed + body + tail)
print(out, out.stat().st_size)
