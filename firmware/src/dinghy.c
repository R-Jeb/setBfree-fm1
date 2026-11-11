/* SPDX-License-Identifier: GPL-3.0-only */
/* B3 ORGAN for the M-VAVE FM-1: a fixed-point tonewheel organ on Dinghy's platform.
 *
 * The model follows setBfree (pantherb/setBfree, GPL-2.0-or-later, so it combines with Dinghy's GPL-3.0):
 *   - 91 tonewheels with the 60 Hz gear ratios of setBfree's tonegen.c (tables from tools/gen_organ.py)
 *   - 9 drawbars with setBfree's footage-to-wheel mapping and its linear level (setting / 8)
 *   - percussion on the 2nd or 3rd harmonic: single trigger, fast/slow decay (-60 dB in 1 s / 4 s), soft/normal,
 *     the 1' drawbar muted while it is on
 *   - a vibrato/chorus scanner at 7.25 Hz (V1..V3, C1..C3), an overdrive and a "Leslie-lite"
 * It is NOT setBfree itself: no float, no malloc, no whirl convolution, three manuals (upper, lower, pedal).
 *
 *   keys        play the organ (key 0 = F3, OCT-/OCT+ shifts) on the manual REC chose    MASTER   volume (platform.c)
 *   SELECT      choose a drawbar    ALGORITHM  set it 0..8         PRESETS  setBfree's registrations (default.pgm)
 *   KNOB1       screen: upper, lower, pedal    KNOB2   vibrato V1 C1 V2 C2 V3 C3    KNOB3   overdrive amount    KNOB4   reverb
 *   FX          vibrato on/off (upper on screen 1, lower on screen 2)    LFO    Leslie stop/slow/fast    SEL    overdrive on/off
 *   ENV         percussion on/off   ARP  2nd/3rd   SEQ  fast/slow   PLAY  soft/normal   (screen 1)     REC   keys: upper/lower/pedal
 *   EDIT, SAVE  free
 *   USB-MIDI    and the TRS MIDI IN jack: channel 1 plays the upper manual, 2 the lower one, 3 the pedal (drawbars 70..78 follow the
 *               channel; the pedal has 16' = 70 and 8' = 72), program change (setBfree's default.pgm), and setBfree's controllers
 *               (CC 1, 7, 11, 30, 31, 64..66, 70..78, 80..83, 91..93, 95)
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division. */
#define APP_NAME "B3 ORGAN"                     /* the rescue and the crash screen say this */
#include "platform.c"
#include "organ_tab.h"

#ifdef DINGHY_HOST
static uint32_t fm1_ticks(void) { return 0; }
#define FM1_TICKS_PER_US 24u
#endif

#define ORGAN_POOL __attribute__((section(".pool")))   /* the reverb and Leslie delay lines: zeroed at boot */
#include "organ_core.h"

/* ---- the screen */
#define C_ACC RGB(255, 190, 90)                 /* tungsten */
#define C_OFF RGB(110, 110, 110)
#define C_BAR RGB(230, 230, 230)
static const char *const FOOT[NB] = {"16", "513", "8", "4", "223", "2", "135", "113", "1"};
static const uint16_t BARC[NB] = {RGB(190, 120, 60), RGB(190, 120, 60), RGB(235, 235, 235), RGB(235, 235, 235),
                                  RGB(90, 90, 100), RGB(235, 235, 235), RGB(90, 90, 100), RGB(90, 90, 100),
                                  RGB(235, 235, 235)};
#define C_TRK RGB(34, 34, 34)
#define C_PCH RGB(44, 30, 18)                   /* the peach boxes of the sketch: vibrato and percussion */

