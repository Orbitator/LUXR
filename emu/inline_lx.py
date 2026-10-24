#!/usr/bin/env python3
# emu.html with the wasm inlined (base64) where it says @WASM@: one file, nothing to serve.
#   inline.py emu.html oneclick.wasm OUT_DIR  ->  OUT_DIR/luxr-emu.html (a whole page, opens from disk)
#                                              OUT_DIR/luxr-emu.body.html (without the document skeleton)
import base64, os, sys
page, wasm, out = sys.argv[1:4]
preset = sys.argv[4] if len(sys.argv) > 4 else None
body = open(page, encoding="utf-8").read().replace("@WASM@", base64.b64encode(open(wasm, "rb").read()).decode()).replace("@PRESET@", base64.b64encode(open(preset, "rb").read()).decode() if preset else "")
open(os.path.join(out, "luxr-emu.body.html"), "w", encoding="utf-8").write(body)
open(os.path.join(out, "luxr-emu.html"), "w", encoding="utf-8").write(
    '<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
    '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
    "<style>:root{color-scheme:light}body{margin:0}</style>\n</head>\n<body>\n" + body + "\n</body>\n</html>\n")
