/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 on a computer: the app (firmware/src/dinghy.c) runs unchanged on it. platform.c includes
 * this file instead of the hardware when DINGHY_HOST is set. A script presses keys over time; out come
 * the screen (PPM, at the times SHOT asks) and the sound (WAV, as the FM-1 sends it over USB).
 *
 *   mkdir -p build/host
 *   cc -O2 -Wall -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/dinghy firmware/src/dinghy.c
 *   build/host/dinghy build/host/dinghy        (-> build/host/dinghy.wav, build/host/dinghy-<ms>.ppm)
 *   build/host/dinghy OUT script.txt [FPS]    (your own script; with FPS: OUT-NNNNN.ppm, a frame every 1/FPS s:
 *                                             a film of your firmware, e.g. ffmpeg -r 30 -i OUT-%05d.ppm ...)
 *
 * Change SCRIPT below to try your own app. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
#if BOARD_SMK37
/* the SMK-37 (hal/smk_input.h's API): 37 keys (fm1_in.keys, bit 0 = C3), the pads as events, 4 faders, 2 wheels, the pedal jack,
 * 8 encoders, 18 buttons with their LEDs, the pads' colours (drawn under the screen in the PPM) */
#define FM1_NENC 8u
static struct {
    uint32_t notes, buttons;
    uint64_t keys;
    uint8_t fader[4];
    uint16_t wheel[2], pedal;
} fm1_in = {.wheel = {535, 474}, .pedal = 985};   /* (as a unit reads them at rest: the mod wheel down, no pedal) */
static uint8_t smk_pad_events;
static int32_t host_padq[64];
static uint32_t host_padq_w, host_padq_r;
static int32_t smk_pad_take(void) { return host_padq_r == host_padq_w ? -1 : host_padq[host_padq_r++ % 64u]; }
static uint32_t padled_rgb[16];                 /* g << 16 | r << 8 | b, as lcd.c */
static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b) { if (pad < 16u) padled_rgb[pad] = (uint32_t)g << 16 | (uint32_t)r << 8 | b; }
static void padled_flush(void) {}
static uint8_t host_led[24];
static void fm1_led_key(uint32_t id, int on) { if (id < 24u) host_led[id] = (uint8_t)(on != 0); }
#else
#define FM1_NENC 7u
static struct { uint32_t notes, buttons; } fm1_in;
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
#endif
static uint32_t host_edges, host_rel;
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t b = host_edges; host_edges = 0; *released = host_rel; host_rel = 0; return b; }
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (the script sets fm1_in.notes) */
static int32_t host_enc[8];                     /* script: "T enc ID DELTA" turns an encoder (ENC_* of platform.c) */
static int32_t fm1_enc_take(uint32_t e) { int32_t d = host_enc[e & 7u]; host_enc[e & 7u] = 0; return d; }

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#ifndef CV_MAX
#define CV_MAX (240u * 64u)                   /* as the firmware (platform.c): what the canvas holds; more hid a cut band */
#endif
#include "gfx.c"

static uint8_t host_store[256 + 4];
static int settings_load(void *p, uint32_t n)
{
    if (host_store[0] != 1 || n > 256u || host_store[1] != n) return -1;   /* as the flash: only the length it was saved with */
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    host_store[0] = 1; host_store[1] = (uint8_t)n;
    memcpy(host_store + 4, p, n);
    printf("%6u ms  settings saved (%u B)\n", fm1_ms, n);
    return 0;
}

static uint8_t host_data[1024 + 4];
static int data_load(void *p, uint32_t n)
{
    if (host_data[0] != 1 || n > 1024u || (host_data[1] | host_data[2] << 8) != n) return -1;
    memcpy(p, host_data + 4, n);
    return 0;
}
static int data_save(const void *p, uint32_t n)
{
    host_data[0] = 1; host_data[1] = (uint8_t)n; host_data[2] = (uint8_t)(n >> 8);
    memcpy(host_data + 4, p, n);
    printf("%6u ms  data saved (%u B)\n", fm1_ms, n);
    return 0;
}

/* SysEx: the script's "T sysex HEX" (the bytes between F0 and F7, e.g. 7D4233 01) waits here; what the app sends is printed */
static uint8_t host_sx[256];
static uint32_t host_sx_n, host_sx_ready;
static int sysex_get(const uint8_t **p, uint32_t *n) { if (!host_sx_ready) return 0; *p = host_sx; *n = host_sx_n; return 1; }
static void sysex_done(void) { host_sx_ready = 0; }
static int sysex_send(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    printf("%6u ms  SysEx out", fm1_ms);
    for (i = 0; i < n; i++) printf(" %02X", p[i]);
    printf("\n");
    return 0;
}

static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static volatile uint8_t midi_in_overflow;       /* the real one is in usb.c */
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { printf("%6u ms  MIDI out %02X %u %u\n", fm1_ms, st, d1, d2); }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

