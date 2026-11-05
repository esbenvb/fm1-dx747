# DX747 configuration protocol

How the workbench page (and any other program) reads and changes the unit's parts, layers, filter, effects, scenes and
settings over MIDI SysEx. It extends the workbench transfer protocol of `app/transfer.c` with commands 23 to 31 and one new
transfer kind. Everything here works the same on the real unit (USB MIDI, SysEx) and on the in-page simulator (a virtual port).
Status: **protocol version 1, implemented in the firmware** (`app/dx747/config.inc`, included by `app/project.c`; `app/transfer.c`
dispatches commands 23 to 31 and transfer kind 5) and tested by recipe 18 in the simulator; not yet tried on a unit.

## Framing (unchanged)

`F0 7D 'F' 'M' 02 cmd token-lo token-hi packed checksum F7`

- The request data is packed into 7-bit bytes, least significant bits first (8 data bits per 7 MIDI bits); the checksum makes
  the sum from `7D` through the checksum zero modulo 128. A reply has the same header with bit 6 of `cmd` set and begins
  with a status byte: 0 success, 1 empty, or `64 - r` for a negative error `r` (`GROOVE_PROJECT_INVALID`, `..._MEMORY`,
  `FM1_FLASH_BUSY`). Reply data after the status is at most 64 bytes.
- **One request at a time**; a frame that arrives while another is pending is dropped. Change the token for every new request:
  a byte-identical request returns the cached reply. A request carries at most 132 data bytes.
- The reply goes back on the port the request came in on. **The unit never sends anything on its own**, so a client polls.
- Never send the sealed updater's frames (`F0 22 24 35 7F F7`, `F0 00 59 ...`) from a configurator: they reboot the unit into
  its updater. Workbench command 22 (capability) is not extended; DX747 has its own HELLO.
- Safe boot: the unit ignores all of this (no workbench transfer while the SAFE BOOT screen is up).

## Commands

All numbers are bytes; `u16` is little endian. Unlisted bytes are not used. A command with the wrong length or an
out-of-range value is refused with `GROOVE_PROJECT_INVALID`.

| Cmd | Name | Request data | Reply data |
|---|---|---|---|
| 23 | HELLO | none | `'D' 'X' proto(=1) flags engines_up parts(=8) layers(=8) scenes(=64) 'V' n n` (the release string, 3 characters) |
| 24 | STATE_CRC | none | `crc32(4) size(u16)`: the CRC of the state image as it is now, and its size |
| 1, 2 | read (existing) | `1: kind=5 target=0`, then `2: at(u16) n` | `1:` size (u16); `2:` up to 64 bytes of the **state image** |
| 25 | SET_LAYER | `engine field value` | none |
| 26 | SET_PART | `part field value` | none |
| 27 | PART_OP | `op a b` | `a` result (the part, the engine, or 0) |
| 28 | SET_MASTER | `field lo hi` | none |
| 29 | SET_SETTING | `field value` | none |
| 30 | SCENE | `op index [name(12)]` | `list:` 4 entries of `used name(12)` (52 bytes); others none |
| 31 | PART_NAME | `part name(12)` | none |

Voice contents are edited with the existing workbench commands (`1` kind 1, `3 4 5` kind 1 write per engine, the 156-byte voice;
library banks kind 4), or DX7 SysEx at the framed layer. Names of the library's 128 voices come from the library bank read.

### SET_LAYER fields (`engine` 0..7 is the layer's engine, `value` is one byte)

| Field | Meaning | Range as sent |
|---|---|---|
| 0 | layer level (inside the part) | 0..127 |
| 1 | pan | 0..100 = -50..+50 |
| 2 | BRIGHT | 0..100 = -50..+50 |
| 3 | reverb send | 0..15 |
| 4 | delay send | 0..15 |
| 5 / 6 | lowest / highest key | 0..127 (the other end follows if they would cross) |
| 7 | octave shift in semitones | 16..112 = -48..+48 |
| 8 | fold: drop, clip, wrap | 0..2 |
| 9 / 10 | lowest / highest velocity | 1..127 |
| 11 | polyphony | 1..8 |
| 12 / 13 | mute / solo | 0 or 1 |
| 14 | program (voice) | 0..127 |

### SET_PART fields: 0 part level 0..127; 1 MIDI channel 1..16 (refused if another part or the scene channel has it).

### PART_OP: 0 add a part in slot `a` (one layer, lowest free channel); 1 add a layer to part `a`; 2 remove layer `b` of part `a`
(the last layer removes the part); 3 select part `a` (the part the keyboard plays); 4 select layer `b` of part `a`. Add and remove
return the part or the freed engine in the first reply byte. A refused add or remove answers with status 95 (no free slot), 96 (no free
engine), 97 (no free channel) or 98 (refused); other refusals answer 71. Held notes of the built-in keys are ended first.

### SET_MASTER: 0 filter type 0..3 (off, low, high, band pass); 1 cutoff Hz 20..20000 (`lo hi`); 2 resonance 0..127;
3 to 6 effects (decay, delay time, feedback, return) 0..255 as the unit stores them.

### SET_SETTING: 0 scene channel 0..16; 1 master channel 0..16; 2 button light 0..8; 3 Bluetooth audio in 0 off, 1 on.

### SCENE ops: 0 list four scenes from `index` (a multiple of 4; `used` is the mask of parts the scene holds; an empty slot has `used` 0 and a blank name);
1 recall; 2 save the live state to `index` with the 12-character `name` (printable ASCII, space padded); 3 rename (the stored
scene is read and written again); 4 delete. A save, rename or delete writes a flash sector: a second one inside 1.5 s answers
`FM1_FLASH_BUSY` (status 67). A recall of an empty slot answers status 1. **The storage claim (`dx_storage_claim`, which erases the stock sample bank) is never available here**; when
the store is not ready the commands answer `GROOVE_PROJECT_INVALID` and the page says to use the panel.

## State image (transfer kind 5, read only, 344 bytes)

Read with `1` (kind 5, target 0) and `2` in chunks of 64. Built fresh on every `1`, so the chunks of one read are consistent.

| Offset | Size | Contents |
|---|---|---|
| 0 | 1 | image version (1) |
| 1 | 1 | selected part |
| 2 | 1 | engines up (bit e) |
| 3 | 1 | current scene, 64 = none |
| 4 | 4 | scene channel, master channel, button light, radio-off bits |
| 8 | 8 | filter type, resonance, cutoff (u16), effects decay, time, feedback, return |
| 16 | 8 x 15 | per part: channel (0 = slot unused), level, selected layer, name(12) |
| 136 | 8 x 26 | per engine: owner part (255 = free), layer level, pan+50, expression, BRIGHT+50, reverb, delay, program, flags (bit 0 muted, bit 1 soloed, bit 2 audible, bit 3 up), low, high, shift+64, fold, poly, velocity low, velocity high, voice name(10) |

## Polling

Every 400 ms while the configurator is visible: `24`. If the CRC differs from the last one, read the image (`1`, then 6 times
`2`) and redraw. A panel edit, a MIDI program change or a scene recall all show up this way. After the client's own setter it
reads again at once. Do not poll while a library or firmware operation is running.

## Cost and tests

Measured code: 2.5 KB (445,800 bytes, headroom 8.1 KB; it was 10.6 KB before). Tests: host suite for the handlers, the native harness
(`wb_query` / `wbserve`), recipes per command group, and `tools/dx747/sim/page-flow.mjs` running the real page client against the
harness.
