#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# Build the DX747 firmware package and the flashing page on a Mac (or Linux) without Windows:
#   1. the firmware, with JieLi's Linux toolchain in Docker, and the SDK's checks (tools/dx747/docker)
#   2. the firmware package, .fwsc (tools/dx747/compare/make_fwsc.py, a port of sdk/Package.cs)
#   3. the browser core, simulator.wasm, with the macOS WASI SDK (tools/dx747/sim)
#   4. the workbench page with the firmware inside (tools/dx747/compare/make_page.py, a port of Build-Web.ps1)
#
#   tools/dx747/mac-build.sh [-o OUT_DIR] [--worktree] [--source-url URL]
#
# By default it builds a clean export of HEAD (what is committed, nothing else), into OUT_DIR
# (default build/dx747/mac). --worktree builds the working tree as it is. The page's source address
# defaults to the GitHub archive of the exact commit, made from the origin remote (push the commit first,
# or pass --source-url). One-time setup: tools/dx747/docker/fetch-toolchain.sh --download and
# tools/dx747/sim/fetch-wasi-sdk.sh --download; Docker must be running; Node and Python 3 are needed.
# The supported route for what you flash is still the Windows build (docs/BUILD-WINDOWS.md); the results
# were compared byte for byte for the firmware package.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$REPO/build/dx747/mac"; WORKTREE=0; URL=""
while [ $# -gt 0 ]; do
  case "$1" in
    -o) OUT="$(mkdir -p "$2" && cd "$2" && pwd)"; shift 2 ;;
    --worktree) WORKTREE=1; shift ;;
    --source-url) URL="$2"; shift 2 ;;
    *) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
  esac
done
mkdir -p "$OUT"
cd "$REPO"
COMMIT="$(git rev-parse HEAD)"
if [ "$WORKTREE" = 1 ]; then
  SRC="$REPO"; echo "warning: building the working tree, not the commit" >&2
  [ -z "$(git status --porcelain)" ] || echo "warning: the working tree has uncommitted changes" >&2
else
  SRC="$OUT/src"; rm -rf "$SRC"; mkdir -p "$SRC"
  git archive "$COMMIT" | tar -x -C "$SRC"
fi
if [ -z "$URL" ]; then
  remote="$(git remote get-url origin | sed -E 's/\.git$//; s#^.*[:/]([^:/]+/[^:/]+)$#\1#')"
  URL="https://github.com/$remote/archive/$COMMIT.zip"
fi
echo "== commit $COMMIT"; echo "== source address $URL"
echo "== 1/4 firmware (Docker)"
"$REPO/tools/dx747/docker/build.sh" -o "$OUT/firmware" "$SRC"
echo "== 2/4 firmware package"
python3 "$REPO/tools/dx747/compare/make_fwsc.py" "$OUT/firmware/sdk.elf" "$OUT/FM1_OFFICIAL_OPEN.fwsc"
echo "== 3/4 browser core"
"$REPO/tools/dx747/sim/build.sh" -o "$OUT/simulator" "$SRC" | tail -1
echo "== 4/4 flashing page"
python3 "$REPO/tools/dx747/compare/make_page.py" --root "$SRC" --fwsc "$OUT/FM1_OFFICIAL_OPEN.fwsc" \
  --identity "$OUT/firmware/build-identity.json" --wasm "$OUT/simulator/simulator.wasm" --source-url "$URL" -o "$OUT/official-web"
echo "== done: $OUT/official-web/FM1-Official-Open-Source.html"
