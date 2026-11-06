# DX747 for the M-VAVE SMK-37 (Pro and Elite)

This folder is the SMK-37 target. The shared DX747 application (`app/`, the router, the parts, the scenes, the configuration protocol,
the web configurator) is built for the SMK-37 with the pieces here instead of the FM-1's:

| Folder | What it is | Where it comes from |
|---|---|---|
| `sdk/` | the SDK and sealed engine library for the SMK-37 | `m-vave/smk37_open` at commit `b2d6bcbcd724adb13bcf85e44cfa16bb2c8153a8`, `sdk/` copied byte for byte |
| `board/` | the scanner for keys, pads, faders and encoders, the lights, the display, the audio pins | the same repository, `app/board/` (and `smk37_controls.h` from `app/`), unmodified |
| `app/` | DX747's own files for this target: `version.h` (identity `SMK-37 Pro_8747747` / `SMK-37 Elite_8747747`) and `smk37_controls.c` (the controls layer) | this project |
| `reference-smk37_controls.c.txt` | M-VAVE's stock controls file, kept to read, not built | the same repository, `app/smk37_controls.c` |

**The FM-1 SDK and the SMK-37 SDK are never mixed.** The FM-1 engine library is licensed for FM-1 devices only (`THIRD_PARTY.md`);
the one in `sdk/` here is licensed for SMK-37 Pro and Elite (`THIRD_PARTY.smk37_open.md`). `tools/dx747/target.mjs` assembles a
build tree for one target out of the repository without the other target's SDK; `./build.sh --target smk37-pro` (or `smk37-elite`)
builds it with the same Docker toolchain and gates as the FM-1 firmware.

**Not run on an SMK-37 yet.** No unit has been available to this project. Everything here builds and passes the size, symbol and
identity gates and the host and simulator tests of the shared code; how the board behaves (pins, fader range, pad pressure, flash)
is taken from smk37_open and is unverified. `docs/ON-DEVICE-CHECKLIST-SMK37.md` lists what must be checked on a unit before anyone installs it.
