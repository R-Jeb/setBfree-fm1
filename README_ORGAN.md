# B3 ORGAN for the M-VAVE FM-1 (on zvenson/dinghy)

A fixed-point tonewheel organ modelled on setBfree. State: **0.1, tested in host mode only (not on an FM-1).**

## Build
```
tools/get_toolchain.sh && git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
python3 tools/gen_organ.py      # only if you change the gears or tuning: rewrites firmware/src/organ_tab.h
tests/run_host.sh               # build/host/dinghy.wav + screen PNGs, no FM-1 needed
./build.sh                      # -> build/dinghy.fwsc ; python3 tools/fm1_install.py build/dinghy.fwsc
```
Before you release it, pick your own package identity in `tools/build.py` (`PRODUCT`; Dinghy itself is FM-1_99N).

## Controls
keys (F3..G5, OCT-/OCT+) | SELECT drawbar, ALGORITHM level 0..8 | PRESETS steps through setBfree's registrations
(default.pgm: 57 of them, e.g. 5 Standard B, 11 Brian Auger 3rd, 15 Booker T, 104 Jimmy Smith) |
KNOB1 overdrive amount | KNOB2 vibrato V1 C1 V2 C2 V3 C3 | KNOB3 Leslie stop/slow/fast |
FX vibrato on/off | LFO Leslie cycle | EDIT overdrive on/off | ENV percussion | SEL 2nd/3rd | ARP fast/slow | SEQ soft/normal.

## MIDI (USB and the TRS jack, any channel)
Notes fold into C2..C7. **Program change** n selects setBfree program n+1 (its `pgm.controller.offset` default), all 110
programs of `default.pgm` are in; a program only changes what it names. Programs for the lower manual, pedals, key
splits and reverb are kept but have no effect here (one manual, no reverb). **Controllers** as setBfree's defaults:
1 rotary slow/stop/fast, 7 and 11 swell, 31 and 95 vibrato on (upper), 64 rotary toggle, 65 overdrive on,
66 and 80 percussion on, 81 soft, 82 fast, 83 2nd harmonic (64 and up), 92 vibrato knob, 93 overdrive amount,
70..78 drawbars **inverted as in setBfree** (127 = pushed in, 0 = fully out), 120 and 123 notes off.
Not used: 30 vibrato lower, 91 reverb, 94 convolution. As in setBfree, a program that sets the vibrato knob does not
switch vibrato on by itself (FX button, CC 31 or program 50 do).

Regenerate the program table with `python3 tools/gen_programs.py` (reads `data/setbfree-default.pgm`).
`tests/organ_test.c` checks programs and controllers against setBfree's source on a computer, and the level of every key and drawbar against setBfree's own tone generator (`tests/organ_ref.h`).

**Tapering and leakage.** The level of each key and drawbar (setBfree's tapering, -10..+7 dB) and the crosstalk between neighbouring tonewheels (compartment, terminal strip, wiring: -34..-46 dB, setBfree's default model) come from setBfree's compiled play matrix: `tools/dump_contrib.c` dumps it (`data/setbfree-contrib.txt`), `tools/gen_contrib.py` makes `firmware/src/organ_contrib.h`. Leakage costs wheels: ten keys with all nine drawbars out sound about 49 wheels (check `W .. us` on the display).

## Check this first on the device
The top right of the screen shows `W <wheels sounding>  <peak render time> us`. A block must stay well under
5800 us (RULES.md asks for under 2900). Hold a big chord with many drawbars out and watch it. If it gets close,
lower the wheel count (fewer simultaneous keys, drawbars) or move work out of the per-frame loop.

## Not in it (yet)
Lower manual and pedals, real Leslie (Doppler, horn/drum split), reverb, key click, tonewheel waveform colouring,
setBfree's equalisation curve, crosstalk and key compression. Vibrato depth and percussion level are set by ear.

## Browser emulator
`web/build.sh` compiles the firmware's own engine (`firmware/src/organ_core.h`) to WebAssembly with clang
(`--target=wasm32`, needs `wasm-ld`; on Ubuntu `sudo apt install clang lld`) and writes the self-contained page
`web/index.html`. Open it in a browser, or publish the `web/` folder with GitHub Pages (Settings, Pages, branch main,
folder /web). It plays through the Web Audio API at 44.1 kHz; it cannot tell you the FM-1's CPU load.

## Build on GitHub
`.github/workflows/build.yml` runs on every push and pull request: the host tests and the web emulator build, and
the real firmware package with JieLi's toolchain. Open the run in the Actions tab and download `firmware`
(`dinghy.fwsc` and the disassembly `dinghy.dis`). A tag like `v0.2` also makes a release with the package attached.
The toolchain download comes from JieLi's server and the SDK from Gitee; if that step fails, the log says which.
