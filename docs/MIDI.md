# DX747 MIDI reference

## Ports and the built-in keyboard

DIN (the TRS socket), USB and Bluetooth LE all feed one router, and so does the
built-in keyboard. The router sends every message to the engines that should
get it. You never see the engines' own internal channels.

Everything below speaks of **parts**: a part is one MIDI channel with one sound, and a
sound is one to eight **layers**, each a DX7 voice on its own engine (see `DX747.md`
and `docs/design/PARTS-MODEL.md`). A part's channel is the channel of every one of its
layers.

- Bluetooth MIDI is a **peripheral**: the phone or computer connects to the FM-1.
- The built-in keyboard has 27 keys and plays notes 53 and up (F3 in the standard
  naming, where 60 is C4). **OCT-** and **OCT+** move it by up to two octaves.
  Its velocity is fixed at 100. It plays the **selected part**, on that part's channel,
  so its channel follows the selection (nothing plays while the selected part is an
  empty slot); a scene stores which part is selected.
- The built-in keys go through the router on every screen, with the part's windows,
  shifts, mute and voice limits like any keyboard. On the Scenes screen the white keys
  recall scenes instead (see `DX747.md`).
- The MIDI in screen (**REC**) shows the latest messages with their port and channel.
- In SAFE BOOT (ENV + LFO held at power-on, see `DX747.md`) all MIDI is ignored; only the
  sealed engine's own firmware-update messages work.

## Notes

A note on channel c is played by every layer of the part on c whose note window and
velocity window contain it. For each such layer the note is moved by that layer's
shift. A note that the shift pushes outside 0 to 127 is dropped (DROP), moved to the
nearest end (CLIP), or moved by whole octaves back into range (WRAP). A velocity
window of 1 to 127 (the default) accepts every note.

A note off always reaches the engines that got the note on, even if the settings
were changed while the key was down. The sustain pedal (CC64) is followed per
channel and source the same way.

Sixteen voices are shared by all engines. If they are all in use, the oldest note
is taken. Each layer also has its own limit, 1 to 8 voices (default 4). One note
on a part with two layers uses two voices.

## What each engine understands

Controllers reach every layer of the part on the message's channel. These are what the
sound engine does with them (the rows for CC7 and the program change are changed by
DX747, see below):

| Message | Effect |
|---|---|
| Note on / off | plays the voice at the velocity the note arrived with (1 to 127); a note off is passed on without a velocity |
| CC1 (mod wheel), channel pressure | vibrato (pitch modulation) |
| CC2 (breath), CC4 (foot) | lower the level of operators that have amplitude-modulation sensitivity; the response is mild in this engine, so do not rely on them to fade a voice |
| CC64 (sustain) | sustain pedal |
| Pitch bend | plus or minus 2 semitones, fixed |
| CC7 | **the part level** (below), not the engine's volume directly |
| CC10 | pan |
| CC11 | expression |
| CC91 | reverb send (0 to 127 becomes 0 to 15) |
| Program change 0 to 127 | on a one-layer part: cuts the engine's notes and loads that patch (1 to 128 on the screen); it also puts back the operator mask, the envelope override, the mono or poly mode and the LFO. Level, pan, expression, the controller values and BRIGHT stay. On a layered part, see below |
| CC120 | all sound off |
| CC121 | reset controllers |
| CC123 | all notes off |

**Voices that need a controller.** After a voice has been loaded, or the engine has
been stopped with CC120, this engine keeps any voice that has amplitude-modulation
sensitivity completely silent until some controller message arrives (CC1, CC2, CC4,
channel pressure or CC121). The "BC" voices of the DX7 ROMs (for example "SAX BC")
are the usual case. DX747 sends each engine breath fully open (CC2 = 127, the engine's
own default) after it loads a voice or is stopped, so such voices play straight away.
The screens' stop (hold HOME, press PLAY) does the same.

## What DX747 adds

| Message | Effect |
|---|---|
| CC74 or CC71 on a part's channel | the **BRIGHT** of every layer of the part: 64 is centre (0), 127 is +50, 0 is -50. Each layer keeps its own offset on the screens; the controller sets them all to the same value |
| CC7 on a part's channel | the **part level**, 0 to 127. Every layer plays at its own level times the part level / 127, so the balance between the layers stays. It is not passed on to the engines as it arrived |
| Bank select (CC0), then program change, on a part's channel | picks the part's **sound** from the bank. Bank 0 is the 128 DX7 patches. Other banks do not exist yet: after a bank select of 1 or more the program change is ignored. The bank is remembered per channel, 0 until a bank select arrives. CC32 is not used |
| Program change on a **layered** part | the part becomes that one patch: its first layer stays (at its own level) and plays the patch over the whole keyboard with no shift and the full velocity window; the other layers go and their engines are free. The notes of the freed layers are cut |
| Program change on the **scene channel** | recalls scene n + 1 (0 to 63); never reaches a part. An empty slot does nothing |
| Song Select (`F3 nn`) | recalls scene nn + 1, the same way, if the sealed engine hands the message to the firmware. The simulator does; **the unit has not been tried** |
| Program change on a channel no part uses | ignored (the MIDI in screen still shows it) |
| CC10, CC11, CC121 | still go to the engine, and are also followed by the screens and scenes: pan is shown on the Mix and Sound screens, expression has no display but is saved in scenes, and CC121 puts both back (pan centre, expression 127). If a reset and a new value arrive within one control period, the new value wins |
| CC74, CC71, CC70 on the **master control channel** | the master filter: cutoff, resonance, type (below) |

