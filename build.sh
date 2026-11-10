#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# build.sh is the macOS and Linux counterpart of build.bat: the same three builds, the same output folders,
# without Windows. It needs Docker and Node 18+. See docs/BUILDING.md.
#
#   ./build.sh                       firmware: build, run the SDK's checks, make build/dx7/generated/FM1_DX747.fwsc
#   ./build.sh --web                 browser preview: build/preview/index.html and DX747-Workbench.html
#   ./build.sh --web --target smk37-pro   the same page with the SMK-37's panel: build/smk37-pro/preview/DX747-Workbench.html
#   ./build.sh --package             firmware and the complete workbench pages: build/official-web/
#   ./build.sh --setup               one-time downloads (JieLi's Linux toolchain, 26 MB; WASI SDK 27, 114-125 MB)
#
#   --source-url URL    the HTTPS address of the matching source archive (--package; or set FM1_SOURCE_URL),
#                       for example "https://github.com/<you>/fm1-dx747/archive/$(git rev-parse HEAD).zip"
#   --from-git REF      build a clean export of a commit instead of the working tree, in build/git-<commit>/
#   --label TEXT        mark the workbench pages as a preview build: a banner with TEXT, a preview title, noindex (tools/dx747/workbench.mjs)
#   --switch JSON       the Device menu of the workbench pages, e.g. '{"self":"fm1","fm1":"./","smk37-pro":"./smk37-pro/"}' (DX_PAGES=1 is the two-page default)
#
#   ./build.sh --package --target smk37-pro   (or smk37-elite): the firmware and an SMK-37 workbench page that can install it, build/smk37-pro/official-web/
#
# DX747 is a fork of FM1_OFFICIAL_OPEN and has its own file names: the SDK steps write the upstream names (FM1_OFFICIAL_OPEN.fwsc,
# FM1-Official-Open-Source.html) and this script leaves a copy of each as FM1_DX747.fwsc and FM1-DX747.html; the DX747 workbench
# page (tools/dx747/workbench.mjs) is DX747-Workbench.html. The Windows build (build.bat, sdk/Build.ps1) stays the reference.
# What the checks and the comparison with it cover is in docs/BUILDING.md.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE=firmware
TARGET=fm1
REF=""
URL="${FM1_SOURCE_URL:-}"
LABEL=""
SWITCH_JSON=""

usage() { sed -n '4,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }
while [ $# -gt 0 ]; do
  case "$1" in
    --web) MODE=web; shift ;;
    --package) MODE=package; shift ;;
    --setup) MODE=setup; shift ;;
    --source-url) URL="${2:?--source-url needs a value}"; shift 2 ;;
    --from-git) REF="${2:?--from-git needs a commit}"; shift 2 ;;
    --label) LABEL="${2:?--label needs a text}"; shift 2 ;;
    --switch) SWITCH_JSON="${2:?--switch needs JSON}"; shift 2 ;;
    --target) TARGET="${2:?--target needs fm1, smk37-pro or smk37-elite}"; shift 2 ;;
    -h | --help) usage; exit 0 ;;
    *) echo "Use ./build.sh, ./build.sh --web, ./build.sh --package or ./build.sh --setup (./build.sh --help)." >&2; exit 2 ;;
  esac
done

command -v node >/dev/null || { echo "error: Node 18 or newer is required (https://nodejs.org)" >&2; exit 1; }

if [ "$MODE" = setup ]; then
  "$HERE/tools/docker/fetch-toolchain.sh" --download
  "$HERE/tools/portable/fetch-wasi-sdk.sh" --download
  exit 0
fi

if [ "$MODE" = package ] && [ -z "$URL" ]; then
  echo "error: set FM1_SOURCE_URL or pass --source-url with the HTTPS address of the matching source archive, e.g." >&2
  echo "  ./build.sh --package --source-url \"https://github.com/<you>/fm1-dx747/archive/\$(git rev-parse HEAD).zip\"" >&2
  exit 1
fi

if [ -n "$REF" ]; then
  COMMIT="$(git -C "$HERE" rev-parse --verify "$REF^{commit}")"
  BUILD="$HERE/build/git-${COMMIT:0:12}"
  SRC="$BUILD/src"
  rm -rf "$SRC"; mkdir -p "$SRC"
  git -C "$HERE" archive "$COMMIT" | tar -x -C "$SRC"
  echo "== building commit $COMMIT"
else
  BUILD="$HERE/build"
  SRC="$HERE"
fi

# Another device: DX747's source tree with that device's SDK, board layer and application files laid over it (tools/dx747/target.mjs,
# targets/<device>/). The FM-1's SDK and sealed engine never go into it (THIRD_PARTY.md: FM-1 devices only).
case "$TARGET" in
  fm1) ;;
  smk37-pro | smk37-elite)
    [ -z "$REF" ] || { echo "error: --from-git is not supported with --target $TARGET" >&2; exit 2; }
    [ "$MODE" = firmware ] || [ "$MODE" = web ] || [ "$MODE" = package ] || { echo "error: for --target $TARGET only the firmware, the browser preview and the page with the firmware are built (./build.sh --target $TARGET [--web | --package])" >&2; exit 2; }
    BUILD="$HERE/build/$TARGET"; SRC="$BUILD/src"
    export FM1_TARGET="$TARGET"
    node "$HERE/tools/dx747/target.mjs" --target "$TARGET" --out "$SRC" ;;
  *) echo "error: unknown target $TARGET: use fm1, smk37-pro or smk37-elite" >&2; exit 2 ;;
esac

# A failed build must not leave the previous build's DX747-named files looking like the result of this one.
rm -f "$BUILD"/dx7/generated/SMK37_DX747_*.fwsc "$BUILD/dx7/generated/FM1_DX747.fwsc" "$BUILD/official-web/FM1-DX747.html" "$BUILD/official-web/DX747-Workbench.html" "$BUILD/preview/DX747-Workbench.html"

