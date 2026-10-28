/* SPDX-License-Identifier: GPL-3.0-only */
/* The organ engine (tonewheels, drawbars, percussion, vibrato, overdrive, Leslie-lite), shared by the firmware
 * (dinghy.c) and the browser emulator (web/organ_wasm.c). Needs <stdint.h> types, fm1_ticks() and
 * FM1_TICKS_PER_US from whoever includes it, and organ_tab.h before it. Integers only. */

#include "organ_pgm.h"
#include "organ_contrib.h"                    /* setBfree's tapering + leakage, per key and drawbar */

#define NW 91u                                  /* tonewheels */
#define NB 9u                                   /* drawbars */
#define NK 61u                                  /* keys C2..C7 */
#define NOTE0 36u                               /* MIDI note of key 0 (C2) */

/* ---- the controls the audio ISR reads (single bytes, written by the main loop) */
static volatile struct {
    uint8_t db[NB];                             /* drawbars 0..8 */
    uint8_t vtype;                              /* setBfree's vibrato knob: 0 V1, 1 C1, 2 V2, 3 C2, 4 V3, 5 C3 */
    uint8_t vup;                                /* vibrato switched on (for the one manual) */
    uint8_t perc, perc3, fast, soft;            /* percussion: on, 3rd (else 2nd), fast decay, soft */
    uint8_t les;                                /* Leslie: 0 stop, 1 slow, 2 fast */
    uint8_t rev;                                /* reverb mix 0..127 (CC 91) */
    uint8_t od, drv;                            /* overdrive on, and how hard 0..8 */
    uint8_t swell;                              /* 0..127 (CC 7 / CC 11) */
    uint8_t pgm;                                /* the program last selected (setBfree numbering), 0 none */
} P = {{8, 8, 8, 0, 0, 0, 0, 0, 0}, 0, 0, 0, 1, 0, 0, 0, 12, 0, 3, 127, 0};
static volatile uint32_t pk_lo, pk_hi, mk_lo, mk_hi;   /* keys held: the panel's, and MIDI's (bit k, k = 0..60) */
static volatile uint32_t load_us, wheels_on;
static volatile uint8_t fx_bypass;              /* tests: skip the reverb and the Leslie (the tone generator alone, in mono) */    /* the last block: render time, wheels sounding */

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen) */
#define PERC_Q10 1536                           /* percussion level, 1024 = a drawbar at 8 (by ear) */
#define DEC_FAST 31480                          /* per 256 frames, Q15: 0.001 ^ (256 / 44100) */
#define DEC_SLOW 32441                          /* 0.001 ^ (256 / (4 * 44100)) */
#define VIB_INC 706089u                         /* 7.25 Hz: f * 2^32 / 44100 */

static uint32_t wph[NW];                        /* tonewheel phases: they turn all the time, as on the organ */
static int32_t wcur[NW], wtgt[NW];              /* level of each wheel on the bus, Q10 (1024 = one drawbar at 8) */
static int32_t acc[256];
static int32_t kcl = 4096 << 4;                 /* key compression, now: Q16 */
static int32_t vdl[256];                        /* scanner delay line */
static uint32_t vwi, vph;
static int32_t mono[256], lbuf[256], rbuf[256]; /* after the overdrive: mono, then through the reverb, then the Leslie makes the pair */
#include "organ_fx.h"
static int32_t perc_env = 32767;                /* Q15 */

static const uint8_t VIBMODE[6] = {1, 4, 2, 5, 3, 6};   /* knob position -> mode: 1..3 V1..V3, 4..6 C1..C3 */