static char *sp(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *su(char *p, uint32_t v)
{
    char t[10];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}

#define C_RED RGB(230, 70, 60)
#define C_GRN RGB(120, 220, 140)
static void draw_battery(uint32_t st)           /* st: bars 0 .. 4, bit 3 = a USB host has it (a bolt instead) */
{
    static const char *const BOLT[7] = {"...XX.", "..XX..", ".XX...", "XXXXXX", "...XX.", "..XX..", ".XX..."};
    const uint32_t x = 212, y = 222, bars = st & 7u;
    uint32_t r, c, k;
    uint16_t line = bars ? C_BAR : C_RED;
    lcd_fill(x, y, 28, 16, C_BLACK);
    if (st & 8u) {
        for (r = 0; r < 7u; r++)
            for (c = 0; c < 6u; c++)
                if (BOLT[r][c] == 'X') lcd_fill(x + 8u + c * 2u, y + 1u + r * 2u, 2, 2, C_GRN);
        return;
    }
    lcd_fill(x + 1u, y + 2u, 24, 1, line);      /* the cell: outline, the nub, then a bar for each step */
    lcd_fill(x + 1u, y + 13u, 24, 1, line);
    lcd_fill(x + 1u, y + 2u, 1, 12, line);
    lcd_fill(x + 24u, y + 2u, 1, 12, line);
    lcd_fill(x + 25u, y + 5u, 2, 6, line);
    for (k = 0; k < bars; k++)
        lcd_fill(x + 3u + k * 5u, y + 4u, 4, 8, bars <= 1u ? C_RED : C_ACC);
}

/* ---- three screens: 1 upper, 2 lower, 3 pedal. Each is built from bands (a canvas of 240 x h, drawn and blitted on its own, and only when
 * what it shows has changed): the header, the three manual blocks, four rows of switches, the program. The block of the manual a screen is
 * for shows its drawbars as bars; the others show their values. */
#define TRACK 44                                /* a drawbar: the track, the cap that hangs down as far as the value says */
#define CAP 12
#define STEP 4
#define BLK_BARS 62                             /* the track, its footage row */
#define Y_CTRL 142                              /* the switches start here, 18 apart, on every screen */
static const char *const MAN[3] = {"UPPER", "LOWER", "PEDAL"};
static const uint8_t PEDID[2] = {0, 2};         /* the pedal's drawbars sit in the columns of the 16' and the 8' */
static uint32_t screen, sel[3];
static uint32_t load_peak, load_now, load_wheels;
static uint32_t midi_ch, midi_kind, midi_cnt, midi_t, midi_d1;   /* the last message that came in: channel 1..16, kind 0 none 1 on 2 off 3 CC 4 PC 5 other */

static uint32_t hmix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t colx(uint32_t i) { return 3u + 26u * i; }
static void cv_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c); cv_rect(x, y + h - 1, w, 1, c); cv_rect(x, y, 1, h, c); cv_rect(x + w - 1, y, 1, h, c);
}
static int32_t chip(int32_t x, int32_t y, const char *t, int on, int edit)     /* a switch: boxed, tungsten when it is on and this screen can set it */
{
    int32_t w = text_w(&FONT_S, t) + 6;
    uint16_t c = !edit ? C_OFF : on ? C_ACC : C_BAR;
    cv_rect(x + 1, y + 1, w - 2, 15, C_BLACK);
    cv_box(x, y, w, 17, edit ? (on ? C_ACC : C_OFF) : C_TRK);
    cv_text(x + 3, y + 1, &FONT_S, t, c);
    return x + w;
}
static int32_t oval(int32_t x, int32_t y, const char *t)                       /* a knob's value, in a pill */
{
    int32_t w = text_w(&FONT_S, t) + 10, i;
    cv_rect(x + 4, y, w - 8, 1, C_ACC); cv_rect(x + 4, y + 16, w - 8, 1, C_ACC);
    cv_rect(x, y + 4, 1, 9, C_ACC); cv_rect(x + w - 1, y + 4, 1, 9, C_ACC);
    for (i = 0; i < 4; i++) {
        cv_pset(x + 3 - i, y + 1 + i, C_ACC); cv_pset(x + w - 4 + i, y + 1 + i, C_ACC);
        cv_pset(x + 3 - i, y + 15 - i, C_ACC); cv_pset(x + w - 4 + i, y + 15 - i, C_ACC);
    }
    cv_text(x + 5, y + 1, &FONT_S, t, C_BAR);
    return x + w;
}
static void draw_bars(int32_t y0, const uint8_t *v, const uint8_t *ids, uint32_t cnt, uint32_t selected)
{
    uint32_t k;
    for (k = 0; k < cnt; k++) {
        uint32_t i = ids ? ids[k] : k;
        int32_t x = (int32_t)colx(i), cy = y0 + (int32_t)v[k] * STEP;
        char d[2] = {(char)('0' + v[k]), 0}, *f = (char *)FOOT[i];
        cv_rect(x + 4, y0, 16, TRACK, C_TRK);
        if (v[k]) cv_rect(x + 9, y0, 6, cy - y0, RGB(70, 70, 70));
        cv_rect(x + 1, cy, 22, CAP, BARC[i]);
        if (k == selected) cv_box(x, cy - 1, 24, CAP + 2, C_ACC);
        cv_text(x + 8, cy - 2, &FONT_S, d, BARC[i] > RGB(150, 150, 150) ? C_BLACK : C_BAR);
        cv_text(x + 13 - text_w(&FONT_S, f) / 2, y0 + TRACK + 2, &FONT_S, f, k == selected ? C_ACC : C_BAR);
    }
}
static void draw_digits(int32_t y0, const uint8_t *v, const uint8_t *ids, uint32_t cnt)
{
    uint32_t k;
    for (k = 0; k < cnt; k++) {
        char d[2] = {(char)('0' + v[k]), 0};
        cv_text((int32_t)colx(ids ? ids[k] : k) + 8, y0, &FONT_S, d, C_OFF);
    }
}
static uint32_t block_h(uint32_t scr, uint32_t m) { return (m ? 16u : 0u) + (m == scr ? BLK_BARS : 16u); }
static uint32_t block_y(uint32_t scr, uint32_t m) { uint32_t y = 16, j; for (j = 0; j < m; j++) y += block_h(scr, j); return y; }

