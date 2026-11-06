# DX747: an eight-part multitimbral DX7 module for the M-VAVE FM-1

<p align="center"><img src="docs/screens/boot-static-logo.png" width="240" alt="The DX747 boot screen: a Boeing 747 in side view above the words DX747, 8-part multitimbral DX7 module"></p>

> **DX747 does for micro synths what the jumbo jet did for air travel half a century ago.**
>
> For the price of a dinner, you get a box in the spirit of the **Yamaha TX816**: eight DX7s in a rack, which cost about as much as a small car in 1984.

**Eight FM engines. Eight parts. Any of sixteen MIDI channels. One compact box that plays your DX7 voices, eight parts at a time.**

DX747 is custom firmware that turns the M-VAVE FM-1 into a **multitimbral, layerable, splittable 6-operator FM sound module**: the glassy electric pianos, the bell-like mallets, the growling basses and the shimmering pads of the DX7 sound world, played and controlled from your keyboard, sequencer, DAW or phone. It replaces the stock sequencer, sampler and drum machine with one job done properly: **a DX7 module that is built for MIDI**.

## The idea

In 1970 the 747 turned long-distance flying from an occasion into something ordinary people could afford. In 1984 the way to get a **multitimbral DX7 sound** was a TX816: eight DX7 tone generators in one rack, one MIDI channel for each, and a price tag to match. DX747 brings that idea to a micro synth that fits in a bag and costs about what a dinner does. It is not a copy: the TX816 has eight separate audio outputs, DX747 mixes its eight parts to one output and adds layering, keysplits, scenes and USB audio on top.

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
>
> **Switching firmware.** DX747 has its own identity, `FM-1_8747747`, and brings M-VAVE's update loader, so another firmware can be installed over it and the official V15 can be put back. It has no sequencer, sampler or projects, so **it cannot read the projects or samples of another firmware**, and DX747 ignores the ones on the unit. Back up what you want to keep before you install, and see [docs/FIRMWARE-HUB.md](docs/FIRMWARE-HUB.md) for how it is listed on The Firmware Hub.

---

> **This tree is DX747**, custom firmware that makes the FM-1 an eight-part multitimbral DX7 module controlled over MIDI. It replaces the stock sequencer, sampler and screens. Start with [DX747.md](DX747.md); build instructions for Windows are in [docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md); read [docs/FLASHING-AND-RECOVERY.md](docs/FLASHING-AND-RECOVERY.md) before flashing. The text below is the SDK's own README for the stock firmware it is based on.
>
> **DX747 is a tribute to the Yamaha DX7.** It is an independent hobby project and is **not in any way associated with, endorsed by or sponsored by Yamaha Corporation or The Boeing Company** (or by M-VAVE). "DX7" is a Yamaha product name and "747" is a Boeing aircraft designation; any trademarks of Yamaha or Boeing, including "DX7" and "747", belong to their owners, and the names are used here only to say what the firmware is a tribute to and what its logo looks like. No rights to them are claimed. The firmware has its own identity (release V01, update identity `FM-1_8747747`) and replaces M-VAVE's Open V14 rather than being a version of it. See [DX747.md](DX747.md), "Licence and names".

## Build DX747 on Windows

Nothing here touches your FM-1: building is not flashing. Read
[docs/FLASHING-AND-RECOVERY.md](docs/FLASHING-AND-RECOVERY.md) before you ever flash, and
[docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md) for every step in detail and what to do when one
fails.

**1. Get the source: clone the repository.** That is all; nothing else needs downloading or
unpacking. Clone it into any folder you like (keep the full path short, plain ASCII, outside OneDrive):

~~~powershell
git -c core.autocrlf=false clone --branch dx747 https://github.com/esbenvb/fm1-dx747.git
cd fm1-dx747
~~~

The folder the clone creates is the **repository root**. Every command below is run from it and uses
paths relative to it, so it does not matter where you cloned. (`-c core.autocrlf=false` stops Git
from changing line endings: the build checks the SDK files byte for byte.)

**2. Install the JieLi 2.5.3 toolchain.** In this order (JieLi's own guide is only in Chinese and is
not needed):

