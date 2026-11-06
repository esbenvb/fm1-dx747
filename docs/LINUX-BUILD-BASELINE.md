# Baseline firmware build (M0, the first Linux/Docker build, 4 November 2026)

This is the record of the first build and comparison. The build scripts it names have since been replaced by the portable build
(`./build.sh`, `tools/docker`, `tools/portable`, [docs/BUILDING.md](BUILDING.md)); the results and the comparison method still hold.

The unmodified `m-vave/fm1_open` tree at commit `4c7496d` ("Fix Quick Edit display and support JieLi 2.5.3",
`FM1_RELEASE_VERSION "V14"`), exported with `git archive 4c7496d` (never the working tree), built in a
linux/amd64 container with JieLi's Linux toolchain. Nothing under `sdk/` was edited or copied.

## Result

| | |
|---|---|
| Result | Builds first try; every `Test-Firmware` gate from `sdk/Build.ps1` passes unchanged |
| **App image** | **453,576 bytes** (`.text` 372,576 + `.data` 0 + `.ram0_data` 80,968 + `.cache_ram_data` 32) |
| Cap (`sdk/Package.cs`, read by `gates.mjs`) | 453,921 bytes = 0xff000 − 536,576 user − 4,096 BTIF − 32,768 min VM − 0x4000 area − 0x120 − 383 − 64 |
| **Headroom** | **345 bytes** |
| **Heap** (`_MALLOC_SIZE` in `sdk.map`) | **247,340 bytes** (0x3c62c, `0x01c43700`..`0x01c7fd2c`); gate minimum 49,152 |
| SRAM (`ram0`, `RAM0_SIZE`) | 523,596 B = 80,968 `.ram0_data` + 195,232 `.ram0_bss` + 247,340 heap + 56 (32 B heap-end guard, alignment); `.boot_info` (52) and `.cache_ram_data` (32) sit outside it |
| ACL pools | tx 1,024 / rx 4,096 (gate minimum) |
| ELF | 920,212 bytes, 24,361 symbols, entry `0x02000120` |
| `sha256(app image)` | `8665ee26676d5237cbdb20a0cf8229e95996be5780027fc275d0734a05ec4779` (objcopy `-O binary` of the four sections, cross-checked) |
| `sha256(sdk.elf)` | `d7e8579c77aae1ded3ad65e0652f632e3e76354e7cd5575563012f977f91eb60` |
| Build time | 7 s in the container (Rosetta), 10 s with gates; Docker image 113 MB |
| Reproducible | Yes: a second export under a different, longer path and output dir gave the identical ELF |

Consequences for the plan:

- **Headroom is 345 B, not "about 185 B"** (that figure is for the V14 *release*, a different commit and
  toolchain build; I did not download the release to compare). Either way the image is full: the M5 audio stage
  alone is estimated at ~3 KB (`m5-audio-out-stage.md`), so M7 has to remove stock code first, as planned.
- **The heap is large: 247,340 B at link time, 5× the gate minimum.** Eight DX7 instances at ~40 KB fit with a wide
  margin on paper. It is the whole static remainder of SRAM, shared at run time with the RTOS, Bluetooth stack
  and USB, so the free heap on a running device is unknown and still needs hardware. Every KB of static data
  added by DX747 comes straight out of it.

## Is the Linux toolchain output equivalent to Windows 2.5.3?

**Plausibly, not proven.** Evidence for:

- Same compiler release: `clang version 4.0.1`, and all 16 compiler headers in `lib/clang/4.0.1/include` match
  `sdk/manifest.json` byte for byte after LF→CRLF.
- Flags, per-source options (`-Oz`, `-fno-lto`, `-fno-pic`, `-fno-inline-functions`), object order and the link line
  are identical to `Build.ps1` (`container-build.sh`). The prebuilt Windows-built `libfm1_engine.a`, `libc.a` and
  `libcompiler_rt.a` link without complaint.
- Every gate passes unchanged: startup entry, SRAM placement of all audio callbacks and the panel scan code,
  required BLE/USB/SBC symbols present, forbidden platform runtimes absent, ACL pools, section whitelist.
- An independent third party (rfb/fm1-time-signal-source) records the same six Linux program hashes, and
  our measured hashes match theirs.

Evidence against / unknowns:

- The linker (`pi32v2/bin/ld`, GNU ld 2.26.51.20160621) and `nm` (GNU nm, not `llvm-nm`) are not the Windows
  `pi32v2-ld.exe` / `llvm-nm.exe`; their hashes cannot be compared. The `nm` letter classes differ
  (SRAM functions print as `T`/`t`, Build.ps1 notes `N` on Windows); the gates accept both.
