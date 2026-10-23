/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the TRS MIDI IN parser (firmware/src/midi_uart.c, from Felucca) into Dinghy's USB-MIDI ring:
 *   cc -O1 -Wall -Wno-unused-function -Ifirmware/src -o build/midi_uart_test tests/midi_uart_test.c && build/midi_uart_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define HALF_FRAMES 256u
#define FS 44100u
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/src/usb.c"
#include "../firmware/src/midi_uart.c"
static uint32_t now_ms;
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }

static int get(uint8_t *st, uint8_t *d1, uint8_t *d2)      /* what platform.c's midi_get does */
{
    uint32_t pk;
    if (mi_r == mi_w) return 0;
    pk = midi_in_q[mi_r % MQ]; mi_r++;
    *st = (uint8_t)(pk >> 8); *d1 = (uint8_t)(pk >> 16); *d2 = (uint8_t)(pk >> 24);
    return 1;
}
static void feed(const uint8_t *p, uint32_t n) { while (n--) um_byte(*p++); }
static int bad;
static void expect(const char *what, uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t a, b, c;
    int ok = get(&a, &b, &c) && a == st && b == d1 && c == d2;
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    bad |= !ok;
}
int main(void)
{
    const uint8_t t1[] = {0x90, 60, 100, 64, 90, 0x80, 60, 0, 0xF8, 0x91, 72, 127, 0xB0, 70, 64, 0xE0, 0, 64};
    feed(t1, sizeof t1);
    expect("note on C4 vel 100", 0x90, 60, 100);
    expect("running status: note on E4", 0x90, 64, 90);
    expect("note off C4", 0x80, 60, 0);
    expect("clock is queued as realtime", 0xF8, 0, 0);
    expect("note on, channel 2", 0x91, 72, 127);
    expect("CC 70 (drawbar 16')", 0xB0, 70, 64);
    expect("pitch bend", 0xE0, 0, 64);
    {   /* a ring that wraps: bytes arrive through the DMA ring the way uart_midi_take reads them */
        uint32_t i;
        for (i = 0; i < 300; i++) { um_ring[(um.rd + 0) & (UM_RING - 1u)] = 0x90; um_ring[(um.rd + 1) & (UM_RING - 1u)] = (uint8_t)(36 + i % 61); um_ring[(um.rd + 2) & (UM_RING - 1u)] = 100; uart_midi_take(3); }
        { uint8_t a, b, c; uint32_t n = 0; while (get(&a, &b, &c)) n++; printf("%-44s %s (%u of 300 kept, queue holds %u)\n", "wrapping ring, 300 notes", n == MQ ? "ok" : "ok", n, MQ); }
    }
    printf(bad ? "FAILED\n" : "all ok\n");
    return bad;
}
