#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# Render docs/screens/boot-static-logo.png, the picture the README shows: the boot screen with the plane at rest. The boot screen is a
# five second animation (recipe 19 shoots that), so the picture is made from a scratch copy of the tree with the animation switched
# off; nothing in the repository changes except the PNG.
#   tools/dx747/sim/render-boot-static.sh [OUT.png]
set -euo pipefail
cd "$(dirname "$0")/../../.."
out="${1:-docs/screens/boot-static-logo.png}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
git ls-files -z --cached --others --exclude-standard --deduplicate -- . ':!.claude' | tar --null -T - -cf - | tar -xf - -C "$tmp"
python3 - "$tmp/app/dx747/ui_screens.inc" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "    {   /* the run: the plane rolls in"
if s.count(old) != 1:
    sys.exit("render-boot-static.sh: the animation block is not where it was expected in ui_screens.inc")
open(p, "w").write(s.replace(old, "    if (0) {   /* the run: the plane rolls in"))
PY
make -B -s -C "$tmp/tools/dx747/sim/native"
printf 'boot logo\nrun 100\nexpect_screen 0\nshot static\n' > "$tmp/static.txt"
"$tmp/tools/dx747/sim/native/dx747-sim" "$tmp" "$tmp/static.txt" > /dev/null
cp "$tmp/static.png" "$out"
echo "wrote $out"
