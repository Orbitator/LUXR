#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LUXR for Ableton Move (Schwung): the documents the host plans and looks up with, checked against each other.
  python3 schwung/tests/check_docs.py build/schwung/out schwung/src/module.json
Every key a page offers must resolve in chain_params the way the Schwung host resolves it (a key itself, or a sound's
sN_<key> through the child template module.json declares): a key that does not resolve makes the host re-read and
re-parse the whole list on the audio thread every 250 ms while it is automated or modulated. And the list stays under
the host's cap of 256."""
import json, re, sys

out, modpath = sys.argv[1], sys.argv[2]
hier = json.load(open(f"{out}/ui_hierarchy.json"))
cp = json.load(open(f"{out}/chain_params.json"))
mod = json.load(open(modpath))
fails = []
declared = {p["key"] for p in cp}
if len(cp) > 256:
    fails.append(f"chain_params: {len(cp)} entries (cap 256)")
tmpls = [(l["child_key_template"], l.get("child_index_base", 0), l["child_count"])
         for l in mod["capabilities"].get("ui_hierarchy", {}).get("levels", {}).values() if "child_key_template" in l]

def resolves(key):                     # as chain_params.c find_param_by_key: the key, else its template's base key
    if key in declared:
        return True
    for t, base, n in tmpls:
        rx = "^" + re.escape(t).replace(r"\{index\}", r"(\d+)").replace(r"\{key\}", r"(.+)") + "$"
        m = re.match(rx, key)
        if m and base <= int(m.group(1)) < base + n and m.group(2) in declared:
            return True
    return False

keys = set()
for name, lvl in hier["levels"].items():
    own = [p["key"] for p in lvl.get("params", []) if isinstance(p, dict) and "key" in p]
    own += [k for k in lvl.get("knobs", []) if isinstance(k, str)]
    if "child_key_template" in lvl:
        for i in range(lvl["child_count"]):
            for k in own:
                keys.add(lvl["child_key_template"].replace("{index}", str(i + lvl.get("child_index_base", 0))).replace("{key}", k))
    else:
        keys.update(own)
        for sel in ("list_param", "count_param", "name_param"):
            keys.discard(lvl.get(sel))
missing = sorted(k for k in keys if not resolves(k))
if missing:
    fails.append(f"{len(missing)} keys do not resolve in chain_params: {missing[:12]}")
if not tmpls:
    fails.append("module.json declares no child template (the host cannot map s1_..s8_)")
for f in fails:
    print("FAIL", f)
print(f"check_docs: {len(keys)} keys on the pages, {len(cp)} in chain_params, " + ("ok" if not fails else f"{len(fails)} failed"))
sys.exit(1 if fails else 0)
