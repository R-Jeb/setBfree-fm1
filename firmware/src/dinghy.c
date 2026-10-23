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
 *   USB-MIDI    and the TRS MIDI IN jack: notes (folded into C2..C7), CC 70..78 = drawbars 16'..1' (as setBfree), CC 11 = swell
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division. */
#define APP_NAME "B3 ORGAN"                     /* the rescue and the crash screen say this */
#include "platform.c"
#include "organ_tab.h"

#ifdef DINGHY_HOST
static uint32_t fm1_ticks(void) { return 0; }
#define FM1_TICKS_PER_US 24u
#endif

#include "organ_core.h"

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
            uint32_t n = d1;                    /* a keyboard may play outside C2..C7: fold it in by octaves */
            while (n < NOTE0) n += 12u;
            while (n >= NOTE0 + NK) n -= 12u;
            if (cmd == 0x90u && d2) key_set(&mk_lo, &mk_hi, n - NOTE0, 1);
            else if (cmd == 0x80u || cmd == 0x90u) key_set(&mk_lo, &mk_hi, n - NOTE0, 0);
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
