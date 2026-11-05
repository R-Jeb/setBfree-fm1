# B3 ORGAN 0.2

What is new since 0.1:

- **Overdrive is setBfree's preamp.** The simple soft clip is replaced by setBfree's model (biased transfer curve between two bass filters,
  feedback, sag), in integers. The nine steps (KNOB 1, or CC 93) are setBfree's `overdrive.character` 0, 16 … 127. Measured against a
  Raspberry Pi: the level of every step against "overdrive off" agrees within 0.6 dB on a single note and 1.4 dB on a chord, including the dip
  at step 2 and the drop of step 0 below "off". The host tests check the model against setBfree's formulas (41 dB signal to error).
- **Battery indicator** at the bottom right of the screen: four bars, red at the last bar, a bolt when a USB host has the FM-1.
- **Percussion click** as setBfree's: the key attack comes out as a click in the percussion (6 dB before, 16 dB now against 15 dB on the Pi).
- Lower load while many keys are held.
- The firmware identifies as `FM-1_953` for the FM-1 Firmware Hub.

Install: download `b3organ-0.2.fwsc` and use the FM-1 Firmware Hub, or see the README. Please report what sounds different from setBfree.
