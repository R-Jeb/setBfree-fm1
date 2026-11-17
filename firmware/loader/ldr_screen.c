/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 zvenson (from OberVave; the Firmware Hub's look)
 * Copyright (C) 2026 illuminus5 (Skiff: the idea, a loader that draws, its 5 x 7 letters and the order of things)
 * B3 ORGAN: the logo above the progress, R-Jeb */
/* The update loader's screen (loader.c; tests/ldr_screen_host.c draws it on the PC). A unit whose install was cut off comes
 * back to the loader at every power-on; it says what it does, under the B3 ORGAN logo (organ_logo.h, the app's):
 *   the logo, 240 x 240, its foot line given over to the progress
 *   20 segments: a light running to and fro while it waits (ldr_anim), the share written while it writes
 *   under them what happens (UPDATE MODE, WRITING 57%, UPDATE FAILED) and what to do
 * Drawn with the loader's own letters (the app area may be half written: never read), before USB starts (USB is
 * polled: the panel's ~0.3 s init would leave the host's enumeration unanswered). */
#include "../src/organ_logo.h"
#define LDR_COLOUR 0xE447u               /* the logo's amber, #E0893A */
#define LDR_BG 0x3964u                   /* the logo's brown, #3E2D20 */
#define LDR_INK 0xF75Bu                  /* #F0EADC */
#define LDR_SUB 0xC5B4u                  /* #C4B4A2 */
#define LDR_DIM 0x28E2u                  /* a segment off, #2A1F17 */
#define LDR_MIX(a, b) ((((a) >> 11) + ((b) >> 11)) / 2u << 11 | ((((a) >> 5) & 63u) + (((b) >> 5) & 63u)) / 2u << 5 | \
                       (((a) & 31u) + ((b) & 31u)) / 2u)
#define LDR_TAIL LDR_MIX(LDR_COLOUR, LDR_DIM)   /* the waiting light's afterglow: halfway to a segment off */

/* the letters the screens use (others: a space): capitals, digits, % and the hub's address in small letters */
static const char LDR_CH[] = "ABCDEFGHIKLMNOPRSTUVWXY0123456789%abcefhimortuw.";
static const uint8_t LDR_FONT[sizeof LDR_CH - 1u][7] = {         /* rows, bit 4 the left dot */
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},   /* A B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},   /* C D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},   /* E F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},   /* G H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},   /* I K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},   /* L M */
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},   /* N O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},   /* P R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},   /* S T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},   /* U V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},   /* W X */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},                                               /* Y */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},   /* 0 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},   /* 2 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},   /* 4 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},   /* 6 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},   /* 8 9 */
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03},                                               /* % */
    {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}, {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E},   /* a b */
    {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E}, {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E},   /* c e */
    {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08}, {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11},   /* f h */
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}, {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11},   /* i m */
    {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}, {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10},   /* o r */
    {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06}, {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D},   /* t u */
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},   /* w . */
};
#define LDR_FOOT 214u                    /* the logo's foot (its subtitle) becomes the progress: segments and a line of text */
#define LDR_SEGS 20u                     /* the segments: 9 px and a 2 px gap, 218 px, centred */
#define LDR_SEG_W 9u
#define LDR_SEG_X 11u
#define LDR_SEG_Y 217u
#define LDR_SEG_H 7u
#define LDR_TXT_Y 228u

static uint32_t ldr_missing;                        /* letters drawn as gaps: 0 (tests/ldr_screen_host.c) */
static uint32_t ldr_len(const char *t)
{
    uint32_t n = 0;
    while (t[n])
        n++;
    return n;
}
static void ldr_text(uint32_t y, uint32_t s, uint16_t c, const char *t)   /* centred, a dot s x s px */
{
    uint32_t n = ldr_len(t), x, r, k, i, b;
    if (!n)
        return;
    for (x = (240u - (n * 6u - 1u) * s) / 2u; *t; t++, x += 6u * s) {
        for (k = 0; LDR_CH[k] && LDR_CH[k] != *t; k++)
            ;
        if (!LDR_CH[k]) {
            ldr_missing += *t != ' ';                   /* (a letter the font lacks: a gap) */
            continue;
        }
        for (r = 0; r < 7u; r++)
            for (i = 0; i < 5u; i = b) {                /* each run of dots in the row: one fill */
                for (; i < 5u && !(LDR_FONT[k][r] >> (4u - i) & 1u); i++)
                    ;
                for (b = i; b < 5u && (LDR_FONT[k][r] >> (4u - b) & 1u); b++)
                    ;
                if (b > i)
                    lcd_fill(x + i * s, y + r * s, (b - i) * s, s, c);
            }
    }
}
static void ldr_seg(uint32_t i, uint16_t c)
{
    lcd_fill(LDR_SEG_X + i * (LDR_SEG_W + 2u), LDR_SEG_Y, LDR_SEG_W, LDR_SEG_H, c);
}
static void ldr_say(const char *what)               /* the line under the segments */
{
    lcd_fill(0, LDR_TXT_Y, 240, 240 - LDR_TXT_Y, LDR_BG);
    ldr_text(LDR_TXT_Y + 2u, 1, LDR_INK, what);
}

