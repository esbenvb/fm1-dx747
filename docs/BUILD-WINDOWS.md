# Building the DX747 firmware on Windows

There are two builds, and it matters which one you want.

1. **The check build** (`Build.ps1 -FirmwareOnly`). It compiles and links the
   firmware with JieLi's Windows toolchain, runs every check the SDK has, and writes
   a firmware package, `FM1_OFFICIAL_OPEN.fwsc`. It is fast and it proves the build
   works. **That file cannot be flashed by itself:** the M-VAVE workbench only
   installs a firmware that was built into its own page.
2. **The flashing page** (`build.bat --package`). It does the check build and then
   builds a single web page, `FM1-Official-Open-Source.html`, with the firmware
   inside it. You open that page in a browser and install from it. It needs a source
   address, a one-time download of 518 MiB, and a few more minutes.

Both use the SDK's own scripts and **JieLi Windows toolchain 2.5.3**, which the
script checks file by file against hashes in `sdk/manifest.json`. Only Windows can
do either, because the packaging step compiles `sdk/Package.cs` with PowerShell
while the build runs.

Building is not flashing. Nothing here touches a device. Read
[FLASHING-AND-RECOVERY.md](FLASHING-AND-RECOVERY.md) before you ever flash.

What was checked and what was not: every check and error message below is read
from `sdk/Build.ps1` and `tools/Build-Web.ps1`, and the Mac route at the end was
run. The firmware's browser build was run too, with the same compiler version and
flags the Windows build uses. Steps 1 to 4 have since been run on the project's
Windows PC and the check build passed (below). Steps 5 and 6 have not been run on
Windows, so if a step does not match what you see, trust what you see and send it to
me (see the end).

## What you need

- Windows 10 or 11, 64-bit, with the built-in **Windows PowerShell 5.1** (PowerShell 7 also works).
- JieLi toolchain **2.5.3**. No other version works: older kits pinned 2.5.2 and reject 2.5.3.
- This source tree, in a short plain path such as `C:\fm1`.
- For the flashing page only: `curl.exe` and `tar.exe` (both ship with Windows 10
  1803 and later), an internet connection for the first run, about 2 GB of free
  disk space, and the **Microsoft Visual C++ Redistributable for x64, version 14.40 or
  newer**. Windows can have an older one (14.34, from 2023, was found on the project's
  PC), and the WASI SDK compiler then crashes at start-up with no message (see the
  table). Install the latest from https://aka.ms/vs/17/release/vc_redist.x64.exe.
  `Check-Environment.ps1` (Step 3) reports the version.

You do not need Visual Studio, Python, Node, Docker or WSL.

## Step 1: put the source on the PC

Use a short folder outside OneDrive and outside Desktop or Documents: `C:\fm1`.
Windows limits paths to 260 characters, and the flashing page's WASI SDK (Step 5,
unpacked in `build\tools` inside the source folder) has files about 165 characters
below the source folder (measured on the macOS build of the same SDK), so keep the
source folder's own path under about 90 characters.

Only what is **committed and pushed** travels, and a clone of GitHub shows only what
is on GitHub. On the Mac, commit everything you need first (`git status` must show
nothing modified or untracked that matters, including this guide, the check script
and the firmware changes), push, and check that `git log -1 origin/dx747` is the
commit you mean to build. On the PC, `git log --oneline -1` must then show the same.

