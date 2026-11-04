# WASM simulator tooling (M0)

Local build of the browser simulator's WebAssembly core and a dependency-free Node harness that drives it.
(`native/`, `recipes/` and `run-recipes.sh` here belong to the native C harness; this is the WASM side.)

```bash
tools/dx747/sim/fetch-wasi-sdk.sh --download   # once: wasi-sdk-27.0-arm64-macos.tar.gz, 113,649,205 B, sha256-pinned
tools/dx747/sim/build.sh [-o OUT_DIR] SRC_DIR  # prints OUT_DIR/simulator.wasm
node tools/dx747/sim/shots.mjs --wasm OUT_DIR/simulator.wasm --out docs/screens/baseline [--scale 2]
```

## build.sh

Shell port of `Build-Simulator` in `tools/Build-Web.ps1`: every `.c` under `app/` (except `app/board/`), `web/` and
`third_party/synth/`, includes `web/engine web/synth third_party/synth sdk/include app/synth app`, flags
`-std=c11 -O2 -fwrapv -fno-fast-math -ffp-contract=off -DFM1_HOST_TEST -DFM1_OPEN_WEB -Wall -Wextra -Werror
-Wno-unused-function -Wno-array-parameter -mexec-model=reactor`, link `--strip-all`, 128 KiB stack, 4 MiB initial and
32 MiB max memory. It only reads `SRC_DIR` (a `git archive` export, or the working tree), caches on a hash of
every input, and writes `simulator.wasm` plus `simulator.json`. Baseline: 32 sources, 295,723 bytes, 5 s, `-Werror` clean
with clang 20.1.8 (WASI SDK 27).

## shots.mjs

`import { Sim } from './shots.mjs'` (Node ≥ 18, no packages). The module instantiates the reactor with no imports,
calls `_initialize()`, and exposes:

| | |
|---|---|
| time | `advance(ms)` renders 128-frame blocks (2.9025 ms); the firmware polls every 10 ms and paints every 33 ms of simulated time, so nothing moves until you advance |
| input | `press(name, {holdMs, settleMs})`, `buttonDown/Up`, `tapKey(i)`, `whiteKey(1..16)`, `encoder(name\|0..6, detents)`, `knob(0..1023)`, `open(page)` |
| names | buttons `OCT- OCT+ FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC` (indices 0..13); white keys `0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26`; encoders `SELECT=0 ALGO=1 K1..K4=2..5 PRESETS=6` (inferred from `app/screen.c`) |
| read | `pixels()` raw 240×240 RGB565, `rgb()`, `png(path, {scale})`, `state()`, `page()`, `knobs()` (the four K tiles' text), `leds()`, `midiIn(bytes, source)`, `midiCapture(true)` + `midiOut()` |
| flash | `flash()` live view of the 536,576-byte flash, `eraseFlash`, `loadFlash`, `sampleRegionBlank()` |

CLI: `--scenario file.mjs` runs `export default async (sim, shot) => {...}` instead of the built-in stock tour. Every
shot is also logged to `index.json` (page, track, K-tile text, lit LEDs, simulated time).

### Sample bank (`sim_boot(1)`)

`sim_boot(1)` erases flash, then copies the factory sample bank (76,436 bytes here) over flash offset 69,632, the
start of the region DX747 will use for scenes. `sim.bootClean()` (or `boot({clean: true})`) does the wipe the
DX747 work needs: `sim_boot(1)`, erase `69,632..536,576` through `sim_flash()`, then `sim_boot(0)`, which keeps
flash and re-runs initialisation so the samples/library/storage code sees the erased region. Verified:
`sampleRegionBlank()` is false after `boot({clear: true})` and true after `bootClean()`. The wipe also clears the
five final sectors (DX7 banks A-D and tuning), which is the state of a never-written device.

### Stock page ids (`PAGES`, `open()`)

`OVERVIEW 0, STEPS 1, FM_EDIT 2, FM_ENV 3, FM_LFO 4, TONE_EDIT 5, MIX 6, BLE 7, PROJECT 8 (not openable), NAME 9,
EFFECTS 10, SOURCE 11, ACTIONS 12, STEP_EDIT 13 and RHYTHM 14 (both show as STEPS), FM_QUICK 15, PRESET_BROWSER 16,
ARP_EDIT 17, STARTUP 18`. `sim_open(5)` on a DX7 track lands on OVERVIEW. The panel's SEL button is `SCL` in the
source (scaling page for DX7 tracks); GLO opens MIX.

The stock tour is deterministic (two runs are byte-identical) and boots clean before each shot, so no state leaks
between them.
