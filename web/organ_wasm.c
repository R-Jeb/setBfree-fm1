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
static uint32_t fm1_ticks(void) { return 0; }
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
EXPORT void organ_key(uint32_t key, uint32_t on) { key_set(&mk_lo, &mk_hi, key, (int)on); }   /* key 0 = C2 */
EXPORT void organ_drawbar(uint32_t i, uint32_t v) { if (i < NB && v <= 8u) P.db[i] = (uint8_t)v; }
EXPORT void organ_preset(uint32_t p) { if (p < 8u) load_preset(p); }
EXPORT uint32_t organ_get_drawbar(uint32_t i) { return i < NB ? P.db[i] : 0u; }
EXPORT void organ_vib(uint32_t v) { if (v < 7u) P.vib = (uint8_t)v; }
EXPORT void organ_leslie(uint32_t v) { if (v < 3u) P.les = (uint8_t)v; }
EXPORT void organ_drive(uint32_t v) { if (v < 9u) P.drv = (uint8_t)v; }
EXPORT void organ_perc(uint32_t on, uint32_t third, uint32_t fast, uint32_t soft)
{ P.perc = (uint8_t)(on & 1u); P.perc3 = (uint8_t)(third & 1u); P.fast = (uint8_t)(fast & 1u); P.soft = (uint8_t)(soft & 1u); }
EXPORT void organ_swell(uint32_t v) { if (v < 128u) P.swell = (uint8_t)v; }
EXPORT uint32_t organ_wheels(void) { return wheels_on; }
EXPORT void organ_all_off(void) { mk_lo = mk_hi = 0; }
EXPORT const char *organ_preset_name(uint32_t p) { return PRESET[p & 7u].name; }
EXPORT void organ_init(uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < NW; i++) { seed = seed * 1664525u + 1013904223u; wph[i] = seed; }
    load_preset(1);
}
