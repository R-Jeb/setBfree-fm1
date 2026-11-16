# B3 ORGAN 0.5

A web editor that reads and writes the FM-1, your own programs, and the first of setBfree's extended parameters.
What is new since 0.4:

- **The web editor:** [r-jeb.github.io/setBfree-fm1/editor](https://r-jeb.github.io/setBfree-fm1/editor/), in Chrome or Edge, the FM-1 on
  USB. Allow MIDI and SysEx when the browser asks. It reads the FM-1's sound when it connects (drawbars of all three manuals, vibrato,
  percussion, Leslie, overdrive, reverb, swell, the program and the firmware's version) and sends every change at once. Five pages:
  Manuals (with the presets beside the pedal), Overdrive & Leslie, Tone & Reverb, Backup and a MIDI monitor.
- **Your own programs 112 to 127.** The editor stores the FM-1's whole sound in one of them, with a name of up to 12 letters: the drawbars
  of all three manuals, vibrato, percussion, Leslie, overdrive (on, drive, character) and reverb. They are in their own flash sector and stay
  through power off; save a backup before you flash another firmware. The PRESETS knob steps through them after setBfree's programs, program change 111 to
  126 selects them, and the screen shows their names. setBfree's programs still change only what they name; yours set everything.
- **Overdrive character.** setBfree's `overdrive.character` (0 to 127, 16 as before) next to the drive: in the editor, or NRPN 1/0.
- **Leslie, extended parameters:** the horn's and the drum's slow and fast speeds and how fast each speeds up and slows down
  (setBfree's `whirl.*`). setBfree's values are the default and a button brings them back. They are hidden until you switch them on.
- **Tuning** 430 to 450 Hz in steps of 1 Hz (440 by default), and the **key click** level (setBfree's `osc.attack.click.level`, 50 %), the
  click under the extended parameters.
- **Backup:** the editor saves your programs and the extended parameters as a file and puts them back, on this FM-1 or another one.
- The settings of 0.4 are kept when you update. With setBfree's values the organ sounds bit for bit as 0.4.

For MIDI: the new values go as NRPN (CC 99 / 98, data CC 6 and, for the 14-bit ones, CC 38), the editor's SysEx starts F0 7D 42 33;
README_ORGAN.md has the list.

Install: download `b3organ-0.5.fwsc` and use the FM-1 Firmware Hub, or see the README. 0.4 stays available under Releases.
Please report what sounds different from setBfree.
