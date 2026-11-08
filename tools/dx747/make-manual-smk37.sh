#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# Build the SMK-37 PDF manual, docs/DX747-SMK37-Manual.pdf, from docs/manual-smk37/manual.md and manual.css: tools/dx747/md-to-html.mjs makes one
# self-contained HTML page (the screenshots from docs/screens-smk37 are embedded) and Google Chrome prints it to A4. Needs node and Chrome (or
# Chromium); set CHROME to the browser's binary if it is not found. No pandoc is needed.
#   tools/dx747/make-manual-smk37.sh
# The screenshots are the SMK-37 build's own screens, taken by the native simulator from a recipe; to take them again (needs `make dx747-sim-smk37`
# in tools/dx747/sim/native):
#   tools/dx747/sim/native/dx747-sim-smk37 docs/screens-smk37 tools/dx747/sim/recipes-smk37-manual/screens.txt
# docs/screens-smk37/panel.png is a picture of the workbench page's SMK-37 simulator panel (./build.sh --web --target smk37-pro), not of a unit.
set -euo pipefail
cd "$(dirname "$0")/../.."
command -v node >/dev/null || { echo "error: node is required" >&2; exit 1; }
CHROME="${CHROME:-}"
if [ -z "$CHROME" ]; then
  for c in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" google-chrome google-chrome-stable chromium chromium-browser; do
    if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then CHROME="$c"; break; fi
  done
fi
[ -n "$CHROME" ] || { echo "error: no Chrome or Chromium found (set CHROME)" >&2; exit 1; }
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
node tools/dx747/md-to-html.mjs docs/manual-smk37/manual.md docs/manual-smk37/manual.css "$tmp/manual.html"
"$CHROME" --headless=new --disable-gpu --no-pdf-header-footer --print-to-pdf="$PWD/docs/DX747-SMK37-Manual.pdf" --generate-pdf-document-outline --export-tagged-pdf "file://$tmp/manual.html" >/dev/null 2>&1
echo "wrote docs/DX747-SMK37-Manual.pdf ($(wc -c < docs/DX747-SMK37-Manual.pdf | tr -d ' ') bytes)"
