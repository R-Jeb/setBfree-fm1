/* SPDX-License-Identifier: GPL-3.0-only */
/* B3 ORGAN for the M-VAVE FM-1: a fixed-point tonewheel organ on Dinghy's platform.
 *
 * The model follows setBfree (pantherb/setBfree, GPL-2.0-or-later, so it combines with Dinghy's GPL-3.0):
 *   - 91 tonewheels with the 60 Hz gear ratios of setBfree's tonegen.c (tables from tools/gen_organ.py)
 *   - 9 drawbars with setBfree's footage-to-wheel mapping and its linear level (setting / 8)
 *   - percussion on the 2nd or 3rd harmonic: single trigger, fast/slow decay (-60 dB in 1 s / 4 s), soft/normal,
 *     the 1' drawbar muted while it is on
 *   - a vibrato/chorus scanner at 7.25 Hz (V1..V3, C1..C3), an overdrive and a "Leslie-lite"
 * It is NOT setBfree itself: no float, no malloc, no whirl convolution, no reverb, one manual only.
 *
 *   keys        play the organ (key 0 = F3, OCT-/OCT+ shifts)      MASTER   volume (platform.c)
 *   SELECT      choose a drawbar    ALGORITHM  set it 0..8         PRESETS  registration presets
 *   KNOB1       overdrive           KNOB2      vibrato/chorus      KNOB3    Leslie off/slow/fast
 *   FX          vibrato cycle       LFO        Leslie cycle        ENV      percussion on/off
 *   SEL         percussion 2nd/3rd  ARP        fast/slow           SEQ      soft/normal
 *   USB-MIDI    notes 36..96 (C2..C7), CC 70..78 = drawbars 16'..1' (as setBfree), CC 11 = swell
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division. */
#define APP_NAME "B3 ORGAN"                     /* the rescue and the crash screen say this */
#include "platform.c"
#include "organ_tab.h"

#ifdef DINGHY_HOST
static uint32_t fm1_ticks(void) { return 0; }
#define FM1_TICKS_PER_US 24u
#endif

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

/* ---- the screen */
#define C_ACC RGB(255, 190, 90)                 /* tungsten */
#define C_OFF RGB(110, 110, 110)
#define C_BAR RGB(230, 230, 230)
static const char *const FOOTAGE[NB] = {"16", "5 1/3", "8", "4", "2 2/3", "2", "1 3/5", "1 1/3", "1"};
static const uint16_t BARC[NB] = {RGB(190, 120, 60), RGB(190, 120, 60), RGB(235, 235, 235), RGB(235, 235, 235),
                                  RGB(90, 90, 100), RGB(235, 235, 235), RGB(90, 90, 100), RGB(90, 90, 100),
                                  RGB(235, 235, 235)};

