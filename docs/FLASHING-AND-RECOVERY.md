# Flashing DX747 and getting back

**Nothing in this repository has been flashed to a real FM-1.** The firmware builds
and passes the SDK's checks, and its behaviour has been tested in the simulator and
on the host. Flashing the first build is the first time it runs on hardware, and a
firmware that does not start can leave a unit that needs more than the workbench to
recover. Read this before you do it.

## The rule: a way back first, then the first flash

Do not flash DX747 to a unit you cannot afford to lose until all of this is done:

1. **A hardware programmer** that can read and rewrite the unit's flash even when the
   firmware does not start (the project plan calls it the Transporter). If you do not
   have one, stop here. The recovery the firmware offers itself (below) is
   unverified, and the workbench can only update a unit that still starts.
   The community tools, as their own pages describe them (read on 2026-11-04, not
   tried by this project): **kurogedelic/FM-1-transporter** puts a small RP2040 board
   between the computer and the unit's USB lines (D+, D- and ground only, **no VBUS**;
   Seeed XIAO RP2040 recommended, Raspberry Pi Pico untested) and uses the chip's ROM
   USB loader to dump and write flash, writing only between 0x4000 and 0x93000 and only
   with an explicit write flag; it needs the `wl82loader.bin` loader, which is not
   included (it comes from jl-uboot-tool). **Quixotic7/MvaveFM1Unbricker** then
   restores the official V15 into that firmware region (hash-checked, with a
   double-read backup first, never writing below 0x4000, so the boot head is never
   touched; it does not run on macOS; its settings repair is untested on a real
   unit). Neither can repair a damaged boot head, and the pages report very few
   recoveries (the unbricker's page names one, the author's own). None of this has
   been shown with DX747 or with Open firmware on the unit.