1. Install [Code::Blocks](https://www.codeblocks.org/downloads/binaries/); JieLi's installer needs it.
2. Start Code::Blocks once, then **close it completely**: end every `codeblocks.exe` in Task Manager.
3. Download and run [JieLi's 2.5.3 installer](https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe) and
   keep the default folder, `C:\JL`. That is where the toolchain lives, whatever folder your clone is in.

**3. Check the machine, then build.** From the repository root:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dx747\windows\Check-Environment.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File sdk\Build.ps1 -FirmwareOnly -Toolchain C:\JL\pi32
~~~

The first command finds problems before the build does; the second proves that the firmware builds.
To build the page that installs it on a unit, see Step 5 of [docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md).

# FM1 Open V14

English · [简体中文](README.zh-CN.md)

8 tracks, 8 sample slots, 16 FX demos; binary SDK included.

[Workbench and development kit](https://open.m-vave.com/fm1-workbench) · [Official forum](https://forum.m-vave.com)

Use the workbench to update firmware, preview the instrument and manage projects, samples and DX7 voices. Select "Source + SDK" to download the development kit matching the online release. Back up projects, voices and samples before updating, and keep the device powered during writes.

## V14 features

- **DX7 library:** 128 voices in four groups of 32, with the M-VAVE FM1 original V15 factory voices and order. Saved user banks take priority. DX7 single-voice and 32-voice bank SysEx import/export is supported; extension parameters are not handled. Changing the library does not change patches already saved in projects.
- **Track roles:** on SEQ **8/8 TRACK ROLE**, K1 selects NORMAL, PLAY or OFF; K2 selects input MIDI channel 1–16. PLAY retains the track's timbre and held notes across project changes. MUTE/SOLO use the existing controls, with independent NORMAL and PLAY solo groups. PLAY timbre edits are not saved with projects: export a timbre file, or save DX7 voices to the independent library.
- **Step recording:** with the track set to NORMAL, press REC while stopped and play notes. Releasing all played keys advances to the next step, including DRUM 808 and SAMPLER. Overlapping notes share a step; during playback, REC records in time. PLAY tracks are for live performance and do not record notes.
- **Tempo and delay:** internal tempo is 30–300 BPM. Tap Tempo is available on the workbench, with SEQ on the FX DELAY page, or with HOME held while tapping SEQ elsewhere. Delay supports tempo synchronization and fixed times of about 30–2040 ms.

## MIDI

FM1 accepts DIN, USB and BLE MIDI input, with USB/BLE output. Default input channels 1–7 address tracks 1–7; channel 10 addresses track 8. Change each input channel on TRACK ROLE.

- **5/8 SYNC / LOOP:** select internal or external clock, tempo, loop length and clock input.
- **6/8 MIDI TRACK:** select SEQ/ARP LOCAL, BOTH or MIDI output, output channel, output ports and physical KEY OUT.
- **7/8 MIDI ROUTING:** configure LOCAL, CLK OUT and MIDI Thru. For DAW monitoring with Local Off, enable KEY OUT and select the output ports.

MIDI preferences are saved independently of projects. The browser simulator previews these settings without sending performance MIDI to external ports; configure the instrument on its panel. MIDI Clock, Start, Continue, Stop and Song Position Pointer input are supported. Clock loss stops synchronized playback.

## Updates and compatibility

V13 can update to V14. The workbench supports returning to original V15 and migrating from compatible third-party firmware. The standard FM1 update protocol remains available. Compatibility has been checked with selected firmware versions; it is not a guarantee for every third-party release.

Firmware switching does not make project and sample formats interchangeable. Keep exported backups for rollback. Older firmware may not support V14 track preferences, the independent voice library or synchronized/extended delay settings.

To back up the independent voice library, read and export groups A–D separately. Project files do not contain sample audio. Restore custom samples from their sample bank backup; writing the built-in sample bank replaces the current samples.

The eight sample slots share about 19.55 seconds of new sample storage. Existing larger banks remain readable; they may need to be backed up and reduced before writing the independent DX7 library.

## Build

You can build on **Windows** or on **macOS and Linux**. The two routes make the same three builds, in the same folders:

| Build | Windows | macOS / Linux |
|---|---|---|
| Firmware and its package (`build/dx7/`) | `build.bat` | `./build.sh` |
| Browser preview (`build/preview/index.html`) | `build.bat --web` | `./build.sh --web` |
| Firmware and the complete workbench page (`build/official-web/`) | `build.bat --package` | `./build.sh --package` |

DX747 is a fork of M-VAVE's open firmware (`FM1_OFFICIAL_OPEN`) and has its own file names. The SDK scripts write the upstream names,
`build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc` and `build/official-web/FM1-Official-Open-Source.html`; both routes leave a copy of
each under DX747's names, **`FM1_DX747.fwsc`** and **`FM1-DX747.html`**, and the macOS and Linux route also builds
**`DX747-Workbench.html`** (the configurator, simulator and updater, [DX747.md](DX747.md)) in the same folder.

**Get the source by cloning the repository** into any folder, and run every command below from its root (the folder that
contains `build.bat`); nothing depends on where it is:

~~~
git -c core.autocrlf=false clone --branch dx747 https://github.com/esbenvb/fm1-dx747.git
cd fm1-dx747
~~~

(`-c core.autocrlf=false` keeps Git from changing line endings: the build checks the SDK files byte for byte.) The
[build guide](docs/BUILDING.md) has the details, the troubleshooting tables and how the two routes were compared.

### Option A: Windows

Requires Windows x64 and PowerShell, `curl.exe` and `tar.exe`, and **JieLi Windows toolchain 2.5.3** (PI32v2 clang 4.0.1; FM1 continues
to use the `r3` CPU target). The build checks all 22 toolchain files against `sdk/manifest.json`; older V14 kits were pinned to 2.5.2 and
reject 2.5.3, so update the complete source + SDK kit together and keep the manifest checks enabled. JieLi's installer needs
Code::Blocks, so install the toolchain in this order:

1. Install [Code::Blocks](https://www.codeblocks.org/downloads/binaries/).
2. Start Code::Blocks once, then **close it completely**: end every `codeblocks.exe` in Task Manager.
3. Download and run [JieLi's 2.5.3 installer](https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe) and keep the default folder, `C:\JL`.

For this revision the expected SHA-256 of `pi32/bin/clang.exe` is `d4bc97cf421c1ccc373dd8924ce88720ff7457b3f1478d15df4aeda6ae923bce`.
Then, in PowerShell from the repository root:

~~~powershell
$env:FM1_PI32_TOOLCHAIN = 'C:\JL\pi32'
.\build.bat                  # firmware
.\build.bat --web            # browser preview
$env:FM1_SOURCE_URL = "https://github.com/<you>/fm1-dx747/archive/$(git rev-parse HEAD).zip"
.\build.bat --package        # firmware and the complete workbench page
~~~

The first `--package` downloads WASI SDK 27 (set `FM1_WASI_SDK` to reuse an installation). For a local firmware build without a
distribution source URL, run `./sdk/Build.ps1 -FirmwareOnly -Toolchain 'C:\JL\pi32'`.

### Option B: macOS and Linux (Docker)

Requires Docker and Node 18 or newer. Nothing else: no PowerShell, no Windows. From the repository root:

~~~bash
./build.sh --setup            # once: JieLi's Linux toolchain (26 MB) and WASI SDK 27, both hash-checked
./build.sh                    # firmware: build, the SDK's checks, package
./build.sh --web              # browser preview
./build.sh --package --source-url "https://github.com/<you>/fm1-dx747/archive/$(git rev-parse HEAD).zip"
~~~

It uses the same compiler release (clang 4.0.1) and the same flags and checks as the Windows route, in a container, and takes
about 20 seconds. The Windows build stays the reference; [docs/BUILDING.md](docs/BUILDING.md#how-the-two-routes-compare) says what
has been compared.

### Distribution builds

For a build you distribute, `FM1_SOURCE_URL` (or `--source-url`) must be the HTTPS URL of the matching source archive, including your
changes, licenses and build/install scripts. A moving GitHub `tree/main` page is not a matching source archive.

The [per-track tuning API](app/instrument.h) supports custom tuning; a dedicated tuning editor is not included.

## License

APP: [GPLv3 with the FM1 SDK linking exception](LICENSE.md). The device engine is supplied as a binary SDK with separate terms. Retain the applicable [SDK terms, third-party licenses and contributor notices](THIRD_PARTY.md) when distributing builds.