static uint16_t ldr_rows[2][240];                   /* two rows: one is drawn while the other goes out */
static void ldr_logo(void)                          /* the logo, rows 0 .. LDR_FOOT - 1 (organ_logo.h: 16 colours, runs) */
{
    uint32_t y, x, i = 0, left = 0, c = 0;
    for (y = 0; y < LDR_FOOT; y++) {
        uint16_t *row = ldr_rows[y & 1u];
        for (x = 0; x < 240u; x++) {
            if (!left) {
                if (i >= sizeof LOGO_RLE) { c = 0; left = 1u << 30; }   /* (never: the runs cover 240 x 240) */
                else { c = LOGO_RLE[i] >> 4; left = (LOGO_RLE[i] & 15u) + 1u; i++; }
            }
            left--;
            row[x] = (uint16_t)(LOGO_PAL[c] >> 8 | LOGO_PAL[c] << 8);   /* (the panel takes the high byte first) */
        }
        lcd_blit(0, y, 240, 1, row);                /* (lcd_blit waits for the row before last: lcd_sync) */
    }
    lcd_fill(0, LDR_FOOT, 240, 240 - LDR_FOOT, LDR_BG);
}

static uint32_t ldr_lit;                            /* segments lit for the write */
static uint32_t ldr_pct = 0xFFFFu;                  /* the percent on the screen */
static uint8_t ldr_writing;                         /* WRITING is on the screen */
static uint8_t ldr_idle = 1;                        /* the waiting light runs (not after a write, nor a failure) */
static uint32_t ldr_pos, ldr_next;                  /* the waiting light: its segment (0..37, to and fro), its next move */
static void ldr_screen(void)
{
    uint32_t i;
    lcd_init();                                     /* (the panel cleared, on) */
    ldr_logo();
    for (i = 0; i < LDR_SEGS; i++)
        ldr_seg(i, LDR_DIM);
    ldr_say("UPDATE MODE   WAITING FOR THE INSTALL");
    lcd_sync();
}
static uint32_t ldr_at(uint32_t pos) { return pos < LDR_SEGS ? pos : 2u * LDR_SEGS - 2u - pos; }   /* to and fro */
static void ldr_anim(uint32_t ms)                   /* while it waits: a light runs to and fro, its afterglow behind */
{
    uint32_t n = 2u * LDR_SEGS - 2u;
    if (!ldr_idle || (int32_t)(ms - ldr_next) < 0)
        return;
    ldr_next = ms + 60u;                            /* (a step every 60 ms: across in 1.2 s) */
    ldr_seg(ldr_at((ldr_pos + n - 1u) % n), LDR_DIM);    /* the old afterglow goes out */
    ldr_seg(ldr_at(ldr_pos), LDR_TAIL);                  /* the light becomes the afterglow */
    ldr_pos = (ldr_pos + 1u) % n;
    ldr_seg(ldr_at(ldr_pos), LDR_COLOUR);
    lcd_sync();
}
static void ldr_bar(uint32_t done, uint32_t total)
{
    uint32_t n = done * LDR_SEGS / total, i;         /* (the app area: below 2^32 / 20) */
    uint32_t p = done * 100u / total;               /* (the app area * 100: below 2^32) */
    if (p > 100u)
        p = 100u;
    if (!ldr_writing || n < ldr_lit) {              /* a write starts (again) */
        ldr_idle = 0;
        for (i = 0; i < LDR_SEGS; i++)
            ldr_seg(i, LDR_DIM);
        ldr_lit = 0;
        ldr_writing = 1;
        ldr_pct = 0xFFFFu;
    }
    for (; ldr_lit < n && ldr_lit < LDR_SEGS; ldr_lit++)
        ldr_seg(ldr_lit, LDR_COLOUR);
    if (p != ldr_pct) {                             /* (the line only when the number changes: few fills) */
        char t[32] = "WRITING ";
        uint32_t k = 8;
        ldr_pct = p;
        if (p >= 100u) t[k++] = '1';
        if (p >= 10u) t[k++] = (char)('0' + p / 10u % 10u);
        t[k++] = (char)('0' + p % 10u);
        t[k++] = '%';
        t[k++] = ' '; t[k++] = ' '; t[k++] = ' ';
        { const char *d = "DO NOT SWITCH OFF"; while (*d) t[k++] = *d++; }
        t[k] = 0;
        ldr_say(t);
    }
    lcd_sync();
}
static void ldr_failed(void)                        /* a session that wrote nothing or stopped: the way out */
{
    uint32_t i;
    for (i = 0; i < LDR_SEGS; i++)
        ldr_seg(i, LDR_DIM);
    ldr_say("UPDATE FAILED   INSTALL AGAIN");
    lcd_sync();
    ldr_pct = 0xFFFFu;
    ldr_writing = 0;
    ldr_idle = 0;
}
