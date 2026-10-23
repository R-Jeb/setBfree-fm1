/* SPDX-License-Identifier: GPL-3.0-only */
/* The organ engine (tonewheels, drawbars, percussion, vibrato, overdrive, Leslie-lite), shared by the firmware
 * (dinghy.c) and the browser emulator (web/organ_wasm.c). Needs <stdint.h> types, fm1_ticks() and
 * FM1_TICKS_PER_US from whoever includes it, and organ_tab.h before it. Integers only. */

#define NW 91u                                  /* tonewheels */
#define NB 9u                                   /* drawbars */
#define NK 61u                                  /* keys C2..C7 */
#define NOTE0 36u                               /* MIDI note of key 0 (C2) */

/* semitones from the 8' pitch: 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' */
static const int8_t FOOT[NB] = {-12, -5, 0, 12, 19, 24, 28, 31, 36};

/* ---- the controls the audio ISR reads (single bytes, written by the main loop) */
static volatile struct {
    uint8_t db[NB];                             /* drawbars 0..8 */
    uint8_t vib;                                /* 0 off, 1..3 V1..V3, 4..6 C1..C3 */
    uint8_t perc, perc3, fast, soft;            /* percussion: on, 3rd (else 2nd), fast decay, soft */
    uint8_t les;                                /* Leslie-lite: 0 off, 1 slow, 2 fast */
    uint8_t drv;                                /* overdrive 0..8 */
    uint8_t swell;                              /* 0..127 (CC 11) */
} P = {{8, 8, 8, 0, 0, 0, 0, 0, 0}, 0, 0, 1, 0, 0, 0, 0, 127};
static volatile uint32_t pk_lo, pk_hi, mk_lo, mk_hi;   /* keys held: the panel's, and MIDI's (bit k, k = 0..60) */
static volatile uint32_t load_us, wheels_on;    /* the last block: render time, wheels sounding */

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen) */
#define PERC_Q8 384                             /* percussion level, 256 = a drawbar at 8 (by ear) */
#define DEC_FAST 31480                          /* per 256 frames, Q15: 0.001 ^ (256 / 44100) */
#define DEC_SLOW 32441                          /* 0.001 ^ (256 / (4 * 44100)) */
#define VIB_INC 706089u                         /* 7.25 Hz: f * 2^32 / 44100 */
#define LES_SLOW 74018u                         /* 0.76 Hz */
#define LES_FAST 630123u                        /* 6.47 Hz */

static uint32_t wph[NW];                        /* tonewheel phases: they turn all the time, as on the organ */
static int32_t wcur[NW], wtgt[NW];              /* level of each wheel on the bus, Q8 (256 = one drawbar at 8) */
static int32_t acc[256];
static int32_t vdl[256];                        /* scanner delay line */
static uint32_t vwi, vph, lph, lrate = LES_SLOW;
static int32_t perc_env = 32767;                /* Q15 */

static uint32_t wheel_of(uint32_t k, int32_t off)
{
    int32_t n = (int32_t)k + 12 + off;          /* frequency number: 0 = C 32.7 Hz (16' of the lowest key) */
    while (n > 90) n -= 12;                     /* the highest harmonics fold back an octave */
    return (uint32_t)n;
}

