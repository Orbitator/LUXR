#!/usr/bin/env python3
"""ONE CLICK emulator page in headless Chromium: it boots, plays, ONE CLICK changes the song, the companion protocol answers,
the keys play, and the page has no console errors. Screenshots into build/emu/.  page_test.py build/emu/oneclick-emu.html"""
import sys, time
from pathlib import Path
from playwright.sync_api import sync_playwright

page_path = Path(sys.argv[1]).resolve()
fails = []
with sync_playwright() as p:
    b = p.chromium.launch(args=["--autoplay-policy=no-user-gesture-required"])
    pg = b.new_page(viewport={"width": 1100, "height": 1250})
    errors = []
    pg.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
    pg.on("pageerror", lambda e: errors.append(str(e)))
    pg.goto((sys.argv[2] if len(sys.argv) > 2 else page_path.as_uri()))
    pg.wait_for_timeout(600)
    song0 = pg.inner_text("#sSong")
    pg.click("#power")
    pg.wait_for_function("document.querySelector('#sAudio').innerText.includes('läuft')", timeout=8000)
    pg.keyboard.press("Space")                       # PLAY
    pg.wait_for_timeout(1500)
    if "gestoppt" in pg.inner_text("#sStep"): fails.append("PLAY did not start")
    if not pg.eval_on_selector('button[aria-label="PLAY: Start/Stop"]', "e => e.classList.contains('lit')"): fails.append("PLAY led")
    before = pg.inner_text("#sSong") + pg.inner_text("#sTracks")
    changed = False
    for _ in range(4):                               # ONE CLICK: REC held (Enter) (a new song can repeat a key by chance)
        pg.keyboard.press("Enter")
        pg.wait_for_timeout(900)
        if pg.inner_text("#sSong") + pg.inner_text("#sTracks") != before:
            changed = True
            break
    if not changed: fails.append("ONE CLICK changed nothing")
    pg.keyboard.press("r"); pg.wait_for_timeout(400)  # REC tapped: armed, live
    if "Overdub, live" not in pg.inner_text("#sRec"): fails.append("REC tap: " + pg.inner_text("#sRec"))
    pg.keyboard.press("r"); pg.wait_for_timeout(400)
    if pg.inner_text("#sRec") != "aus": fails.append("REC off: " + pg.inner_text("#sRec"))
    pg.keyboard.down("Shift"); pg.wait_for_timeout(100); pg.keyboard.press("a"); pg.wait_for_timeout(200); pg.keyboard.up("Shift")
    pg.wait_for_timeout(400)                         # SHIFT + C4: solo track 1
    if pg.inner_text("#sTracks").count("(stumm)") != 3: fails.append("SHIFT + C4 solo: " + pg.inner_text("#sTracks"))
    pg.keyboard.down("Shift"); pg.wait_for_timeout(100); pg.keyboard.press("a"); pg.wait_for_timeout(200); pg.keyboard.up("Shift")
    pg.wait_for_timeout(400)
    if "(stumm)" in pg.inner_text("#sTracks"): fails.append("solo off: " + pg.inner_text("#sTracks"))
    with pg.expect_download(timeout=5000) as dl:     # MIDI export from the dump
        pg.click("#xMidi")
    mid = Path(dl.value.path()).read_bytes()
    if mid[:4] != b"MThd" or mid.count(b"MTrk") != 5: fails.append(f"MIDI file ({len(mid)} B)")
    pg.click("#uDump")
    pg.wait_for_timeout(500)
    if "Dump" not in pg.inner_text("#log") or "Version 1" not in pg.inner_text("#log"): fails.append("no dump answer")
    pg.keyboard.down("a"); pg.wait_for_timeout(300)
    if not pg.eval_on_selector('.key[data-k="7"]', "e => e.classList.contains('on')"): fails.append("key A not shown")
    pg.keyboard.up("a")
    lit = pg.evaluate("""() => { const c = document.getElementById('lcd').getContext('2d').getImageData(0,0,240,240).data;
                                  let n = 0; for (let i = 0; i < c.length; i += 4) n += (c[i] + c[i+1] + c[i+2]) > 120; return n; }""")
    if lit < 3000: fails.append(f"screen nearly dark ({lit} px)")
    pg.screenshot(path="build/emu/page_desktop.png", full_page=True)
    pg.set_viewport_size({"width": 400, "height": 900})
    pg.wait_for_timeout(300)
    if pg.evaluate("document.documentElement.scrollWidth") > 400: fails.append("horizontal scroll at 400 px")
    pg.screenshot(path="build/emu/page_phone.png", full_page=True)
    real = [e for e in errors if "fonts.g" not in e and "ERR_TUNNEL" not in e]   # (Google Fonts: blocked in the test sandbox)
    if real: fails.append("console: " + " | ".join(real[:3]))
    print("song before / after:", song0, "/", pg.inner_text("#sSong"))
    b.close()
print("page_test:", "FAIL " + "; ".join(fails) if fails else "ok")
sys.exit(1 if fails else 0)
