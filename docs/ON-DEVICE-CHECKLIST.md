# On-device checklist

Everything here is something that has **only** been tested in the simulator and on
the host, or not at all. Run it on a real FM-1 once the way back is in place (see
[FLASHING-AND-RECOVERY.md](FLASHING-AND-RECOVERY.md)). Write the result next to
each line; the numbers matter more than the ticks.

Why these are open: the simulator runs an open stand-in for the sound engine, not the
sealed one in the firmware, and it has no real audio load, no real MIDI timing, no
LED hardware and no radio.

Record first: the SHA-256 of the `.fwsc` you flashed, the date, and the unit's
serial or label.

## 0. Installing

| Check | How | Pass |
|---|---|---|
| Samples, voices and projects saved first | with the original firmware still on the unit, save them with the workbench; keep the `.syx` files of voices you care about (reading voices from V15 is not verified) | you have the files |
| The flashing page | open `FM1-Official-Open-Source.html` ([BUILD-WINDOWS.md](BUILD-WINDOWS.md), Step 5) in Chrome or Edge with the unit connected over USB and nothing else MIDI plugged in | the page sees the unit; from the original V15 it says "Different firmware detected" and offers **Enable force flash and reconnect**; write down anything that is not as described |
| Installing | Step 6 of the same guide | the unit restarts into DX747 (logo, then Parts). The page ends with "sample initialization is incomplete" and an "Invalid file or parameters" error, which is expected; write down the exact texts. Write down the Version row (V01), the Build ID (Firmware details) and the identity shown at the end (`FM-1_8747747`); the boot screen says "DX747 V01 PREVIEW" |
| Going back | **Recovery & force update**, **Restore original V15** | the unit runs the original firmware again; the four voice banks are the factory voices; restore your own from the backup |

## 0b. Safe boot (do this before anything else is trusted)

| Check | How | Pass |
|---|---|---|
| Safe boot | hold ENV and LFO, switch on, release after the screen appears | the SAFE BOOT screen shows, the ENV and LFO lights are on, nothing responds to buttons, keys or MIDI |
| The update page still works in safe boot | with the unit in safe boot, open the flashing page and connect | the page connects as it does on a normal start; do **not** start an install just for this |
| Back to normal | switch off, release, switch on | the normal logo, then Parts |
| No false trigger | switch on five times with nothing held | never SAFE BOOT |

## 1. Start-up

| Check | How | Pass |
|---|---|---|
| Boots to the DX747 logo, then Parts | power on | the screen shows the aircraft and "DX747", then the Parts screen with eight parts on channels 1 to 8 |
| All eight engines exist | an external keyboard on channel 1, then 2, up to 8 (part n is on channel n by default) | every part sounds; none says it is out of memory |
| Memory | System screen | CPU reading is steady at idle |
| All eight engines up | look at every screen's header | the `ENGINES` count reads 8/8 and is not amber; write down the second number if it is lower |
| Octave buttons step | on EDIT's pages, FX, Scenes, System, Mix and the part in detail (HOME twice) press OCT- and OCT+ | each steps the page, the filter page, the bank, the row, the part; on Parts, Part and MIDI in they move the keyboard octave |
| Part in detail | HOME twice; K1 and K2; ARP then two keys | K1 moves the MIDI channel, K2 the part level; a split and an octave shift show as bars and a small amber tag |
| Readable text | look at every screen at arm's length, in a bright and a dim room | the text you need to read is white and legible, the large text is smooth, not blocky; write down any text that is hard to read and on which screen |
| PRESETS | turn PRESETS on Parts, Part, the detail screen and each of the four layer pages | the framed layer's patch changes (the name shows in the overlay); K1 on the Sound page is the layer's volume |
| Overlay | turn each knob on each screen once | a box with the label and the new value shows at the bottom for about a second after the last click, readable at arm's length; it is gone after that and the screen under it is unchanged |

All eight engines at once is the biggest unknown: the firmware link leaves about
231,000 bytes of heap, but eight real engine instances were never created together
on hardware.

## 2. Load and voices

