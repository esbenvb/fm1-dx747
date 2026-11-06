#!/bin/bash
# Build the native simulator and run every recipe, saving screenshots to OUTDIR
# (default docs/screens). Exit status is non-zero if any assertion fails.
cd "$(dirname "$0")/../../.." || exit 1
tools/dx747/sim/native/ensure-built.sh || exit 1
out="${1:-docs/screens}"
mkdir -p "$out"
status=0
for r in tools/dx747/sim/recipes/*.txt; do
    echo "== $(basename "$r")"
    tools/dx747/sim/native/dx747-sim "$out" "$r" > /dev/null || status=1
done
# The SMK-37 flavour of the simulator (DX_SMK37, targets/smk37/app/smk37_controls.c): its own recipes, screenshots not kept.
make -B -s -C tools/dx747/sim/native dx747-sim-smk37 || status=1
smk_out="$(mktemp -d)"
for r in tools/dx747/sim/recipes-smk37/*.txt; do
    echo "== smk37 $(basename "$r")"
    tools/dx747/sim/native/dx747-sim-smk37 "$smk_out" "$r" > /dev/null || status=1
done
rm -rf "$smk_out"
# The real workbench page code (sdk/WebUpdate.js) against the real firmware: library tab, ROM import, voices.
if command -v node >/dev/null 2>&1; then
    echo "== page-flow.mjs"
    node tools/dx747/sim/page-flow.mjs > /dev/null || status=1
fi
# The DX747 workbench's own client (web/page/) against the firmware compiled to WebAssembly, through the page's virtual MIDI port.
if command -v node >/dev/null 2>&1; then
    echo "== tools/dx747/page/test-client.mjs"
    node tools/dx747/page/test-client.mjs > /dev/null || status=1
    echo "== tools/dx747/page/test-controls.mjs"
    node tools/dx747/page/test-controls.mjs > /dev/null || status=1
    # The SMK-37 panel's tables always; the firmware behind them when ./build.sh --web --target smk37-pro has built the browser core.
    echo "== tools/dx747/page/test-smk37.mjs"
    node tools/dx747/page/test-smk37.mjs || status=1
fi
exit $status
