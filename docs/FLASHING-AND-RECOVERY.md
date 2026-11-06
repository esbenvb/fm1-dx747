# Flashing DX747 and getting back

**The author has flashed DX747 to a real FM-1 and it works**, installing from the
stock firmware and from another firmware, and installing another firmware over it. The
caution stands: much of the firmware's behaviour is proven only in the simulator and on
the host, and a firmware that does not start can leave a unit that needs more than the
workbench to recover. Read this before you do it.

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
   `build.bat --package` steps as in [BUILDING.md](BUILDING.md)), install
   it the way "Installing from the page" below describes (from a unit on the original V15 that means the
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

## Installing from the page

Only after the way back is in place (above). The page is `build/official-web/FM1-DX747.html` ([BUILDING.md](BUILDING.md)), or the
Update tab of `DX747-Workbench.html`. These steps were written from the page's code and a simulated unit; the author has since
installed on a real FM-1, but if a step does not match what you see, trust what you see. The page says itself that a successful
transfer does not guarantee that your data is kept, or that the unit recovers from a power cut during the write.

Before you start: Chrome or Edge, a good USB cable straight into the PC, the PC set
not to sleep, **every other MIDI device unplugged** from the PC (the force flash
sends identity queries to every MIDI port it finds), and no other MIDI program open.

1. Open the page. It opens on the "Play & edit" tab; the update is on **Device update**.
   Tick **I have backed up the sounds, projects and samples I want to keep**:
   the install button stays disabled until you do.
2. **Connect.** Answer the browser's MIDI and SysEx permission prompt at once: the
   connection gives up after 12 seconds ("Connection timed out"), and you can click
   Connect again.
   - **From a unit that runs the original V15:** the page says "Different firmware
     detected" and keeps Install disabled. Click **Enable force flash and reconnect**
     (it only switches the mode on and reconnects; nothing is written), then **Force
     flash**, and accept the warning that it may prevent startup and need a recovery
     tool. That warning is meant seriously.
   - **From a unit that runs an Open build** (the official one or DX747): the page says
     "FM1 connected. Ready to update" and the button reads "Start update". The page
     treats any identity of the shape `FM-1_8` plus six digits as an Open-compatible
     build, so DX747 (`FM-1_8747747`) and M-VAVE's Open V14 (`FM-1_8000014`) update each
     other as normal (see below). If the unit already runs this exact build it says
     "This version is already installed. No update is needed." and installs nothing.
3. **While it writes:** keep USB and power connected, keep the PC awake, keep the tab
   open, touch nothing. Closing the tab only brings up a warning.
   If something is interrupted, open the same page again and connect: a unit that comes
   back in update mode shows "Connected in update mode. You can continue writing this
   version". Whether a unit interrupted in the middle of the write always comes back
   that way is not verified.
4. **When it finishes** the page's status line says "Firmware updated, but sample
   initialization is incomplete. Write the built-in bank from the sampler page", and
   the red error line below it says "Invalid file or parameters. The current project is
   unchanged". **That is expected for DX747 and the firmware is installed.** DX747 has
   no sample bank to write and refuses the sample step, so the page's automatic sample
   step always fails; nothing is written and no samples are lost at this step. Do not
   repeat the update and do not follow the page's advice to use the sampler page (the
   page may also suggest checking the sample bank under "Projects & sounds"; DX747 has
   none).

To go back to the original V15, use **Recovery & force update** and **Restore original
V15**. Choosing it clears the backup box, so tick it again, then Connect and **Install
original V15**, and confirm. The page's own confirmation says that the four voice banks
will be replaced and that Open projects and samples may be erased. **Back up voices,
projects and samples before you do this.** (On a unit that already runs V15 the same
button only restores the four voice banks.)

DX747 has its own identity, release **V01** and update identity **`FM-1_8747747`**
(`app/version.h`). The page still labels any such build "Official Open firmware" (M-VAVE's
wording; its files are hash-checked, so it cannot be changed), but its Version row shows
V01, **Firmware details** shows the **Build ID** (the first 16 characters of the build
identity; hover for all 64; also in `build\dx7\build-identity.json` after a build), and
at the end of an install the page shows the identity `FM-1_8747747`. After the
reboot the page waits for the unit to answer with that same identity; a unit that answers
with another one makes it report that the version check failed. The page is M-VAVE's, with
their name and logo in it.

## Going back to the stock firmware

With the unit starting normally, use the page you built, which carries the original
V15 (the official hosted workbench has no way to take a local file, and a loose
firmware file cannot be loaded): **Recovery & force update**, **Restore original V15**,
connect, tick the backup box again (choosing Restore clears it), **Install original
V15** and confirm ("Installing from the page", above). It also restores the unit if DX747
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

- The check build (`sdk\Build.ps1 -FirmwareOnly`) writes `build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc`
  (`build.bat --package` and `./build.sh` also leave it as `FM1_DX747.fwsc`), but the
  workbench cannot load a loose file. A custom firmware is installed from the page
  that `build.bat --package` builds, `build\official-web\FM1-DX747.html`,
  which carries the firmware inside it ([BUILDING.md](BUILDING.md)), or
  from `DX747-Workbench.html` (its Update tab, [DX747.md](../DX747.md), "The DX747 workbench"), which
  carries the same package.
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