- No Windows build of this exact tree was available, so code generation (and therefore the 345 B) is untested against it.
- The 32-byte build id depends on the file order of `Get-ChildItem | Sort-Object FullName`, which is
  culture-aware on Windows; `gates.mjs` approximates it (identical for the baseline's file names). It affects
  data bytes only, not size.

To settle it: build this tree on a Windows machine or runner (the plan's GitHub Actions fallback) and compare
`app_image_sha256` from `firmware-report.json` (the same hash printed above). A match proves equivalence for the tree.

## What was built, and how

```bash
tools/docker/fetch-toolchain.sh --download     # once: 26,027,508 B, sha256-pinned in toolchain.json
git archive 4c7496d | tar -x -C /some/empty/dir
tools/docker/build.sh [-o OUT_DIR] /some/empty/dir
```

`build.sh SRC_DIR` takes any tree laid out like the SDK repo (pristine export now, the working tree later). It mounts
the source read-only at `/src`, the toolchain read-only at `/opt/jieli`, writes only `OUT_DIR`
(default `<SRC_DIR>/build/dx7`, gitignored), and needs Docker, Node 18+ and nothing else (no PowerShell).

| Step | Where | What |
|---|---|---|
| `gates.mjs inputs` | host | `sdk/manifest.json` payload hashes, no unexpected SDK files, `synth.json` DSP contract and portable `.inc` hashes, `FM1_DSP_ABI`, toolchain headers and Linux program hashes (`toolchain.json`) |
| `gates.mjs prepare` | host | source list, `build_identity.c`, linker script from `linker.ld.in` |
| `container-build.sh` | container | compile every `app/**/*.c` (not `app/synth/`) with the `Build.ps1` flags, link, `nm --print-size` |
| `gates.mjs firmware` | host | port of `Test-Firmware` (symbols, registrations, SRAM, map, ACL, heap ≥ 49,152, ELF sections) **plus the app-image cap**, writes `firmware-report.json` |

Outputs in OUT_DIR: `sdk.elf`, `sdk.map`, `nm.txt`, `firmware-report.json` (sections, heap, headroom, hashes),
`commands.log` (exact compile/link argv), `build.log`, `gates.log`, `toolchain-versions.txt`. The gates fail with
`error: ...` and exit 1, e.g. an image over the cap (tested by shrinking the cap: `App image is 453576 bytes;
the FM1 layout allows at most 452921`). `gates.mjs` derives the cap from `sdk/Package.cs` and refuses to run if
that check changes shape.

Differences from `Build.ps1` (all deliberate): tool names (`common/bin/clang`, `common/bin/LLVMgold.so`,
`pi32v2/bin/ld`, `pi32v2/bin/nm`), absolute `/src` paths instead of `..\..`, `-MMD` dependency files dropped,
`.DS_Store` ignored in the SDK scan, and `ulimit -n 8192` per the toolchain README.

## Not done in M0

- No `.fwsc` packaging at that time (`Package.cs` port): not needed for the size gate. It exists now as
  `tools/portable/fwsc.mjs`, and its package is byte for byte the Windows one (docs/BUILDING.md).
- The working tree was not built (sibling sessions are editing `app/`); `build.sh` takes it as an argument.
- No download of the V14 release workbench for a cross-check, and no hardware run.

## Second comparison: Windows and Mac builds of the portable build (6 November 2026)

Commit `656b37a734972053d4d280eb3335e3f1aa80c17b` (branch `dx747-on-portable`, DX747 on the portable build of the SDK it
is a fork of), built once on a Windows PC with `build.bat --package` and once on a Mac with
`./build.sh --package --from-git 656b37a734972053d4d280eb3335e3f1aa80c17b`, both with
`FM1_SOURCE_URL=https://github.com/esbenvb/fm1-dx747/archive/656b37a734972053d4d280eb3335e3f1aa80c17b.zip` (the page embeds it).

| | Windows (JieLi 2.5.3, WASI SDK for Windows) | Mac (JieLi Linux toolchain in Docker, macOS WASI SDK) |
|---|---|---|
| `build_id` | `a7ed189583cac5e7fd24b55bbcfadaa1834b3257f2fc9fa7592b41acf055aecf` | the same |
| App image | 447,432 bytes of 453,921 | the same (headroom 6,489) |
| Image fit | image 466,944, VM 36,864, user 536,576 bytes | the same |
| `FM1_OFFICIAL_OPEN.fwsc` and `FM1_DX747.fwsc` | `25861feece92755d93e4c93eda01fe1ff4fcda559cd9c70489e9c5b87498e981` (487,988 bytes) | the same |
| `FM1-Official-Open-Source.html` and `FM1-DX747.html` | `85e68bc09477385c4e1df3d3ca9c8aa9f7452f34d3d1babe933603657ea45344` | the same |

So the firmware package is byte for byte the same from the two toolchains, which settles that the firmware image is, and the
flashing page is byte for byte the same too, so the browser core compiled by the two WASI SDKs is the same. On Windows
`build.bat --package` left `FM1_DX747.fwsc` and `FM1-DX747.html` beside the SDK-named files with the same bytes (the alias step
works); the Windows `SHA256SUMS.txt` lists only the SDK-named page, which has the same hash. `DX747-Workbench.html` is made only
by `./build.sh` (Mac and Linux, `tools/dx747/workbench.mjs`), so there is no Windows counterpart to compare. The loadable
sections were not compared separately (`tools/portable/elf-compare.mjs`) because the identical package contains them.
