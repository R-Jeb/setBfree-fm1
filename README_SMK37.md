# B3 ORGAN on the M-Vave SMK-37 (experimental)

> **Experimental, first tested on a real keyboard.** A player has run B3 ORGAN on an SMK-37 Elite
> ([issue #57](https://github.com/R-Jeb/setBfree-fm1/issues/57)); what he found (the expression pedal's direction,
> the swell's minimum, a hard pad hit) is fixed in 0.7.1. The Pro and MKE-P37 packages have run only in Joakim
> Eriksson's [SMK-37 emulator](https://joakimeriksson.github.io/fm1-emu/smk37/). If you try one on your SMK-37,
> **please tell us how it went**: open a
> [test report](https://github.com/R-Jeb/setBfree-fm1/issues/new?template=smk37-report.md), whether it works or not.
> You install it at your own risk; read [Before you install](#before-you-install) first.

The M-Vave SMK-37 (Pro, Elite, MKE-P37) is a 37-key keyboard with the FM-1's chip inside. B3 ORGAN builds for it from
the same source as for the FM-1: the same organ, the same screen, the same web editor, with the keyboard's own
controls. Its board layer is genkigenki's [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit as
[Dinghy](https://github.com/zvenson/dinghy) 0.6 carries it.

## Packages

Each release has one package per model name. Install the one for the name printed on your keyboard:

| Keyboard | Package | Identity | Status |
|---|---|---|---|
| SMK-37 Pro (also Starrykey 37 Play) | `b3organ-X.Y-smk37.fwsc` | `SMK-37 Pro_953` | emulator only |
| SMK-37 Elite | `b3organ-X.Y-smk37-elite.fwsc` | `SMK-37 Elite_953` | tested on the keyboard (issue #57) |
| MKE-P37 | `b3organ-X.Y-smk37-mke.fwsc` | `MKE-P37_953` | built the same way, not run |

They are offered as files in the release only; the installer site is for the FM-1. The Firmware Hub lists B3 ORGAN
in its SMK-37 list too and points here to install it.

## The controls

| Control | Does |
|---|---|
| 37 keys (C3 .. C6) | play the manual that PARA chose (upper, lower, pedal); on the pedal two octaves down (C1 .. C4); no velocity, as on a B3 |
| Pads, top row | push a drawbar in (16' .. 1 1/3', one column under each knob): a tap one step, held a step every 1/4 s (0 to 8 in 2 s) |
| Pads, bottom row | pull a drawbar out, the same way; the pads light in the drawbars' colours, brighter the further out |
| KNOB BANK, FADER BANK | vibrato / chorus of the upper, the lower manual on / off (lit: on), whatever manual the screen shows |
| ARP, NOTE REPEAT, SCALE, CHORD | percussion (the upper manual's, set from any screen): on / off, soft, fast, 3rd (lit: on, soft, fast, 3rd) |
| BT | Leslie: stop, slow, fast (its light: on, blinking slowly, blinking fast) |
| GLOBE | free |
| PATCH | the bank of pads and faders: 1 the upper manual, 2 the lower manual and the pedal; the screen follows, PATCH lights in bank 2 |
| PARA | the keys' manual: upper, lower, pedal (lit: lower; blinking: pedal) |
| FX | overdrive on / off (lit: on) |
| SEQ, SEQ PLAY, SEQ REC | free |
| Faders, bank 1 | 1: the upper 1'; 2, 3, 4: the equalizer's bass, middle, treble |
| Faders, bank 2 | 1: the lower 1'; 2: -; 3, 4: the pedal's 16' and 8' |
| Screen | the upper and lower pages show the pedal and the equalizer (bass, middle, treble) in their info line |
| Mod wheel | Leslie: down slow, middle stop, up fast |
| K1 .. K4 | as KNOB 1 .. 4 on the FM-1: the screen, vibrato / chorus type, drive, reverb |
| K5, K6 | SELECT, ALGORITHM (as on the FM-1) |
| K7 | swell (as CC 11) |
| K8 | volume (the SMK has no MASTER pot) |
| < > | the presets |
| OCTAVE- / OCTAVE+ | octave |
| Pedal jack | swell from an expression pedal (toe down is louder), once the pedal has moved; the range adapts to the pedal: move it through its whole travel once |

A drawbar's fader works like a drawbar: up is in (0), down is out (8). The equalizer's faders: up is more, the middle
0 dB. A fader takes over only when it reaches or passes the value it sets, so nothing jumps after a bank change, a
preset or the editor changed it: move it through the value once.

Differences to the FM-1: no TRS MIDI IN (MIDI comes in over USB only); the update loader's screen stays dark while
an update writes.

## Before you install

- **Back up the whole flash first.** The SMK-37 keeps its factory key and pad calibration in its flash, and it cannot
  be downloaded anywhere if it is lost. B3 ORGAN never writes there (it can only write its own settings area and the
  update area, checked when it is built and again by its flash driver), but a backup is your safety net. How:
  [fm1-to-smk37, full flash backup](https://github.com/genkigenki/fm1-to-smk37/blob/main/docs/02-update-and-recovery.md).
- **The way back:** M-Vave's update package for your model (`.fwsc`, V016) installs over B3 ORGAN with M-Vave's own
  updater, with its calibration intact. Keep it at hand.
- **Both cores:** B3 ORGAN runs its tone generator on both of the chip's cores; the other SMK firmwares use one. It works
  in the emulator, on the FM-1 and on an SMK-37 Elite.

## Install

With Python 3 and `pip install mido python-rtmidi`, the keyboard on USB, no other program using its MIDI port:

```bash
python3 tools/fm1_install.py --info                      # what is installed now
python3 tools/fm1_install.py b3organ-X.Y-smk37.fwsc      # the package for your model
```

The installer checks that the package's model matches the keyboard's. Install over M-Vave's firmware (V016) or an
earlier B3 ORGAN; over another custom firmware, go back to M-Vave's first. Do not unplug the keyboard while it
updates (its screen stays dark for a while, then it restarts).

The first start clears the keyboard's settings area (M-Vave's settings, or another firmware's) and marks it as
B3 ORGAN's; your own programs then live there, as on the FM-1.

## Build and try it without a keyboard

```bash
./build.sh --board smk37                  # build/b3organ-smk37.fwsc        (SMK-37 Pro)
./build.sh --board smk37 --model elite    # build/b3organ-smk37-elite.fwsc
./build.sh --board smk37 --model mke      # build/b3organ-smk37-mke.fwsc
BOARD=smk37 tests/run_host.sh tests/scripts/smk37.txt   # on this computer: screens with the pads' colours, build/host/smk37-*.png
```

In the [SMK-37 emulator](https://joakimeriksson.github.io/fm1-emu/smk37/): "Open .fwsc..." and the package. The CI
runs the same in the emulator by hand (Actions, build, Run workflow: the job "SMK-37 in the emulator", with
`tools/emu/smk37.script`). The FM-1 build stays byte for byte the same; CI checks it.
