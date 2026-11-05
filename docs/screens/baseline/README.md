# Stock FM-1 screens (baseline)

The unmodified firmware at upstream `4c7496d` (V14), rendered by the WASM simulator built from a pristine
`git archive` export (`tools/portable/browser-core.sh`, wasm sha256 `a62ea3c1...3c9b80`), 240×240 shown at 2×.
Regenerate with `tools/dx747/sim/shots.mjs` (see `tools/dx747/sim/README.md`); the run is deterministic.

File names are `NN-<button>[-again]-<page>` (from a fresh boot: HOME, then that button once or twice),
`open-NN-<page>` (straight to a page through `sim_open`), and `00`..`02` (startup animation, boot overview).
`index.json` lists page, selected track, K-tile text, lit LEDs and simulated time for each shot.
Sample bank wiped (`bootClean`), so the DX7 tracks and the project list are in their never-written state.
