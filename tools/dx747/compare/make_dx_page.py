#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Assemble the DX747 workbench page (OUTDIR/DX747-Workbench.html): one file with the simulator, the configurator and the keyboard.

The stock FM-1 page is built from sdk/Update.html.in by make_page.py; sdk/ is pinned (sdk/manifest.json) and DX747 never edits it.
This page is built from web/page/ instead and reuses sdk/WebUpdate.js (the workbench protocol: framing, tokens, retries, the
voice library) unchanged.

  make_dx_page.py --root SRC --wasm simulator.wasm --source-url https://.../archive/<commit>.zip -o OUTDIR \\
                  [--fwsc FILE.fwsc --identity build-identity.json]

With --fwsc and --identity the page also carries the firmware (and the original V15 recovery firmware, from sdk/firmware) for its
Update tab, exactly as the stock page does. Without them the Update tab says the page has no firmware.

tools/dx747/mac-build.sh runs it after the browser core is built. Standard library only.
"""
import base64
import hashlib
import json
import os
import re
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_page as mp  # noqa: E402  (the stock page assembler: its helpers are reused)

SCRIPTS = [("/*DX_I18N*/", "sdk/I18n.js"), ("/*DX_WEBUPDATE*/", "sdk/WebUpdate.js"), ("/*DX_CORE*/", "web/page/dx747-core.js"), ("/*DX_DEVICE*/", "web/page/dx747-device.js"),
           ("/*DX_CLIENT*/", "web/page/dx747-client.js"), ("/*DX_CONTROLS*/", "web/page/dx747-controls.js"), ("/*DX_SIM*/", "web/page/dx747-sim.js"),
           ("/*DX_KEYBOARD*/", "web/page/dx747-keyboard.js"), ("/*DX_UI*/", "web/page/dx747-ui.js"),
           ("/*DX_UPDATE*/", "web/page/dx747-update.js")]


def build_package(root, fwsc_path, identity_path, release):
    """The firmware package and the original V15 recovery firmware as JSON, as Build.ps1 -Package embeds them (see make_page.py)."""
    version_h = mp.read_text(os.path.join(root, "app", "version.h"))
    p = re.search(r'^#define FM1_UPDATE_PRODUCT "(FM-1_8[0-9]{6})"[ \t]*$', version_h, re.M)
    if not p:
        raise SystemExit("Set FM1_UPDATE_PRODUCT in app/version.h.")
    identity = json.load(open(identity_path))
    build_id = identity["build_id"]
    if not re.fullmatch(r"[0-9a-f]{64}", build_id) or identity["release_version"] != release:
        raise SystemExit("Firmware and web release versions differ, or the source identifier is invalid.")
    fwsc = open(fwsc_path, "rb").read()
    stock_path = os.path.join(root, "sdk", "firmware", "FM-1_015.fwsc")
    stock_hash = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
    if not os.path.exists(stock_path) or mp.sha_file(stock_path) != stock_hash:
        raise SystemExit("Matching SDK original V15 recovery firmware is missing or invalid.")
    stock_bytes = open(stock_path, "rb").read()
    stock = {"schema": 1, "kind": "stock", "product": "FM-1_015", "profile": "M-VAVE Original V15", "family": 7,
             "release_version": "V15", "build_id": stock_hash, "sha256": stock_hash, "bytes": len(stock_bytes),
             "loader_sha256": "ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541",
             "data": base64.b64encode(stock_bytes).decode()}
    package = {"schema": 1, "product": p.group(1), "profile": identity.get("profile", "dx7"), "family": 7, "release_version": release,
               "build_id": build_id, "sha256": hashlib.sha256(fwsc).hexdigest(), "bytes": len(fwsc),
               "loader_sha256": mp.sha_file(os.path.join(root, "sdk", "tools", "pack", "ota.bin")), "hardware_validated": False,
               "data": base64.b64encode(fwsc).decode(), "stock": stock}
    return mp.compact(package)


def main(argv):
    opts = {}
    it = iter(argv)
    for a in it:
        if a in ("--root", "--wasm", "--source-url", "-o", "--fwsc", "--identity"):
            opts[a] = next(it)
        else:
            print(__doc__)
            return 2
    for need in ("--root", "--wasm", "--source-url", "-o"):
        if need not in opts:
            print(__doc__)
            return 2
    root = os.path.abspath(opts["--root"])
    url = opts["--source-url"]
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password or re.search(r"[\r\n]", url):
        raise SystemExit("The source URL must be an HTTPS URL without credentials.")
    wasm = open(opts["--wasm"], "rb").read()
    version_h = mp.read_text(os.path.join(root, "app", "version.h"))
    m = re.search(r'^#define FM1_RELEASE_VERSION "(V[0-9]{2,3})"', version_h, re.M)
    release = m.group(1) if m else "V00"
    simulation = mp.compact({"schema": 1, "release_version": release, "sha256": hashlib.sha256(wasm).hexdigest(),
                             "source_sha256": mp.source_hash(root), "data": base64.b64encode(wasm).decode()})
    notices = ["APP: GPLv3 with the FM1 SDK linking exception; no warranty. Source: " + url]
    for name in ("THIRD_PARTY.md", "LICENSE.md"):
        notices.append("\n===== %s =====\n" % name + mp.read_text(os.path.join(root, name)))
    page = mp.read_text(os.path.join(root, "web", "page", "dx747.html.in"))
    payload = "const FM1_PACKAGE = null;"
    if "--fwsc" in opts and "--identity" in opts:
        payload = "const FM1_PACKAGE = " + build_package(root, opts["--fwsc"], opts["--identity"], release) + ";"
    replacements = {"/*DX_PAYLOAD*/": payload, "/*DX_SOURCE_URL*/": mp.html_encode(url), "/*DX_NOTICES*/": mp.html_encode("\n".join(notices)),
                    "/*DX_SIMULATOR*/": "const DX_SIMULATOR = " + simulation + ";"}
    for key, path in SCRIPTS:
        text = mp.read_text(os.path.join(root, path))
        if re.search(r"</script", text, re.I):
            raise SystemExit(path + " contains a closing script tag")
        replacements[key] = text
    for key, value in replacements.items():
        page = page.replace(key, value)
    left = re.findall(r"/\*DX_[A-Z_]+\*/", page)
    if left:
        raise SystemExit("Unexpanded placeholder: " + left[0])
    outdir = opts["-o"]
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "DX747-Workbench.html")
    open(path, "wb").write(page.encode("utf-8"))
    print("DX747 workbench: " + path)
    print("sha256 " + mp.sha_file(path) + "   (%d bytes, simulator %s...)" % (os.path.getsize(path), hashlib.sha256(wasm).hexdigest()[:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