static void band_header(uint32_t scr)
{
    char t[24];
    cv_begin(240, 16, C_BLACK);
    cv_text(4, 0, &FONT_S, scr == 0 ? "UPPER" : "UPPER INFO", scr == 0 ? C_ACC : C_OFF);
    sp(sp(t, "B3 ORGAN v"), APP_VER)[0] = 0;
    cv_text(238 - text_w(&FONT_S, t), 0, &FONT_S, t, C_OFF);
    cv_blit(0, 0);
}
static void band_block(uint32_t scr, uint32_t m)
{
    uint32_t h = block_h(scr, m), act = m == scr;
    int32_t y0 = 0;
    const uint8_t *v = m == 0 ? (const uint8_t *)P.db : m == 1 ? (const uint8_t *)P.dbl : (const uint8_t *)P.dbp;
    uint32_t cnt = m == 2 ? 2u : NB;
    cv_begin(240, h, C_BLACK);
    if (m) {
        char t[16];
        cv_text(4, 0, &FONT_S, MAN[m], act ? C_ACC : C_OFF);
        if (!act) cv_text(4 + 8 * 6, 0, &FONT_S, "INFO", C_OFF);
        if (m == 2 && act) {                    /* the free area of the pedal screen: the load of the audio block */
            char *q;
            cv_text(104, 0, &FONT_S, "LOAD", C_OFF);
            q = su(sp(t, "W "), load_wheels); *q = 0;
            cv_text(104, 16, &FONT_S, t, C_BAR);
            q = sp(su(t, load_now), " us"); *q = 0;
            cv_text(104, 32, &FONT_S, t, C_BAR);
            q = su(sp(t, "MAX "), load_peak); *q = 0;
            cv_text(104, 48, &FONT_S, t, C_OFF);
            {                                   /* MIDI in: the channel of the last message (bright for a second), and how many came */
                static const char *const KN[6] = {"--", "ON", "OFF", "CC", "PC", "?"};
                uint16_t mc = midi_kind && fm1_ms - midi_t < 1000u ? C_ACC : C_OFF;
                cv_text(184, 0, &FONT_S, "MIDI", C_OFF);
                q = midi_kind ? su(sp(t, "CH "), midi_ch) : sp(t, "CH -"); *q = 0;
                cv_text(184, 16, &FONT_S, t, mc);
                q = sp(t, KN[midi_kind]);                       /* a controller or program change shows its number: "CC 64" */
                if (midi_kind == 3u || midi_kind == 4u) q = su(sp(q, " "), midi_d1);
                *q = 0;
                cv_text(184, 32, &FONT_S, t, mc);
                q = su(t, midi_cnt % 10000u); *q = 0;
                cv_text(184, 48, &FONT_S, t, C_OFF);
            }
        }
        y0 = 16;
    }
    if (act) draw_bars(y0 + 1, v, m == 2 ? PEDID : 0, cnt, sel[m]);
    else draw_digits(y0, v, m == 2 ? PEDID : 0, cnt);
    cv_blit(0, block_y(scr, m));
}
static const char *const VIB[6] = {"V1", "C1", "V2", "C2", "V3", "C3"};
static const char *const LES[3] = {"STOP", "SLOW", "FAST"};
static const char *const KEYN[3] = {"UPPER", "LOWER", "PEDAL"};
static void band_ctrl(uint32_t scr, uint32_t r)
{
    int32_t x;
    char t[24], *q;
    cv_begin(240, 18, C_BLACK);
    if (r == 0) {                               /* vibrato / chorus: the type (KNOB 2) and where it is on (FX) */
        cv_rect(0, 0, 240, 17, C_PCH);
        x = cv_text(4, 1, &FONT_S, "V/C", C_ACC) + 4;
        x = oval(x, 0, VIB[P.vtype % 6u]) + 6;
        x = chip(x, 0, P.vup ? "UP ON" : "UP OFF", P.vup, scr == 0) + 4;
        chip(x, 0, P.vlo ? "LO ON" : "LO OFF", P.vlo, scr == 1);
    } else if (r == 1) {                        /* percussion belongs to the upper manual: set on screen 1, shown on the others */
        int pe = scr == 0;
        cv_rect(0, 0, 240, 17, C_PCH);
        x = cv_text(4, 1, &FONT_S, "PERC", pe ? C_ACC : C_OFF) + 4;
        x = chip(x, 0, P.perc ? "ON" : "OFF", P.perc, pe) + 3;
        x = chip(x, 0, P.perc3 ? "3RD" : "2ND", P.perc, pe) + 3;
        x = chip(x, 0, P.fast ? "FAST" : "SLOW", P.perc, pe) + 3;
        chip(x, 0, P.soft ? "SOFT" : "NORM", P.perc, pe);
    } else if (r == 2) {
        q = sp(sp(t, "LES "), LES[P.les % 3u]); *q = 0;
        x = chip(1, 0, t, 1, 1) + 4;
        q = sp(t, P.od ? "OD ON" : "OD OFF"); *q = 0;
        x = chip(x, 0, t, P.od, 1) + 4;
        q = sp(sp(t, "KEYS "), KEYN[P.keys % 3u]); *q = 0;
        chip(x, 0, t, 1, 1);
    } else {
        x = cv_text(4, 1, &FONT_S, "DRIVE", C_BAR) + 3;
        q = su(t, P.drv); *q = 0;
        x = oval(x, 0, t) + 8;
        x = cv_text(x, 1, &FONT_S, "REVERB", C_BAR) + 3;
        q = su(t, P.rev); *q = 0;
        oval(x, 0, t);
    }
    cv_blit(0, Y_CTRL + 18u * r);
}
static void band_preset(void)
{
    char s[40], *p = s;
    if (P.pgm && PGM[P.pgm].name) { p = su(p, P.pgm); p = sp(p, " "); p = sp(p, PGM[P.pgm].name); }
    *p = 0;
    while (p > s && text_w(&FONT_S, s) > 206) *--p = 0;     /* the corner is the battery's */
    draw_text_box(0, 222, 208, &FONT_S, s, C_ACC, 0);
}

