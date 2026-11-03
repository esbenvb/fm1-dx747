#!/bin/bash
# Build the native simulator and run every recipe, saving screenshots to OUTDIR
# (default docs/screens). Exit status is non-zero if any assertion fails.
cd "$(dirname "$0")/../../.." || exit 1
make -s -C tools/dx747/sim/native || exit 1
out="${1:-docs/screens}"
mkdir -p "$out"
status=0
for r in tools/dx747/sim/recipes/*.txt; do
    echo "== $(basename "$r")"
    tools/dx747/sim/native/dx747-sim "$out" "$r" > /dev/null || status=1
done
exit $status
