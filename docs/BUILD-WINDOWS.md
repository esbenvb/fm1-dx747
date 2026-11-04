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
flags the Windows build uses. The Windows steps themselves could not be run where
this was written, so if a step does not match what you see, trust what you see and
send it to me (see the end).

## What you need

- Windows 10 or 11, 64-bit, with the built-in **Windows PowerShell 5.1** (PowerShell 7 also works).
- JieLi toolchain **2.5.3**. No other version works: older kits pinned 2.5.2 and reject 2.5.3.
- This source tree, in a short plain path such as `C:\fm1`.
- For the flashing page only: `curl.exe` and `tar.exe` (both ship with Windows 10
  1803 and later), an internet connection for the first run, and about 2 GB of free
  disk space.

You do not need Visual Studio, Python, Node, Docker or WSL.

## Step 1: put the source on the PC

Use a short folder outside OneDrive and outside Desktop or Documents: `C:\fm1`.
Windows limits paths to 260 characters.

Only what is **committed and pushed** travels, and the documents and the check
script in `docs\` and `tools\dx747\windows\` are in the tree only once they are
committed. If `git status` on the Mac shows them as untracked, commit first.

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
payload changed` or `Native DSP kernels and SDK audio contracts do not match`.

In a clone, `git log --oneline -1` in `C:\fm1` should show the commit the Mac shows.

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
install: uninstall it, delete `C:\JL`, and install 2.5.3 again.

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
FM1: 427xxx bytes app image; SRAM/BLE/USB checks passed
Offline package fits 1 MiB: image ..., VM ..., user 536576 bytes
Firmware: C:\fm1\build\dx7\sdk.elf
```

The app image of this DX747 tree is about 427,500 bytes with JieLi's Linux
toolchain (the limit is 453,921); the Windows number has not been measured and
may differ by a few hundred bytes. The unmodified upstream firmware is about
453,600 bytes with the same Linux toolchain. A result far from these means
something is off.

Results are in `C:\fm1\build\dx7\`:

| File | What it is |
|---|---|
| `generated\FM1_OFFICIAL_OPEN.fwsc` | the firmware package (not installable by itself, see above) |
| `sdk.elf`, `sdk.map` | the linked program and its map |
| `firmware-report.json` | every check the build made, with sizes |
| `package-report.json` | how the image fits the 1 MiB flash |

**Every build deletes the previous one's output** (`sdk.elf`, `sdk.map`, the
`.fwsc`), and all builds write to the same place. Copy a result somewhere before
you build again. To build another version of the source, such as the unmodified
upstream for comparison (`git clone https://github.com/m-vave/fm1_open.git
C:\fm1-upstream`, then `git -C C:\fm1-upstream checkout 4c7496d`), use its own
folder.

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
inside the page, so installing it downloads nothing. Open it in Chrome or Edge, connect the FM-1 by USB and use the page's firmware
update. The page's own screens could not be checked here.

## When it fails

The first line of the error tells you which check failed.

| What you see | What it means and what to do |
|---|---|
| `running scripts is disabled on this system` | Start with `powershell -NoProfile -ExecutionPolicy Bypass -File ...` as in Step 4. |
| `Install JieLi Windows toolchain 2.5.3 ... set FM1_PI32_TOOLCHAIN` | `-Toolchain` (or `FM1_PI32_TOOLCHAIN`) does not point at the folder that contains `bin\clang.exe`. Use `C:\JL\pi32`. The check script reports a wrong value. |
| `External toolchain does not match the SDK: <file>` | Wrong toolchain version or a damaged install. Reinstall exactly 2.5.3 (Step 2). The check script lists which of the 22 pinned files differ. |
| `Native DSP kernels and SDK audio contracts do not match` | `sdk\include\fm1_block.h`, `sdk\synth.json` or `sdk\lib\libfm1_engine.a` differs from what the SDK expects. Usually line endings: get the whole source again (Step 1). |
| `SDK payload changed: <file>` | A file under `sdk\` is not byte for byte what the manifest says. Almost always line endings or an editor. Get the source again and do not edit anything under `sdk\`. |
| `Unexpected SDK file: <path>` | An extra file under `sdk\`: `Thumbs.db`, `desktop.ini`, `.DS_Store`, an editor backup. Delete it. |
| `Set FM1_RELEASE_VERSION in app/version.h` | `app\version.h` was edited. Restore it. |
| `Required runtime symbol absent: <name>` or `Application callback was lost or replaced by a weak default: <name>` | A code change removed something the firmware must keep. A source problem, not a PC problem: send me the line. |
| `FM1 runtime heap is N bytes; need at least 49152` | The program uses too much RAM. Source problem: send me the line. |
| `Add-Type` errors, or "language mode" | The packaging step compiles `sdk\Package.cs` with `Add-Type`. A company policy (AppLocker, WDAC) can block it: use a personal machine. |
| `error:` lines from `clang.exe` | A compile error in the source. Send me the first one with its file and line. |
| `The specified path ... too long` | Move the folder to `C:\fm1`. |
| `clang.exe` or `pi32v2-ld.exe` disappears or "Access is denied" | Antivirus quarantined a toolchain file. Restore it and add `C:\JL` and `C:\fm1` to the exclusions. |
| `Set FM1_SOURCE_URL or -SourceUrl to the HTTPS URL ...` | Step 5: set `FM1_SOURCE_URL` first. It must start with `https://` and have no user name or password in it. |
| `WASI SDK archive SHA-256 mismatch`, or `curl.exe` errors | The download was cut off or blocked. Delete `build\tools` and run again, or unpack a WASI SDK 27 yourself and set `FM1_WASI_SDK`. |
| `Matching SDK original V15 recovery firmware is missing or invalid` | `sdk\firmware\FM-1_015.fwsc` is missing or changed. Get the source again. |
| errors from the `Compile the browser application` step | The page's browser instrument did not compile. This tree compiles with the same flags on the Mac, so suspect a changed file: send me the first error. |

