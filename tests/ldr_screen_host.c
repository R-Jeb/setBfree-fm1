/* SPDX-License-Identifier: GPL-3.0-only */
/* The update loader's screen on the PC: firmware/loader/ldr_screen.c drawn into a frame buffer, three states as PPM
 * (waiting, writing 57 %, failed), and a check that every letter it uses is in its font.
 *   cc -Ifirmware/src -o build/ldr_screen tests/ldr_screen_host.c && build/ldr_screen build/ldr */
#include <stdint.h>
#include <stdio.h>
static uint16_t fb[240 * 240];
static void lcd_init(void) {}
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    for (uint32_t j = y; j < y + h && j < 240u; j++)
        for (uint32_t i = x; i < x + w && i < 240u; i++) fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)   /* the panel's byte order */
{
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++) { uint16_t v = px[j * w + i]; fb[(y + j) * 240u + x + i] = (uint16_t)(v >> 8 | v << 8); }
}
#include "../firmware/loader/ldr_screen.c"
static void shot(const char *base, const char *what)
{
    char n[256];
    snprintf(n, sizeof n, "%s-%s.ppm", base, what);
    FILE *f = fopen(n, "wb");
    fprintf(f, "P6 240 240 255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f); fputc((c >> 5 & 63) * 255 / 63, f); fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
    printf("%s\n", n);
}
int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : "ldr";
    uint32_t ms;
    ldr_screen();
    for (ms = 0; ms < 700u; ms += 60u) ldr_anim(ms);
    shot(base, "wait");
    ldr_bar(57, 100);
    shot(base, "write");
    ldr_bar(100, 100);
    shot(base, "done");
    ldr_failed();
    shot(base, "failed");
    if (ldr_missing) { printf("letters missing from the font: %u\n", ldr_missing); return 1; }
    printf("ldr_screen: ok\n");
    return 0;
}
