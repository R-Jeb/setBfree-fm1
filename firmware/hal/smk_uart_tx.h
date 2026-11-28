/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R-Jeb (B3 ORGAN); the setup from genkigenki's fm1-to-smk37 guide (01-hardware.md 9, 06-mapping-patterns.md,
 * 08-pitfalls.md: the stock v016 init) */
/* The SMK-37's TRS jack: MIDI OUT, UART1 TX on PC7, 31250 baud (SMK-37 only: PC7 is the FM-1's LCD chip select, and the FM-1's
 * jack is MIDI IN on PH8, src/midi_uart.c, which stays the FM-1's alone).
 *
 *   smk_trs_init()           before TIMER5 starts
 *   smk_trs_put(st, d1, d2)  a channel message into the ring (main loop only); a full ring drops the whole message
 *   smk_trs_tick()           from the 10 kHz timer tick, also nested in the audio render: one byte when the last has gone
 *                            (~2500 bytes/s of the line's 3125), never a wait. A byte not gone in 30 ticks (3 ms) is given
 *                            up; after 4 in a row the jack stays quiet (no UART: nothing hangs).
 * Not yet confirmed on a unit (the guide's status: analysis). The jack is said to be type A; if a synth does not answer, try
 * a type B adapter. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#define SMK_UT1_CON0  (*(volatile uint32_t *)0x12100u)
#define SMK_UT1_CON1  (*(volatile uint32_t *)0x12104u)
#define SMK_UT1_BAUD  (*(volatile uint32_t *)0x12108u)
#define SMK_UT1_BUF   (*(volatile uint32_t *)0x1210Cu)
#define SMK_UT_CLK_CON1 (*(volatile uint32_t *)0x10010u)   /* [11:10] UART clock: 1 = PLL48M */
#define SMK_UT_IOMAP3 (*(volatile uint32_t *)0x51028u)     /* b7: UT1 on its fixed pins; [23:20]: UT1 TX onto an output channel */
#define SMK_PC_OUT    (*(volatile uint32_t *)0x50080u)
#define SMK_PC_DIR    (*(volatile uint32_t *)0x50088u)
#define SMK_PC_DIE    (*(volatile uint32_t *)0x5008Cu)
#define SMK_PC_PU     (*(volatile uint32_t *)0x50090u)
#define SMK_PC_PD     (*(volatile uint32_t *)0x50094u)
#define SMK_TRS_PIN   (1u << 7)                            /* PC7 */
#define SMK_TRS_TPND  (1u << 15)
#define SMK_TRS_CLRT  (1u << 13)

#define SMK_TRS_RING 256u
static volatile uint8_t smk_trs_ring[SMK_TRS_RING];
static volatile uint32_t smk_trs_w, smk_trs_r;
static volatile uint8_t smk_trs_busy, smk_trs_wait, smk_trs_fails, smk_trs_dead;

static void smk_trs_init(void)
{
    SMK_UT1_CON0 = 0x3400u;                                /* off, pendings cleared */
    SMK_UT1_CON1 = 0;
    SMK_UT_CLK_CON1 = (SMK_UT_CLK_CON1 & ~(3u << 10)) | (1u << 10);
    SMK_UT_IOMAP3 = (SMK_UT_IOMAP3 & ~0x80u & ~(0xFu << 20)) | (1u << 20);   /* UT1 TX onto the output channel (stock) */
    SMK_PC_OUT &= ~SMK_TRS_PIN;                            /* output low, pull-up and pull-down both set: the output channel's pin */
    SMK_PC_DIR &= ~SMK_TRS_PIN;
    SMK_PC_PU |= SMK_TRS_PIN;
    SMK_PC_PD |= SMK_TRS_PIN;
    SMK_PC_DIE |= SMK_TRS_PIN;
    SMK_UT1_BAUD = 48000000u / 31250u / 4u - 1u;           /* 383 */
    __asm__ volatile("csync" ::: "memory");
    SMK_UT1_CON0 = 0x3400u | 1u;                           /* UTEN; no RX, no IEs */
    __asm__ volatile("csync" ::: "memory");
}

static void smk_trs_put(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t n = (st & 0xE0u) == 0xC0u ? 2u : 3u, w = smk_trs_w;
    if (smk_trs_dead || st < 0x80u || st >= 0xF0u || SMK_TRS_RING - (w - smk_trs_r) < n) return;
    smk_trs_ring[w % SMK_TRS_RING] = (uint8_t)st;
    smk_trs_ring[(w + 1u) % SMK_TRS_RING] = (uint8_t)(d1 & 0x7Fu);
    if (n == 3u) smk_trs_ring[(w + 2u) % SMK_TRS_RING] = (uint8_t)(d2 & 0x7Fu);
    __asm__ volatile("" ::: "memory");
    smk_trs_w = w + n;                                     /* (the whole message at once: the tick never sends half) */
}

static void smk_trs_tick(void)
{
    if (smk_trs_dead) return;
    if (smk_trs_busy) {                                    /* the last byte: gone, or given up */
        if (SMK_UT1_CON0 & SMK_TRS_TPND) { smk_trs_busy = 0; smk_trs_fails = 0; }
        else if (++smk_trs_wait >= 30u) {
            smk_trs_busy = 0;
            if (++smk_trs_fails >= 4u) smk_trs_dead = 1;
        } else return;
    }
    if (smk_trs_r == smk_trs_w) return;
    SMK_UT1_CON0 |= SMK_TRS_CLRT;                          /* clear TPND, then the byte */
    SMK_UT1_BUF = smk_trs_ring[smk_trs_r % SMK_TRS_RING];
    smk_trs_r++;
    smk_trs_busy = 1;
    smk_trs_wait = 0;
}