## What to send me if it still fails

Send these three things and I can usually say what is wrong:

1. The whole output of `Check-Environment.ps1`.
2. The last 40 lines of the build output, starting a few lines before the first red text.
3. The output of `git log --oneline -1` in `C:\fm1`, or say you used the zip.

## Quick checks on the Mac, without Windows

Docker can build and gate the firmware in seconds, which is how size and symbol
problems are found early. It uses JieLi's Linux toolchain, whose output differs
from the Windows toolchain by a few hundred bytes, and it cannot package a
`.fwsc` or make the flashing page. Treat it as a quick check, and treat the
Windows build as the image that gets flashed.

You need Docker Desktop running, Node on the path, and the Linux toolchain
unpacked once:

```bash
mkdir -p ~/.cache/dx747/jieli-toolchain && cd ~/.cache/dx747/jieli-toolchain
curl -LO https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-20260730.1.tar.xz
shasum -a 256 jieli-linux-toolchains-20260730.1.tar.xz   # a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd
tar -xf jieli-linux-toolchains-20260730.1.tar.xz
```

(26,027,508 bytes; the address and hash are also in `tools/dx747/docker/toolchain.json`.)
Then, from the repository:

```bash
tools/dx747/docker/build.sh -o build/dx747/docker/work .
```

It prints the app image size, the headroom under the 453,921 byte limit, the heap,
and whether every gate passed. This tree:

```
FM1: 427464 bytes app image (cap 453921, headroom 26457); heap 237772 bytes (min 49152); SRAM/BLE/USB checks passed
```

and the unmodified upstream (commit `4c7496d`), for comparison:

```
FM1: 453576 bytes app image (cap 453921, headroom 345); heap 247340 bytes (min 49152); SRAM/BLE/USB checks passed
```

The browser build that `build.bat --package` needs can be tried on the Mac too
(`tools/dx747/sim/build.sh -o build/dx747/sim/work .`, with a macOS WASI SDK 27
unpacked in `~/.cache/dx747/wasi-sdk/`); it passes for this tree.