### BRIGHT

BRIGHT adds its offset (-50 to +50 steps) to the output level of the modulator
operators of the voice, never the carriers, so the sound gets brighter or duller
without getting louder or quieter. A modulator at level 0 stays off, and levels are
kept between 0 and 99. The carriers depend on the algorithm; DX747 reads the voice
and works them out.

The voice's own levels are remembered. Changing the patch, a program change, a
SysEx edit, a voice dump or recalling a scene starts again from the new voice.
Saved scenes, and voices downloaded with the workbench, carry the voice's own
levels, not the brightened ones.

Not yet checked on the real engine: whether BRIGHT also changes notes that are
already sounding.

### Scene channel

Set on the System screen: **SCENE CHANNEL**, Off or 1 to 16. It is Off by default and
the setting is remembered across power cycles. Use a channel no part uses (the System
screen warns when one does): on that channel a program change recalls a scene, so a
part there would never get its program changes. Notes and controllers on it still
reach the part. A sequencer or DAW that sends a program change on that channel at the
start of a song or pattern switches scenes; MIDI Song Select works the same where the
engine passes it on.

### Master filter

Set on the System screen: **MASTER CONTROL CHANNEL**, Off or 1 to 16. Use a channel
no part plays on. On that channel:

| Controller | Meaning |
|---|---|
| CC74 | cutoff, 20 Hz to 20 kHz, exponential |
| CC71 | resonance, 0 to 127 |
| CC70 | type: 0 to 31 off, 32 to 63 low-pass, 64 to 95 high-pass, 96 to 127 band-pass |

A cutoff or resonance move turns an off filter into a low-pass so you hear it. If
the three controllers are sent together, their order is not kept; type is applied
last. These controllers are not passed on to the engines. The filter is also set by
hand on the Master screen (**FX** twice).

## DX7 SysEx

Two kinds of DX7 SysEx messages are understood. Anything else is ignored.

**Parameter change** (7 bytes): `F0 43 1n gg pp dd F7`

- `n` is the device number 0 to 15, which means MIDI channel n + 1.
- `gg` is 0 for parameters 0 to 127 and 1 for 128 to 155; `pp` is the low 7 bits of
  the parameter number; `dd` is the value.
- Parameters use the numbering of the DX7 voice data: six blocks of 21 values, the
  block for operator 6 first (parameters 0 to 20) and the block for operator 1 last
  (105 to 125). With k counted from 0 for operator 6 to 5 for operator 1, block k
  starts at 21k and its output level, the 17th value, is parameter 21k + 16 (so
  operator 1's output level is 121). Then 126 to 133 pitch EG, 134 algorithm (0 to
  31), 135 feedback, 136 oscillator sync, 137 to 143 LFO, 144 transpose, 145 to 154
  the ten name characters, 155 the operator on/off mask (bit 5 is operator 1, bit 0
  is operator 6).
- A value above a parameter's limit is limited to it, and the mask is limited to 63.
  A name character that is not printable (outside 32 to 126) is ignored.
- Function parameters (`gg` of 8 and up) are ignored.

**Single voice dump** (163 bytes): `F0 43 0n 00 01 1B <155 bytes> checksum F7`

The voice replaces the engine's patch and cuts the engine's notes. All operators are
switched on, and the envelope override, the mono or poly mode and the LFO are put
back to the voice's own. A value above a parameter's limit is limited, not refused,
so a dump with one slightly out of range byte still loads. A frame with a bad
checksum or a byte of 128 or more is ignored without any notice.

**Which layer gets it.** The **framed layer** of the part on channel n + 1: on the Part
screen, **ALGO** frames a layer (layer 1 until you do), and a scene keeps which one is
framed. If no part is on that channel, nothing happens, and the screen says nothing.
A layered part is therefore never overwritten by accident, and an editor can work one
layer at a time.

**Order.** Parameter changes and dumps are applied in the order they arrive. An
engine that has just received a program change is left alone for 20 ms so that an
edit that follows it lands on the new voice. Parameter changes are applied at most
a few per control cycle (each pauses the audio for an instant); a value that is
replaced by a newer one before it is applied is skipped. Up to 64 parameter changes
and 8 dumps can wait; what does not fit is dropped and the screen says
"DX7 SYSEX LOST".

**While BRIGHT is on**, an editor shows the voice's own level, and a level you set
is the voice's own level: the sound plays it plus the BRIGHT offset.

**What does not fit:** the 32-voice bank dump (4,104 bytes) cannot reach the unit
over MIDI, because the FM-1's MIDI input throws away SysEx over 1,024 bytes. Load
banks with the M-VAVE workbench (see `DX747.md`). The unit does not send voice dumps.

The M-VAVE workbench also reaches the unit with its own SysEx (`F0 7D 46 4D 02 ...`)
and with MMC. DX747 answers that file protocol for voices and the four voice banks, and
refuses projects, samples and timbres of any source but DX7 (see `DX747.md`).
