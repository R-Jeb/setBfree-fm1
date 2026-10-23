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
keys (F3..G5, OCT-/OCT+) | SELECT drawbar, ALGORITHM level 0..8 | PRESETS registration | KNOB1 overdrive |
KNOB2 vibrato/chorus V1-V3 C1-C3 | KNOB3 Leslie off/slow/fast | FX vibrato cycle | LFO Leslie cycle |
ENV percussion | SEL 2nd/3rd | ARP fast/slow | SEQ soft/normal.
USB-MIDI: notes C2..C7, CC 70..78 drawbars (as setBfree), CC 11 swell.

## Check this first on the device
The top right of the screen shows `W <wheels sounding>  <peak render time> us`. A block must stay well under
5800 us (RULES.md asks for under 2900). Hold a big chord with many drawbars out and watch it. If it gets close,
lower the wheel count (fewer simultaneous keys, drawbars) or move work out of the per-frame loop.

## Not in it (yet)
Lower manual and pedals, real Leslie (Doppler, horn/drum split), reverb, key click, tonewheel waveform colouring,
setBfree's equalisation curve, crosstalk and key compression. Vibrato depth and percussion level are set by ear.
