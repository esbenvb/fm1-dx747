# DX747 "Parts" model: design note

**Status: first build implemented in the simulator and host tests; not yet run on a unit.**
This note is the design. [DX747.md](../../DX747.md) and [MIDI.md](../MIDI.md) describe
the firmware as built. Differences from the design as first written:

- **Not built yet:** the user sound library (banks 1 and up, SAVE SOUND, scrolling a
  sound's library) and part names (the field is in the scene format, no editor). A
  program change after a bank select of 1 or more is ignored for now. Scenes already
  carry layered sounds.
- **Engines are not renumbered.** A layer keeps its engine until it is removed and a new
  layer takes the lowest free engine; a part's layer 1 is its lowest engine. Nothing is
  ever moved from one engine to another (that would mean exporting and importing a voice).
- **The router did change a little** (a velocity window, CC7 as the part level, bank
  select, program change per part, the scene channel, Song Select), but not its note
  logic or its pacing. The parts are mapped onto the router's own configuration.
- **Part level and layer level** are kept next to the engine configuration, in the
  control task's state (`struct dx_parts`, `app/dx747/parts.h`), not in the router.
- **A fresh unit has eight single-layer parts** on channels 1 to 8, as the engines were
  before, so a sequencer can use them at once. Layering needs a free engine.
- **Extra controls:** ENV and LFO add and remove layers (and mute and solo on Mix, and
  step scenes on Scenes); PLAY held with OCT- or OCT+ steps scenes anywhere.
- **Panel changes after the first try (2026-11-05):** the chips show the *part number*
  (the MIDI channel is written `CH n`, so the two are never confused); OCT- and OCT+ step
  pages, rows, banks and parts wherever a screen has them (the keyboard octave only on
  Parts, Part and MIDI in); HOME a second time shows the part in detail (channel, part
  level, a line per layer over a keyboard ruler); every knob and message shows an overlay
  box; Mix works on whole parts (pan and sends move all layers together, ALGO sets the
  channel) and EDIT there turns the white keys into mute and solo keys with lamp levels.

## Why

Today DX747 has 8 engines. Each listens on a set of MIDI channels with a note window and a
shift (`struct dx_engine_cfg` in `app/dx747/route.h`). That is flexible but hard to reason
about:

- A layered channel is several engines, and a program change on that channel overwrites
  *all* of them with one voice (`app/dx747/route.inc`, the program-change path).
- The home screen's white keys select things instead of playing
  (`on_white_key` in `app/dx747/ui_input.inc`).
- Scenes cannot be recalled over MIDI.
- A sequencer cannot tell the unit "play this sound on track 3" without also knowing which
  engines make up that track.

The proposal is a new layer above the router: the **part**, one MIDI channel with one
sound, that may itself be made of several layers. The router core and the engine pool stay
as they are.

## 1. Use cases it must serve

1. **Live keyboard:** one controller, a single or layered sound, splits, octave shifts, a
   scene per song.
2. **Sequencer-driven:** one track per channel (Circuit Tracks MIDI tracks, a Raspberry Pi
   sequencer, a DAW), each track one sound or a layer stack.
3. **More sounds from few tracks:** one track split by note range into several sounds
   ("kit mode"), layers, octave shifts.
4. **Several controllers merged** (USB, Bluetooth, DIN) on different channels.
5. **DAW recording:** sounds recalled by bank select and program change at song start; one
   stereo mix out.
6. **Setlist:** step through scenes from the panel (and over MIDI) while playing.
7. **Audition and sound design** with the internal keyboard.

## 2. Vocabulary

| Word | Meaning | Visible to the user |
|---|---|---|
| Scene | everything: 8 parts, effects, master filter, which part is selected | yes, 64 of them (4 banks of 16) |
| Part | one MIDI channel with one sound; mute; one **part level** (follows CC7) that scales all its layers | yes: the unit you think in |
| Sound | 1 to 8 layers plus a name; a single DX7 voice is a one-layer sound | yes |
| Layer | one DX7 voice with level (balance inside the part), pan, BRIGHT, sends, key range, velocity range, octave shift | on the part's page |
| Engine | the sealed engine's track; a layer needs one | only as the counter `ENGINES n/8` |
| Voice | one sounding note on an engine; 16 are shared | only as the counter `VOICES n/16` |

Limits are shown, not hidden. **Engines** are shared by all layers of all parts (8 in
total). **Voices:** a note on an n-layer part uses n voices. The sound editor shows
"voices per note = n" and the header shows both counters.

There is no per-part cap on layers: the pool of 8 engines is the only limit.

## 3. Data model

- **Layer** (stored as a copy): the 223-byte preset (voice, mono/ADSR/LFO controls, volume,
  program, sends) plus `lo`, `hi`, `shift`, `fold`, `pan`, `expr`, `bright`, `poly`, and a
  velocity range `vlo`/`vhi` (default 1-127). `rx` and `ctl` of today's engine config
  disappear: the layer inherits the part's channel. Sends and pan stay per layer, so a layer
  can sit in a different place in the mix.
- **Part:**
  - `channel`: 1-16, unique among parts; the scene channel (if on) is excluded.
  - `mute`.
  - `level`: one byte that scales all layers; the layers' own levels remain the balance
    inside the part.
  - The sound: layers 1 to n.
  - `name`: 12 characters, only needed when n > 1 (a single voice shows its own
    10-character voice name).
  - `cursor`: library bank and index the sound came from, so the user can scroll on from
    there.
  - `modified` flag.
  - `selected layer`: the SysEx target (section 4).
- **Library:** bank 0 is the 128 DX7 voices (the existing four-bank voice library); banks 1
  and up hold user sounds (layered), 128 per bank.
- **Scene:** name, selected part, effects, master filter and 8 part records. It stores
  **copies** of all sounds, their names and their cursors, so editing the library never
  changes an old scene.
- **Device settings** (not part of a scene, kept in the settings sector, survive power
  cycles): the **scene channel** (Off or 1-16, default Off), the last scene, plus today's
  backlight, radio, role and master channel. `struct dx_settings` gets a `scene_ch` field.
- **Engine allocation is derived, never stored.** Layers are assigned engines in order
  (part 1 layer 1, and so on). The router's `struct dx_cfg` is rebuilt from the parts on
  every change, so `route.h` / `route.inc`, the held-note table, pacing and the voice
  budget are untouched.
- **Scene storage: one scene per 4 KB flash sector.** Today a scene is 1,916 bytes
  (`DX_SCENE_BYTES`) in a 2,028-byte slot (`DX_SLOT_BYTES`), two per sector, which is too
  tight for names, cursors, velocity ranges and part levels. With a whole sector per scene
  there is room for all of it and for future fields, the slot logic gets simpler (no
  pairing of two scenes in one erase unit), and a power cut while saving a scene can never
  touch its neighbour.
  - **Cost:** 64 scenes plus the settings sector is **65 sectors (260 KB)**, up from 33
    (132 KB). `DX_STORE_SECTORS` goes from 33 to 65, and the first-save claim erases more
    sectors, a few seconds longer, once. The staging image must fit 4 KB (a compile-time
    assert, as for the current format).
  - **Where it sits:** the store starts at flash offset 69,632 (`DX_STORE_BASE`, where the
    stock sample bank begins). 65 sectors end at offset 335,872. The sample bank runs to
    536,576, and its last five sectors (four DX7 banks and the tuning journal) start at
    516,096 (`GROOVE_LIBRARY_OFFSET` in `app/instrument.h`). So the bigger store stays well
    inside the region DX747 already takes over from the sample bank and does not touch the
    voice banks, the tuning journal or the stock projects (offsets 0 to 69,631). To be
    re-checked against `scene.inc` / `scene_port.inc` when the format is written.
- **Compatibility:** nothing has shipped, so the scene sector version is bumped and old
  images read as empty. No migration.

## 4. MIDI rules

| Message | Behaviour |
|---|---|
| Note on/off on a part's channel | every layer whose key and velocity ranges match plays it, shifted and folded as today |
| CC1, 2, 4, 10, 11, 64, 91, 120, 121, pitch bend, pressure | to all layers of the part |
| CC7 (volume) | sets the **part level**; the layers' own levels stay as their balance |
| CC74 / CC71 | BRIGHT of all layers of the part (offsets keep their per-layer differences) |
| Bank select (CC0), then program change, on a part's channel | selects the part's **sound**: bank 0 is DX7 voice n+1 (one layer), bank 1 and up is a user sound. Only that part's engines stop; other parts keep sounding. |
| Program change without bank select | uses the last bank select seen on that channel, bank 0 by default |
| Program change on the **scene channel** (default **Off**; set on the System screen; remembered across power cycles) | recalls scene n+1 (0-63) |
| Song Select (`F3 nn`) | recalls scene n+1, **if** the sealed engine passes system messages to the app. Unverified: the SDK header only promises "complete short messages". Program change on the scene channel is the dependable route. |
| Program change on an unassigned channel | ignored (shown on the MIDI in screen) |
| Not enough free engines for a sound | the lowest layers are dropped and the part shows a warning; never a refusal that leaves the old sound half replaced |
| DX7 SysEx parameter change or voice dump, device n | edits the **selected layer** of the part on channel n+1 (chosen on the part page, layer 1 by default) |

A scene recall stops all notes (the engine cuts a part's notes on any voice change); the
shared reverb and delay tail continues.

## 5. Screens and controls

- **The internal keyboard always plays the selected part,** on every screen (layers,
  splits and shifts included). There is no separate "keyboard part" setting. The selected
  part is highlighted on both the Parts overview and the part page, so you always see what
  the keys will play. The SELECT encoder picks the part; holding a button and pressing a
  white key is an optional shortcut. No screen turns the keys into selectors any more (the
  Scenes screen may keep them as quick recall).
- **Parts (home, HOME):** eight rows of 25 px under a 25 px header, one per part, always
  the same layout: `[part chip] NAME [mini range bar] [+N]` and `CH n` under the name.
  - NAME is 10 characters, the same size on every row. It is the layer with the widest key
    range, ties going to layer 1. `+N` is the number of other layers (blank when 1).
  - The range bar shows splits side by side and layers stacked, in the part's colour. The
    row flashes on incoming notes, muted parts are dimmed, unused parts show their number
    only.
  - Header: scene name, `ENGINES n/8`, `VOICES n/16`.
  - Only the **selected** row may show a second line with the layer names in small text,
    so the other rows stay uniform and readable. Uppercase only (the 5x7 font); `+` exists.

  ```
   SCENE 03 ROCK DEMO        VOICES 7/16
   PARTS                     ENGINES 6/8
   [1] E. PIANO 1  ==_======  +1
   [2] BASS 2      ====
   [3] STRINGS 1   ==========  +2
   [4] -
  ```
- **Part page (SEQ):** the layer list of the selected part (scroll with K1), channel,
  mute, level; add and remove a layer; the `ENGINES n/8` counter; LEARN (ARP) sets a
  layer's key range from two played keys, as today.
- **Layer editor (EDIT, cycles Sound / Range / Sends):** the existing Sound, Plays-from and
  Sends screens, per layer. Plays-from loses the 16-channel grid (the channel belongs to
  the part) and keeps low, high, shift, fold, plus the velocity range.
- **Sound library (PRESETS encoder, SAVE):** scroll the part's library from its cursor;
  PRESETS up and down step to the next and previous sound in the library the part's sound
  came from. "SAVE SOUND" stores the current layer stack as a user sound.
- **Scenes (PLAY):** unchanged in spirit: recall by key, OCT-/OCT+ banks, SAVE with a name;
  plus next and previous scene on two buttons for live use. Copy semantics as in section 3.
- **Mix, FX, Master, System, MIDI in** stay. Mix becomes per part (layers move together).
  MIDI in also logs program change, bank select and Song Select.

## 6. What changes in the code (for a later implementation plan)

| Area | Files | Change |
|---|---|---|
| Parts and allocation | new `app/dx747/parts.h/.inc` | part and layer types, allocation to engines, derive `dx_cfg`, host tests |
| Router | `route.h/.inc` | none for notes; program change maps to the part's sound (new mailbox slot per part), bank select latch, scene-channel program change and Song Select become one control-task request |
| Scenes | `scene.h/.inc`, `scene_port.inc` | new record (one per 4 KB sector), `DX_STORE_SECTORS` 33 to 65, version bump, capture and apply through parts, `scene_ch` in `struct dx_settings` |
| Control | `ctl.inc`, `project.c` | SysEx target is the selected layer; program change mailbox handling per part; scene recall request |
| UI | `ui_*.inc`, `screen.c` | Parts and Part pages, keys always play, library scroll, SAVE SOUND |
| Docs and tests | `DX747.md`, `docs/MIDI.md`, host tests, recipes | rewritten rules; new recipes for splits, program change and bank, scene channel |

Phases: (1) data model, allocation and scene format with host tests; (2) router wiring and
the program change, bank and scene-channel rules; (3) screens; (4) docs, recipes, review.
Verification as before: host suites, recipes, page-flow, the Docker build size gate
(headroom 24 KB; the UI growth is the main size risk) and an updated on-device checklist.

## 7. Decisions taken, and what is still open

Decided, with the reason:

1. **Velocity range per layer is in from the start.** It is cheap in the router and makes
   velocity-switched layers (a soft and a hard sound) possible.
2. **One level per part,** on top of the layers' levels, which stay as the inner balance.
   The Mix screen then shows one fader per part and a layered part is turned up or down as
   one instrument. Pan and sends stay per layer.
3. **DX7 SysEx edits the selected layer** of the part on that channel (layer 1 by default),
   so a DX7 editor can work layer by layer without disturbing the others.
4. **Scene channel: Off by default, remembered across power cycles** (a device setting).
   Nothing is reserved until you turn it on, so all 16 channels are available to parts.
5. **Scene storage: one scene per flash sector** (section 3): room for everything, simpler
   code, safer saves; 65 sectors instead of 33.
6. **The internal keyboard plays the selected part,** highlighted on the overview and on
   the part page; no separate keyboard-part setting.

Still open, and not something thinking can settle (to check on the hardware or in the
simulator):

- Does the sealed engine pass **Song Select** (`F3 nn`) through to the app? Not verified,
  and the MIDI in screen does not log it today. Program change on the scene channel is the
  dependable route.
- What the **Circuit Tracks** (and a Raspberry Pi sequencer) actually send on a pattern or
  project change: program change, with or without bank select, on which channel. Check with
  the MIDI in screen once the new logging exists.