static char *sp(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *su(char *p, uint32_t v)
{
    char t[10];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}

static void draw_row(uint32_t i, uint32_t sel)
{
    uint32_t j;
    cv_begin(240, 16, C_BLACK);
    cv_text(i == sel ? 6 : 10, 0, &FONT_S, i == sel ? ">" : " ", C_ACC);
    cv_text(26, 0, &FONT_S, FOOTAGE[i], i == sel ? C_ACC : C_BAR);
    for (j = 0; j < 8u; j++)
        cv_rect(76 + (int32_t)j * 18, 2, 16, 12, j < P.db[i] ? BARC[i] : RGB(34, 34, 34));
    {
        char s[2] = {(char)('0' + P.db[i]), 0};
        cv_text(226, 0, &FONT_S, s, i == sel ? C_ACC : C_BAR);
    }
    cv_blit(0, 38u + 16u * i);
}

static void draw_status(uint32_t preset)
{
    static const char *const VIB[7] = {"OFF", "V1", "V2", "V3", "C1", "C2", "C3"};
    static const char *const LES[3] = {"OFF", "SLOW", "FAST"};
    char s[40], *p = s;
    p = sp(p, "VIB "); p = sp(p, VIB[P.vib]); p = sp(p, "  LESLIE "); p = sp(p, LES[P.les]); *p = 0;
    draw_text_box(0, 184, 240, &FONT_S, s, C_BAR, 1);
    p = s;
    if (P.perc) { p = sp(p, "PERC "); p = sp(p, P.perc3 ? "3RD " : "2ND "); p = sp(p, P.fast ? "FAST " : "SLOW "); p = sp(p, P.soft ? "SOFT" : "NORM"); }
    else p = sp(p, "PERC OFF");
    p = sp(p, "  DRV "); p = su(p, P.drv); *p = 0;
    draw_text_box(0, 202, 240, &FONT_S, s, C_BAR, 1);
    p = sp(s, PRESET[preset].name); *p = 0;
    draw_text_box(0, 220, 240, &FONT_S, s, C_ACC, 1);
}

static void draw_load(uint32_t us, uint32_t w)
{
    char s[24], *p = sp(s, "W ");
    p = su(p, w); p = sp(p, "  "); p = su(p, us); p = sp(p, " us"); *p = 0;
    draw_text_box(120, 4, 120, &FONT_S, s, C_OFF, 2);
}

static void app_main(void)
{
    struct { uint8_t magic, db[NB], vib, perc, perc3, fast, soft, les, drv, preset; int8_t oct; } set =
        {0xB3, {8, 8, 8, 0, 0, 0, 0, 0, 0}, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    uint32_t prev = 0, k, i, sel = 2, preset = 1, t_change = 0, dirty = 0, t_load = 0, seed = 12345u;
    uint32_t drawn_db[NB], drawn_sel = 99, status_key = ~0u;
    uint8_t held[27];
    int32_t oct = 0;

    for (i = 0; i < NW; i++) { seed = seed * 1664525u + 1013904223u; wph[i] = seed; }   /* the wheels start anywhere */
    for (i = 0; i < 27u; i++) held[i] = 255;
    for (i = 0; i < NB; i++) drawn_db[i] = 99;
    if (settings_load(&set, sizeof set) == 0 && set.magic == 0xB3 && set.preset < 8u && set.oct >= -2 && set.oct <= 2 &&
        set.vib < 7u && set.les < 3u && set.drv < 9u) {
        for (i = 0; i < NB; i++) P.db[i] = set.db[i] > 8u ? 8u : set.db[i];
        P.vib = set.vib; P.perc = set.perc & 1u; P.perc3 = set.perc3 & 1u; P.fast = set.fast & 1u;
        P.soft = set.soft & 1u; P.les = set.les; P.drv = set.drv;
        preset = set.preset; oct = set.oct;
    } else
        load_preset(preset);

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 4, 120, &FONT_S, "B3 ORGAN v" APP_VER, C_ACC, 0);

    for (;;) {
        uint32_t keys, down, up, rel, b;
        int32_t e;
        uint8_t st, d1, d2;
        platform_poll();

        keys = fm1_in.notes;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {             /* a key stops the note it started, whatever the octave is now */
            if (down >> k & 1u) {
                int32_t key = (int32_t)(53 + 12 * oct + (int32_t)k) - (int32_t)NOTE0;
                if (key >= 0 && key < (int32_t)NK) { held[k] = (uint8_t)key; key_set(&pk_lo, &pk_hi, (uint32_t)key, 1); }
            }
            if (up >> k & 1u && held[k] != 255) { key_set(&pk_lo, &pk_hi, held[k], 0); held[k] = 255; }
        }

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN) && oct > -2) { oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && oct < 2) { oct++; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_FX)) { P.vib = (uint8_t)((P.vib + 1u) % 7u); dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_LFO)) { P.les = (uint8_t)((P.les + 1u) % 3u); dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_ENV)) { P.perc ^= 1u; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_SEL)) { P.perc3 ^= 1u; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_ARP)) { P.fast ^= 1u; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_SEQ)) { P.soft ^= 1u; dirty = 1; t_change = fm1_ms; }

        if ((e = fm1_enc_take(ENC_SELECT)) != 0) sel = (uint32_t)((int32_t)sel + e < 0 ? 0 : (int32_t)sel + e > 8 ? 8 : (int32_t)sel + e);
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            int32_t v = (int32_t)P.db[sel] + e;
            P.db[sel] = (uint8_t)(v < 0 ? 0 : v > 8 ? 8 : v);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            int32_t v = (int32_t)preset + e;
            preset = (uint32_t)(v < 0 ? 0 : v > 7 ? 7 : v);
            load_preset(preset);
            dirty = 1; t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) { int32_t v = (int32_t)P.drv + e; P.drv = (uint8_t)(v < 0 ? 0 : v > 8 ? 8 : v); dirty = 1; t_change = fm1_ms; }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) { int32_t v = (int32_t)P.vib + e; P.vib = (uint8_t)(v < 0 ? 0 : v > 6 ? 6 : v); dirty = 1; t_change = fm1_ms; }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) { int32_t v = (int32_t)P.les + e; P.les = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v); dirty = 1; t_change = fm1_ms; }

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in, any channel */
            uint32_t cmd = st & 0xF0u;
            if (cmd == 0x90u && d2 && d1 >= NOTE0) key_set(&mk_lo, &mk_hi, d1 - NOTE0, 1);
            else if ((cmd == 0x80u || cmd == 0x90u) && d1 >= NOTE0) key_set(&mk_lo, &mk_hi, d1 - NOTE0, 0);
            else if (cmd == 0xB0u) {
                if (d1 >= 70u && d1 <= 78u) P.db[d1 - 70u] = (uint8_t)((d2 * 9u) >> 7);   /* as setBfree */
                else if (d1 == 11u) P.swell = d2;
            }
        }

        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), held[k] != 255);
        fm1_led_key(BTN_OCT_DOWN, oct < 0);
        fm1_led_key(BTN_OCT_UP, oct > 0);
        fm1_led_key(BTN_FX, P.vib != 0);
        fm1_led_key(BTN_LFO, P.les != 0);
        fm1_led_key(BTN_ENV, P.perc != 0);

        for (i = 0; i < NB; i++)                /* draw only what changed */
            if (P.db[i] != drawn_db[i] || (i == sel) != (i == drawn_sel)) {
                drawn_db[i] = P.db[i];
                draw_row(i, sel);
            }
        drawn_sel = sel;
        {
            uint32_t key = P.vib | P.les << 3 | P.perc << 5 | P.perc3 << 6 | P.fast << 7 | P.soft << 8 | P.drv << 9 | preset << 13;
            if (key != status_key) { status_key = key; draw_status(preset); }
        }
        if (fm1_ms - t_load > 500u) {           /* how close to the 5800 us a block has: the number to watch */
            t_load = fm1_ms;
            draw_load(load_us, wheels_on);
            load_us = 0;
        }

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {   /* an erase stalls the sound: save when silent */
            for (i = 0; i < NB; i++) set.db[i] = P.db[i];
            set.vib = P.vib; set.perc = P.perc; set.perc3 = P.perc3; set.fast = P.fast; set.soft = P.soft;
            set.les = P.les; set.drv = P.drv; set.preset = (uint8_t)preset; set.oct = (int8_t)oct;
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
