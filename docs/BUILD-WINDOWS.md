# Building the DX747 firmware on Windows

There are two builds, and it matters which one you want.

1. **The check build** (`Build.ps1 -FirmwareOnly`). It compiles and links the
   firmware with JieLi's Windows toolchain, runs every check the SDK has, and writes
   a firmware package, `FM1_OFFICIAL_OPEN.fwsc` (the SDK's name; `build.bat --package` also leaves it as `FM1_DX747.fwsc`). It is fast and it proves the build
   works. **That file cannot be flashed by itself:** the M-VAVE workbench only
   installs a firmware that was built into its own page.
2. **The flashing page** (`build.bat --package`). It does the check build and then
   builds a single web page, `FM1-DX747.html`, with the firmware
   inside it. You open that page in a browser and install from it. It needs a source
   address, a one-time download of 518 MiB, and a few more minutes.

**Names.** DX747 is a fork of M-VAVE's open firmware (`FM1_OFFICIAL_OPEN`) and has its own identity, so its files are called
`FM1_DX747.fwsc` and `FM1-DX747.html`. The SDK scripts (`sdk/Build.ps1`, `sdk/WebUpdate.js`) are hash-pinned and not edited, so
they write the upstream names `FM1_OFFICIAL_OPEN.fwsc` and `FM1-Official-Open-Source.html`. **`build.bat --package`** (and
`./build.sh`) leaves a copy of each under DX747's name beside it, the same bytes; the copies from an earlier run are deleted
first, so a failed build never leaves a stale one. The check build of Step 4 (`sdk\Build.ps1 -FirmwareOnly`, and plain
`build.bat`) writes only the upstream names, and so does `Build.ps1` itself: where this guide says `FM1_DX747` for something
that `Build.ps1` writes, read the upstream name. `SHA256SUMS.txt` on Windows names the upstream page; the DX747-named copy has
the same bytes, so it has the same hash.

Both use the SDK's own scripts and **JieLi Windows toolchain 2.5.3**, which the
script checks file by file against hashes in `sdk/manifest.json`. This is the reference
route: the packaging step compiles `sdk/Package.cs` with PowerShell while the build runs.
On a Mac or Linux, `./build.sh` makes the same files with Docker and Node (last section).

Building is not flashing. Nothing here touches a device. Read
[FLASHING-AND-RECOVERY.md](FLASHING-AND-RECOVERY.md) before you ever flash.

**One rule for every command in this guide: run it from the root of the repository**,
the folder that contains `build.bat` and `sdk\`. You can clone the repository to any
folder you like (see Step 1); the commands and the paths in this guide are relative to
that folder, so there is nothing to adjust.

What was checked and what was not: every check and error message below is read
from `sdk/Build.ps1` and `tools/Build-Web.ps1`; the route for Mac and Linux at the end
is described in [BUILDING.md](BUILDING.md). Steps 1 to 5 have been run on a Windows PC (the project's) and passed. Step 6,
installing on a real FM-1, has not been run on a unit, so if a step does not match
what you see, trust what you see and report it (see the end).

## What you need

- Windows 10 or 11, 64-bit, with the built-in **Windows PowerShell 5.1** (PowerShell 7 also works).
- **Git for Windows** (https://git-scm.com/download/win) to get the source.
- **Code::Blocks**, because JieLi's toolchain installer needs it (Step 2), and
  JieLi's toolchain **2.5.3**. No other toolchain version works: older kits pinned 2.5.2
  and reject 2.5.3.
- For the flashing page only: `curl.exe` and `tar.exe` (both ship with Windows 10
  1803 and later), an internet connection for the first run, about 2 GB of free
  disk space, and the **Microsoft Visual C++ Redistributable for x64, version 14.40 or
  newer**. Windows can have an older one (14.34, from 2023, was found on the project's
  PC), and the WASI SDK compiler then crashes at start-up with no message (see the
  table). Install the latest from https://aka.ms/vs/17/release/vc_redist.x64.exe.
  `Check-Environment.ps1` (Step 3) reports the version.

You do not need Visual Studio, Python, Node, Docker or WSL.

## Step 1: get the source

The simplest way, and the only one you need, is to clone the repository. In PowerShell,
in the folder where you want it:

```powershell
git -c core.autocrlf=false clone --branch dx747 https://github.com/esbenvb/fm1-dx747.git
cd fm1-dx747
```

The clone is the whole source: the app, the SDK, the build scripts and these documents.
It creates a folder called `fm1-dx747` in the folder you ran it in (add a folder name
at the end of the `git clone` command to call it something else). **That folder is the
repository root, and every command in this guide is run from it.** It can be anywhere:
the guide never uses an absolute path to it. `git log --oneline -1` shows which
commit you have. (A private repository asks you to sign in; use the HTTPS address
above, not an SSH one.)

Choose the folder with two things in mind:

- **Keep the full path short**, under about 90 characters, plain ASCII, and outside
  OneDrive, Desktop and Documents. Windows limits paths to 260 characters, and the
  flashing page's WASI SDK (Step 5, unpacked in `build\tools` inside the clone) has
  files about 165 characters below the clone's folder. `C:\Users\you\code\fm1-dx747`
  is fine; a deep OneDrive path is not.
- **Do not edit files under `sdk\`, and do not let any tool convert line endings.**
  The build checks those files byte for byte against hashes, which is why the clone
  command turns line-ending conversion off. A file that went through an editor, a
  text-mode transfer or "convert line endings" stops the build with `SDK payload
  changed`, `Native DSP kernels and SDK audio contracts do not match` or `Fixed DSP
  coefficients differ from the native kernels` (the last one is what Windows line
  endings in `app\synth\*.inc` cause). If that happens, make a fresh clone.

## Step 2: install the toolchain

JieLi's installer would not run on the project's PC until Code::Blocks was installed
and had been started once and closed completely. This is the order that worked:

1. **Install Code::Blocks** (the standard Windows installer from
   https://www.codeblocks.org/downloads/binaries/). You will not use it for anything else.
2. **Start Code::Blocks once, then close it completely.** Closing the window is not
   enough: it can keep running in the background. Open Task Manager and end every
   `codeblocks.exe` process.
3. **Download and run JieLi's 2.5.3 installer:**
   `https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe`. Keep the default
   folder, `C:\JL`. (The toolchain lives there, separately from your clone of the
   repository. If you install it somewhere else, use that folder's `pi32` folder
   wherever this guide says `C:\JL\pi32`.)
4. **Check that `C:\JL\pi32\bin\clang.exe` exists, and its hash.** In PowerShell:

```powershell
(Get-FileHash C:\JL\pi32\bin\clang.exe -Algorithm SHA256).Hash.ToLower()
```

It must print `d4bc97cf421c1ccc373dd8924ce88720ff7457b3f1478d15df4aeda6ae923bce`.
If it prints something else, you have a different toolchain version or a damaged
install: remove it with its own uninstaller (or delete only `C:\JL\pi32`, since
`C:\JL` may hold other JieLi tools of yours), and install 2.5.3 again. JieLi's own
installation guide is only in Chinese and is not needed for this.

## Step 3: let the check script find problems first

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dx747\windows\Check-Environment.ps1
```

It repeats the checks `Build.ps1` makes before compiling (the SDK files, no stray
files under `sdk\`, the engine library, the version header, the 22 toolchain
files) and prints `ok`, `FAIL` (this would stop the build) or `advice` (commonly
goes wrong) with what to do. It changes nothing. Fix the first `FAIL` and run it
again. When it says "All checks passed" it also prints the exact build command.

## Step 4: the check build

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File sdk\Build.ps1 -FirmwareOnly -Toolchain C:\JL\pi32
```

`-ExecutionPolicy Bypass` matters: without it Windows usually refuses to run the
script ("running scripts is disabled on this system").

Expected last lines (the numbers vary a little between toolchain builds):

```
Build FM1 Official Open V01
...
Link dx7 using prebuilt FM1 SDK
FM1: 429544 bytes app image; SRAM/BLE/USB checks passed
Offline package fits 1 MiB: image 446464, VM 57344, user 536576 bytes
Firmware: <your clone>\build\dx7\sdk.elf
```

The numbers above are what the project's Windows PC printed for commit `2243433`. The
limit is 453,921 bytes. JieLi's Linux toolchain, run in Docker on the Mac, produced exactly the
same loadable content for that commit, byte for byte (see "How the two routes compare" in
[BUILDING.md](BUILDING.md)), so the Docker numbers and gates are a reliable stand-in for the
Windows ones for this tree. A result far from these numbers (say more than a few thousand
bytes) means something is off.

Results are in `build\dx7\` (inside your clone):

| File | What it is |
|---|---|
| `generated\FM1_OFFICIAL_OPEN.fwsc` | the firmware package, under the SDK's name (not installable by itself, see above) |
| `sdk.elf`, `sdk.map` | the linked program and its map |
| `firmware-report.json` | every check the build made, with sizes |
| `package-report.json` | how the image fits the 1 MiB flash |

**Every `Build.ps1` run deletes the previous output**, including a failed run and
this check build: `sdk.elf`, `sdk.map`, the `.fwsc`, **and the flashing page
`build\official-web\FM1-Official-Open-Source.html` with its `SHA256SUMS.txt`**, which
it replaces with a "Build incomplete" stub (a `FM1-DX747.html` copy from an earlier `--package` run is not touched by
`Build.ps1`: `build.bat` deletes it, so do not trust a `FM1-DX747.html` that is older than the build you want). All builds write to the same place. The
page is the only thing that can install DX747 and take you back to V15, so copy it and
its `SHA256SUMS.txt` out of `build\` before you run anything else. To build another
version of the source, such as the unmodified upstream for comparison (next to your clone, `git clone https://github.com/m-vave/fm1_open.git fm1-upstream`, then
`git -C fm1-upstream checkout 4c7496d`), use its own folder and run its commands from there.

Note the package's hash, so you can tell builds apart:

```powershell
(Get-FileHash build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc -Algorithm SHA256).Hash
```

## Step 5: the flashing page

From the repository root:

```powershell
$env:FM1_PI32_TOOLCHAIN = 'C:\JL\pi32'
$env:FM1_SOURCE_URL = "https://github.com/esbenvb/fm1-dx747/archive/$(git rev-parse HEAD).zip"
.\build.bat --package
```

- `build.bat --package` has no toolchain option; it reads `FM1_PI32_TOOLCHAIN`.
- `FM1_SOURCE_URL` is the address of the source this firmware was built from. The
  page prints it, and the SDK refuses to build without one. The build only checks
  that it is an `https` address with no password in it. The command above builds the
  archive address of **the exact commit** you cloned (`git rev-parse HEAD`), not of a
  branch, because a branch moves; a normal clone of the `dx747` branch is on GitHub,
  so the address works. If you changed the source, commit and push it first, and use
  your own repository's address. If you ever share the page or the firmware, that address has to work for
  the people who get it, which for a private repository means making it public.
- The first run downloads the WASI SDK, 518 MiB, with `curl.exe` and checks its
  hash. Set `FM1_WASI_SDK` to an unpacked copy to reuse one. The page contains a
  copy of the instrument that runs in the browser, and this SDK compiles it.

The result is `build\official-web\FM1-Official-Open-Source.html` inside your clone (with
`SHA256SUMS.txt` beside it), and `build.bat --package` leaves the same page as `FM1-DX747.html`. It contains the workbench, the browser instrument, the
DX747 firmware, and the original V15 firmware for going back. The firmware travels
inside the page, so installing it downloads nothing. **Copy the page and its
`SHA256SUMS.txt` somewhere safe now** (see the warning above), then go to Step 6.

The last lines of a good run (this is what the project's Windows PC printed, after
the Visual C++ runtime was updated, for commit `e59e3e7`):

```
Compile the browser application
Integrated FM1 workbench: <your clone>\build\official-web\FM1-Official-Open-Source.html   (build.bat then copies it to FM1-DX747.html)
Firmware: <your clone>\build\dx7\sdk.elf
```

**You can try the page with no unit connected.** Open it in Chrome or Edge: it says
"Browser preview · no device", and the **Play & edit** tab shows a 240 by 240 screen
that runs this firmware's own screens and logic (the browser build compiles the
`app\` sources), with on-screen controls. It is the SDK's open stand-in for the sound
engine, so it shows screens and behaviour, not the unit's CPU load or timing. Nothing
is sent to the unit while none is connected.

## Step 6: install from the page

Only after the way back is in place: read [FLASHING-AND-RECOVERY.md](FLASHING-AND-RECOVERY.md)
first. These steps come from reading the page's code and running it against a
simulated unit, not from a real FM-1, so write down anything that differs. The page
says itself that a successful transfer does not guarantee that your data is kept, or
that the unit recovers from a power cut during the write.

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

## When it fails

The first line of the error tells you which check failed.

| What you see | What it means and what to do |
|---|---|
| `running scripts is disabled on this system` | Start with `powershell -NoProfile -ExecutionPolicy Bypass -File ...` as in Step 4. |
| `Install JieLi Windows toolchain 2.5.3 ... set FM1_PI32_TOOLCHAIN` | `-Toolchain` (or `FM1_PI32_TOOLCHAIN`) does not point at the folder that contains `bin\clang.exe`. Use `C:\JL\pi32`. The check script reports a wrong value. |
| `External toolchain does not match the SDK: <file>` | Wrong toolchain version or a damaged install. Reinstall exactly 2.5.3 (Step 2). The check script lists which of the 22 pinned files differ. |
| `Native DSP kernels and SDK audio contracts do not match`, or `Fixed DSP coefficients differ from the native kernels` | `sdk\include\fm1_block.h`, `sdk\synth.json` or `sdk\lib\libfm1_engine.a` differs from what the SDK expects. Usually line endings (the second form is Windows line endings in `app\synth\*.inc`): get the whole source again (Step 1). |
| `SDK payload changed: <file>` | A file under `sdk\` is not byte for byte what the manifest says. Almost always line endings or an editor. Get the source again and do not edit anything under `sdk\`. |
| `Unexpected SDK file: <path>` | An extra file under `sdk\`: `Thumbs.db`, `desktop.ini`, `.DS_Store`, an editor backup. Delete it. |
| `Set FM1_RELEASE_VERSION in app/version.h`, or `Set a compatible FM1_UPDATE_PRODUCT in app/version.h` | `app\version.h` was edited. Restore it. |
| `Required runtime symbol absent: <name>` or `Application callback was lost or replaced by a weak default: <name>` | A code change removed something the firmware must keep. A source problem, not a PC problem: send me the line. |
| `FM1 runtime heap is N bytes; need at least 49152` | The program uses too much RAM. Source problem: send me the line. |
| `Command failed: ...\wasi-sdk-27.0-x86_64-windows\bin\clang.exe (-1073741819)` with no other message, at "Compile the browser application" | The WASI SDK compiler cannot start: it crashes in `MSVCP140.dll` because the Microsoft Visual C++ runtime is older than 14.40 (`clang.exe --version` crashes the same way, and Event Viewer, Windows Logs, Application shows `Faulting module name: MSVCP140.dll`). Install the latest Visual C++ Redistributable for x64 (https://aka.ms/vs/17/release/vc_redist.x64.exe), check that `build\tools\wasi-sdk-27.0-x86_64-windows\bin\clang.exe --version` prints a version, and run again. The firmware check build is not affected. |
| `Add-Type` errors, or "language mode" | The packaging step compiles `sdk\Package.cs` with `Add-Type`. A company policy (AppLocker, WDAC) can block it: use a personal machine. |
| `error:` lines from `clang.exe` | A compile error in the source. Send me the first one with its file and line. |
| `The specified path ... too long` | The clone's path is too long: clone to a shorter folder (any short path works, for example `C:\fm1`). |
| `clang.exe` or `pi32v2-ld.exe` disappears or "Access is denied" | Antivirus quarantined a toolchain file. Restore it and add `C:\JL` and your clone's folder to the exclusions. |
| `Set FM1_SOURCE_URL or -SourceUrl to the HTTPS URL ...` | Step 5: set `FM1_SOURCE_URL` first. It must start with `https://` and have no user name or password in it. |
| `WASI SDK archive SHA-256 mismatch`, or `curl.exe` errors | The download was cut off or blocked. Run again (a wrong archive is downloaded again), or unpack a WASI SDK 27 yourself and set `FM1_WASI_SDK`. |
| `Set FM1_WASI_SDK to an extracted WASI SDK 27 directory.` | The unpacking was interrupted. Delete the folder `build\tools\wasi-sdk-27.0-x86_64-windows` and run again. |
| `Matching SDK original V15 recovery firmware is missing or invalid` | `sdk\firmware\FM-1_015.fwsc` is missing or changed. Get the source again. |
| errors from the `Compile the browser application` step | The page's browser instrument did not compile. This tree compiles with the same flags on the Mac, so suspect a changed file: send me the first error. |

## What to send me if it still fails

Send these three things and I can usually say what is wrong:

1. The whole output of `Check-Environment.ps1`.
2. The last 40 lines of the build output, starting a few lines before the first red text.
3. The output of `git log --oneline -1` in the repository root.

## Building on a Mac or Linux, without Windows

DX747 uses the portable build that its author contributed to the SDK it is a fork of (`esbenvb/fm1_open`, branch `portable-build`, proposed to `m-vave/fm1_open`): `./build.sh` in the
repository root does what `build.bat` does, with Docker (JieLi's Linux toolchain in a container) and Node 18 or newer, and no
PowerShell. The guide is [docs/BUILDING.md](BUILDING.md), Option B: the one-time downloads (`./build.sh --setup`), the commands,
the checks and how the Linux toolchain was compared with the Windows one (every loadable section identical, a byte-identical
`.fwsc`, a byte-identical page).

```bash
./build.sh --setup                                           # once
./build.sh                                                   # firmware, the SDK's checks, build/dx7/generated/FM1_DX747.fwsc
./build.sh --web                                             # build/preview/index.html and DX747-Workbench.html
./build.sh --package --source-url "https://github.com/<you>/fm1-dx747/archive/$(git rev-parse HEAD).zip"
./build.sh --from-git <commit> ...                           # a clean export of a commit instead of the working tree
```

What DX747 adds to it:

- **Names.** The SDK steps write `FM1_OFFICIAL_OPEN.fwsc` and `FM1-Official-Open-Source.html`; `build.sh` (as `build.bat` does) leaves
  a copy of each as **`FM1_DX747.fwsc`** and **`FM1-DX747.html`**. `--package` also builds **`DX747-Workbench.html`**, the DX747
  workbench with the configurator, simulator, keyboard and updater (`tools/dx747/workbench.mjs`, from `web/page/`), in
  `build/official-web/`; its `SHA256SUMS.txt` lists all three pages. `--web` builds it without firmware beside the preview.
- **One more symbol check.** `tools/docker/gates.mjs` also requires the master filter's functions (`mfx_run`, `mfx_lp`, `mfx_bp`,
  `mfx_hp`), which run in the audio interrupt, to be in SRAM wherever they survive as symbols.
- **Checked against the previous DX747 build scripts.** The same commit built with the old DX747-only scripts and with
  `./build.sh --package` and the same source address gave the same application image, the same package and byte-identical
  `FM1-DX747.html` and `DX747-Workbench.html`.

To compare a Windows build of your tree with a Docker build, see "Comparing your own builds" in [docs/BUILDING.md](BUILDING.md):
`node tools/portable/elf-compare.mjs build/dx7/sdk.elf <windows folder>/sdk.elf` ends with `loadable content: IDENTICAL` or
`DIFFERENT`. The record of the first comparison for DX747 (commit `2243433`, 4 November 2026: the same `build_id` and sizes,
identical loadable sections, and a `.fwsc` byte for byte the Windows one, SHA-256 `385f83e5a01a17e2...`) is in
[docs/LINUX-BUILD-BASELINE.md](LINUX-BUILD-BASELINE.md).
