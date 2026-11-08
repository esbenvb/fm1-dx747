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

## The controls, by the labels printed on the unit

DX747's screens, the workbench page and these documents call every control by what is printed on the SMK-37 (the drawing on the front page of its
manual): the buttons **KNOB BANK, FADER BANK, <, >, OCTAVE-, OCTAVE+** (left of the keys' pads) and **ARP, NOTE REPEAT, SCALE, CHORD, GLOBE, BT, PATCH,
PARA, FX, SEQ, SEQ PLAY, SEQ REC** (below the faders), the knobs **K1 to K8**, the faders **FADER1 to FADER4**, the pads, the keys, **PITCH** and **MOD**.
There is no SHIFT: every button that does something has a button of its own. (The FM-1's names for the same things, HOME, SAVE, ENV, LFO,
SELECT, ALGO, PRESETS, are used only on the FM-1.) The names, and which of the board's button numbers each one is, are in `app/smk37_controls.c`
(`button_label`, `button_map`, `encoder_map`); the screens read their tip lines from the same tables, so a change there changes what they say.

| Button | What it does |
|---|---|
| **KNOB BANK** | on Mix, K1 to K4 pan parts 1 to 4, or parts 5 to 8 (the lamp is lit for 5 to 8) |
| **FADER BANK** | where the faders are part levels, FADER1 to FADER4 are parts 1 to 4, or parts 5 to 8 (the lamp is lit for 5 to 8) |
| **<** and **>** | one step back and forward: the part on Parts, Mix and FX, the layer on Part, Details and the edit pages, the next stored scene on Scenes (it is recalled), the value of the row on System, the cursor while a scene is named |
| **OCTAVE-** and **OCTAVE+** | the keys one octave on Parts, Part, Mix and MIDI in; the previous or next page on the edit pages, FX and Master filter; the previous or next part on Details; 16 scenes on Scenes; the previous or next row on System. Pressed while SEQ PLAY is held: the previous or next scene, on any screen |
| **ARP** | LEARN: play the lowest and the highest key to set the key range of a layer (Part, Range, Details) |
| **NOTE REPEAT** | the Part screen: the layers of the selected part |
| **SCALE** | add a layer to the selected part (in an empty slot, a new part) on Parts, Part, Details, the edit pages and Mix |
| **CHORD** | remove the framed layer (the last layer takes the part with it), on the same screens: the left button adds and the right one removes, as ENV and LFO on the FM-1 |
| **GLOBE** | the System screen |
| **BT** | the MIDI in screen |
| **PATCH** | the Parts screen; pressed again, the selected part in Details. Held, then SEQ PLAY: all notes off. Cancels naming a scene |
| **PARA** | the edit pages (Sound, Range, Velocity, Sends), one page on at each press |
| **FX** | the shared reverb and delay; pressed again, the master filter |
| **SEQ** | the Mix screen |
| **SEQ PLAY** | the Scenes screen |
| **SEQ REC** | save the setup as a scene: it asks for a name (K1 or < and > move the cursor, K2 changes the letter), and SEQ REC again stores it |

The lamp of the button that opens the screen on view is lit. Power on with **SCALE and CHORD** held for safe boot.

| Knob | What it does |
|---|---|
| **K1** to **K4** | what the screen shows under them: the levels, range and so on; on Mix the pan of parts 1 to 4, or 5 to 8 after KNOB BANK |
| **K5** | the part (on Scenes the scene on the cursor, on System the row) |
| **K6** | the layer inside the part (on Mix the MIDI channel of the part) |
| **K7** | the voice of the framed layer (on System the value of the row) |
| **K8** | the master level |

| Fader | Parts | Master filter | Details and FX | Everywhere else |
|---|---|---|---|---|
| **FADER1** | filter cutoff | filter type | reverb decay | level of part 1 (5 after FADER BANK) |
| **FADER2** | filter resonance | filter cutoff | delay time | level of part 2 (6) |
| **FADER3** | filter type | filter resonance | delay feedback | level of part 3 (7) |
| **FADER4** | not used | not used | effects return | level of part 4 (8) |

A fader sets its thing as soon as it moves, wherever it was left: there is no pickup, so the level jumps to the fader's position. The first reading of each fader at power-on only says where it stands.

The 16 pads, by screen. **Mix:** the top row is the mute of parts 1 to 8 (dim red, bright red while muted), the lower row the solo (dim amber, bright amber while
soloed), dark where there is no part. **Part, Details and the edit pages:** the top row picks the layers of the selected part and the lower row the eight parts, in the colour of the part
(bright for the one framed or selected). **Everywhere else:** they recall the scenes of the bank on screen (OCTAVE- and OCTAVE+ change the bank on Scenes): dark where empty,
teal where stored, bright teal for the scene in use, white for the cursor; a pad on an empty slot says which button stores a scene (SEQ REC). The symbols printed under the lower row are not used.
The 37 keys play the selected part with the velocity they are struck with, **PITCH** and **MOD** bend and modulate it, the sustain pedal holds it.
The printed names are matched to the board's button numbers by position, from the manual's drawing and from M-VAVE's own simulator, which agree on
five of the 18; the first thing to check on a unit is that each button does what its row says (docs/ON-DEVICE-CHECKLIST-SMK37.md).
