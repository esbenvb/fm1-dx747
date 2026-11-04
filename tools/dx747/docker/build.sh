#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# Build the FM1 firmware (profile dx7, like `sdk/Build.ps1 -FirmwareOnly`) in a
# linux/amd64 container with JieLi's Linux toolchain, then run the Build.ps1
# symbol gates and the 453,921-byte image cap on the result.
#
#   tools/dx747/docker/build.sh [-o OUT_DIR] SRC_DIR
#
# SRC_DIR is any tree laid out like the SDK repo (sdk/, app/, third_party/...):
# a pristine `git archive` export for the baseline, or the working tree later.
# It is mounted read-only; nothing in it is written. OUT_DIR defaults to
# <repo>/build/dx747/docker/<basename SRC_DIR> (build/ is gitignored).
#
# Environment:
#   DX747_TOOLCHAIN  extracted jieli-linux-toolchains-* directory
#                    (default ~/.cache/dx747/jieli-toolchain/jieli-linux-toolchains-20260730.1)
#   DX747_IMAGE      image tag (default dx747-fm1-build)
#
# Nothing is downloaded here; see BASELINE.md for the one-time toolchain fetch.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
TOOLCHAIN="${DX747_TOOLCHAIN:-$HOME/.cache/dx747/jieli-toolchain/jieli-linux-toolchains-20260730.1}"
IMAGE="${DX747_IMAGE:-dx747-fm1-build}"
OUT=""

usage() { sed -n '4,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }
while getopts "o:h" opt; do
  case "$opt" in
    o) OUT="$OPTARG" ;;
    *) usage ;;
  esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || usage

SRC="$(cd "$1" && pwd)"
[ -f "$SRC/sdk/Build.ps1" ] && [ -f "$SRC/app/version.h" ] || { echo "error: $SRC does not look like an fm1_open tree" >&2; exit 1; }
[ -x "$TOOLCHAIN/common/bin/clang" ] || { echo "error: toolchain not found at $TOOLCHAIN (set DX747_TOOLCHAIN)" >&2; exit 1; }
command -v node >/dev/null || { echo "error: node is required for the gates" >&2; exit 1; }
docker info >/dev/null 2>&1 || { echo "error: the Docker daemon is not running (open Docker Desktop)" >&2; exit 1; }

OUT="${OUT:-$REPO/build/dx747/docker/$(basename "$SRC")}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
echo "source:    $SRC"
echo "toolchain: $TOOLCHAIN"
echo "output:    $OUT"

# Invalidate stale results first, like Build.ps1.
rm -f "$OUT/firmware-report.json" "$OUT/sdk.elf" "$OUT/sdk.map" "$OUT/nm.txt"

node "$HERE/gates.mjs" inputs "$SRC" "$TOOLCHAIN"
node "$HERE/gates.mjs" prepare "$SRC" "$OUT"

docker build --platform linux/amd64 --quiet -t "$IMAGE" -f "$HERE/Dockerfile" "$HERE" >/dev/null
start=$SECONDS
docker run --rm --platform linux/amd64 \
  --user "$(id -u):$(id -g)" \
  --ulimit nofile=8192:8192 \
  -v "$SRC:/src:ro" \
  -v "$OUT:/out" \
  -v "$TOOLCHAIN:/opt/jieli:ro" \
  -v "$HERE/container-build.sh:/opt/dx747/container-build.sh:ro" \
  "$IMAGE" 2>&1 | tee "$OUT/build.log"
echo "container build: $((SECONDS - start)) s"

node "$HERE/gates.mjs" firmware "$SRC" "$OUT" | tee "$OUT/gates.log"
