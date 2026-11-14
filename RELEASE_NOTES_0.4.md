# B3 ORGAN 0.4

Two processor cores, a drive control for the overdrive, a limiter against clipping, a quieter Leslie and a DIN MIDI input that does not hang.
What is new since 0.3:

- **Two processor cores.** The tonewheels are split in two halves (even and odd wheels); the second core of the FM-1 renders one half, the first
  core the other, then the preamp, reverb and Leslie. The two halves add up to exactly what one core gave (checked bit for bit on every build),
  so the sound and the latency are the same, but chords with many notes, with overdrive, reverb and Leslie on, no longer run out of time.
  Measured on an FM-1 with a loaded patch (4 keys on upper and lower, percussion, chorus, Leslie fast, reverb 4): organ 3600 to 3800 us before,
  about 2000 us now; the whole block about 5800 to 6200 us before (limit 5800), about 4000 us now (4400 us with the overdrive on). If the
  second core does not answer at start, the firmware plays on one core as before (screen 4 shows `C1` instead of `C2`).
- **Overdrive with a drive control.** KNOB 3 and CC 93 are now a **drive** (0 to 16): setBfree's `overdrive.inputgain` in steps of 1.5 dB, 12 is
  its default. The character (`overdrive.character`) is fixed at 16; the nine character steps of 0.3 are gone. The output follows the drive, so
  overdrive on and off are about equally loud. The character is to become adjustable in the web editor later.
- **Limiter.** The tone generator is back at setBfree's level (0 dB), and a limiter keeps the peaks below full scale: on the wheels' sum with
  the overdrive off, and on the output after reverb and Leslie. It works by gain, per block with a short attack and a slow release, not as a
  static clip: a sine over full scale has a distortion of -62 dB (a static soft clip had -29 dB). Many keys with the swell pedal at 60 to
  100 % no longer clip, with or without overdrive.
- **Leslie without the noise.** The delays of the horn and the drum are now in 1/256 sample (they were 1/16): the coarse steps made a broadband
  noise at 6 to 12 kHz while the horn turns (16 dB less at slow). It is the full setBfree model, as before.
- **DIN MIDI input (TRS jack) that holds.** After some minutes of playing, notes could hang or drop out. The input now reads its buffer by
  content and not by the hardware's byte count, finds the start of a message again after a lost byte, releases the notes of a lost message and
  ignores aftertouch and pitch bend. A CC 7 / CC 11 value that repeats or jitters by one step (an expression pedal at rest) is no longer queued
  again, so the MIDI indicator no longer shows CC 11 all the time. The USB MIDI queue is 128 messages (it was 64).
- **Screen 4 (KNOB 1): test values.** One row each: the load per stage, the sum of the wheels and the limiter's gain, the output peak and its
  limiter, MIDI and MIDI errors. The pedal screen keeps the load and the MIDI indicator.
- **Flash chip.** The firmware runs on the newer FM-1 with the UC25HQ80 flash chip (accepted since 0.3); tested on both kinds.
- `tools/emu_test.py` (a scripted run in the fm1-emu emulator) is in the repository; it does not run this firmware properly yet, so it is not
  a test you can rely on.

Please note:

- The saved settings keep the 0.3 layout. SAVE stores one global state, not one per preset.
- A recording made at the same settings will be quieter than 0.3 at the tone generator only where the limiter acts; the overdrive's level
  against "off" is unchanged.
- Open: a line at 4190 Hz (the 12th harmonic of F4) that is louder on the Raspberry Pi; the Leslie at fast still has a little more noise than
  the Pi (partly the real Doppler); not explained yet.

Install: download `b3organ-0.4.fwsc` and use the FM-1 Firmware Hub, or see the README. 0.3 stays available under Releases as a fallback.
Please report what sounds different from setBfree.
