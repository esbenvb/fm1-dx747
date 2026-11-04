#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Assemble the workbench page with the firmware inside (build/official-web/FM1-Official-Open-Source.html).

A port, for machines without PowerShell, of tools/Build-Web.ps1 and of the package-JSON step of
sdk/Build.ps1 -Package. It compiles nothing itself: it takes the firmware package (make_fwsc.py), the
build identity (build-identity.json) and the browser core (simulator.wasm, from tools/dx747/sim/build.sh)
and fills sdk/Update.html.in with them, exactly as Build-Web.ps1 does.

  make_page.py --root SRC --fwsc FILE.fwsc --identity build-identity.json --wasm simulator.wasm \\
               --source-url https://.../archive/<commit>.zip -o OUTDIR

--root is the source tree the firmware and the wasm were built from (a clean export of the commit, or
the repository). OUTDIR gets FM1-Official-Open-Source.html and SHA256SUMS.txt. tools/dx747/mac-build.sh
runs the whole chain. Standard library only.
"""
import base64
import hashlib
import json
import os
import re
import sys
import urllib.parse


def sha_file(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def read_text(path):
    return open(path, "rb").read().decode("utf-8-sig")


def html_encode(text):
    """System.Net.WebUtility.HtmlEncode: & < > " ' and U+00A0..U+00FF, and non-BMP characters, numerically."""
    out = []
    for ch in text:
        c = ord(ch)
        if ch == "&": out.append("&amp;")
        elif ch == "<": out.append("&lt;")
        elif ch == ">": out.append("&gt;")
        elif ch == '"': out.append("&quot;")
        elif ch == "'": out.append("&#39;")
        elif 160 <= c <= 255 or c >= 0x10000: out.append("&#%d;" % c)
        else: out.append(ch)
    return "".join(out)


def source_hash(root):
    """The hash Build-Web.ps1 gives the browser sources (and records in the page)."""
    inputs = {}
    for top in ("app", "web", os.path.join("third_party", "synth")):
        for folder, _dirs, files in os.walk(os.path.join(root, top)):
            for name in files:
                rel = os.path.relpath(os.path.join(folder, name), root).replace(os.sep, "/")
                if not rel.startswith("app/board/") and os.path.splitext(name)[1] in (".c", ".h", ".inc"):
                    inputs[rel] = sha_file(os.path.join(folder, name))
    for rel in ["tools/Build-Web.ps1"] + ["sdk/include/" + n for n in os.listdir(os.path.join(root, "sdk", "include")) if n.endswith(".h")]:
        inputs[rel] = sha_file(os.path.join(root, rel))
    names = sorted(inputs)
    return hashlib.sha256("\n".join(n + ":" + inputs[n] for n in names).encode("utf-8")).hexdigest()


def compact(obj):
    return json.dumps(obj, separators=(",", ":"))