static uint32_t state_key(int32_t oct, uint32_t *status)   /* what is worth saving; *status: what the screen shows */
{
    uint32_t i, dbk = 0, k = P.vtype | P.vup << 3 | P.les << 4 | P.perc << 6 | P.perc3 << 7 | P.fast << 8 | P.soft << 9 |
                             P.od << 10 | P.drv << 11 | P.pgm << 15 | (uint32_t)P.rev << 22 | P.vlo << 29 | P.keys << 30;
    for (i = 0; i < NB; i++) dbk = dbk * 9u + P.db[i];
    for (i = 0; i < NB; i++) dbk = dbk * 9u + P.dbl[i];
    dbk = dbk * 9u + P.dbp[0]; dbk = dbk * 9u + P.dbp[1];
    *status = k;
    return (k ^ dbk * 2654435761u) + (uint32_t)(oct + 2);
}

static uint8_t clampu(int32_t v, int32_t hi) { return (uint8_t)(v < 0 ? 0 : v > hi ? hi : v); }

static void app_main(void)
{
    struct { uint8_t magic, db[NB], vtype, vup, perc, perc3, fast, soft, les, od, drv, pgm, rev; int8_t oct;
             uint8_t dbl[NB], dbp[2], vlo, keys; } set;
    uint32_t prev = 0, k, i, t_change = 0, dirty = 0, t_load = 0, seed = 12345u, saved_key = 0, last_key = 0;
    uint32_t hash[9], drawn_screen = 99, drawn_batt = ~0u;
    uint8_t held[27], heldm[27];
    int32_t oct = 0;

    for (i = 0; i < NW; i++) { seed = seed * 1664525u + 1013904223u; wph[i] = seed; }   /* the wheels start anywhere */
    for (i = 0; i < 27u; i++) { held[i] = 255; heldm[i] = 0; }
    sel[0] = sel[1] = 2; sel[2] = 0;
    if (settings_load(&set, sizeof set) == 0 && set.magic == 0xB6 && set.oct >= -2 && set.oct <= 2 && set.vtype < 6u &&
        set.les < 3u && set.drv < 9u && set.pgm < 128u && set.rev < 128u && set.keys < 3u) {
        for (i = 0; i < NB; i++) { P.db[i] = set.db[i] > 8u ? 8u : set.db[i]; P.dbl[i] = set.dbl[i] > 8u ? 8u : set.dbl[i]; }
        P.dbp[0] = set.dbp[0] > 8u ? 8u : set.dbp[0]; P.dbp[1] = set.dbp[1] > 8u ? 8u : set.dbp[1];
        P.vtype = set.vtype; P.vup = set.vup & 1u; P.vlo = set.vlo & 1u; P.perc = set.perc & 1u; P.perc3 = set.perc3 & 1u;
        P.fast = set.fast & 1u; P.soft = set.soft & 1u; P.les = set.les; P.od = set.od & 1u; P.drv = set.drv;
        P.pgm = set.pgm; P.rev = set.rev; P.keys = set.keys; oct = set.oct;
    } else
        organ_program(5u);                      /* Standard B, 88 8000 000 */
    last_key = saved_key = state_key(oct, &i);

    for (;;) {
        uint32_t keys, down, up, rel, b;
        int32_t e;
        uint8_t st, d1, d2;
        platform_poll();

        keys = fm1_in.notes;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {             /* a key stops the note it started, on the manual it started it, whatever the octave is now */
            if (down >> k & 1u) {
                uint32_t note = (uint32_t)(53 + 12 * oct + (int32_t)k);
                held[k] = (uint8_t)note; heldm[k] = P.keys;
                organ_note(P.keys, 1u, note, 1, 0);
            }
            if (up >> k & 1u && held[k] != 255) { organ_note(heldm[k], 1u, held[k], 0, 0); held[k] = 255; }
        }

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN) && oct > -2) { oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && oct < 2) { oct++; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_FX)) { if (screen == 0) P.vup ^= 1u; else if (screen == 1) P.vlo ^= 1u; }   /* vibrato / chorus on the manual of the screen */
        if (b & (1u << BTN_LFO)) P.les = (uint8_t)((P.les + 1u) % 3u);   /* Leslie: stop, slow, fast */
        if (b & (1u << BTN_SEL)) P.od ^= 1u;                    /* overdrive on / off */
        if (b & (1u << BTN_REC)) P.keys = (uint8_t)((P.keys + 1u) % 3u);   /* the FM-1's own keys: upper, lower, pedal */
        if (screen == 0) {                      /* percussion is the upper manual's */
            if (b & (1u << BTN_ENV)) P.perc ^= 1u;
            if (b & (1u << BTN_ARP)) P.perc3 ^= 1u;
            if (b & (1u << BTN_SEQ)) P.fast ^= 1u;
            if (b & (1u << BTN_PLAY)) P.soft ^= 1u;
        }

        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) screen = clampu((int32_t)screen + e, 2);
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) sel[screen] = clampu((int32_t)sel[screen] + e, screen == 2 ? 1 : 8);
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            volatile uint8_t *db = screen == 0 ? P.db : screen == 1 ? P.dbl : P.dbp;
            db[sel[screen]] = clampu((int32_t)db[sel[screen]] + e, 8);
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {             /* setBfree's registrations, one after the other (the upper manual's) */
            uint32_t n = P.pgm ? P.pgm : 5u;
            for (; e > 0; e--) n = pgm_step(n, 1);
            for (; e < 0; e++) n = pgm_step(n, -1);
            organ_program(n);
        }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) P.vtype = clampu((int32_t)P.vtype + e, 5);
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) P.drv = clampu((int32_t)P.drv + e, 8);
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) P.rev = clampu((int32_t)P.rev + e * 8, 127);   /* reverb mix, like CC 91 */

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in: channel 1 upper, 2 lower, 3 pedal */
            uint32_t cmd = st & 0xF0u, ch = st & 0x0Fu;
            if (cmd >= 0x80u && cmd < 0xF0u) {
                midi_ch = ch + 1u; midi_cnt++; midi_t = fm1_ms; midi_d1 = d1;
                midi_kind = cmd == 0x90u && d2 ? 1u : cmd == 0x80u || cmd == 0x90u ? 2u : cmd == 0xB0u ? 3u : cmd == 0xC0u ? 4u : 5u;
            }
            if (cmd == 0x90u && d2) organ_midi_note(ch, d1, 1);
            else if (cmd == 0x80u || cmd == 0x90u) organ_midi_note(ch, d1, 0);
            else if (cmd == 0xB0u) organ_cc_ch(ch, d1, d2);     /* setBfree's controller numbers (organ_core.h) */
            else if (cmd == 0xC0u) organ_pc(d1);                /* program change: setBfree's default.pgm */
        }

        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), held[k] != 255);
        fm1_led_key(BTN_OCT_DOWN, oct < 0);
        fm1_led_key(BTN_OCT_UP, oct > 0);
        fm1_led_key(BTN_FX, screen == 0 ? P.vup != 0 : screen == 1 ? P.vlo != 0 : 0);
        fm1_led_key(BTN_LFO, P.les != 0);
        fm1_led_key(BTN_ENV, P.perc != 0);
        fm1_led_key(BTN_SEL, P.od != 0);
        fm1_led_key(BTN_ARP, P.perc3 != 0);
        fm1_led_key(BTN_SEQ, P.fast != 0);
        fm1_led_key(BTN_PLAY, P.soft != 0);
        fm1_led_key(BTN_REC, P.keys != 0);

        if (fm1_ms - t_load > 500u) {           /* how close to the 5800 us a block has: the number to watch */
            t_load = fm1_ms;
            load_now = load_us; load_wheels = wheels_on;
            if (load_now > load_peak) load_peak = load_now;
            load_us = 0;
        }

        if (screen != drawn_screen) {           /* a new screen: clear, and draw every band */
            drawn_screen = screen;
            lcd_fill(0, 0, 240, 240, C_BLACK);
            for (i = 0; i < 9u; i++) hash[i] = 0xFFFFFFFFu;
            drawn_batt = ~0u;
        }
        {
            uint32_t h, m, r;
            h = hmix(0, screen);
            if (h != hash[0]) { hash[0] = h; band_header(screen); }
            for (m = 0; m < 3u; m++) {
                const volatile uint8_t *v = m == 0 ? P.db : m == 1 ? P.dbl : P.dbp;
                uint32_t cnt = m == 2u ? 2u : NB;
                h = hmix(hmix(1, screen), m);
                for (i = 0; i < cnt; i++) h = hmix(h, v[i]);
                h = hmix(h, sel[m]);
                if (m == 2u && screen == 2u) h = hmix(hmix(hmix(hmix(hmix(h, load_now), load_peak), load_wheels), midi_ch * 8u + midi_kind + midi_cnt * 64u + midi_d1 * 4096u), midi_kind && fm1_ms - midi_t < 1000u);
                if (h != hash[1 + m]) { hash[1 + m] = h; band_block(screen, m); }
            }
            for (r = 0; r < 4u; r++) {
                h = hmix(2, screen);
                if (r == 0) h = hmix(hmix(hmix(h, P.vtype), P.vup), P.vlo);
                else if (r == 1) h = hmix(hmix(hmix(hmix(h, P.perc), P.perc3), P.fast), P.soft);
                else if (r == 2) h = hmix(hmix(hmix(h, P.les), P.od), P.keys);
                else h = hmix(hmix(h, P.drv), P.rev);
                h = hmix(h, r);
                if (h != hash[4 + r]) { hash[4 + r] = h; band_ctrl(screen, r); }
            }
            h = hmix(3, P.pgm);
            if (h != hash[8]) { hash[8] = h; band_preset(); }
        }
        {
            uint32_t bs = battery_state(), sk, key = state_key(oct, &sk);
            if (bs != drawn_batt) { drawn_batt = bs; draw_battery(bs); }
            if (key != last_key) { last_key = key; dirty = key != saved_key; t_change = fm1_ms; }
        }

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {   /* an erase stalls the sound: save when silent */
            set.magic = 0xB6;
            for (i = 0; i < NB; i++) { set.db[i] = P.db[i]; set.dbl[i] = P.dbl[i]; }
            set.dbp[0] = P.dbp[0]; set.dbp[1] = P.dbp[1]; set.vlo = P.vlo; set.keys = P.keys;
            set.vtype = P.vtype; set.vup = P.vup; set.perc = P.perc; set.perc3 = P.perc3; set.fast = P.fast;
            set.soft = P.soft; set.les = P.les; set.od = P.od; set.drv = P.drv; set.pgm = P.pgm; set.rev = P.rev; set.oct = (int8_t)oct;
            settings_save(&set, sizeof set);
            saved_key = last_key;
            dirty = 0;
        }
    }
}