/* the script: at t ms, keys down (bit k = key k, 0 = F3), a button edge, a MIDI message. A script file (the second
 * argument) replaces it, a line each: "T keys MASK" (hex or decimal), "T btn ID", "T midi ST D1 D2", "T enc ID DELTA", "T shot" (a screen as PNG-to-be), "T end".
 * SMK-37 builds also: "T keys MASK" over 37 keys (bit 0 = C3), "T down ID" / "T up ID" (a button held / let go), "T pad P V" (hardware pad P
 * hit at velocity V, V 0 = let go), "T fader F V" (0..127), "T wheel W RAW" (0 pitch, 1 mod: raw 0..1023, up = lower), "T pedal RAW" */
enum { HS_NONE, HS_DOWN, HS_UP, HS_PAD, HS_FADER, HS_WHEEL, HS_PEDAL };
typedef struct { uint32_t t; uint64_t keys; int btn; uint8_t st, d1, d2; int enc, delta, shot; uint8_t sx[160], sxn; int smk, sa, sb; } host_ev_t;
static const host_ev_t SCRIPT0[] = {
    {200, 1u << 7, -1, 0, 0, 0, -1, 0, 0},                                /* C4 */
    {700, 1u << 7 | 1u << 11 | 1u << 14, -1, 0, 0, 0},          /* + E4 G4: a chord */
    {1500, 0, -1, 0, 0, 0},
    {1700, 0, -1, 0x90, 81, 120},                               /* MIDI in: A5 */
    {2300, 0, -1, 0x80, 81, 0},
    {2500, 0, BTN_OCT_UP, 0, 0, 0},
    {2700, 1u << 2 | 1u << 9, -1, 0, 0, 0},                     /* G4 + D5, an octave up */
    {3400, 0, -1, 0, 0, 0},
};
static const host_ev_t *script = SCRIPT0;
static uint32_t script_n = sizeof SCRIPT0 / sizeof SCRIPT0[0], end_ms = 6500, frame_fps;
static const uint32_t SHOT[] = {1000, 3000};

static void read_script(const char *path)
{
    static host_ev_t ev[4096];
    char line[256], what[16];
    FILE *f = fopen(path, "r");
    uint32_t n = 0;
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f) && n < 4096u) {
        host_ev_t e = {0, 0, -1, 0, 0, 0, -1, 0, 0};
        unsigned a = 0, b = 0, c = 0;
        if (line[0] == '#' || sscanf(line, "%u %15s", &e.t, what) != 2) continue;
        if (!strcmp(what, "end")) { end_ms = e.t; continue; }
        if (!strcmp(what, "keys")) { unsigned long long k = 0; sscanf(line, "%*u %*s %lli", (long long *)&k); e.keys = k; }
        else if (!strcmp(what, "down")) { e.smk = HS_DOWN; sscanf(line, "%*u %*s %d", &e.sa); }
        else if (!strcmp(what, "up")) { e.smk = HS_UP; sscanf(line, "%*u %*s %d", &e.sa); }
        else if (!strcmp(what, "pad")) { e.smk = HS_PAD; sscanf(line, "%*u %*s %d %d", &e.sa, &e.sb); }
        else if (!strcmp(what, "fader")) { e.smk = HS_FADER; sscanf(line, "%*u %*s %d %d", &e.sa, &e.sb); }
        else if (!strcmp(what, "wheel")) { e.smk = HS_WHEEL; sscanf(line, "%*u %*s %d %d", &e.sa, &e.sb); }
        else if (!strcmp(what, "pedal")) { e.smk = HS_PEDAL; sscanf(line, "%*u %*s %d", &e.sb); }
        else if (!strcmp(what, "btn")) { sscanf(line, "%*u %*s %d", &e.btn); }
        else if (!strcmp(what, "enc")) { sscanf(line, "%*u %*s %d %d", &e.enc, &e.delta); }
        else if (!strcmp(what, "shot")) { e.shot = 1; }
        else if (!strcmp(what, "sysex")) {      /* hex pairs, spaces allowed */
            const char *q = strstr(line, "sysex") + 5;
            unsigned v;
            while (*q && e.sxn < 160u) {
                while (*q == ' ' || *q == '\t') q++;
                if (sscanf(q, "%2x", &v) != 1) break;
                e.sx[e.sxn++] = (uint8_t)v;
                q += 2;
            }
            e.shot = 0;
        }
        else if (!strcmp(what, "midi")) { sscanf(line, "%*u %*s %i %u %u", (int *)&a, &b, &c); e.st = (uint8_t)a; e.d1 = (uint8_t)b; e.d2 = (uint8_t)c; }
        else continue;
        ev[n++] = e;
    }
    fclose(f);
    script = ev;
    script_n = n;
}

static const char *host_out = "build/host/dinghy";
static int16_t *wav;
static uint32_t wav_n;