**From GitHub**, if the branch is in your own repository (`git remote -v` on the
Mac shows where; this project's remote is `esbenvb/fm1-dx747`):

```powershell
git -c core.autocrlf=false clone --branch dx747 https://github.com/esbenvb/fm1-dx747.git C:\fm1
```

A private repository asks you to sign in. The SSH address the Mac uses for it is
specific to the Mac, so use the HTTPS one on the PC.

**Without GitHub**, on the Mac, in the repository, pick one:

```bash
git bundle create ~/dx747.bundle dx747
git archive --format=zip --prefix=fm1/ -o ~/dx747.zip dx747
```

A bundle is cloned on the PC like a repository:

```powershell
git -c core.autocrlf=false clone --branch dx747 D:\path\to\dx747.bundle C:\fm1
```

A zip needs no Git on the PC. Extract it so that the folder is `C:\fm1`:

```powershell
tar.exe -xf D:\path\to\dx747.zip -C C:\
```

(or in Explorer: Extract All, and change the destination to `C:\`.)

Do not make the zip with Finder's "Compress". It adds `__MACOSX` folders and
`.DS_Store` files; a stray file **inside `sdk\`** stops the build ("Unexpected SDK
file"), and they are not wanted anywhere else either.

Line endings matter. `.gitattributes` keeps the hashed files untouched, so a clone
or an extracted `git archive` is correct. A copy that has been through an editor, a
text-mode transfer or "convert line endings" is not: the build stops with `SDK
payload changed`, `Native DSP kernels and SDK audio contracts do not match` or
`Fixed DSP coefficients differ from the native kernels` (the last one is what Windows
line endings in `app\synth\*.inc` cause).

## Step 2: install the toolchain

1. Download the 2.5.3 installer from JieLi:
   `https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe`
   (JieLi's own installation guide, in Chinese:
   `https://doc.zh-jieli.com/Tools/zh-cn/dev_tools/dev_env/jl_toolchain.html`).
2. Run it and keep the default folder, `C:\JL`.
3. Check that this file exists: `C:\JL\pi32\bin\clang.exe`.
4. Check its hash. In PowerShell:

```powershell
(Get-FileHash C:\JL\pi32\bin\clang.exe -Algorithm SHA256).Hash.ToLower()
```

It must print `d4bc97cf421c1ccc373dd8924ce88720ff7457b3f1478d15df4aeda6ae923bce`.
If it prints something else, you have a different toolchain version or a damaged
install: remove it with its own uninstaller (or delete only `C:\JL\pi32`, since
`C:\JL` may hold other JieLi tools of yours), and install 2.5.3 again.

The installer's own screens could not be checked here; the folder names come from
the SDK's README and manifest.

## Step 3: let the check script find problems first

```powershell
cd C:\fm1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dx747\windows\Check-Environment.ps1
```

It repeats the checks `Build.ps1` makes before compiling (the SDK files, no stray
files under `sdk\`, the engine library, the version header, the 22 toolchain
files) and prints `ok`, `FAIL` (this would stop the build) or `advice` (commonly
goes wrong) with what to do. It changes nothing. Fix the first `FAIL` and run it
again. When it says "All checks passed" it also prints the exact build command.

## Step 4: the check build

```powershell
cd C:\fm1
powershell -NoProfile -ExecutionPolicy Bypass -File sdk\Build.ps1 -FirmwareOnly -Toolchain C:\JL\pi32
```

`-ExecutionPolicy Bypass` matters: without it Windows usually refuses to run the
script ("running scripts is disabled on this system").

Expected last lines (the numbers vary a little between toolchain builds):

```
Build FM1 Official Open V14
...
Link dx7 using prebuilt FM1 SDK
FM1: 427880 bytes app image; SRAM/BLE/USB checks passed
Offline package fits 1 MiB: image 446464, VM 57344, user 536576 bytes
Firmware: C:\fm1\build\dx7\sdk.elf
```

This is what the project's Windows PC printed for commit `e59e3e7`. JieLi's Linux
toolchain, run in Docker on the Mac, gave the same 427,880 bytes for that tree (the
limit is 453,921), the same section sizes (`.text` 346,048, allocated image 427,932)
and the same build identity (`b6715d53...`), so the Docker numbers are a good
stand-in for the Windows ones. The two `sdk.elf` files are not byte-identical (the
cause was not examined). Later commits add code (safe boot, the scene-store rules, the
workbench compatibility fixes): the current tree is 429,320 bytes with the Linux
toolchain, and has not been built on Windows yet, so expect about that number and
compare. On the unmodified upstream the Linux toolchain gave 453,576 bytes, about 160 bytes under
what the official build is recorded at, so the two toolchains have not been shown to
agree on every tree. A result far from these numbers (say more than a few thousand
bytes) means something is off.

Results are in `C:\fm1\build\dx7\`:

| File | What it is |
|---|---|
| `generated\FM1_OFFICIAL_OPEN.fwsc` | the firmware package (not installable by itself, see above) |
| `sdk.elf`, `sdk.map` | the linked program and its map |
| `firmware-report.json` | every check the build made, with sizes |
| `package-report.json` | how the image fits the 1 MiB flash |

**Every `Build.ps1` run deletes the previous output**, including a failed run and
this check build: `sdk.elf`, `sdk.map`, the `.fwsc`, **and the flashing page
`build\official-web\FM1-Official-Open-Source.html` with its `SHA256SUMS.txt`**, which
it replaces with a "Build incomplete" stub. All builds write to the same place. The
page is the only thing that can install DX747 and take you back to V15, so copy it and
its `SHA256SUMS.txt` out of `build\` before you run anything else. To build another
version of the source, such as the unmodified upstream for comparison (`git clone
https://github.com/m-vave/fm1_open.git C:\fm1-upstream`, then `git -C C:\fm1-upstream
checkout 4c7496d`), use its own folder.

Note the package's hash, so you can tell builds apart:

```powershell
(Get-FileHash C:\fm1\build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc -Algorithm SHA256).Hash
```

## Step 5: the flashing page

```powershell
cd C:\fm1
$env:FM1_PI32_TOOLCHAIN = 'C:\JL\pi32'
$env:FM1_SOURCE_URL = 'https://github.com/esbenvb/fm1-dx747/archive/<the full commit id>.zip'
.\build.bat --package
```

- `build.bat --package` has no toolchain option; it reads `FM1_PI32_TOOLCHAIN`.
- `FM1_SOURCE_URL` is the address of the source this firmware was built from. The
  page prints it, and the SDK refuses to build without one. The build only checks
  that it is an `https` address with no password in it. Use the archive address of
  **the exact commit** (`git rev-parse HEAD`), not of a branch, because a branch
  moves. If you ever share the page or the firmware, that address has to work for
  the people who get it, which for a private repository means making it public.
- The first run downloads the WASI SDK, 518 MiB, with `curl.exe` and checks its
  hash. Set `FM1_WASI_SDK` to an unpacked copy to reuse one. The page contains a
  copy of the instrument that runs in the browser, and this SDK compiles it.

The result is `C:\fm1\build\official-web\FM1-Official-Open-Source.html` (with
`SHA256SUMS.txt` beside it). It contains the workbench, the browser instrument, the
DX747 firmware, and the original V15 firmware for going back. The firmware travels
inside the page, so installing it downloads nothing. **Copy the page and its
`SHA256SUMS.txt` somewhere safe now** (see the warning above), then go to Step 6.

The last lines of a good run (this is what the project's Windows PC printed, after
the Visual C++ runtime was updated, for commit `e59e3e7`):

```
Compile the browser application
Integrated FM1 workbench: C:\fm1\build\official-web\FM1-Official-Open-Source.html
Firmware: C:\fm1\build\dx7\sdk.elf
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
     "FM1 connected. Ready to update" and the button reads "Start update". It treats
     DX747 as the same product and version as the official Open V14 (see below). It
     does compare the build identity: if the unit already runs this exact build it says
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

The page labels DX747 "Official Open firmware", with version V14, because DX747
reports the same product and version as the official Open V14. To tell builds apart,
open **Firmware details** on the page: **Build ID** shows the first 16 characters of
the build identity (hover for all 64). Note them; they are also in
`build\dx7\build-identity.json` after a build. The page is M-VAVE's, with their name and
logo in it; the SDK checks its files by hash, so it cannot be changed.

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
| `The specified path ... too long` | Move the folder to `C:\fm1`. |
| `clang.exe` or `pi32v2-ld.exe` disappears or "Access is denied" | Antivirus quarantined a toolchain file. Restore it and add `C:\JL` and `C:\fm1` to the exclusions. |
| `Set FM1_SOURCE_URL or -SourceUrl to the HTTPS URL ...` | Step 5: set `FM1_SOURCE_URL` first. It must start with `https://` and have no user name or password in it. |
| `WASI SDK archive SHA-256 mismatch`, or `curl.exe` errors | The download was cut off or blocked. Run again (a wrong archive is downloaded again), or unpack a WASI SDK 27 yourself and set `FM1_WASI_SDK`. |
| `Set FM1_WASI_SDK to an extracted WASI SDK 27 directory.` | The unpacking was interrupted. Delete the folder `build\tools\wasi-sdk-27.0-x86_64-windows` and run again. |
| `Matching SDK original V15 recovery firmware is missing or invalid` | `sdk\firmware\FM-1_015.fwsc` is missing or changed. Get the source again. |
| errors from the `Compile the browser application` step | The page's browser instrument did not compile. This tree compiles with the same flags on the Mac, so suspect a changed file: send me the first error. |

## What to send me if it still fails

Send these three things and I can usually say what is wrong:

1. The whole output of `Check-Environment.ps1`.
2. The last 40 lines of the build output, starting a few lines before the first red text.
3. The output of `git log --oneline -1` in `C:\fm1`, or say you used the zip.

## Quick checks on the Mac, without Windows

Docker can build and gate the firmware in seconds, which is how size and symbol
problems are found early. It uses JieLi's Linux toolchain, which is close to but not
proven identical with the Windows one, and it cannot package a `.fwsc` or make the
flashing page. Treat it as a quick check, and treat the
Windows build as the image that gets flashed.

You need Docker Desktop running, Node on the path, and the Linux toolchain
unpacked once:

```bash
mkdir -p ~/.cache/dx747/jieli-toolchain && cd ~/.cache/dx747/jieli-toolchain
curl -LO https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-20260730.1.tar.xz
shasum -a 256 jieli-linux-toolchains-20260730.1.tar.xz
tar -xf jieli-linux-toolchains-20260730.1.tar.xz
```

The archive is 26,027,508 bytes and its SHA-256 must be
`a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd`; the address and
hash are also in `tools/dx747/docker/toolchain.json`.
Then, from the repository:

```bash
tools/dx747/docker/build.sh -o build/dx747/docker/work .
```

It prints the app image size, the headroom under the 453,921 byte limit, the heap,
and whether every gate passed. This tree:

```
FM1: 429320 bytes app image (cap 453921, headroom 24601); heap 231884 bytes (min 49152); SRAM/BLE/USB checks passed
```

and the unmodified upstream (commit `4c7496d`), for comparison:

```
FM1: 453576 bytes app image (cap 453921, headroom 345); heap 247340 bytes (min 49152); SRAM/BLE/USB checks passed
```

The browser build that `build.bat --package` needs can be tried on the Mac too, with
the macOS WASI SDK 27 unpacked in `~/.cache/dx747/wasi-sdk/`:

```bash
mkdir -p ~/.cache/dx747/wasi-sdk && cd ~/.cache/dx747/wasi-sdk
curl -LO https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-27/wasi-sdk-27.0-arm64-macos.tar.gz
shasum -a 256 wasi-sdk-27.0-arm64-macos.tar.gz
tar -xzf wasi-sdk-27.0-arm64-macos.tar.gz
cd - && tools/dx747/sim/build.sh -o build/dx747/sim/work .
```

That archive is 113,649,205 bytes with SHA-256
`055c3dc2766772c38e71a05d353e35c322c7b2c6458a36a26a836f9808a550f8` (Apple Silicon; the
build script's default path assumes it). It passes for this tree.

The helper scripts `tools/dx747/docker/fetch-toolchain.sh` and
`tools/dx747/sim/fetch-wasi-sdk.sh` do these two downloads and the hash checks for you
(add `--download` to fetch; without it they only verify). `tools/dx747/docker/BASELINE.md`
and `tools/dx747/sim/README.md` describe the baseline build and the browser build.
