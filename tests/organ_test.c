/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the organ's programs and MIDI controllers (organ_core.h), against what setBfree does:
 *   cc -O1 -Wall -Wno-unused-function -Ifirmware/src -o build/organ_test tests/organ_test.c && build/organ_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return 0; }
#include "organ_tab.h"
#include "organ_core.h"
#include <math.h>
#include "organ_ref.h"

static int bad;
#define CHECK(what, cond) do { int ok_ = !!(cond); if (!ok_) bad = 1; printf("%-58s %s\n", what, ok_ ? "ok" : "FAIL"); } while (0)
static int dbs(const char *want)
{
    uint32_t i;
    for (i = 0; i < NB; i++) if (P.db[i] != (uint8_t)(want[i] - '0')) return 0;
    return 1;
}

/* the level of each key and drawbar against setBfree's own tone generator (tapering and leakage included) */
static double render_db(int key, int drawbar)
{
    static int32_t out[512];
    uint32_t b, i, j;
    double e = 0;
    for (i = 0; i < NB; i++) P.db[i] = (uint8_t)(i == (uint32_t)drawbar ? 8 : 0);
    P.perc = P.vup = P.od = 0; P.les = 0; P.swell = 127;
    pk_lo = pk_hi = mk_lo = mk_hi = 0;
    key_set(&mk_lo, &mk_hi, (uint32_t)key, 1);
    for (b = 0; b < 400; b++) {
        memset(out, 0, sizeof out);
        app_audio(out, 256);
        if (b >= 300) for (j = 0; j < 256; j++) e += (double)out[2 * j] * out[2 * j];
    }
    key_set(&mk_lo, &mk_hi, (uint32_t)key, 0);
    for (b = 0; b < 400; b++) { memset(out, 0, sizeof out); app_audio(out, 256); }   /* let it fade */
    return 10.0 * log10(e / (100.0 * 256.0) / 1073741824.0 + 1e-20);
}
static void check_levels(void)
{
    uint32_t r, d, n = 0, within = 0;
    double off = render_db(24, 2) - REF[3].db[2];       /* the engine's own gain: C4, 8' */
    double worst = 0;
    for (r = 0; r < sizeof REF / sizeof REF[0]; r++)
        for (d = 0; d < 9u; d++) {
            double diff = render_db(REF[r].key, (int)d) - off - REF[r].db[d];
            if (fabs(diff) > worst) worst = fabs(diff);
            n++;
            if (fabs(diff) < 1.5) within++;
        }
    printf("levels vs setBfree: %u of %u within 1.5 dB, worst %.1f dB\n", within, n, worst);
    CHECK("levels follow setBfree's tapering and leakage", within == n);
}
static void wheel_count(void)                           /* how many wheels sound: the render time is about linear in it */
{
    static int32_t out[512];
    static const int chord[] = {0, 7, 12, 16, 19, 24, 28, 31, 36, 40};
    uint32_t i, b, c;
    for (i = 0; i < NB; i++) P.db[i] = 8;
    P.perc = P.vup = P.od = 0; P.les = 0;
    for (c = 1; c <= 10; c += 3 + (c > 7)) {
        pk_lo = pk_hi = mk_lo = mk_hi = 0;
        for (i = 0; i < c; i++) key_set(&mk_lo, &mk_hi, (uint32_t)chord[i], 1);
        for (b = 0; b < 4; b++) { memset(out, 0, sizeof out); app_audio(out, 256); }
        printf("%2u keys, all drawbars at 8: %u wheels sound\n", c, (unsigned)wheels_on);
    }
}
int main(void)
{
    uint32_t n, cnt = 0, reg = 0, i, chg;

    organ_pc(0);                                                    /* program change 0 = program 1 (offset 1) */
    CHECK("PC 0 selects 'Jazz 1 all'", P.pgm == 1 && !strcmp(PGM[P.pgm].name, "Jazz 1 all"));
    CHECK("  drawbars 888000000 (the 10th digit is ignored)", dbs("888000000"));
    CHECK("  perc on, soft, fast, 3rd", P.perc && P.soft && P.fast && P.perc3);
    CHECK("  vibrato C3 on, overdrive on, rotary chorale = slow", P.vtype == 5 && P.vup && P.od && P.les == 1);

    organ_program(5);
    CHECK("program 5 'Standard B' 888000000, leaves the rest alone", dbs("888000000") && P.perc && P.vtype == 5);
    organ_program(2);
    CHECK("program 2 'Fredriks tfn 1' 758757200", dbs("758757200"));
    organ_program(11);
    CHECK("program 11 sets 881 1 0000, vibrato C3, perc 3rd", dbs("888110000") && P.vtype == 5 && P.perc && P.perc3);
    organ_program(15);
    CHECK("program 15 'Booker T' perc on, 2nd harmonic", P.perc && !P.perc3);
    organ_program(108);
    CHECK("program 108 'Paul Shaffer' rotary tremolo = fast", P.les == 2 && dbs("888788678"));
    organ_program(109);
    CHECK("program 109 is 'Reggae' (the later one replaces)", !strcmp(PGM[109].name, "Reggae") && dbs("808000008"));
    organ_program(22);
    CHECK("program 22 'Silence' all zero", dbs("000000000"));
    organ_program(55);
    CHECK("program 55 'Random' fills drawbars within 0..8", ({ int ok = 1; for (i = 0; i < NB; i++) ok &= P.db[i] <= 8; ok; }));
    chg = 0; for (i = 0; i < 5; i++) { uint8_t before = P.db[0] * 100 + P.db[4]; organ_program(55); chg |= before != P.db[0] * 100 + P.db[4]; }
    CHECK("  and differs from call to call", chg);
    P.pgm = 7; organ_program(64);
    CHECK("program 64 (lower manual only) is kept but changes nothing", P.pgm == 64 && PGM[64].set == 0);
    organ_program(120);
    CHECK("program 120 does not exist: nothing happens", P.pgm == 64);

    for (n = 1; n < 128; n++) { cnt += PGM[n].name != 0; reg += pgm_registration(n); }
    CHECK("110 programs, 57 registrations", cnt == 110 && reg == 57);
    CHECK("PRESETS steps 1 -> 2 -> ... -> 26 -> 55 -> 56 -> 82", pgm_step(1, 1) == 2 && pgm_step(26, 1) == 55 && pgm_step(56, 1) == 82);
    CHECK("  and back, and stops at the ends", pgm_step(82, -1) == 56 && pgm_step(1, -1) == 1 && pgm_step(110, 1) == 110);

    organ_cc(70, 0);   CHECK("CC 70 value 0 = drawbar 16' fully out (8)", P.db[0] == 8);
    organ_cc(70, 127); CHECK("CC 70 value 127 = pushed in (0)", P.db[0] == 0);
    organ_cc(72, 64);  CHECK("CC 72 value 64 = 4 (as rint((127-64)*8/127))", P.db[2] == 4);
    organ_cc(78, 0);   CHECK("CC 78 = drawbar 1'", P.db[8] == 8);
    organ_cc(7, 100);  CHECK("CC 7 and CC 11 are the swell", P.swell == 100);
    organ_cc(11, 50);  CHECK("  CC 11", P.swell == 50);
    organ_cc(66, 127); CHECK("CC 66 percussion on", P.perc == 1);
    organ_cc(80, 0);   CHECK("CC 80 percussion off", P.perc == 0);
    organ_cc(81, 127); CHECK("CC 81 soft", P.soft == 1);
    organ_cc(82, 0);   CHECK("CC 82 slow", P.fast == 0);
    organ_cc(83, 127); CHECK("CC 83 >= 64: 2nd harmonic", P.perc3 == 0);
    organ_cc(83, 0);   CHECK("CC 83 < 64: 3rd harmonic", P.perc3 == 1);
    organ_cc(92, 0);   CHECK("CC 92 knob 0 = V1", P.vtype == 0);
    organ_cc(92, 23);  CHECK("CC 92 value 23 = C1", P.vtype == 1);
    organ_cc(92, 127); CHECK("CC 92 value 127 = C3 (clamped)", P.vtype == 5);
    organ_cc(31, 127); CHECK("CC 31 vibrato upper on", P.vup == 1);
    organ_cc(95, 0);   CHECK("CC 95 routing 0 = off", P.vup == 0);
    organ_cc(95, 64);  CHECK("CC 95 routing 64 = upper", P.vup == 1);
    organ_cc(1, 0);    CHECK("CC 1 (mod wheel) 0 = slow", P.les == 1);
    organ_cc(1, 60);   CHECK("CC 1 60 = stop", P.les == 0);
    organ_cc(1, 127);  CHECK("CC 1 127 = fast", P.les == 2);
    organ_cc(64, 127); CHECK("CC 64 sustain toggles fast -> slow", P.les == 1);
    organ_cc(64, 127); CHECK("  and slow -> fast", P.les == 2);
    organ_cc(64, 0);   CHECK("  a release (0) does nothing", P.les == 2);
    organ_cc(65, 127); CHECK("CC 65 overdrive on", P.od == 1);
    organ_cc(65, 0);   CHECK("CC 65 overdrive off", P.od == 0);
    organ_cc(93, 127); CHECK("CC 93 character 127 = 8", P.drv == 8);
    organ_cc(93, 0);   CHECK("CC 93 character 0 = 0", P.drv == 0);
    mk_lo = 5; organ_cc(123, 0); CHECK("CC 123 releases MIDI notes", mk_lo == 0);
    printf(bad ? "FAILED\n" : "all ok\n");
    check_levels();
    wheel_count();
    return bad;
}