def main(argv):
    opts = {}
    it = iter(argv)
    for a in it:
        if a in ("--root", "--fwsc", "--identity", "--wasm", "--source-url", "-o", "--profile"):
            opts[a] = next(it)
        else:
            print(__doc__)
            return 2
    for need in ("--root", "--fwsc", "--identity", "--wasm", "--source-url", "-o"):
        if need not in opts:
            print(__doc__)
            return 2
    root = os.path.abspath(opts["--root"])
    source_url = opts["--source-url"]
    parsed = urllib.parse.urlparse(source_url)
    if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password or re.search(r"[\r\n]", source_url):
        raise SystemExit("FM1_SOURCE_URL must be an HTTPS source URL without credentials.")
    version_h = read_text(os.path.join(root, "app", "version.h"))
    m = re.search(r'^#define FM1_RELEASE_VERSION "(V(?:0[1-9]|[1-9][0-9]{1,2}))"[ \t]*$', version_h, re.M)
    p = re.search(r'^#define FM1_UPDATE_PRODUCT "(FM-1_8[0-9]{6})"[ \t]*$', version_h, re.M)
    if not m or not p:
        raise SystemExit("Set FM1_RELEASE_VERSION and a compatible FM1_UPDATE_PRODUCT in app/version.h.")
    release, product = m.group(1), p.group(1)
    identity = json.load(open(opts["--identity"]))
    build_id = identity["build_id"]
    if not re.fullmatch(r"[0-9a-f]{64}", build_id) or identity["release_version"] != release:
        raise SystemExit("Firmware and web release versions differ, or the source identifier is invalid.")

    # The firmware package and the original V15 recovery firmware, as Build.ps1 -Package embeds them.
    fwsc = open(opts["--fwsc"], "rb").read()
    stock_path = os.path.join(root, "sdk", "firmware", "FM-1_015.fwsc")
    stock_hash = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
    if not os.path.exists(stock_path) or sha_file(stock_path) != stock_hash:
        raise SystemExit("Matching SDK original V15 recovery firmware is missing or invalid.")
    stock_bytes = open(stock_path, "rb").read()
    stock = {"schema": 1, "kind": "stock", "product": "FM-1_015", "profile": "M-VAVE Original V15", "family": 7,
             "release_version": "V15", "build_id": stock_hash, "sha256": stock_hash, "bytes": len(stock_bytes),
             "loader_sha256": "ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541",
             "data": base64.b64encode(stock_bytes).decode()}
    package = {"schema": 1, "product": product, "profile": identity.get("profile", opts.get("--profile", "dx7")), "family": 7,
               "release_version": release, "build_id": build_id, "sha256": hashlib.sha256(fwsc).hexdigest(), "bytes": len(fwsc),
               "loader_sha256": sha_file(os.path.join(root, "sdk", "tools", "pack", "ota.bin")), "hardware_validated": False,
               "data": base64.b64encode(fwsc).decode(), "stock": stock}
    package_json = compact(package)

    wasm = open(opts["--wasm"], "rb").read()
    simulation = compact({"schema": 1, "release_version": release, "sha256": hashlib.sha256(wasm).hexdigest(),
                          "source_sha256": source_hash(root), "sample_bank": "synthesized-demo",
                          "data": base64.b64encode(wasm).decode()})

    notices = ["APP: GPLv3 with the FM1 SDK linking exception; no warranty. SDK and firmware resources have separate "
               "redistribution terms in THIRD_PARTY.md. Source: " + source_url,
               "FM-1 V15 recovery firmware: https://www.m-vave.com/download (PC Firmware / FM-1 V15). Distributed by M-VAVE "
               "under Apache-2.0. Retain applicable licenses and notices; components with separate licenses retain their terms. "
               "See THIRD_PARTY.md."]
    for name in ("THIRD_PARTY.md", "LICENSE.md"):
        text = read_text(os.path.join(root, name))
        if name == "THIRD_PARTY.md" and not text.startswith("# Third-party notices"):
            raise SystemExit("Use the public distribution notices.")
        notices.append("\n===== %s =====\n" % name + text)

    web = read_text(os.path.join(root, "sdk", "Update.html.in"))
    replacements = {
        "/*FM1_SOURCE_URL*/": html_encode(source_url),
        "/*FM1_PAYLOAD*/": "const FM1_PACKAGE = " + package_json + ";",
        "/*FM1_SIMULATOR*/": "const FM1_SIMULATOR = " + simulation + ";",
        "/*FM1_I18N*/": read_text(os.path.join(root, "sdk", "I18n.js")),
        "/*FM1_SCRIPT*/": read_text(os.path.join(root, "sdk", "WebUpdate.js")),
        "/*FM1_SIM_SCRIPT*/": read_text(os.path.join(root, "sdk", "Simulator.js")),
        "/*FM1_SAMPLE_SCRIPT*/": read_text(os.path.join(root, "sdk", "Samples.js")),
        "/*FM1_NOTICES*/": html_encode("\n".join(notices)),
    }
    for key, value in replacements.items():
        web = web.replace(key, value)
    if re.search(r"/\*FM1_[A-Z_]+\*/", web):
        raise SystemExit("Unexpanded HTML placeholder.")
    outdir = opts["-o"]
    os.makedirs(outdir, exist_ok=True)
    page = os.path.join(outdir, "FM1-Official-Open-Source.html")
    for stale in (page, os.path.join(outdir, "SHA256SUMS.txt")):
        if os.path.exists(stale):
            os.remove(stale)
    open(page, "wb").write(web.encode("utf-8"))
    digest = sha_file(page)
    open(os.path.join(outdir, "SHA256SUMS.txt"), "wb").write(("%s  %s\n" % (digest, os.path.basename(page))).encode("utf-8"))
    print("Integrated FM1 workbench: " + page)
    print("sha256 " + digest + "   (firmware " + hashlib.sha256(fwsc).hexdigest()[:16] + "..., simulator " + hashlib.sha256(wasm).hexdigest()[:16] + "...)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
