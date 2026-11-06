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
| 5 | Tested on a real FM-1: installed from V15, installed from another firmware, and another firmware installs from it | **Yours to confirm**: see "Before the mail". |
| 6 | `fmhub.json` in the root of the default branch (`dx747`) | [fmhub.json](../fmhub.json), with the card picture [docs/card.png](card.png) (240 x 240, the splash screen). |

## The card

`fmhub.json` is the whole card: text, author, kind (`synth`) and colour (the DX green, `#04DFBF`); the picture is
`docs/card.png`. `play` and `editor` (a browser version and a web editor) are left out until the workbench page is hosted somewhere
with an `https://` address, for example on GitHub Pages; `cheat` (a cheat sheet) has none. To change the card, edit the file
and push; the hub reads it every three hours. Rules: English, 20 to 300 characters, no links or HTML, no advertising; the picture
square, 240 to 1024 px, PNG, WebP or JPEG, 300 KB at most. The repository's check is
**<https://thefirmwarehub.com/kb/#fmhub>** ("Check your file"): it shows what the card will show, or what is wrong.

Checks made here: `python3 -m json.tool fmhub.json` passes, the text is 273 characters with no links or forbidden characters, only
allowed fields, and `docs/card.png` is 240 x 240, 3 KB. The release command was run once locally
(`FM1_LINUX_TOOLCHAIN=<toolchain> ./build.sh`): it makes `build/dx7/generated/FM1_DX747.fwsc` (identity `FM-1_8747747`).

## Before the mail

1. **Test the three installs on a real FM-1** and write down the result: from the official V15 to DX747; from another
   firmware to DX747; and from DX747 to another firmware (or back to V15). The page for it is `build/official-web/FM1-DX747.html`
   (or `DX747-Workbench.html`, Update tab); [docs/FLASHING-AND-RECOVERY.md](FLASHING-AND-RECOVERY.md) has the steps and the way back.
2. **Newer FM-1s have a different flash chip** (UC25HQ80, JEDEC ID `0xB36014` instead of the Puya `0x856014`). The hub says a
   firmware or loader that only knows the old ID cannot save or install on those units. DX747 has no flash-ID check of its own;
   its flash code and the update loader are M-VAVE's binaries, which cannot be changed here. If you have one of the newer units,
   try saving a scene and an update on it, and tell the hub if either fails.
3. Make the release: `git tag v1.0 && git push origin v1.0` (pick the version; a `0.x`, *beta*, *alpha* or *rc* version gets a beta
   tag on the hub). Watch the *Actions* tab; when it is green, the release has the signed `FM1_DX747.fwsc`. Do not replace the file by
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

New version: tag it (`git tag v1.1 && git push origin v1.1`). The same asset name and identity is fine. Card, picture and links: edit
`fmhub.json` and `docs/card.png`, push, wait up to three hours; never mail text changes.
