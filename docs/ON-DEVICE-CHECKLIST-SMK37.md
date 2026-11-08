# SMK-37: what must be checked on a unit

DX747 for the SMK-37 Pro and Elite (`./build.sh --target smk37-pro` / `smk37-elite`, `targets/smk37/`) builds and passes the size,
symbol and identity gates, and the shared code (parts, router, scenes, configuration protocol) runs in the host and simulator tests.
**It has never run on an SMK-37**: this project has had no unit. Nobody should install it before the points below are checked, and the
firmware must not be listed on the Firmware Hub's SMK-37 page before then.

Before the first install
- [ ] Install M-VAVE's own SMK37 Open V01 first, to learn the update path and to check that the unit's restore (Pro V16 / Elite V16
      package, `targets/smk37/sdk/firmware/`) works. Keep a flash backup if a programmer is at hand.
- [ ] The unit's flash JEDEC id is read by the update (0xEB6014 on the Pro M-VAVE tested); note it for the Elite.
- [ ] Install `SMK37_DX747_PRO.fwsc` (or `_ELITE`) with M-VAVE's installer or the SMK-37 workbench; the unit boots to the DX747 boot screen.
      If it does not, restore V16 and stop.

Controls (the scanner, `smk37_controls.c`)
- [ ] 37 keys: every key sounds, with velocity (soft and hard differ); no stuck notes after releasing many at once or changing octave.
- [ ] 16 pads: pad numbers match their lights; scenes recall (Scenes and the screens that are not listed next), mute on the top row and solo on the lower (Mix), layers on the top row
      and parts on the lower (Part, Details, edit pages); the colours and their dim and bright states (tables in targets/smk37/README.md).
- [ ] 4 faders: part levels 1 to 4 and, with FADER BANK, 5 to 8; the range is 0 to 127 (assumed); a fader sets its thing as soon as it moves (no pickup) and the first reading at power-on changes nothing. On Parts they are the master filter
      (cutoff, resonance, type); on Details and FX the reverb decay, delay time, delay feedback and effects return; FADER4 does nothing on Parts.
- [ ] Pitch wheel and mod wheel move the selected part; sustain pedal holds it and releases cleanly.
- [ ] 8 knobs K1 to K8 (K5 part, K6 layer, K7 voice, K8 master level; on Mix K1 to K4 pan parts 1 to 4, or 5 to 8 after KNOB BANK): direction, one step per detent; the 18 buttons: each does what its row says in
      targets/smk37/README.md (SCALE adds a layer, CHORD removes one, < and > step back and forward, OCTAVE- and OCTAVE+ as listed), and the lamp of the
      button that opens the screen on view is lit. The printed names were matched to the board's button numbers by position: this is where that is proved.
- [ ] Safe boot: hold SCALE and CHORD while switching on; the unit starts without loading the saved project.

Sound and system
- [ ] Eight parts, sixteen voices, scenes, the filter and effects: no dropouts (the SMK-37 library renders 625 frames per second against
      the FM-1's 909; CPU headroom is unknown); the app image has about 9 KB of headroom.
- [ ] Scene and settings storage: save, power-cycle, recall. Never touches the protected ranges (boot 0 to 0x3FFF, BTIF 0xC0000,
      USRTRIM 0xC1000, key_mac 0xFF000).
- [ ] USB and Bluetooth MIDI in, MIDI out on the TRS jack (the SMK-37 has no TRS MIDI input); the workbench configuration page connects.
- [ ] Display: 240 x 240, colours and orientation; no tearing.
- [ ] Restore V16 from the workbench; reinstall DX747; both work.
