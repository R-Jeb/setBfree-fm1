/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MIDI IN on the TRS jack: PH8 -> input channel 1 -> UART1 RX, 31250 baud,
 * RX DMA into a 128-byte ring, polled from the TIMER5 ISR (no UART IRQ).
 * FELUCCA_UART=0 disables this input. Channel
 * messages go into midi_in_q next to USB, as
 * USB-MIDI packets (cable 0, CIN = status >> 4), so seq.c routes them by channel
 * exactly as USB (CH1-4 or SEL;
 * host tests: midi_uart_test, hostsim TRACKS trs_test);
 * Clock/Start/Continue/Stop retain timestamps and source; other realtime,
 * system common and SysEx are ignored (no SysEx->UBOOT from DIN). */
#include "../hal/fm1_uart.h"   /* registers; relative, so the host tests find it too */

#define UM_RING 128u
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));   /* empty cells hold 0xFF: see uart_midi_take */
static struct {
    uint32_t rd, pend;
    volatile uint32_t bytes, drops, msgs, resync;  /* TIMER5 writes; read-only CDC diagnostics; resync: the DMA lapped the reader */
    uint8_t st, need, got, d0, sysex;
} um;

static void uart_midi_init(void)                   /* before timer5_start(): PORTH RMW */
{
    uint32_t i;
    for (i = 0; i < UM_RING; i++)
        um_ring[i] = 0xFFu;
    um.rd = 0;
    fm1_uart1_midi_init(um_ring, UM_RING);
}

static uint32_t um_len(uint32_t s)                 /* data bytes of a channel status */
{
    return (s & 0xE0u) == 0xC0u ? 1u : 2u;         /* Cx Dx: 1, else 2 */
}

static void um_byte(uint32_t b)
{
    if (b >= 0xF8u) {
        if (b == 0xF8u || b == 0xFAu || b == 0xFBu || b == 0xFCu) {
            if (midi_enqueue(0xFu | b << 8, 2u))
                um.msgs++;
            else
                um.drops++;
        }
        return;                                    /* realtime keeps running status */
    }
    if (b & 0x80u) {
        um.sysex = b == 0xF0u;
        um.st = b < 0xF0u ? (uint8_t)b : 0;        /* system common/SysEx cancel running status */
        um.need = (uint8_t)um_len(b);
        um.got = 0;
        return;
    }
    if (um.sysex || !um.st)
        return;
    if (um.need == 2u && !um.got) {
        um.d0 = (uint8_t)b;
        um.got = 1;
        return;
    }
    {
        uint32_t d1 = um.need == 2u ? um.d0 : b, d2 = um.need == 2u ? b : 0u;
        uint32_t pkt = (um.st >> 4) | (uint32_t)um.st << 8 | d1 << 16 | d2 << 24;
        um.got = 0;
        if ((um.st & 0xF0u) == 0xA0u || (um.st & 0xF0u) == 0xD0u || (um.st & 0xF0u) == 0xE0u)
            return;                                    /* aftertouch and pitch bend: the organ has no use for them, and a stream of them fills the queue */
        if (midi_enqueue(pkt, 2u)) {
            um.msgs++;
        } else {
            midi_in_overflow = 1;
            um.drops++;
        }
    }
}

/* The DMA fills the ring from the start and wraps. The hardware's count of bytes since the last look can miss a byte; once it does, a reader that
 * trusts it falls further behind with every miss (notes late, then stuck, then lost, and it stays so after the keyboard is reset). So the ring is
 * read by content: a cell holds 0xFF until the DMA writes a byte there, the reader takes bytes while the cell is not 0xFF and writes 0xFF back.
 * A real 0xFF byte (system reset, never sent by a keyboard in practice) is told from an empty cell by the next cell: empty too = nothing yet. */
static void uart_midi_take(uint32_t n)
{
    uint32_t guard = UM_RING;
    (void)n;                                       /* the count is only read to clear the pendings */
    while (guard--) {
        uint32_t b = um_ring[um.rd];
        if (b == 0xFFu && um_ring[(um.rd + 1u) & (UM_RING - 1u)] == 0xFFu)
            break;                                 /* nothing new */
        um_ring[um.rd] = 0xFFu;
        um.rd = (um.rd + 1u) & (UM_RING - 1u);
        um.bytes++;
        if (b != 0xFFu)
            um_byte(b);
    }
    if (guard == (uint32_t)-1) {                   /* a full lap without an empty cell: the DMA outran the reader; start clean */
        um.resync++;
        um.st = um.got = um.sysex = 0;
        midi_in_overflow = 1;
    }
}
static void uart_midi_poll(void)                   /* TIMER5 ISR, same context as usb_poll */
{
    uart_midi_take(fm1_uart1_rx_take());
}
