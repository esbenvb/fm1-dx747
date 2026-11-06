#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# Build the PDF manual, docs/DX747-Manual.pdf, from docs/manual/manual.md and manual.css: pandoc makes one self-contained HTML page
# (the screenshots from docs/screens are embedded), and Google Chrome prints it to A4. Needs pandoc and Chrome (or Chromium);
# set CHROME to the browser's binary if it is not found.
#   tools/dx747/make-manual.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
command -v pandoc >/dev/null || { echo "error: pandoc is required (https://pandoc.org)" >&2; exit 1; }
CHROME="${CHROME:-}"
if [ -z "$CHROME" ]; then
  for c in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" google-chrome google-chrome-stable chromium chromium-browser; do
    if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then CHROME="$c"; break; fi
  done
fi
[ -n "$CHROME" ] || { echo "error: no Chrome or Chromium found (set CHROME)" >&2; exit 1; }
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
( cd docs/manual && pandoc manual.md --from markdown+implicit_figures+pipe_tables+fenced_divs --to html5 --standalone --embed-resources \
    --css manual.css --metadata pagetitle="DX747 manual" -o "$tmp/manual.html" )
"$CHROME" --headless=new --disable-gpu --no-pdf-header-footer --print-to-pdf="$PWD/docs/DX747-Manual.pdf" --generate-pdf-document-outline --export-tagged-pdf "file://$tmp/manual.html" >/dev/null 2>&1
echo "wrote docs/DX747-Manual.pdf ($(wc -c < docs/DX747-Manual.pdf | tr -d ' ') bytes)"
