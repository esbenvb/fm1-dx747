#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# Build the FM1 WASM simulator locally. Shell port of Build-Simulator in
# tools/Build-Web.ps1: same sources, include paths, flags and link options,
# but a macOS/Linux WASI SDK 27 instead of the Windows one, and only the
# .wasm (no HTML page).
#
#   tools/dx747/sim/build.sh [-o OUT_DIR] SRC_DIR
#
# SRC_DIR is any tree laid out like the SDK repo: a pristine `git archive`
# export for the baseline, or the working tree later. It is only read.
# OUT_DIR defaults to <repo>/build/dx747/sim/<basename SRC_DIR> (gitignored).
# Prints the path of simulator.wasm on the last line.
#
# Environment:
#   FM1_WASI_SDK  extracted wasi-sdk-27.0-* directory
#                 (default ~/.cache/dx747/wasi-sdk/wasi-sdk-27.0-arm64-macos)
#
# Nothing is downloaded here; see tools/dx747/sim/README.md for the one-time fetch.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
WASI="${FM1_WASI_SDK:-$HOME/.cache/dx747/wasi-sdk/wasi-sdk-27.0-arm64-macos}"
OUT=""

usage() { sed -n '4,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }
while getopts "o:h" opt; do
  case "$opt" in
    o) OUT="$OPTARG" ;;
    *) usage ;;
  esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || usage

SRC="$(cd "$1" && pwd)"
[ -f "$SRC/web/engine/Simulator.c" ] || { echo "error: $SRC has no web/engine/Simulator.c (not an fm1_open tree)" >&2; exit 1; }
CLANG="$WASI/bin/clang"
[ -x "$CLANG" ] && [ -d "$WASI/share/wasi-sysroot" ] || { echo "error: set FM1_WASI_SDK to an extracted WASI SDK 27 directory (looked in $WASI)" >&2; exit 1; }
case "$("$CLANG" --version | head -1)" in
  *wasi-sdk*) ;;
  *) echo "error: $CLANG is not a WASI SDK clang" >&2; exit 1 ;;
esac

OUT="${OUT:-$REPO/build/dx747/sim/$(basename "$SRC")}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
WASM="$OUT/simulator.wasm"
META="$OUT/simulator.json"

# Sources, like Build-Web.ps1: every .c under app/ (except app/board/, which is
# hardware only), web/ and third_party/synth/. Ordinal order.
cd "$SRC"
SOURCES=()
while IFS= read -r f; do SOURCES+=("$f"); done < <(
  { find app web third_party/synth -type f -name '*.c' ! -path 'app/board/*'; } | LC_ALL=C sort)

# Cache key: every .c/.h/.inc the build can see, the SDK headers, this script
# (it carries the flags) and the compiler identity.
source_hash="$(
  {
    find app web third_party/synth sdk/include -type f \( -name '*.c' -o -name '*.h' -o -name '*.inc' \) ! -path 'app/board/*'
  } | LC_ALL=C sort | while IFS= read -r f; do printf '%s:%s\n' "$f" "$(shasum -a 256 "$f" | cut -d' ' -f1)"; done |
    { cat; shasum -a 256 "$HERE/build.sh" | cut -d' ' -f1; "$CLANG" --version | head -1; } | shasum -a 256 | cut -d' ' -f1
)"

if [ -f "$WASM" ] && [ -f "$META" ] && grep -q "\"source_sha256\": \"$source_hash\"" "$META" &&
  grep -q "\"wasm_sha256\": \"$(shasum -a 256 "$WASM" | cut -d' ' -f1)\"" "$META"; then
  echo "up to date (source $source_hash)" >&2
  echo "$WASM"
  exit 0
fi
rm -f "$WASM" "$META"

options=(--sysroot="$WASI/share/wasi-sysroot"
  -std=c11 -O2 -fwrapv -fno-fast-math -ffp-contract=off
  -DFM1_HOST_TEST -DFM1_OPEN_WEB -Wall -Wextra -Werror
  -Wno-unused-function -Wno-array-parameter -mexec-model=reactor)
for dir in web/engine web/synth third_party/synth sdk/include app/synth app; do options+=("-I$SRC/$dir"); done
for f in "${SOURCES[@]}"; do options+=("$SRC/$f"); done
options+=(-Wl,--strip-all -Wl,-z,stack-size=131072 -Wl,--initial-memory=4194304 -Wl,--max-memory=33554432 -o "$WASM")

echo "Compile the browser application (${#SOURCES[@]} sources, $("$CLANG" --version | head -1 | cut -d' ' -f3))" >&2
if ! "$CLANG" "${options[@]}"; then rm -f "$WASM"; exit 1; fi

printf '{\n  "schema": 1,\n  "source_sha256": "%s",\n  "wasm_sha256": "%s",\n  "wasm_bytes": %s,\n  "sources": %s,\n  "compiler": "%s"\n}\n' \
  "$source_hash" "$(shasum -a 256 "$WASM" | cut -d' ' -f1)" "$(wc -c <"$WASM" | tr -d ' ')" "${#SOURCES[@]}" \
  "$("$CLANG" --version | head -1)" >"$META"
echo "$WASM"
