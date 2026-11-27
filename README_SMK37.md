# B3 ORGAN on the M-Vave SMK-37 (experimental)

> **Tested in an emulator only, not on a real keyboard.** The SMK-37 packages run in Joakim Eriksson's
> [SMK-37 emulator](https://joakimeriksson.github.io/fm1-emu/smk37/), where everything below works except the
> expression pedal (the emulator has none). Nobody has played them on real hardware yet. If you try one on your
> SMK-37, **please tell us how it went**: open a
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
| SMK-37 Elite | `b3organ-X.Y-smk37-elite.fwsc` | `SMK-37 Elite_953` | built the same way, not run |
| MKE-P37 | `b3organ-X.Y-smk37-mke.fwsc` | `MKE-P37_953` | built the same way, not run |

They are offered as files in the release only: the installer site and the Firmware Hub entry are for the FM-1.

## The controls

| Control | Does |
|---|---|
| 37 keys (C3 .. C6) | play the manual that SEQ REC chose (upper, lower, pedal); no velocity, as on a B3 |
| Pads, top row | push a drawbar in (16' .. 1 1/3', one column under each knob), held: on to 0; hit hard: all the way in |
| Pads, bottom row | pull a drawbar out, held: on to 8; hit hard: all the way out; the pads light in the drawbars' colours, brighter the further out |
| PATCH (also KNOB BANK, FADER BANK) | the bank of pads and faders: 1 the upper manual, 2 the lower manual and the pedal; the screen follows, the buttons light in bank 2 |
| Faders, bank 1 | 1: the upper 1'; 2, 3, 4: the equalizer's bass, middle, treble |
| Faders, bank 2 | 1: the lower 1'; 2: -; 3, 4: the pedal's 16' and 8' |
| Mod wheel | Leslie: down slow, middle stop, up fast |
| K1 .. K4 | as KNOB 1 .. 4 on the FM-1: the screen, vibrato / chorus, drive, reverb |
| K5, K6 | SELECT, ALGORITHM (as on the FM-1) |
| K7 | swell (as CC 11) |
| K8 | volume (the SMK has no MASTER pot) |
| < > | the presets |
| OCTAVE- / OCTAVE+ | octave |
| FX, NOTE REPEAT, SCALE, CHORD | as the FM-1's FX, LFO, SEL, ENV: vibrato on / off, Leslie, overdrive, percussion |
| ARP, SEQ, SEQ PLAY, SEQ REC | as on the FM-1: percussion 2nd / 3rd, fast / slow, soft / normal; the keys' manual |
| PARA, BT, GLOBE | free (PARA and BT are the FM-1's EDIT and SAVE) |
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
- **Unknown:** B3 ORGAN runs its tone generator on both of the chip's cores; the other SMK firmwares use one. It works
  in the emulator, on the FM-1 and on the same chip, but not yet seen on an SMK-37.

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
