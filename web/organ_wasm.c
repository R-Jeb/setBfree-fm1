/* SPDX-License-Identifier: GPL-3.0-only */
/* The organ engine of the FM-1 firmware (firmware/src/organ_core.h, unchanged) as a WebAssembly module for the
 * browser emulator. No libc: freestanding. Build: web/build.sh */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
#define FM1_TICKS_PER_US 24u
static uint32_t tick_fake;
static uint32_t fm1_ticks(void) { return tick_fake += 7919u; }     /* only used to stir the random program */
#include "organ_tab.h"
#include "organ_core.h"

#define EXPORT __attribute__((visibility("default")))
static int32_t buf[2 * 256];

EXPORT int32_t *organ_buffer(void) { return buf; }
EXPORT uint32_t organ_render(void)              /* one block of 256 stereo frames into organ_buffer(); returns frames */
{
    uint32_t i;
    for (i = 0; i < 512u; i++) buf[i] = 0;
    app_audio(buf, 256u);
    return 256u;
}
EXPORT void organ_init(uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < NW; i++) { seed = seed * 1664525u + 1013904223u; wph[i] = seed; }
    organ_program(5u);
}
EXPORT void organ_key(uint32_t key, uint32_t on) { key_set(&mk_lo, &mk_hi, key, (int)on); }   /* key 0 = C2 */
EXPORT void organ_all_off(void) { mk_lo = mk_hi = 0; }
EXPORT uint32_t organ_wheels(void) { return wheels_on; }

/* the same controls as MIDI: the page uses these for its own buttons, so its state is the engine's state */
EXPORT void organ_midi_cc(uint32_t cc, uint32_t v) { organ_cc(cc, v); }
EXPORT void organ_midi_pc(uint32_t uc) { organ_pc(uc); }
EXPORT void organ_select_program(uint32_t n) { organ_program(n); }
EXPORT uint32_t organ_program_step(uint32_t cur, int32_t dir) { return pgm_step(cur, dir); }
EXPORT const char *organ_program_name(uint32_t n) { return n < 128u ? PGM[n].name : 0; }
EXPORT uint32_t organ_program_has_effect(uint32_t n) { return n < 128u && PGM[n].name && PGM[n].set ? 1u : 0u; }
EXPORT uint32_t organ_program_is_registration(uint32_t n) { return (uint32_t)pgm_registration(n); }

/* state by id: 0..8 drawbars, 9 vibrato knob, 10 vibrato on, 11 perc, 12 perc 3rd, 13 perc fast, 14 perc soft,
 * 15 leslie (0 stop, 1 slow, 2 fast), 16 overdrive on, 17 overdrive amount, 18 swell, 19 program, 20 reverb (0..127) */
EXPORT uint32_t organ_get(uint32_t id)
{
    switch (id) {
    case 9: return P.vtype; case 10: return P.vup; case 11: return P.perc; case 12: return P.perc3;
    case 13: return P.fast; case 14: return P.soft; case 15: return P.les; case 16: return P.od;
    case 17: return P.drv; case 18: return P.swell; case 19: return P.pgm; case 20: return P.rev;
    default: return id < NB ? P.db[id] : 0u;
    }
}
EXPORT void organ_set(uint32_t id, uint32_t v)
{
    switch (id) {
    case 9: if (v < 6u) P.vtype = (uint8_t)v; break;
    case 10: P.vup = v & 1u; break;
    case 11: P.perc = v & 1u; break;
    case 12: P.perc3 = v & 1u; break;
    case 13: P.fast = v & 1u; break;
    case 14: P.soft = v & 1u; break;
    case 15: if (v < 3u) P.les = (uint8_t)v; break;
    case 16: P.od = v & 1u; break;
    case 17: if (v < 9u) P.drv = (uint8_t)v; break;
    case 20: if (v < 128u) P.rev = (uint8_t)v; break;
    case 18: if (v < 128u) P.swell = (uint8_t)v; break;
    default: if (id < NB && v < 9u) P.db[id] = (uint8_t)v; break;
    }
}
