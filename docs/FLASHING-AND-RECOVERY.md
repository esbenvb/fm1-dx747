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
  the device settings take over the flash that holds the stock sample bank.
  Going back to stock firmware does not bring the samples back: restore them from
  your workbench backup.
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

## The power-on recovery chord (unverified)

Holding **OCT-** and **OCT+** while switching the unit on calls `fm1_device_recovery()`,
a routine inside the chip's ROM that the SDK does not document. It is meant to put
the unit into its USB updater. It has **not been tested on hardware**: whether it
works, and what state the unit is in afterwards, are unknown. DX747's own code writes
no flash before calling it; what the ROM routine does is not known.
Do not count on it. Test it only after the programmer round trip above works.

## What the build is, and who it says it is

- The check build writes `build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc`, but the
  workbench cannot load a loose file. A custom firmware is installed from the page
  that `build.bat --package` builds, `build\official-web\FM1-Official-Open-Source.html`,
  which carries the firmware inside it ([BUILD-WINDOWS.md](BUILD-WINDOWS.md), Step 5).
  The same page also carries the original V15 firmware, for going back. The
  official hosted workbench has no way to take a local file.
- The firmware still reports the SDK's identity, `FM-1_8000014` and release `V14`,
  so the page shows it as the "Official Open firmware", version V14, and cannot tell
  it from M-VAVE's own Open V14. Tell builds apart by the **Build ID** under "Firmware
  details" on the page (its first 16 characters; also in `build\dx7\build-identity.json`),
  and by the file's SHA-256. Giving DX747 an identity of its own is an open decision (the
  workbench pages would then recognise it as a different product).
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