PACKAGE=FM1_DX747.fwsc
[ "$TARGET" = fm1 ] || PACKAGE="SMK37_DX747_$(echo "${TARGET#smk37-}" | tr a-z A-Z).fwsc"

firmware() {
  echo "== firmware (JieLi Linux toolchain in Docker)"
  "$HERE/tools/docker/build.sh" -o "$BUILD/dx7" "$SRC"
  mkdir -p "$BUILD/dx7/generated"
  if [ "$TARGET" = fm1 ]; then
    node "$HERE/tools/portable/fwsc.mjs" "$BUILD/dx7/sdk.elf" "$BUILD/dx7/generated/FM1_OFFICIAL_OPEN.fwsc" --root "$SRC"
    cp "$BUILD/dx7/generated/FM1_OFFICIAL_OPEN.fwsc" "$BUILD/dx7/generated/FM1_DX747.fwsc"
  else
    node "$HERE/tools/portable/fwsc.mjs" "$BUILD/dx7/sdk.elf" "$BUILD/dx7/generated/$PACKAGE" --root "$SRC"
  fi
}

# The GitHub Pages site (.github/workflows/pages.yml sets DX_PAGES=1) has the FM-1 page at its root and the SMK-37 page in smk37/; each page gets
# a device menu at the top that goes to the other one.
SWITCH=()
if [ -n "$SWITCH_JSON" ]; then SWITCH=(--switch "$SWITCH_JSON")
elif [ -n "${DX_PAGES:-}" ]; then
  if [ "$TARGET" = fm1 ]; then SWITCH=(--switch '{"self":"fm1","fm1":"./","smk37":"./smk37/"}')
  else SWITCH=(--switch '{"self":"smk37","fm1":"../","smk37":"./"}'); fi
fi
[ -z "$LABEL" ] || SWITCH+=(--label "$LABEL")      # (the label rides with the switch: both only go to workbench.mjs)

case "$MODE" in
  firmware)
    firmware
    echo "== done: $BUILD/dx7/generated/$PACKAGE" ;;
  web)
    echo "== browser core"
    "$HERE/tools/portable/browser-core.sh" -o "$BUILD/web" "$SRC" >/dev/null
    if [ "$TARGET" = fm1 ]; then
      node "$HERE/tools/portable/page.mjs" --root "$SRC" --wasm "$BUILD/web/simulator.wasm" --preview -o "$BUILD/preview"
    fi                                        # (the stock page is the FM-1's: the SMK-37 target has only the DX747 workbench page)
    node "$HERE/tools/dx747/workbench.mjs" --root "$SRC" --wasm "$BUILD/web/simulator.wasm" --preview -o "$BUILD/preview" ${SWITCH[@]+"${SWITCH[@]}"}
    if [ "$TARGET" = fm1 ]; then echo "== done: $BUILD/preview/index.html and $BUILD/preview/DX747-Workbench.html"
    else echo "== done: $BUILD/preview/DX747-Workbench.html (the SMK-37 panel)"; fi ;;
  package)
    firmware
    echo "== browser core"
    "$HERE/tools/portable/browser-core.sh" -o "$BUILD/web" "$SRC" >/dev/null
    echo "== workbench page"
    if [ "$TARGET" = fm1 ]; then
      node "$HERE/tools/portable/page.mjs" --root "$SRC" --fwsc "$BUILD/dx7/generated/FM1_OFFICIAL_OPEN.fwsc" \
        --identity "$BUILD/dx7/build-identity.json" --wasm "$BUILD/web/simulator.wasm" --source-url "$URL" -o "$BUILD/official-web"
      node "$HERE/tools/dx747/workbench.mjs" --root "$SRC" --fwsc "$BUILD/dx7/generated/FM1_OFFICIAL_OPEN.fwsc" \
        --identity "$BUILD/dx7/build-identity.json" --wasm "$BUILD/web/simulator.wasm" --source-url "$URL" -o "$BUILD/official-web" ${SWITCH[@]+"${SWITCH[@]}"}
      cp "$BUILD/official-web/FM1-Official-Open-Source.html" "$BUILD/official-web/FM1-DX747.html"
      ( cd "$BUILD/official-web" && node -e 'const c=require("crypto"),f=require("fs");for(const n of process.argv.slice(1))console.log(c.createHash("sha256").update(f.readFileSync(n)).digest("hex")+"  "+n)' FM1-DX747.html DX747-Workbench.html >> SHA256SUMS.txt )
      echo "== done: $BUILD/official-web/DX747-Workbench.html and FM1-DX747.html"
    else
      # The SMK-37 page: DX747's workbench only (the stock page is the FM-1's). One page, one model: the firmware of $TARGET and M-VAVE's original V16 for it.
      node "$HERE/tools/dx747/workbench.mjs" --root "$SRC" --fwsc "$BUILD/dx7/generated/$PACKAGE" --model "${TARGET#smk37-}" \
        --identity "$BUILD/dx7/build-identity.json" --wasm "$BUILD/web/simulator.wasm" --source-url "$URL" -o "$BUILD/official-web" ${SWITCH[@]+"${SWITCH[@]}"}
      ( cd "$BUILD/official-web" && node -e 'const c=require("crypto"),f=require("fs");for(const n of process.argv.slice(1))console.log(c.createHash("sha256").update(f.readFileSync(n)).digest("hex")+"  "+n)' DX747-Workbench.html > SHA256SUMS.txt )
      echo "== done: $BUILD/official-web/DX747-Workbench.html (the SMK-37 ${TARGET#smk37-} page with its firmware inside)"
    fi ;;
esac
