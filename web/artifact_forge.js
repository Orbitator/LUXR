/* the embedded firmware: shown, loaded into the installer, offered as a file */
(() => {
  const css = document.createElement("style");
  css.textContent = `.btnlink{display:inline-flex;align-items:center;font:600 14px var(--body);color:var(--ink);background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:9px 16px;min-height:42px;text-decoration:none}
  .btnlink.go{background:var(--accent);color:var(--on-accent);border-color:transparent}.btnlink:focus-visible{outline:2px solid var(--accent);outline-offset:2px}`;
  document.head.appendChild(css);
  const bin = atob(EMBED.b64), fw = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) fw[i] = bin.charCodeAt(i);
  $("fBuild").textContent = EMBED.date + " UTC";
  $("fCommit").textContent = EMBED.commit.slice(0, 10);
  $("fPkg").textContent = `${productOf(fw)} · ${fw.length.toLocaleString("de-DE")} Bytes`;
  $("fSha").textContent = EMBED.sha256;
  sha256hex(fw).then((h) => { if (h !== EMBED.sha256) $("fSha").textContent += " (stimmt nicht!)"; });
  usePkg(fw, `oneclick.fwsc · Build ${EMBED.date} (${EMBED.commit.slice(0, 7)})`);
  $("pkgSrc").textContent = "Die Firmware aus Schritt 1 ist schon geladen. Eine andere Datei kann trotzdem gewählt werden.";

  /* Web MIDI: the artifact frame may refuse it; then the standalone page takes over */
  const blocked = () => { $("midiBlocked").hidden = false; };
  if (!navigator.requestMIDIAccess) blocked();
  else if (navigator.permissions) navigator.permissions.query({ name: "midi", sysex: true }).then((p) => { if (p.state === "denied") blocked(); }).catch(() => {});
  new MutationObserver(() => { if (/MIDI|Web MIDI/.test($("status").textContent) && /nicht erlaubt|Kein Web MIDI/.test($("status").textContent)) blocked(); })
    .observe($("status"), { childList: true, characterData: true, subtree: true });

  /* the file: .fwsc directly, a zip around it when the viewer refuses the extension */
  const crcT = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc32 = (d) => { let c = ~0; for (const b of d) c = crcT[(c ^ b) & 255] ^ (c >>> 8); return ~c >>> 0; };
  function zip(name, data) {
    const n = new TextEncoder().encode(name), c = crc32(data), le = (v, k) => Array.from({ length: k }, (_, i) => (v >>> (8 * i)) & 255);
    const loc = [0x50, 0x4B, 3, 4, 20, 0, 0, 0, 0, 0, 0, 0, 0x21, 0, ...le(c, 4), ...le(data.length, 4), ...le(data.length, 4), ...le(n.length, 2), 0, 0, ...n];
    const cen = [0x50, 0x4B, 1, 2, 20, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0x21, 0, ...le(c, 4), ...le(data.length, 4), ...le(data.length, 4), ...le(n.length, 2), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ...n];
    const off = loc.length + data.length;
    const end = [0x50, 0x4B, 5, 6, 0, 0, 0, 0, 1, 0, 1, 0, ...le(cen.length, 4), ...le(off, 4), 0, 0];
    return new Blob([new Uint8Array(loc), data, new Uint8Array(cen), new Uint8Array(end)], { type: "application/zip" });
  }
  let dl = null;
  $("fSave").disabled = true;
  (window.claude?.use ? window.claude.use("downloads") : Promise.resolve(null)).then((d) => { dl = d; $("fSave").disabled = !d; if (!d) $("fSave").title = "Speichern ist in dieser Ansicht nicht verfügbar"; });
  $("fSave").onclick = async () => {
    if (!dl) return;
    try { await dl.save({ filename: "oneclick.fwsc", data: fw }); $("fNote").textContent = "Gespeichert."; }
    catch (e) {
      if (e && e.code === "rejected_extension") {
        try { await dl.save({ filename: "oneclick-fwsc.zip", data: zip("oneclick.fwsc", fw) }); $("fNote").textContent = "Als ZIP gespeichert (darin oneclick.fwsc)."; }
        catch (e2) { $("fNote").textContent = "Nicht gespeichert: " + (e2.message || e2.code); }
      } else if (!e || e.code !== "declined") $("fNote").textContent = "Nicht gespeichert: " + ((e && (e.message || e.code)) || e);
    }
  };
})();
