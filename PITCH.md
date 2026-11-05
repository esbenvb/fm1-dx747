# DX747: an eight-part multitimbral DX7 module for the M-VAVE FM-1

> **DX747 does for micro synths what the jumbo jet did for air travel half a century ago.**
>
> For the price of a dinner, you get a box in the spirit of the **Yamaha TX816**: eight DX7s in a rack, which cost about as much as a small car in 1984.

## The idea

In 1970 the 747 turned long-distance flying from an occasion into something ordinary people could afford. In 1984 the way to get a **multitimbral DX7 sound** was a TX816: eight DX7 tone generators in one rack, one MIDI channel for each, and a price tag to match. DX747 brings that idea to a micro synth that fits in a bag and costs about what a dinner does. It is not a copy: the TX816 has eight separate audio outputs, DX747 mixes its eight parts to one output and adds layering, keysplits, scenes and USB audio on top.

**Eight FM engines. Eight parts. Any of sixteen MIDI channels. One compact box that plays your DX7 voices, eight parts at a time.**

DX747 is custom firmware that turns the M-VAVE FM-1 into a **multitimbral, layerable, splittable 6-operator FM sound module**: the glassy electric pianos, the bell-like mallets, the growling basses and the shimmering pads of the DX7 sound world, played and controlled from your keyboard, sequencer, DAW or phone. It replaces the stock sequencer, sampler and drum machine with one job done properly: **a DX7 module that is built for MIDI**.

## Why you will love it

- **8-part multitimbral.** Eight independent parts, each on its own MIDI channel (pick any of the sixteen). Give up to eight tracks of your sequencer, controllers on your stage or channels of your DAW their own sound, all at once, with sixteen voices shared between them.
- **Layering with no extra modes.** Stack up to **eight DX7 voices in a single part**: a bright e-piano on top of a warm pad, a bell over a bass, a thick stack of leads. Every layer has its own level, pan, reverb send, delay send, BRIGHT and voice limit, so you balance the stack like a mix.
- **Keysplits you can set by playing.** Give each layer its own **note window** and the keyboard splits into bass, comp and lead, or into as many zones as you have layers. Press ARP (LEARN) on the Part or Range screen, play the lowest and the highest key, and the window is set.
- **Velocity switching.** A **velocity window** per layer lets a soft touch play one sound and a hard hit another: velocity-layered pianos, soft and hard brass, a pad that blooms when you dig in. No sequencer tricks needed. (Play it from a velocity-sensitive MIDI keyboard; the built-in keys send a fixed velocity.)
- **Octave shift and fold, per layer.** Move any layer by up to **four octaves up or down**, and choose what happens to notes the shift pushes outside the MIDI range: drop them, clip them to the nearest end, or wrap them back by whole octaves. A keysplit with an octave change is one window and one shift: bass an octave down on the left, lead an octave up on the right.
- **Engines are your budget.** Eight single-layer parts, one monster part of eight layers, or anything in between. A note on a part takes one of the sixteen shared voices for each of its layers, and the screen header shows how many engines and voices you are using.
- **A mixer made for parts.** One fader per part, level meters, **mute and solo for the whole stack**, and pan and both effect sends for the whole part, with the spread between its layers kept. Fine-tune each layer on the edit pages. MIDI CC7 moves the part fader, so your DAW's automation mixes the module too.
- **BRIGHT: an FM "filter" in one control.** One macro (or CC74 / CC71 from your controller) opens and closes the sound by moving the modulators and leaving the volume alone, on a single layer or on a whole part.
- **Master filter, shared reverb and delay.** A low-pass, high-pass or band-pass filter with resonance over the whole mix of the engines, answering to MIDI on a channel of your choice, plus a shared reverb and delay with a send on every layer.
- **Scenes: your whole rig in one press.** Store **64 scenes** in four banks of 16, each with all eight parts, their layers, splits, shifts, levels, sends, the filter and a name. Recall them from the white keys, step through a setlist with the buttons, or switch with a **program change on a scene channel you choose**. Your last scene is waiting when you switch on, and a power cut in the middle of a save can never damage another scene.

## Made to be controlled

- **MIDI in from everywhere.** DIN, USB and Bluetooth LE MIDI all feed one router, together with the built-in keyboard. Merge several controllers on different channels.
- **DX7 SysEx editing.** A DX7 editor can change voice parameters in real time, one layer at a time, and send single voices straight to a layer.
- **Program change** picks any of the 128 voices on a part's channel, and **CC7, CC10, CC11, CC64, CC91 and pitch bend** do what you expect.
- **128 factory voices** in four banks of 32, plus **DX7 ROM cartridge import** from the M-VAVE workbench.
- **USB audio out** of the finished mix, taken after the master filter, so the computer records what you designed. Bluetooth audio in lets a phone or computer play along.
- **A live MIDI monitor** lists the latest notes, controllers and program changes with the port (DIN, USB, Bluetooth or the keys) and the channel they came in on, and lights every channel that is sending, so finding the right channel is a glance.

## Play it, too

The built-in 27-key keyboard plays the part you have selected on every screen, through the same router as everything else: layers, splits and shifts included (on the Scenes screen the white keys recall scenes instead). Shift it two octaves either way, and see every part, its layers and its key ranges on the 240x240 colour screen.

## Perfect for

- a **live keyboard rig** with a scene per song
- a **hardware sequencer or DAW setup** with one DX7 sound per track
- **layered pads and stacked pianos** from a single key press
- a **bass-and-lead split** on one controller
- a **compact studio sound module** with USB audio and MIDI

> **Status.** DX747 has been flashed to a real FM-1 and tested by its author, and it works great. It is still a young hobby project, so read [DX747.md](DX747.md) and [docs/ON-DEVICE-CHECKLIST.md](docs/ON-DEVICE-CHECKLIST.md) before you trust it with a gig, and [docs/FLASHING-AND-RECOVERY.md](docs/FLASHING-AND-RECOVERY.md) before you flash it.

*DX747 is a tribute to the Yamaha DX7 and an independent hobby project. It is not associated with, endorsed by or sponsored by Yamaha Corporation, The Boeing Company or M-VAVE. See the note in [README.md](README.md).*
