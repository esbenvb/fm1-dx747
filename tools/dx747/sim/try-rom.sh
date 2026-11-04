#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# Try a standard 32-voice DX7 ROM (.syx, 4104 bytes) in the native simulator: it is imported the way the
# workbench does it (transfer commands 3/4/5/6), read back and compared, then every voice is selected on
# engine 1, played for 0.6 s, and its screen and peak output level are recorded.
#
#   tools/dx747/sim/try-rom.sh ROM.syx [GROUP 0-3] [OUTDIR]
#
# GROUP is the library group the ROM goes into (programs 32*GROUP+1 ..), default 3 (D). OUTDIR defaults to
# build/dx747/try-rom (gitignored) and receives one screenshot per voice. The ROM file is only read.
# A slow-attack voice can show a low peak after 0.6 s. The engine keeps voices with amplitude-modulation
# sensitivity (the "BC" voices) silent until a controller message arrives; DX747 sends one (CC2 = 127)
# after every voice load and every stop, so they sound here. A silent BC voice would be a regression.
set -euo pipefail
[ $# -ge 1 ] || { sed -n '4,11p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
cd "$(dirname "$0")/../../.."
ROM="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
GROUP="${2:-3}"
OUT="${3:-build/dx747/try-rom}"
[ -f "$ROM" ] || { echo "error: $ROM not found" >&2; exit 1; }
tools/dx747/sim/native/ensure-built.sh
mkdir -p "$OUT"
python3 - "$ROM" "$GROUP" "$OUT" <<'PY'
import subprocess, sys
rom, group, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
data = open(rom, "rb").read()
if len(data) != 4104:
    sys.exit(f"error: {rom} is {len(data)} bytes; a standard 32-voice DX7 bank is 4104")
names = [data[6 + v * 128 + 118:6 + v * 128 + 128].decode("ascii", "replace").rstrip() for v in range(32)]
script = ["boot", "wipe", "run 400", f"bank_load {group} {rom}", "run 200",
          "tap btn 8", "expect_screen 2", "white 1", "expect_screen 4", f"enc 2 {group * 32}", "run 100"]
for v in range(32):
    script += [f"shot voice{v + 1:02d}", "midi 1 90 3c 64", "expect_peak 600 0", "midi 1 80 3c 00",
               "midi 1 b0 78 00", "run 200", "enc 2 1", "run 60"]
path = f"{out}/script.txt"
open(path, "w").write("\n".join(script) + "\n")
r = subprocess.run(["tools/dx747/sim/native/dx747-sim", out, path], capture_output=True, text=True)
sys.stderr.write(r.stderr)
peaks = [float(l.split()[1]) for l in r.stdout.splitlines() if l.startswith("peak ")]
if r.returncode or len(peaks) != 32:
    sys.exit(f"import or playback failed (exit {r.returncode}, {len(peaks)} of 32 voices measured)")
print(f"{rom}: imported into group {'ABCD'[group]}; library read-back and all 32 names verified")
for v in range(32):
    if peaks[v] >= 0.001: note = ""
    elif names[v].endswith(" BC") or " BC " in names[v] + " ": note = "   SILENT: a BC voice should sound (CC2 = 127 is sent after the load)"
    else: note = "   quiet after 0.6 s (slow attack?)"
    print(f"  program {group * 32 + v + 1:3d}  {names[v]:<10s}  peak {peaks[v]:.4f}{note}")
print(f"screenshots: {out}/voice01.png .. voice32.png")
PY