| Check | How | Pass |
|---|---|---|
| CPU under load | add a second layer to a part, play dense chords with the pedal down, watch System CPU and the header voice meter | no crackles or dropouts; note the highest CPU % |
| The 16-voice pool | hold more than 16 voices | the oldest notes are taken, no glitch, no reset |
| MIDI flood | send 100 or more notes in a few milliseconds from a computer (USB), then release | no sudden silence of every engine (that is the engine's input overflow reset), no stuck notes |
| Config change while holding | hold notes, change the part's channel, a layer's window or shift, or remove a layer, release | no stuck notes |
| Selection change while holding | hold built-in keys, turn SELECT to another part, release | the notes stop; none hangs |

## 3. MIDI

| Check | How | Pass |
|---|---|---|
| Each port | DIN, USB, Bluetooth MIDI each send notes on a new channel | MIDI in shows the right port name and the channel (a channel with no part makes no sound) |
| Make a part for it | remove a part (LFO), ENV in an empty slot, K1 on the Part screen to the channel | the part sounds from that keyboard |
| Split and layer | two layers in a part, one window each, then the same window | the split and the layer sound as drawn on the Parts and Part screens |
| LEARN | **ARP** on Part, play low then high | the framed layer's window becomes those notes |
| Shift and fold | shift +2 octaves with a window near the top | notes move; DROP, CLIP and WRAP behave as described in `docs/MIDI.md` |
| Sustain, bend, mod | CC64, pitch bend, CC1 | as on a DX7 |
| Program change | MIDI program change 0 to 127 on a part's channel | the patch changes |
| Program change on a layered part | the same on a part with two layers | one patch plays over the whole keyboard, the second layer is gone, its sound cut |
| Bank select | CC0 = 1, then a program change, then CC0 = 0 and a program change | the first is ignored, the second loads |
| Part level | CC7 from 127 to 0 on a part's channel, with two layers at different levels | the part fades with the balance between the layers kept; the Mix fader follows |
| Velocity windows | two layers at 1-63 and 64-127 on the Velocity page, one note at velocity 30 and 100 | the soft note sounds one layer, the hard note the other |
| Velocity | the same note at velocity 20, 64 and 127 from an external keyboard | clearly louder and brighter as velocity rises (the built-in keys are fixed at 100) |
| LEARN with the keys | **ARP** on Part, then two built-in keys | the window becomes those two notes |
| Keys play the selected part | on every screen, play the built-in keys | they sound the selected part with its windows and shifts; on the Scenes screen the white keys recall scenes instead |
| Solo and mute marks | Mix screen, ENV (mute) and LFO (solo) on a part | a filled red M and a filled amber S; the other parts are dimmed while one is soloed |

## 4. Control from outside

| Check | How | Pass |
|---|---|---|
| Parameter change | an editor or a SysEx tool sends `F0 43 10 01 06 00 F7` (algorithm 1) on device 0 | part 1's framed layer changes its algorithm; the Sound screen follows |
| Voice dump | send a 163-byte single voice dump | the voice loads, with the dump's name |
| Layered part edits one layer | two layers in part 1, frame layer 2 with ALGO, send a parameter change for device 0 | only layer 2 changes; frame layer 1 and the same message changes layer 1 |
| BRIGHT | CC74 from 0 to 127 on a part's channel while holding a note, then while playing new notes | the tone brightens and dulls with no volume change; **write down whether a held note follows** (not yet known) |
| BRIGHT is not permanent | set CC74 to 64 | the voice is back to its own sound; save a scene with BRIGHT on and recall it: not brighter than before |
| Master filter | set a master control channel; CC74, CC71, CC70 on it | cutoff, resonance and type move; the Master screen follows |
| Program change then edits | program change then a parameter change straight after | the edit lands on the new voice |
| Scene channel | System, SCENE CHANNEL = 16, save scenes, then a program change on channel 16 | the scene with that number (program change 0 = scene 1) is recalled; an empty slot does nothing; the setting is still there after a power cycle |
| Song Select | `F3 nn` from a sequencer with the scene channel off | **write down whether the scene changes and whether MIDI in shows SONG n**: the simulator does, the sealed engine may drop the message |
| Circuit Tracks or another sequencer | what it sends when a pattern or project changes | **write down the message type, bank select and channel**, so the scene channel can be set to match |
| Voices that need a controller | load a voice with amplitude-modulation sensitivity (the "BC" voices of ROM 3A, patches 113 and 120 when it is in bank D), then send CC120, then a program change | the voice sounds at once each time; **write down whether any voice stays silent until you move a controller**: the fix sends CC2 = 127 and was only tested in the simulator |

## 4b. The workbench page (`DX747-Workbench.html`)

Open the page in Chrome or Edge on a computer, close other MIDI programs, plug the unit in by USB.

| Check | How | Pass |
|---|---|---|
| Connect | press Connect FM-1 and allow MIDI with SysEx | the status pill reads `FM-1 connected · DX747 V01 · protocol 1`; write down the USB MIDI port name the browser shows and any message if it fails |
| State and voice names | open Configure and Sounds | eight parts with their channels and layers as on the Parts screen; the voice names of banks A to D appear (note how long they take) |
| Page to unit | change a layer's lowest key, a level, a channel, add and remove a layer | the unit's Parts and Part-in-detail screens show it at once; the unit refuses what the panel would refuse and the page says so |
| Unit to page | change the same things on the panel (PRESETS, K1, ENV) | the page follows within about a second |
| Playing while connected | hold a chord from a MIDI keyboard while the page is open and polling | no dropouts, stuck notes or screen glitches; write down any |
| Scenes | save, rename, recall, delete a scene from the Scenes tab | works on the unit (set up the scene store from the panel first); two saves within 1.5 s are refused with a "busy" message |
| Keyboard | Keyboard tab, output = the unit, play from the computer keyboard on a part's channel | the part sounds; changing the channel, octave and velocity works; Panic ends all notes |
| Update tab | open it (do not install) | it says the firmware checks out; Install is only enabled after Connect and the backup box |

## 5. Scenes and the first save

| Check | How | Pass |
|---|---|---|
| The prompt | **SAVE** on a unit with a sample bank | "FIRST SAVE: FLASH SETUP" appears; HOME cancels and nothing is erased |
| Save and recall | agree, save, change things, recall | everything comes back, including the shift, BRIGHT and the filter |
| Power cycle | switch off and on | the last scene is back |
| 64 scenes | save into the last slot of bank D, recall | works (each scene has its own flash sector) |
| Layers and parts survive | save a scene with a layered part and a part moved to another channel, recall after a power cycle | channels, layers, windows, levels and the selected part are as saved |
| Backup was taken | before this section | you have your samples saved with the workbench |

Do not cut power during a save to test it.

## 6. ROM import

| Check | How | Pass |
|---|---|---|
| The tab unlocks | the page's **Projects and voices** tab, Connect | the bank, voice and project controls become usable (the page reads an empty working project first; this passed against the simulator only) |
| Import | workbench, import a 32-voice `.syx` ROM into bank D | the workbench's read-back check passes |
| Timbre import is DX7 only | the page's "import track timbre" with a file from a stock FM-1 | refused; every engine stays a DX7 |
| Names and sound | select patches 97 to 128 | the names match the ROM; every voice sounds, including the "BC" voices (113 and 120), because DX747 sends CC2 = 127 after each load; write down any voice that stays silent |
| Scenes survive | recall a scene saved earlier | still correct |

## 7. Audio paths

| Check | How | Pass |
|---|---|---|
| Master filter | Master screen, low-pass at 1 kHz | clearly filtered |
| USB out ignores the knob | record the USB audio out, turn the master volume knob | the recording does not change level; the headphone/line output does |
| USB out carries the filter | filter on, record | the recording is filtered |
| USB audio in and Bluetooth audio in | play from the computer and the phone | both heard, after the knob |
| Bluetooth audio and MIDI together | phone plays music while a MIDI app plays the engines | both work at once without dropouts |
| No loopback | record USB out while USB audio in plays | the recording does not contain the incoming audio |

## 8. Button light

| Check | How | Pass |
|---|---|---|
| Levels | System, BUTTON LIGHT 0 to 8 | the idle light of every button and key steps visibly from dark to bright |
| Flicker | look at it and at a phone camera in slow motion at levels 1 to 4 | no visible flicker |
| States | keys and buttons that light for a reason (selected, active) | clearly brighter than the idle light |
| Mix keys | Mix screen, EDIT: the white keys are mute and solo | with part 8 empty, its two keys are dark; the other keys are dim; muting or soloing a part makes its key bright. Write down whether the dim level (2 of 8) is clearly dimmer than the bright one and still visible at BUTTON LIGHT 0 |
| Audio unaffected | listen while changing the light level | no clicks |

## 9. Recovery and return

Do these last, and only with the programmer round trip proven.

| Check | How | Pass |
|---|---|---|
| The ROM updater chord | power on holding OCT- and OCT+ (nothing else, no key) for a second | write down exactly what happens: what the unit enumerates as, whether the page sees it, and how to get back. Not known |
| Return to stock | the page's **Restore original V15** | the unit runs the original firmware with the factory voice banks; restore voices, samples and projects from backups |

## What to report

Send the table with the results, the CPU numbers, anything you wrote down as "not
known", and for any failure what you did, what you expected and what happened. A
photo or a video of the screen helps.
