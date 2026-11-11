# The second core (branch dual-core-2)

State: **runs on an FM-1** (tested 2026-11-11). Idle 94 us (0 wheels). Loaded patch (4 keys upper and lower, percussion, chorus, Leslie fast, reverb 4):
organ about 2000 us, total about 4000 us with the overdrive off and about 4400 us with it on (one core: organ 3600 to 3800, total 5800 to 6200).
No dropouts, the response is as before. The host tests pass (the two halves are identical to one call, bit for bit).

## Why
On the FM-1 one audio block (256 frames, 5800 us) of a loaded patch costs: organ 3600 to 3800 us (70 wheels), Leslie 1580, preamp 440, reverb 244,
sum 6200 us with the overdrive on: over the limit. The tonewheels are most of the organ's time.

## The split: even / odd tonewheels, no extra latency
`wheels_render(w0, ws, n, acc, accv, &act, &vany)` (firmware/src/organ_core.h) renders the wheels w0, w0 + ws, ... of a block into its own buses.
Every wheel has its own phase and levels, so the even wheels on core 0 and the odd ones on core 1 add up to exactly what one call over all wheels
gives, bit for bit (integer addition). `organ_split` hands the odd wheels to core 1 (`ORGAN_C1_RUN`), does the even ones itself, waits
(`ORGAN_C1_WAIT`), adds the buses. Nothing is delayed: the block is the same block, so the latency stays what it was. The preamp, reverb and
Leslie stay on core 0 (about 2300 us after the organ's half: expected total about 4400 us). Core 1 could take them later in 64 sample chunks
if that is not enough.

Checks (host): `ORGAN_ONE=1 build/host/dinghy ...` renders with all wheels in one call; the WAV is identical to the two halves (CI step).
On the FM-1 screen 3 shows `C2` when core 1 answers at start, `C1` (orange) when it did not (one core: the old behaviour), and the times
ORG / OD / RV / LES; ORG should drop to about half.

## The hardware part (not compiled yet)
- hal/fm1_cpu1.h / .S: the second core (Charles Vestal's X0X, after Kerem Kilic's Melodee, via SLOOP): start, one job at a time with a check word,
  8 ms timeout, then one core until power-off.
- firmware/app.ld: `.c1_text` (core 1's waiting loop, in RAM), `.lowram` (its stacks); tools/build.py: fm1_cpu1.S, c1text.bin.
- platform.c `fm1_cstart`: core 1 held first thing, `fm1_supplies_stock()` (the supply levels M-VAVE's own firmware sets; both cores at the boot
  loader's levels read wrong on some units), then after the timer: PC limits open, `fm1_cpu1_start()`, PC limits on, the top of RAM locked.
- Things to check when it builds: `_rt_end` now covers `.c1_text` (crt0's copy loop); `fm1_crash_t` fields used by the core 1 fault path;
  that `.lowram` (the stacks) is where `app.ld` puts it and below 0x01C07F00 (crt0 writes there); that nothing else uses 0x01C06000..0x01C08000.