static void write_ppm_as(const char *name)
{
    FILE *f;
    uint32_t i;
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
#if BOARD_SMK37
    fprintf(f, "P6 240 300 255\n");             /* the screen, and under it the 16 pads in their colours, the bank LEDs */
#else
    fprintf(f, "P6 240 240 255\n");
#endif
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
#if BOARD_SMK37
    for (i = 0; i < 240u * 60u; i++) {
        uint32_t x = i % 240u, y = i / 240u, col = x / 30u, row = y < 6u ? 9u : y < 32u ? 0u : y < 58u ? 1u : 9u;
        uint32_t in = x % 30u >= 3u && x % 30u < 27u, r = 24, g = 24, b = 24;
        if (row < 2u && in) {
            uint32_t c = padled_rgb[row * 8u + col];
            r = c >> 8 & 255u; g = c >> 16 & 255u; b = c & 255u;
            if (r + g + b < 12u) r = g = b = 12;
        } else if (y < 5u && (x < 12u || (x >= 20u && x < 32u))) {   /* KNOB BANK, FADER BANK LEDs */
            r = g = b = host_led[x < 12u ? 0 : 1] ? 230 : 50;
        }
        fputc((int)r, f); fputc((int)g, f); fputc((int)b, f);
    }
#endif
    fclose(f);
}
static void write_ppm(uint32_t t)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%u.ppm", host_out, t);
    write_ppm_as(name);
    printf("%6u ms  screen -> %s\n", t, name);
}

static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
}

#ifndef HOST_BATT
#define HOST_BATT 4u                           /* the fake battery: bars 0 .. 4, + 8 for a USB host (cc -DHOST_BATT=11) */
#endif
static uint32_t battery_state(void) { return HOST_BATT; }
static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t step, shot, owed, frame;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
    while (step < script_n && script[step].t <= fm1_ms) {
        if (script[step].smk) {
#if BOARD_SMK37
            const host_ev_t *e = &script[step];
            switch (e->smk) {
            case HS_DOWN: fm1_in.buttons |= 1u << e->sa; host_edges |= 1u << e->sa; break;
            case HS_UP: fm1_in.buttons &= ~(1u << e->sa); host_rel |= 1u << e->sa; break;
            case HS_PAD: host_padq[host_padq_w++ % 64u] = (e->sa & 15) << 8 | (e->sb & 0x7F); break;
            case HS_FADER: fm1_in.fader[e->sa & 3] = (uint8_t)(e->sb > 127 ? 127 : e->sb); break;
            case HS_WHEEL: fm1_in.wheel[e->sa & 1] = (uint16_t)e->sb; break;
            case HS_PEDAL: fm1_in.pedal = (uint16_t)e->sb; break;
            }
#else
            fprintf(stderr, "script: an SMK-37 line in an FM-1 build, ignored\n");
#endif
        } else if (script[step].sxn) {
            memcpy(host_sx, script[step].sx, script[step].sxn); host_sx_n = script[step].sxn; host_sx_ready = 1;
        } else if (script[step].st) {
            mq[mq_w % 64u][0] = script[step].st; mq[mq_w % 64u][1] = script[step].d1; mq[mq_w % 64u][2] = script[step].d2;
            mq_w++;
        } else if (script[step].btn >= 0) {     /* a press and its release */
            host_edges |= 1u << script[step].btn;
            host_rel |= 1u << script[step].btn;
        }
        else if (script[step].enc > 0 || script[step].delta)
            host_enc[(uint32_t)script[step].enc & 7u] += script[step].delta;
        else if (script[step].shot)
            write_ppm(fm1_ms);
        else {
#if BOARD_SMK37
            fm1_in.keys = script[step].keys;
            fm1_in.notes = (uint32_t)(script[step].keys >> 5) & 0x7FFFFFFu;   /* (the middle 27: F3 .. G5) */
#else
            fm1_in.notes = (uint32_t)script[step].keys;
#endif
        }
        step++;
    }
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        wav = realloc(wav, (wav_n + HALF_FRAMES) * 4u);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];                 /* (USB audio at the MASTER knob all the way up) */
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += HALF_FRAMES;
    }
    if (frame_fps) {                            /* a film: a frame every 1/fps s, as the screen is then */
        if ((uint64_t)frame * 1000u <= (uint64_t)fm1_ms * frame_fps) {
            char name[512];
            snprintf(name, sizeof name, "%s-%05u.ppm", host_out, frame++);
            write_ppm_as(name);
        }
    } else if (shot < sizeof SHOT / sizeof SHOT[0] && fm1_ms == SHOT[shot])
        write_ppm(SHOT[shot++]);
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) host_out = argv[1];
    if (argc > 2) read_script(argv[2]);
    if (argc > 3) frame_fps = (uint32_t)atoi(argv[3]);
    app_main();
    return 0;
}
