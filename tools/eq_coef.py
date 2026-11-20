#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The output equalizer's biquads (as docs/editor computes them, Robert Bristow-Johnson's formulas like setBfree's eqcomp.c), Q28 for
bq_run: b0 b1 b2 a1 a2 (a0 divided out). For the tests and to check the editor:  python3 tools/eq_coef.py BASS_DB BASS_HZ MID_DB MID_HZ TREBLE_DB TREBLE_HZ"""
import math, sys
FS = 44100.0
def biquad(kind, f, g, q):
    A = 10 ** (g / 40); w = 2 * math.pi * f / FS; c, s = math.cos(w), math.sin(w); al = s / (2 * q); sa = 2 * math.sqrt(A) * al
    if kind == "peak":
        b = (1 + al * A, -2 * c, 1 - al * A); a = (1 + al / A, -2 * c, 1 - al / A)
    elif kind == "low":
        b = (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa))
        a = ((A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa)
    else:
        b = (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa))
        a = ((A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa)
    return [round(v / a[0] * (1 << 28)) for v in (b[0], b[1], b[2], a[1], a[2])]
def bands(bd, bf, md, mf, td, tf):
    return [biquad("low", bf, bd, 0.7071), biquad("peak", mf, md, 1.0), biquad("high", tf, td, 0.7071)]
if __name__ == "__main__":
    v = [float(x) for x in sys.argv[1:7]] if len(sys.argv) > 6 else [9, 100, -6, 1000, 3, 6000]
    for c in bands(*v): print("{" + ", ".join(str(x) for x in c) + "},")