static void app_audio(int32_t *out, uint32_t n)
{
    static const int32_t DEPTH[3] = {6 << 8, 12 << 8, 18 << 8};   /* scanner delay swing, samples Q8 (by ear) */
    uint32_t lo = pk_lo | mk_lo, hi = pk_hi | mk_hi, k, d, w, i, nk = 0, act = 0, t0 = fm1_ticks();
    int32_t pa = 0;
    uint32_t vm = P.vib, dm = P.drv, les = P.les;
    int32_t gsw = (int32_t)((1229u * P.swell) / 127u);
    if (n > 256u) n = 256u;

    for (w = 0; w < NW; w++) wtgt[w] = 0;
    for (k = 0; k < NK; k++)
        nk += k < 32u ? lo >> k & 1u : hi >> (k - 32u) & 1u;
    if (!nk) perc_env = P.soft ? 16424 : 32767; /* single trigger: the envelope restarts when every key is up */
    else pa = (PERC_Q8 * perc_env) >> 15;
    for (k = 0; k < NK; k++) {
        if (!(k < 32u ? lo >> k & 1u : hi >> (k - 32u) & 1u)) continue;
        for (d = 0; d < NB; d++) {
            int32_t lvl = P.db[d];
            if (!lvl || (d == 8u && P.perc)) continue;   /* percussion takes the 1' drawbar's place */
            wtgt[wheel_of(k, FOOT[d])] += lvl * 32;
        }
        if (P.perc) wtgt[wheel_of(k, P.perc3 ? 19 : 12)] += pa;
    }
    if (nk && P.perc) perc_env = (perc_env * (P.fast ? DEC_FAST : DEC_SLOW)) >> 15;

    for (i = 0; i < n; i++) acc[i] = 0;
    for (w = 0; w < NW; w++) {
        int32_t c = wcur[w], t = wtgt[w];
        uint32_t ph = wph[w], inc = WHEEL_INC[w];
        if (!c && !t) { wph[w] = ph + inc * n; continue; }   /* silent: costs nothing, the wheel turns on */
        {
            int32_t a = c << 8, step = ((t - c) * (int32_t)(65536u / n)) >> 8;   /* the level glides over the block: no clicks */
            act++;
            for (i = 0; i < n; i++) {
                uint32_t idx;
                int32_t s0, s1, s;
                ph += inc;
                idx = ph >> 24;
                s0 = SINE_TAB[idx];
                s1 = SINE_TAB[idx + 1u];
                s = s0 + (((s1 - s0) * (int32_t)((ph >> 8) & 0xFFFFu)) >> 16);
                a += step;
                acc[i] += (s >> 2) * (a >> 8);
            }
        }
        wph[w] = ph;
        wcur[w] = t;
    }

    if (les) {                                  /* the rotor speeds up and slows down slowly */
        uint32_t target = les == 2u ? LES_FAST : LES_SLOW;
        lrate = (uint32_t)((int32_t)lrate + (((int32_t)target - (int32_t)lrate) >> 7));
    }
    for (i = 0; i < n; i++) {
        int32_t x = acc[i] >> 8, y, u, l, r;
        if (x > 262144) x = 262144;
        if (x < -262144) x = -262144;
        x = (x * gsw) >> 12;
        if (vm) {                               /* scanner vibrato / chorus: a delay swinging 7.25 times a second */
            uint32_t tri, pos, i0;
            int32_t v0, v1, v;
            vph += VIB_INC;
            tri = vph >> 16;
            if (tri >= 32768u) tri = 65535u - tri;
            pos = (vwi << 8) - (uint32_t)(1024 + ((DEPTH[(vm - 1u) % 3u] * (int32_t)tri) >> 15));
            i0 = (pos >> 8) & 255u;
            v0 = vdl[i0];
            v1 = vdl[(i0 + 1u) & 255u];
            v = v0 + (((v1 - v0) * (int32_t)(pos & 255u)) >> 8);
            vdl[vwi] = x;
            vwi = (vwi + 1u) & 255u;
            x = vm >= 4u ? (x + v) >> 1 : v;
        }
        u = (x * (int32_t)(16u + 12u * dm)) >> 4;   /* overdrive: gain, then a soft clip (3u - u^3) / 3 */
        if (u > 32767) u = 32767;
        if (u < -32767) u = -32767;
        {
            int32_t u3 = (((u * u) >> 15) * u) >> 15;
            y = ((3 * u - u3) * 21845) >> 16;
        }
        l = r = y;
        if (les) {                              /* Leslie-lite: two amplitudes swinging against each other (no Doppler yet) */
            uint32_t tri;
            lph += lrate;
            tri = lph >> 16;
            if (tri >= 32768u) tri = 65535u - tri;
            l = (y * (18000 + (((int32_t)tri * 14767) >> 15))) >> 15;
            r = (y * (18000 + (((32767 - (int32_t)tri) * 14767) >> 15))) >> 15;
        }
        out[2u * i] += l;
        out[2u * i + 1u] += r;
    }
    wheels_on = act;
    {
        uint32_t us = (uint32_t)(fm1_ticks() - t0) / FM1_TICKS_PER_US;
        if (us > load_us) load_us = us;         /* the peak since the screen last looked */
    }
}

/* ---- keys (main loop) */
static void key_set(volatile uint32_t *lo, volatile uint32_t *hi, uint32_t k, int on)
{
    if (k >= NK) return;
    if (k < 32u) *lo = on ? *lo | 1u << k : *lo & ~(1u << k);
    else *hi = on ? *hi | 1u << (k - 32u) : *hi & ~(1u << (k - 32u));
}
static int quiet(void)                          /* nothing sounds, not even the glide out */
{
    uint32_t w;
    if (pk_lo | pk_hi | mk_lo | mk_hi) return 0;
    for (w = 0; w < NW; w++)
        if (wcur[w]) return 0;
    return 1;
}

/* ---- registrations to start from (the nine digits are the drawbars 16' .. 1') */
static const struct { const char *name, *db; } PRESET[8] = {
    {"FULL", "888888888"}, {"JAZZ", "888000000"}, {"BLUES", "888800000"}, {"FLUTE", "008000000"},
    {"SOFT", "006800000"}, {"REED", "008642000"}, {"BRIGHT", "005888000"}, {"LOW", "880000000"}};
static void load_preset(uint32_t p)
{
    uint32_t i;
    for (i = 0; i < NB; i++) P.db[i] = (uint8_t)(PRESET[p].db[i] - '0');
}