static void app_audio(int32_t *out, uint32_t n)
{
    static const int32_t DEPTH[3] = {6 << 8, 12 << 8, 18 << 8};   /* scanner delay swing, samples Q8 (by ear) */
    uint32_t lo = pk_lo | mk_lo, hi = pk_hi | mk_hi, k, d, w, i, nk = 0, act = 0, t0 = fm1_ticks();
    int32_t pa = 0, kd;
    uint32_t vm = P.vup ? VIBMODE[P.vtype % 6u] : 0u, dm = P.drv, od = P.od, les = P.les;
    int32_t gsw = (int32_t)((1229u * P.swell) / 127u);
    if (n > 256u) n = 256u;

    for (w = 0; w < NW; w++) wtgt[w] = 0;
    for (k = 0; k < NK; k++)
        nk += k < 32u ? lo >> k & 1u : hi >> (k - 32u) & 1u;
    if (!nk) perc_env = P.soft ? 16424 : 32767; /* single trigger: the envelope restarts when every key is up */
    else pa = (PERC_Q10 * perc_env) >> 15;
    for (k = 0; k < NK; k++) {
        if (!(k < 32u ? lo >> k & 1u : hi >> (k - 32u) & 1u)) continue;
        for (d = 0; d < NB; d++) {
            int32_t lvl = P.db[d];
            uint32_t e;
            if (!lvl || (d == 8u && P.perc)) continue;   /* percussion takes the 1' drawbar's place */
            for (e = CT_IDX[k * 9u + d]; e < CT_IDX[k * 9u + d + 1u]; e++)   /* the main wheel, tapered, and the leakage */
                wtgt[CT_W[e]] += (lvl * 128 * (int32_t)CT_G[e] + 2048) >> 12;
        }
        if (P.perc) wtgt[CT_MAIN[k * 9u + (P.perc3 ? 4u : 3u)]] += pa;
    }
    if (nk && P.perc) perc_env = (perc_env * (P.fast ? DEC_FAST : DEC_SLOW)) >> 15;

    for (i = 0; i < n; i++) acc[i] = 0;
    for (w = 0; w < NW; w++) {
        int32_t c = wcur[w], t = wtgt[w];
        uint32_t ph = wph[w], inc = WHEEL_INC[w];
        if (!c && !t) { wph[w] = ph + inc * n; continue; }   /* silent: costs nothing, the wheel turns on */
        {
            act++;
            if (c == t) {                       /* steady: no envelope */
                for (i = 0; i < n; i++) {
                    uint32_t idx;
                    int32_t s0, s1, s;
                    ph += inc;
                    idx = ph >> 24;
                    s0 = SINE_TAB[idx];
                    s1 = SINE_TAB[idx + 1u];
                    s = s0 + (((s1 - s0) * (int32_t)((ph >> 8) & 0xFFFFu)) >> 16);
                    acc[i] += (s >> 2) * c;
                }
            } else {                            /* the level changes: setBfree's key click (an attack envelope; the release is a ramp) */
                const uint16_t *env = ATK_ENV[w & 7u];
                int32_t d = t - c;
                for (i = 0; i < n; i++) {
                    uint32_t idx;
                    int32_t s0, s1, s, e, a;
                    ph += inc;
                    idx = ph >> 24;
                    s0 = SINE_TAB[idx];
                    s1 = SINE_TAB[idx + 1u];
                    s = s0 + (((s1 - s0) * (int32_t)((ph >> 8) & 0xFFFFu)) >> 16);
                    e = i >= 128u ? 4096 : t ? (int32_t)env[i] : (int32_t)i << 5;
                    a = c + ((d * e) >> 12);
                    acc[i] += (s >> 2) * a;
                }
            }
        }
        wph[w] = ph;
        wcur[w] = t;
    }

    {
        int32_t kt = (int32_t)KEY_COMP[nk > 61u ? 61u : nk] << 4;       /* key compression glides over the block */
        kd = ((kt - kcl) * (int32_t)(65536u / n)) >> 16;
    }
    for (i = 0; i < n; i++) {
        int32_t x = acc[i] >> 10, y, u;
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
        kcl += kd;
        x = (x * (kcl >> 4)) >> 12;
        if (od) {                               /* overdrive: gain, then a soft clip (3u - u^3) / 3 */
            u = (x * (int32_t)(16u + 12u * dm)) >> 4;
            if (u > 32767) u = 32767;
            if (u < -32767) u = -32767;
            {
                int32_t u3 = (((u * u) >> 15) * u) >> 15;
                y = ((3 * u - u3) * 21845) >> 16;
            }
        } else
            y = x > 32767 ? 32767 : x < -32767 ? -32767 : x;
        mono[i] = y;
    }
    if (fx_bypass) {
        for (i = 0; i < n; i++) lbuf[i] = rbuf[i] = mono[i];
    } else {
        reverb_run(mono, n, P.rev);             /* setBfree's order: overdrive, reverb, Leslie */
        leslie_run(mono, lbuf, rbuf, n, les);
    }
    for (i = 0; i < n; i++) {
        int32_t l = lbuf[i], r = rbuf[i];
        out[2u * i] += l > 32767 ? 32767 : l < -32767 ? -32767 : l;
        out[2u * i + 1u] += r > 32767 ? 32767 : r < -32767 ? -32767 : r;
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

/* ---- setBfree's programs (default.pgm) and MIDI controllers (midi.c, midiPrimeControllerMapping) */
static uint32_t rnd_state = 2463534242u;
static uint32_t rnd(void)                       /* xorshift, stirred with the clock */
{
    rnd_state ^= fm1_ticks();
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}

static int pgm_registration(uint32_t n)         /* a program that sets the drawbars: what the PRESETS knob steps through */
{
    return n < 128u && PGM[n].name && (PGM[n].set & (PGM_DB | PGM_RND));
}
static uint32_t pgm_step(uint32_t cur, int32_t dir)
{
    int32_t n = (int32_t)cur;
    for (;;) {
        n += dir;
        if (n < 1 || n > 127) return cur;
        if (pgm_registration((uint32_t)n)) return (uint32_t)n;
    }
}

static void organ_program(uint32_t n)           /* only the properties a program names change, as in setBfree */
{
    const organ_pgm_t *g;
    uint32_t i;
    if (n < 1u || n > 127u || !PGM[n].name) return;
    g = &PGM[n];
    if (g->set & PGM_RND) for (i = 0; i < NB; i++) P.db[i] = (uint8_t)(rnd() % 9u);
    if (g->set & PGM_DB) for (i = 0; i < NB; i++) P.db[i] = g->db[i];
    if (g->set & PGM_PERC) P.perc = g->perc;
    if (g->set & PGM_PVOL) P.soft = g->pvol;
    if (g->set & PGM_PSPD) P.fast = g->pspd;
    if (g->set & PGM_PHARM) P.perc3 = g->pharm;
    if (g->set & PGM_VIB) P.vtype = g->vib;
    if (g->set & PGM_VUP) P.vup = g->vup;
    if (g->set & PGM_OD) P.od = g->od;
    if (g->set & PGM_ROT) P.les = g->rot;
    P.pgm = (uint8_t)n;
}
static void organ_pc(uint32_t uc)               /* MIDI program change: setBfree's pgm.controller.offset is 1 */
{
    organ_program(uc + 1u);
}

/* MIDI controllers, as setBfree's defaults (any channel: this organ has one manual). Not used here, no such part:
 * 30 vibrato lower, 91 reverb, 94 convolution, and the lower / pedal drawbars. */
static void organ_cc(uint32_t cc, uint32_t v)
{
    if (v > 127u) return;
    if (cc >= 70u && cc <= 78u)                 /* drawbars: 127 is pushed in, 0 pulled out (setBfree: 127 - v) */
        P.db[cc - 70u] = (uint8_t)((16u * (127u - v) + 127u) / 254u);
    else switch (cc) {
    case 7: case 11: P.swell = (uint8_t)v; break;                       /* swell pedal */
    case 91: P.rev = (uint8_t)v; break;                                 /* reverb mix (setBfree: reverb.mix) */
    case 1: P.les = v / 43u == 0u ? 1u : v / 43u == 1u ? 0u : 2u; break;    /* rotary preset: slow, stop, fast */
    case 64:                                                            /* rotary speed toggle (sustain pedal) */
        if (v > 63u)
            P.les = P.les == 1u ? 2u : 1u;
        break;
    case 65: P.od = v > 63u; break;                                     /* overdrive on */
    case 93: P.drv = (uint8_t)((16u * v + 127u) / 254u); break;         /* overdrive character */
    case 66: case 80: P.perc = v > 63u; break;                          /* percussion on */
    case 81: P.soft = v > 63u; break;                                   /* percussion volume: soft at 64 and up */
    case 82: P.fast = v > 63u; break;                                   /* percussion decay: fast at 64 and up */
    case 83: P.perc3 = v < 64u; break;                                  /* percussion harmonic: 2nd at 64 and up */
    case 92: P.vtype = (uint8_t)(v / 23u > 5u ? 5u : v / 23u); break;   /* vibrato knob: V1 C1 V2 C2 V3 C3 */
    case 95: P.vup = v / 32u >= 2u; break;                              /* vibrato routing: upper from 64 */
    case 31: P.vup = v > 63u; break;                                    /* vibrato upper */
    case 120: case 123: mk_lo = mk_hi = 0; break;                       /* all sound / notes off */
    default: break;
    }
}