2. **A full backup** of the unit's flash with that programmer, stored somewhere safe.
3. **A practice run with something known good:** build the flashing page for the
   unmodified upstream firmware (commit `4c7496d`, in its own folder, with the same
   `build.bat --package` steps as in [BUILD-WINDOWS.md](BUILD-WINDOWS.md)), install
   it the way Step 6 there describes (from a unit on the original V15 that means the
   page's force flash), check it starts, and restore the backup with the programmer.
   Only when that round trip works is the setup proven.
4. **A backup of your own data, before DX747 goes on the unit:** projects, DX7 voices
   and **samples**. DX747 erases the stock sample bank on the first scene save (below)
   and cannot read it back, so this is your only chance to save the samples. The
   voices matter too: going back to the original V15 replaces all four voice banks
   with the factory voices (below). Use the workbench for what it can read; whether
   it can read the voice banks of a unit that still runs V15 is not verified (the
   page's code reads them only from Open firmware), so keep the `.syx` files of
   voices you care about.

## What DX747 changes in flash

The unit's flash holds the firmware and a user area. DX747 treats the user area like this:

- **Samples are erased** on the first scene save, after a prompt. The 64 scenes and
  the device settings (one 4 KB flash sector each: 65 sectors, 260 KB, from offset
  69,632) take over the flash that holds the stock sample bank.
  Going back to stock firmware does not bring the samples back: restore them from
  your workbench backup. Until you agree to that prompt DX747 writes nothing to
  that flash (settings changes are not saved either). The prompt also covers an
  unfinished stock save that a power cut left in the shared staging area; that is
  the only complete copy of it and the agreement discards it. The agreement erases
  only what stands in the way: scenes that are already stored are kept. (A
  stranded stock *tuning* journal is not recognised and is overwritten by the next
  write; stock projects themselves are never touched.)
- The **128 DX7 voices** (four banks A to D) are not touched by DX747's own code, which
  reads and writes them exactly as the stock firmware does. The installer is another
  matter: it is M-VAVE's binary loader, and its page says that a successful transfer
  does not guarantee that your data is kept. Do not count on voices surviving an
  install in either direction; keep the `.syx` files.
- **Stock projects** (sequencer data) are not read or changed by DX747 and are not
  erased by it. They stay in flash but DX747 cannot show them.
- Scene writes are made so that a power cut leaves either the old scene or the new
  one. Do not remove power while the screen says it is saving.

Project and sample formats of different firmware are not interchangeable. Keep
exported backups for rollback, as the SDK's own README says.

## Going back to the stock firmware

With the unit starting normally, use the page you built, which carries the original
V15 (the official hosted workbench has no way to take a local file, and a loose
firmware file cannot be loaded): **Recovery & force update**, **Restore original V15**,
connect, tick the backup box again (choosing Restore clears it), **Install original
V15** and confirm ([BUILD-WINDOWS.md](BUILD-WINDOWS.md), Step 6). It also restores the unit if DX747
starts but misbehaves. Then restore your samples and projects from your backups.

**Going back replaces all four DX7 voice banks with the 128 factory voices**, and the
page's own confirmation adds that Open projects and samples may be erased. Back up
all three before you do it.

If the unit does not start, the page cannot reach it. Use the programmer and your
full backup.

Do not interrupt an install, in either direction: keep USB and power connected and
the browser tab open until the page says it is done. Whether a unit interrupted in
the middle of a write always comes back in update mode, so that you can run the page
again, is not verified.

## Power-on chords: safe boot, and the ROM updater (unverified)

Both are read once, at power-on, before the screen starts ([DX747.md](../DX747.md)).

**Safe boot: hold ENV and LFO.** DX747 then starts none of its own parts (engines,
scenes, settings, flash access, Bluetooth, MIDI routing, workbench file protocol,
panel) and says SAFE BOOT. USB MIDI and the firmware update page are the sealed
engine's, so they still work: this is the way to get at a unit whose own start-up
misbehaves. It is simulated and unit-tested only; **check it on the unit before you
need it** ([ON-DEVICE-CHECKLIST.md](ON-DEVICE-CHECKLIST.md)). It cannot help if the unit
fails before that point (the sealed engine's own start-up, or DX747's few
initialisation calls before the chord is read).

**ROM updater: hold OCT- and OCT+ only** (no other button, no note key), for about
0.4 s (a key matrix that ghosts a third key while the two buttons are held makes the
unit refuse the chord, which is the safe direction). It calls the SDK's `fm1_device_recovery()`, which in the linked firmware
disables interrupts, copies the text "usb_update_mode" into the RAM block the boot
loader reads, sets a chip register bit and then waits (as far as the disassembly of the
linked firmware shows) for a reset to follow. (The
same routine is behind the SysEx `F0 22 24 35 7D F7` over DIN or Bluetooth MIDI in
the sealed engine.) DX747's own code writes no flash before calling it. **What the
boot loader does after that reset, and whether the workbench page can talk to it,
are not known:** it has not been tried on hardware. Do not count on it. Test it only
after the programmer round trip above works.

**If the voice-library write of a ROM import is refused** with a device error while
you have not saved a scene yet, an unfinished stock save may be waiting in the
staging area; saving a scene on the unit and agreeing to the flash setup clears it.

## What the build is, and who it says it is

- The check build writes `build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc`, but the
  workbench cannot load a loose file. A custom firmware is installed from the page
  that `build.bat --package` builds, `build\official-web\FM1-Official-Open-Source.html`,
  which carries the firmware inside it ([BUILD-WINDOWS.md](BUILD-WINDOWS.md), Step 5).
  The same page also carries the original V15 firmware, for going back. The
  official hosted workbench has no way to take a local file.
- DX747 has its own identity: release label **V01** and update identity
  **`FM-1_8747747`** (`app/version.h`), not M-VAVE's Open V14 (`FM-1_8000014`). The page
  still calls any such build "Official Open firmware" (that wording is in M-VAVE's page,
  which cannot be changed), but its Version row shows V01, and when an install
  finishes it shows the identity `FM-1_8747747`. The page treats every identity of the
  shape `FM-1_8` plus six digits as an Open-compatible build and arms the update with
  the firmware family and the build hash, so an update from or to M-VAVE's Open V14 is a
  normal "Start update". The same constant is the product code inside the update package;
  that the unit's update loader accepts a package with a product code other than the
  running firmware's is **not verified** (the SDK's `app/version.h` is the designed place
  to set it, and a stock V15 accepts Open packages, but nobody has tried this one).
  Tell builds apart by the **Build ID** under "Firmware details" on the page (its first 16
  characters; also in `build\dx7\build-identity.json`) and by the file's SHA-256.
- The page is M-VAVE's, with their name and logo in it, and the SDK checks its files
  by hash, so it cannot be rebranded.
- At the end of an install the page's status says that sample initialization is
  incomplete and tells you to write the built-in bank from the sampler page, and an
  error line says "Invalid file or parameters". That is expected: DX747 has no
  sample bank and refuses the sample step, and nothing is written. The firmware is
  installed. Do not use the sampler page.

## Before you publish a build

- **Publish the source that matches the file you publish.** The SDK's build refuses
  to make a distribution package without it: set `FM1_SOURCE_URL` to the HTTPS
  address of an archive of this exact source (with the licences and the build
  scripts), then run `build.bat --package`. A moving page such as a branch view is
  not a matching archive.
- Say plainly that it has not been run on hardware, until it has. The checklist is
  in [ON-DEVICE-CHECKLIST.md](ON-DEVICE-CHECKLIST.md).
- **The name.** "DX7" is a Yamaha product name and "747" is a Boeing aircraft name,
  and "FM-1" is M-VAVE's product name. Before a public release, decide the name and
  check it. Things to settle: whether to keep "DX747", trademark searches for the
  words in your country, and asking M-VAVE how they want firmware for their product
  named. Keep the "not made or endorsed by Yamaha, Boeing or M-VAVE" line.
- The logo on the boot screen is drawn in code (a white aircraft with a cyan tail);
  it copies no artwork.
- The firmware's code is GPL-3.0-only with the SDK's linking exception; the sealed
  engine library is the SDK's. `THIRD_PARTY.md` lists the third-party code, and
  DX747 adds none.
