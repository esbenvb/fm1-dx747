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
| Installing | Step 6 of the same guide | the unit restarts into DX747 (logo, then Setup). The page ends with "sample initialization is incomplete" and an "Invalid file or parameters" error, which is expected; write down the exact texts. Write down the Build ID (Firmware details) |
| Going back | **Recovery & force update**, **Restore original V15** | the unit runs the original firmware again; the four voice banks are the factory voices; restore your own from the backup |

## 1. Start-up

| Check | How | Pass |
|---|---|---|
| Boots to the DX747 logo, then Setup | power on | the screen shows the aircraft and "DX747", then the Setup screen |
| All eight engines exist | an external keyboard on channel 1, then 2, up to 8 (engine n plays on channel n by default) | every engine sounds; none says it is out of memory |
| Memory | System screen | CPU reading is steady at idle |

All eight engines at once is the biggest unknown: the firmware link leaves about
237,000 bytes of heap, but eight real engine instances were never created together
on hardware.

## 2. Load and voices

| Check | How | Pass |
|---|---|---|
| CPU under load | layer two engines on one channel, play dense chords with the pedal down, watch System CPU and the header voice meter | no crackles or dropouts; note the highest CPU % |
| The 16-voice pool | hold more than 16 voices | the oldest notes are taken, no glitch, no reset |
| MIDI flood | send 100 or more notes in a few milliseconds from a computer (USB), then release | no sudden silence of every engine (that is the engine's input overflow reset), no stuck notes |
| Config change while holding | hold notes, change channels, window or shift, release | no stuck notes |

## 3. MIDI

| Check | How | Pass |
|---|---|---|
| Each port | DIN, USB, Bluetooth MIDI each send notes on a new channel | Setup shows the channel as "ACTIVE, NOT ROUTED YET"; MIDI in shows the right port name |
| Assign by white key | Setup, key n, then white keys 1 to 8 | the engines sound from that keyboard |
| Split and layer | two engines, one window each, then the same window | the split and the layer sound as drawn on the Channel screen |
| LEARN | **ARP** on Channel, play low then high | the window becomes those notes |
| Shift and fold | shift +2 octaves with a window near the top | notes move; DROP, CLIP and WRAP behave as described in `docs/MIDI.md` |
| Sustain, bend, mod | CC64, pitch bend, CC1 | as on a DX7 |
| Program change | MIDI program change 0 to 127 on a play channel | the patch changes on that engine |
| Velocity | the same note at velocity 20, 64 and 127 from an external keyboard | clearly louder and brighter as velocity rises (the built-in keys are fixed at 100) |
| LEARN with the keys | **ARP** on Channel, then two built-in keys | the window becomes those two notes |
| Solo and mute marks | Mix screen, white keys 1 to 8 and 9 to 16 | a filled red M and a filled amber S; the other engines are dimmed while one is soloed |

## 4. Control from outside

| Check | How | Pass |
|---|---|---|
| Parameter change | an editor or a SysEx tool sends `F0 43 10 01 06 00 F7` (algorithm 1) on device 0 | engine 1's algorithm changes; the Sound screen follows |
| Voice dump | send a 163-byte single voice dump | the voice loads, with the dump's name |
| Layered channel is safe | layer two engines on channel 1 and send a parameter change for device 0 | nothing changes until one engine has control channel 1 |
| BRIGHT | CC74 from 0 to 127 on a play channel while holding a note, then while playing new notes | the tone brightens and dulls with no volume change; **write down whether a held note follows** (not yet known) |
| BRIGHT is not permanent | set CC74 to 64 | the voice is back to its own sound; save a scene with BRIGHT on and recall it: not brighter than before |
| Master filter | set a master control channel; CC74, CC71, CC70 on it | cutoff, resonance and type move; the Master screen follows |
| Program change then edits | program change then a parameter change straight after | the edit lands on the new voice |
| Voices that need a controller | load a voice with amplitude-modulation sensitivity (the "BC" voices of ROM 3A, patches 113 and 120 when it is in bank D), then send CC120, then a program change | the voice sounds at once each time; **write down whether any voice stays silent until you move a controller**: the fix sends CC2 = 127 and was only tested in the simulator |

## 5. Scenes and the first save

| Check | How | Pass |
|---|---|---|
| The prompt | **SAVE** on a unit with a sample bank | "FIRST SAVE: FLASH SETUP" appears; HOME cancels and nothing is erased |
| Save and recall | agree, save, change things, recall | everything comes back, including the shift, BRIGHT and the filter |
| Power cycle | switch off and on | the last scene is back |
| 64 scenes | save into the last slot of bank D, recall | works |
| Backup was taken | before this section | you have your samples saved with the workbench |

Do not cut power during a save to test it.

## 6. ROM import

| Check | How | Pass |
|---|---|---|
| Import | workbench, import a 32-voice `.syx` ROM into bank D | the workbench's read-back check passes |
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
| Audio unaffected | listen while changing the light level | no clicks |

## 9. Recovery and return

Do these last, and only with the programmer round trip proven.

| Check | How | Pass |
|---|---|---|
| Recovery chord | power on holding OCT- and OCT+ | write down what happens. Not known |
| Return to stock | the page's **Restore original V15** | the unit runs the original firmware with the factory voice banks; restore voices, samples and projects from backups |

## What to report

Send the table with the results, the CPU numbers, anything you wrote down as "not
known", and for any failure what you did, what you expected and what happened. A
photo or a video of the screen helps.
