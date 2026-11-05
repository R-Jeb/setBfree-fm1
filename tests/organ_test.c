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
#include <stdlib.h>
#include "organ_ref.h"
#include "fx_ref.h"

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

/* key compression and key click, against setBfree (the level relative to one key: 3.64, 6.37, 9.26 dB louder for 4, 7, 10 keys) */
static double chord_db(int n)
{
    static int32_t out[512];
    static const int ch[] = {24, 28, 31, 36, 40, 43, 48, 52, 55, 60};
    uint32_t b, j, i;
    double e = 0;
    for (i = 0; i < NB; i++) P.db[i] = i == 2 ? 8 : 0;
    P.perc = P.vup = P.od = 0; P.les = 0; P.swell = 127;
    pk_lo = pk_hi = mk_lo = mk_hi = 0;
    for (b = 0; b < 60; b++) { memset(out, 0, sizeof out); app_audio(out, 256); }
    for (i = 0; i < (uint32_t)n; i++) key_set(&mk_lo, &mk_hi, (uint32_t)ch[i], 1);
    for (b = 0; b < 150; b++) {
        memset(out, 0, sizeof out);
        app_audio(out, 256);
        if (b >= 100) for (j = 0; j < 256; j++) e += (double)out[2 * j] * out[2 * j];
    }
    for (i = 0; i < (uint32_t)n; i++) key_set(&mk_lo, &mk_hi, (uint32_t)ch[i], 0);
    for (b = 0; b < 60; b++) { memset(out, 0, sizeof out); app_audio(out, 256); }
    return 10.0 * log10(e / 25600.0 / 1073741824.0);
}
static void check_compression(void)
{
    double one = chord_db(1), d4 = chord_db(4) - one, d7 = chord_db(7) - one, d10 = chord_db(10) - one;
    printf("key compression: 4 keys %+.2f, 7 keys %+.2f, 10 keys %+.2f dB (setBfree 3.64, 6.37, 9.26)\n", d4, d7, d10);
    CHECK("key compression follows setBfree", fabs(d4 - 3.64) < 0.4 && fabs(d7 - 6.37) < 0.4 && fabs(d10 - 9.26) < 0.4);
}
static void check_click(void)                           /* the attack has a burst of noise: a note-on is not a clean sine */
{
    static int32_t out[512];
    static const int keys[] = {17, 24, 29, 33, 40};
    uint32_t b, i, j, k;
    double worst = 1e9;
    for (k = 0; k < 5; k++) {
        double on = 0, st = 0;
        for (i = 0; i < NB; i++) P.db[i] = i == 2 ? 8 : 0;
        P.perc = P.vup = P.od = 0; P.les = 0; P.swell = 127;
        pk_lo = pk_hi = mk_lo = mk_hi = 0;
        for (b = 0; b < 60; b++) { memset(out, 0, sizeof out); app_audio(out, 256); }
        key_set(&mk_lo, &mk_hi, (uint32_t)keys[k], 1);
        for (b = 0; b < 12; b++) {
            memset(out, 0, sizeof out);
            app_audio(out, 256);
            for (j = 2; j < 256; j++) {                 /* the second difference: a high-pass, a sine hardly shows */
                double d = (double)out[2 * j] - 2.0 * out[2 * (j - 1)] + out[2 * (j - 2)];
                if (b == 0) on += d * d; else if (b == 11) st += d * d;
            }
        }
        key_set(&mk_lo, &mk_hi, (uint32_t)keys[k], 0);
        for (b = 0; b < 60; b++) { memset(out, 0, sizeof out); app_audio(out, 256); }
        if (10.0 * log10(on / st) < worst) worst = 10.0 * log10(on / st);
    }
    printf("key click: the first block is %.1f dB above the steady one in the second difference (worst of 5 keys)\n", worst);
    CHECK("a note-on has a click", worst > 6.0);
}
int main(void)
{
    fx_bypass = 1;                                                  /* the tone generator alone; the reverb and the Leslie are checked below */
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
    check_compression();
    check_click();
    wheel_count();
    {   /* the reverb and the stopped Leslie against setBfree's own, sample by sample */
        static int32_t x[256], l[256], r[256];
        double er = 0, sr = 0, el = 0, sl = 0, ee = 0, se = 0;
        uint32_t b, i, s = 12345u;
        for (b = 0; b < 32u; b++) {
            for (i = 0; i < 256u; i++) x[i] = (b | i) ? 0 : 16384;
            reverb_run(x, 256, 64);
            for (i = 0; i < 256u; i++) { double d = x[i] - FXR_RV[b * 256u + i]; er += d * d; sr += (double)FXR_RV[b * 256u + i] * FXR_RV[b * 256u + i]; }
        }
        for (b = 0; b < 32u; b++) {
            for (i = 0; i < 256u; i++) { s = s * 1664525u + 1013904223u; x[i] = (int32_t)((s >> 16) & 0x7FFF) - 16384; }
            leslie_run(x, l, r, 256, 0);
            for (i = 0; i < 256u; i++) {
                double d = l[i] - FXR_WL[b * 256u + i], f = r[i] - FXR_WR[b * 256u + i];
                el += d * d; sl += (double)FXR_WL[b * 256u + i] * FXR_WL[b * 256u + i];
                ee += f * f; se += (double)FXR_WR[b * 256u + i] * FXR_WR[b * 256u + i];
            }
        }
        printf("reverb error %.1f dB, Leslie (stop) error L %.1f dB R %.1f dB\n", 10 * log10(er / sr), 10 * log10(el / sl), 10 * log10(ee / se));
        CHECK("reverb impulse response = setBfree (error < -30 dB)", er < sr * 0.001);
        CHECK("Leslie (stop) = setBfree (error < -30 dB)", el < sl * 0.001 && ee < se * 0.001);
    }
    {   /* percussion against setBfree's tone generator (3rd and 2nd harmonic, soft and normal, slow; level at 85 ms after the key
         * relative to the 8' fundamental; the decay: setBfree's plug-in multiplies by 0.9999 per sample at 48 kHz, 41.7 dB/s) */
        static const struct { int key, third, soft; double f, want; } PC[] = {
            {29, 1, 1, 349.23, -6.0}, {36, 1, 1, 523.25, -2.5}, {41, 1, 1, 698.46, -7.0},
            {29, 1, 0, 349.23, 0.0},  {36, 1, 0, 523.25, 3.5},  {41, 1, 0, 698.46, -1.0},
            {29, 0, 1, 349.23, -9.5}, {36, 0, 1, 523.25, -6.0}, {41, 0, 1, 698.46, -10.5},
            {29, 0, 0, 349.23, -3.5}, {36, 0, 0, 523.25, 0.0},  {41, 0, 0, 698.46, -4.5}};   /* keys F4, C5, F5; setBfree at 48 kHz */
        static float y[44100];
        static int32_t out[512];
        uint32_t c, b, i, j;
        double worst = 0, decay = 0;
        for (c = 0; c < sizeof PC / sizeof PC[0]; c++) {
            double h = PC[c].third ? 3.0 : 2.0, a[3];
            struct { double f; uint32_t n0, n1; } W[3];
            for (i = 0; i < NB; i++) P.db[i] = (uint8_t)(i == 2u ? 8 : 0);
            P.perc = 1; P.perc3 = (uint8_t)PC[c].third; P.soft = (uint8_t)PC[c].soft; P.fast = 0; P.vup = P.od = 0; P.les = 0; P.swell = 127;
            pk_lo = pk_hi = mk_lo = mk_hi = 0;
            for (b = 0; b < 60; b++) { memset(out, 0, sizeof out); app_audio(out, 256); }
            key_set(&mk_lo, &mk_hi, (uint32_t)PC[c].key, 1);
            for (b = 0; b < 160; b++) {
                memset(out, 0, sizeof out);
                app_audio(out, 256);
                for (j = 0; j < 256u; j++) y[b * 256u + j] = (float)out[2u * j] / 32768.0f;
            }
            key_set(&mk_lo, &mk_hi, (uint32_t)PC[c].key, 0);
            W[0].f = PC[c].f;     W[0].n0 = 26460; W[0].n1 = 35280;   /* the fundamental, 0.6 .. 0.8 s */
            W[1].f = h * PC[c].f; W[1].n0 = 1764;  W[1].n1 = 5733;    /* the percussion around 85 ms (40 .. 130 ms) */
            W[2].f = h * PC[c].f; W[2].n0 = 10584; W[2].n1 = 14553;   /* and 200 ms later */
            for (i = 0; i < 3u; i++) {
                double re = 0, im = 0, wsum = 0;
                for (j = W[i].n0; j < W[i].n1; j++) {
                    double w = 0.5 - 0.5 * cos(2 * M_PI * (j - W[i].n0) / (W[i].n1 - W[i].n0)), ph = 2 * M_PI * W[i].f * j / 44100.0;
                    re += w * y[j] * cos(ph); im += w * y[j] * sin(ph); wsum += w;
                }
                a[i] = 2.0 * sqrt(re * re + im * im) / wsum;
            }
            {
                double got = 20 * log10(a[1] / a[0]), d = got - PC[c].want;
                if (fabs(d) > worst) worst = fabs(d);
                decay += 20 * log10(a[2] / a[1]);
            }
        }
        decay /= (double)(sizeof PC / sizeof PC[0]);
        printf("percussion: worst level error %.2f dB, decay %.1f dB per 200 ms (setBfree 48 kHz: -8.3)\n", worst, decay);
        CHECK("percussion level = setBfree (3rd, 2nd, soft, normal; < 0.6 dB)", worst < 0.6);
        CHECK("percussion slow decay = setBfree's plug-in (-8.3 dB per 200 ms)", fabs(decay + 8.3) < 0.5);
    }
    {   /* the percussion's click: setBfree high-passes the percussion bus as a first difference, envelope and all, so the key's attack
         * (a bouncing envelope) comes out as a click. rms of the first difference 0 .. 10 ms after the key against 50 .. 100 ms, dB,
         * the percussion alone (no drawbars) on F4, setBfree's tone generator: 3rd 10.8, 2nd 18.0 */
        static const struct { int third; double want; } CK[2] = {{1, 10.8}, {0, 18.0}};
        static int32_t out[512];
        double worst = 0;
        uint32_t c, b, i, j;
        fx_bypass = 1;
        for (c = 0; c < 2u; c++) {
            static float y[8820];
            double r0 = 0, r1 = 0, got;
            for (i = 0; i < NB; i++) P.db[i] = 0;
            P.perc = 1; P.perc3 = (uint8_t)CK[c].third; P.soft = 1; P.fast = 0; P.vup = P.od = 0; P.les = 0; P.swell = 127;
            pk_lo = pk_hi = mk_lo = mk_hi = 0;
            for (b = 0; b < 60; b++) { memset(out, 0, sizeof out); app_audio(out, 256); }
            key_set(&mk_lo, &mk_hi, 29, 1);
            for (b = 0; b < 34; b++) {
                memset(out, 0, sizeof out);
                app_audio(out, 256);
                for (j = 0; j < 256u; j++) y[b * 256u + j] = (float)out[2u * j] / 32768.0f;
            }
            key_set(&mk_lo, &mk_hi, 29, 0);
            for (j = 1; j < 441u; j++) r0 += (double)(y[j] - y[j - 1]) * (double)(y[j] - y[j - 1]);
            for (j = 2206; j < 4410u; j++) r1 += (double)(y[j] - y[j - 1]) * (double)(y[j] - y[j - 1]);
            got = 10 * log10((r0 / 440.0) / (r1 / 2204.0));
            if (fabs(got - CK[c].want) > worst) worst = fabs(got - CK[c].want);
        }
        fx_bypass = 0;
        printf("percussion click: worst error %.1f dB against setBfree's tone generator\n", worst);
        CHECK("percussion click = setBfree (F4, 3rd and 2nd; < 3 dB)", worst < 3.0);
    }
    {   /* the overdrive is setBfree's preamp (b_overdrive/overdrive.c): the same model in double precision, a three-tone signal, three characters */
        static const double CF[8] = {0.00127616566, 0.00497944288, 0.0142455412, 0.14084346, 0.464444974, 0.0142455412, 0.00497944288, 0.00127616566};
        static const double AF[8] = {0.204190232, -0.0633467727, 0.0327789369, -0.0185333957, 0.0103064728, -0.00532702988, 0.00249628401, -0.00122011836};
        static const uint32_t CH[3] = {0, 64, 127};
        static int32_t q[4096];
        double worst = 1e9, xh[8], yh[33];
        uint32_t c, i, k;
        for (c = 0; c < 3u; c++) {
            double fb, fb2, sag = 0, z = 0, z1 = 0, gf = 0, se = 0, sr = 0;
            uint32_t ch = CH[c];
            if (ch < 32u) { fb = 0.5821; fb2 = 0.999 + (0.5821 - 0.999) * (ch / 31.0); }
            else if (ch < 64u) { fb = 0.5821 + (0.999 - 0.5821) * ((ch - 32u) / 31.0); fb2 = 0.5821; }
            else { fb = 0.999; fb2 = 0.5821 + (0.999 - 0.5821) * ((ch - 64u) / 63.0); }
            memset(xh, 0, sizeof xh); memset(yh, 0, sizeof yh);
            od_xi = od_yi = 0; od_sag = od_z = od_z1 = od_gf = 0; memset(od_xh, 0, sizeof od_xh); memset(od_yh, 0, sizeof od_yh);
            for (i = 0; i < 4096u; i++)
                q[i] = (int32_t)lrint(32768.0 * 0.045 * (sin(0.0592 * i) + 0.5 * sin(0.1488 * i) + 0.35 * sin(0.2961 * i)) / 1.85 * 1.6);
            od_run(q, 4096, ch);
            for (i = 0; i < 4096u; i++) {
                double xin, bias, norm, u, v, t, in = 32768.0 * 0.045 * (sin(0.0592 * i) + 0.5 * sin(0.1488 * i) + 0.35 * sin(0.2961 * i)) / 1.85 * 1.6;
                in = floor(in + 0.5) / 32768.0;
                xin = 3.5675 * in; sag = 0.991 * sag + fabs(xin); bias = 0.5347 - 0.0094 * sag; norm = 1 - 1 / (1 + bias * bias);
                for (k = 7; k > 0; k--) xh[k] = xh[k - 1];
                xh[0] = xin;
                u = 0;
                for (k = 0; k < 8u; k++) u += CF[k] * xh[k];
                u += 0.6214 * gf;
                t = u - z; z = u + z * fb; u = t;
                if (u < 0) { v = 1 / (1 + (u - bias) * (u - bias)) - 1 + norm; } else { v = 1 - norm - 1 / (1 + (u + bias) * (u + bias)); }
                t = v + fb2 * z1; v = t - z1; z1 = t; gf = v;
                for (k = 32; k > 0; k--) yh[k] = yh[k - 1];
                yh[0] = v;
                t = 0.323601515 * yh[16];
                for (k = 0; k < 8u; k++) t += AF[k] * (yh[15 - 2 * k] + yh[17 + 2 * k]);
                t *= 0.8795;
                if (i >= 256u) { se += (q[i] / 32768.0 - t) * (q[i] / 32768.0 - t); sr += t * t; }
            }
            if (10 * log10(sr / (se + 1e-30)) < worst) worst = 10 * log10(sr / (se + 1e-30));
        }
        printf("overdrive vs setBfree's preamp model: worst SNR %.1f dB (characters 0, 64, 127)\n", worst);
        CHECK("overdrive = setBfree's preamp (SNR > 35 dB)", worst > 35.0);
    }
    return bad;
}
