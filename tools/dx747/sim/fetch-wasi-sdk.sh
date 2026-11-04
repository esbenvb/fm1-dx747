#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# One-time fetch of WASI SDK 27 for Apple Silicon (113,649,205 bytes) from the
# WebAssembly/wasi-sdk GitHub release into ~/.cache/dx747, verified against the
# SHA-256 GitHub publishes for the asset, then extracted into its own directory.
# Downloads only with --download; without it, it just verifies what is already there.
#
#   tools/dx747/sim/fetch-wasi-sdk.sh [--download]
set -euo pipefail

CACHE="${DX747_CACHE:-$HOME/.cache/dx747}"
name="wasi-sdk-27.0-arm64-macos"
url="https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-27/$name.tar.gz"
want="055c3dc2766772c38e71a05d353e35c322c7b2c6458a36a26a836f9808a550f8"
archive="$CACHE/dl-wasi-sdk/$name.tar.gz"
target="$CACHE/wasi-sdk"

if [ ! -f "$archive" ]; then
  [ "${1:-}" = "--download" ] || { echo "missing $archive; re-run with --download to fetch $url" >&2; exit 1; }
  mkdir -p "$(dirname "$archive")"
  curl -fL --retry 2 -o "$archive" "$url"
fi
got="$(shasum -a 256 "$archive" | cut -d' ' -f1)"
[ "$got" = "$want" ] || { echo "error: $archive sha256 $got != pinned $want" >&2; exit 1; }
if [ ! -x "$target/$name/bin/clang" ]; then
  [ -z "$(ls -A "$target" 2>/dev/null)" ] || { echo "error: $target exists and is not an extracted $name" >&2; exit 1; }
  mkdir -p "$target"
  tar -xzf "$archive" -C "$target"
fi
echo "$target/$name"
