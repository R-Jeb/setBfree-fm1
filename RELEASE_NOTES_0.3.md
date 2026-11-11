# B3 ORGAN 0.3

Three manuals, a new flash chip, finer reverb. What is new since 0.2:

- **Three manuals.** Upper, lower and pedal, as in setBfree. Each has its own drawbars; the lower manual has its own vibrato on/off; the
  pedal has the 16' and 8' drawbars, no percussion and no vibrato. One scanner (vibrato / chorus) serves upper and lower, as in
  setBfree; the percussion and the pedal never go through it.
- **MIDI channels.** Channel 1 plays the upper manual, 2 the lower one, 3 the pedal (USB and the TRS jack). The drawbar controllers
  70 to 78 follow the channel (pedal: 70 = 16', 72 = 8').
- **Three screens** (KNOB 1 switches): upper, lower, pedal. The manual of the screen shows its drawbars, the other two a line of digits.
  The chosen preset is on every screen. SELECT picks the drawbar, ALGORITHM sets it.
- **Screen 3** also shows the load of the audio block (µs, wheels sounding, peak) and a **MIDI indicator**: the channel and kind of the
  last message that came in (`CH 2`, `ON`, `CC 64`, `PC 12`) and a counter.
- **Keys:** REC puts the FM-1's own keys on the upper, lower or pedal manual (shown as `KEYS UPPER` and so on on every screen).
  FX switches the vibrato of the screen's manual, LFO the Leslie, SEL the overdrive. **EDIT and SAVE are left free.**
- **Controls moved:** the screen is on KNOB 1, the overdrive amount (0 to 8) on KNOB 3, the reverb on KNOB 4, the overdrive on/off on
  SEL (it was EDIT).
- **Reverb in doubling steps** on KNOB 4: 0, 1, 2, 4, 8, 16, 32, 64, 127 (fine at the bottom). CC 91 still sets any value.
- **New flash chip.** Newer FM-1s carry a UC25HQ80 (JEDEC id 0xB36014) instead of the Puya P25Q80 (0x856014). The firmware and the
  update loader accept both now (as Dinghy 0.5): on the new chip the settings are saved and the firmware can be updated, where an older
  build saved to RAM only. Please say if your FM-1 has the new chip and it works.
- **Signal path changes** that can be heard: the percussion is no longer modulated by the vibrato (it was in 0.2; setBfree keeps it dry),
  and the key compression counts the keys of all three manuals.

Measured, as in 0.2: with the same settings the overdrive behaves as in 0.2 (the level of every step against "overdrive off" within
0.3 dB); the Leslie (stop, slow, fast, ramps) agrees with a Raspberry Pi running setBfree in the modulation depth and the speeds.

Please note:

- The saved settings have a new layout: **the first start of 0.3 begins with the defaults** (going back to 0.2 does the same).
- A MIDI keyboard that sends CC 1, 64, 65 or 66 (modulation wheel, sustain, sostenuto) switches the Leslie, the overdrive or the
  percussion, as in setBfree. Screen 3 shows what arrives.
- "Play in Browser" is still the upper manual only.
- Open: a line at 4190 Hz (the 12th harmonic of F4) that is louder on the Raspberry Pi than here; not explained yet.

Install: download `b3organ-0.3.fwsc` and use the FM-1 Firmware Hub, or see the README. 0.2 stays available under Releases as a fallback.
Please report what sounds different from setBfree.
