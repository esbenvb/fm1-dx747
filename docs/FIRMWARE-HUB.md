# Listing DX747 on The Firmware Hub

[The Firmware Hub](https://thefirmwarehub.com) lists open FM-1 firmwares with a card, one-click install in the browser and a
follow-the-releases sync. These are the hub's own rules ([submit guide](https://thefirmwarehub.com/submit/), [fmhub.md](https://thefirmwarehub.com/fmhub.md),
[knowledge base](https://thefirmwarehub.com/kb/)) and where DX747 stands against each. Checked on 6 November 2026.

| # | The hub needs | DX747 |
|---|---|---|
| 1 | Open source: an open licence, the complete source public, a `LICENSE` file in the root | The repository is public. [LICENSE.md](../LICENSE.md): GPL-3.0-only with the FM1 SDK linking exception. The sealed engine library (`sdk/lib/libfm1_engine.a`) is M-VAVE's binary, under the terms in [THIRD_PARTY.md](../THIRD_PARTY.md), as in the open firmware it is a fork of. Say so in the mail. |
| 2 | A GitHub release with the `.fwsc` attached | [`.github/workflows/release.yml`](../.github/workflows/release.yml): pushing a tag `v...` builds `FM1_DX747.fwsc` on GitHub with the hub's shared workflow, signs it and attaches it to the release (about two minutes). |
| 3 | Its own identity, not shared with another firmware | **`FM-1_8747747`** (`app/version.h`). Not in the hub's catalogue on 6 November 2026 ([check it live](https://thefirmwarehub.com/submit/#check)). |
| 4 | The update loader in the package | Yes: M-VAVE's own loader (`sdk/tools/pack/ota.bin`), the way a firmware built on the official V15 brings it. |
| 5 | Tested on a real FM-1: installed from V15, installed from another firmware, and another firmware installs from it | Tested by the author on a real FM-1, all three (confirmed 6 November 2026); see "Before the mail" for what else to try. |
| 6 | `fmhub.json` in the root of the default branch (`dx747`) | [fmhub.json](../fmhub.json), with the card picture [docs/card.png](card.png) (240 x 240, the splash screen). |

## The card

`fmhub.json` is the whole card: text, author, kind (`synth`) and colour (the DX green, `#04DFBF`); the picture is
`docs/card.png`. `play` and `editor` point at the workbench on GitHub Pages, `https://esbenvb.github.io/fm1-dx747/#device` (the firmware
running in the browser) and `#configure` (the web editor); [`.github/workflows/pages.yml`](../.github/workflows/pages.yml) builds and
publishes that page on every push to `dx747` (the repository's Settings > Pages > Source must be "GitHub Actions"). `cheat` (a
cheat sheet) has none. To change the card, edit the file and push; the hub reads it every three hours. Rules: English, 20 to 300 characters, no links or HTML, no advertising; the picture
square, 240 to 1024 px, PNG, WebP or JPEG, 300 KB at most. The repository's check is
**<https://thefirmwarehub.com/kb/#fmhub>** ("Check your file"): it shows what the card will show, or what is wrong.

Checks made here: `python3 -m json.tool fmhub.json` passes, the text is 273 characters with no links or forbidden characters, only
allowed fields, and `docs/card.png` is 240 x 240, 3 KB. The release command was run once locally
(`FM1_LINUX_TOOLCHAIN=<toolchain> ./build.sh`): it makes `build/dx7/generated/FM1_DX747.fwsc` (identity `FM-1_8747747`).

## Before the mail

1. **The three installs on a real FM-1** (done by the author): from the official V15 to DX747; from another firmware to
   DX747; and from DX747 to another firmware or back to V15. The page for it is `build/official-web/FM1-DX747.html` (or
   `DX747-Workbench.html`, Update tab); [docs/FLASHING-AND-RECOVERY.md](FLASHING-AND-RECOVERY.md) has the steps and the way back.
2. **Newer FM-1s have a different flash chip** (UC25HQ80, JEDEC ID `0xB36014` instead of the Puya `0x856014`). The hub says a
   firmware or loader that only knows the old ID cannot save or install on those units. DX747 has no flash-ID check of its own;
   its flash code and the update loader are M-VAVE's binaries, which cannot be changed here. If you have one of the newer units,
   try saving a scene and an update on it, and tell the hub if either fails.
3. Make the release: `git tag v0.1 && git push origin v0.1` (the first version is 0.1: a `0.x`, *beta*, *alpha* or *rc* version gets a beta
   tag on the hub; tag `v1.0` when it is no longer a beta). Watch the *Actions* tab; when it is green, the release has the signed `FM1_DX747.fwsc`. Do not replace the file by
   hand. Check it: `gh attestation verify FM1_DX747.fwsc --repo esbenvb/fm1-dx747 --signer-workflow zvenson/fm1-build/.github/workflows/build.yml`.
4. Push, and enter the repository at <https://thefirmwarehub.com/kb/#fmhub> to see the card.

## The mail

To **zven@thefirmwarehub.com**, short, with no card text in it (that comes from `fmhub.json`):

> Subject: New FM-1 firmware: DX747
>
> Firmware: DX747, by Esben von Buchwald
> Repository: https://github.com/esbenvb/fm1-dx747 (default branch dx747; fmhub.json and docs/card.png are in it)
> Identity: FM-1_8747747
> Licence: GPL-3.0-only with the FM1 SDK linking exception (it is a fork of M-VAVE's open firmware, FM1_OFFICIAL_OPEN; the
> sealed engine library is M-VAVE's, under the terms in THIRD_PARTY.md). The package carries M-VAVE's update loader.
> Tested on a real FM-1: installed from the stock V15 and from another firmware, and another firmware installs from it again.
> (Say exactly what you tested.)
> It has no projects or samples, so it cannot read other firmwares' projects or samples (the README says so).

## Afterwards

New version: tag it (`git tag v0.2 && git push origin v0.2`). The same asset name and identity is fine. Card, picture and links: edit
`fmhub.json` and `docs/card.png`, push, wait up to three hours; never mail text changes.

## Other lists

Both take a short submission from the maker's own GitHub account (`esbenvb`); texts checked against their rules on 6 November 2026.

### awesome-fm-1 (pull request)

<https://github.com/cicloid/awesome-fm-1>. Its [contribution guidelines](https://github.com/cicloid/awesome-fm-1/blob/main/CONTRIBUTING.md): one line
`- [Name](url) - Description.` ending in a full stop; say what it does and who made it and name the licence; right section; one entry per
pull request; not a bare fork with no changes. Fork it, add this line to **Custom firmware > Open source** (after the sloopDX line, the
other DX7 firmware), and open a pull request titled *Add DX747, an eight-part multitimbral DX7 module*:

```
- [DX747](https://github.com/esbenvb/fm1-dx747) - Eight-part multitimbral DX7 module by Esben von Buchwald: up to eight parts on their own MIDI channels, each with up to eight layered DX7 voices, key splits, velocity windows, 64 scenes, a master filter, USB audio, and a web configurator with a browser simulator and installer ([try it](https://esbenvb.github.io/fm1-dx747/)). A fork of FM1 Open with its own identity (FM-1_8747747). GPL-3.0 with the FM1 SDK linking exception.
```

Pull request text: "Adds DX747, an open-source firmware that turns the FM-1 into an eight-part multitimbral, layerable DX7 module (the
release is v0.1, a beta; the source is public; it installs from the web page and brings M-VAVE's update loader, so other firmwares install
over it). One entry, in Open source."

### fm1-editor.com firmware directory (issue)

The page says "Know of a firmware missing here? Open an issue", and the issues are those of
<https://github.com/benny-sparra/fm1-dx7-patch-importer/issues> (earlier requests are titled like *Add Miku-FM* and give the install link, the
try-it link, the source, a cheat sheet and the picture). Title and text, in the same shape:

```
Add DX747

DX747, by Esben von Buchwald (esbenvb): an eight-part multitimbral DX7 module for the FM-1. Every part has its own MIDI channel and up to eight layered DX7 voices, with key splits, velocity windows, 64 scenes, a master filter, shared reverb and delay, and DX7 ROM import. It has no sequencer, sampler or projects.

    Install (Web MIDI, Chrome/Edge): https://esbenvb.github.io/fm1-dx747/ (Update tab)
    Try it in a browser: https://esbenvb.github.io/fm1-dx747/ (the firmware itself, with a web editor)
    Source: https://github.com/esbenvb/fm1-dx747 (GPL-3.0 with the FM1 SDK linking exception; a fork of FM1 Open, identity FM-1_8747747, brings M-VAVE's update loader)
    Release: v0.1 (beta), signed build: https://github.com/esbenvb/fm1-dx747/releases/tag/v0.1
    Manual: https://github.com/esbenvb/fm1-dx747/blob/dx747/docs/DX747-Manual.pdf
    Picture: https://raw.githubusercontent.com/esbenvb/fm1-dx747/dx747/docs/card.png (240 x 240 screen capture)
```
