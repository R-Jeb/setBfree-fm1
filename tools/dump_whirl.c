/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Dumps setBfree's Leslie (b_whirl/whirl.c) tables at 44.1 kHz into data/setbfree-whirl.txt:
 * the horn and drum path delays by angle, the horn's 5-tap angle-dependent filter, the path spacings and
 * the filter coefficients. The tables are decimated 16384 -> 1024 entries (the firmware interpolates).
 * Build against a setBfree checkout, as tools/dump_contrib.c:
 *   S=<setBfree>/src; gcc -O1 -I$S -I<setBfree> -I<setBfree>/b_overdrive -I<setBfree>/b_reverb -I<setBfree>/b_whirl \
 *       -DHAVE_CONFIG_H -DLV2SYNTH tools/dump_whirl.c $S/midi.c $S/cfgParser.c $S/program.c $S/vibrato.c $S/state.c \
 *       $S/tonegen.c $S/pgmParser.c $S/midnam.c <setBfree>/b_whirl/eqcomp.c <setBfree>/b_whirl/whirl.c \
 *       <setBfree>/b_overdrive/overdrive.c <setBfree>/b_reverb/reverb.c -lm -o dump_whirl && ./dump_whirl > data/setbfree-whirl.txt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "global_inst.h"
#include "cfgParser.h"
#include "midi.h"
#include "whirl.h"
#include "tonegen.h"
#include "state.h"
#include "program.h"
double SampleRateD = 44100.0;
int mainConfig(ConfigContext *c) { return 0; }
const ConfigDoc *mainDoc() { return NULL; }
int main(void)
{
    struct b_instance inst;
    struct b_whirl *w;
    int i, j, step = WHIRL_DISPLC_SIZE / 1024;
    memset(&inst, 0, sizeof inst);
    inst.state = allocRunningConfig(); inst.progs = allocProgs(); inst.whirl = allocWhirl();
    inst.midicfg = allocMidiCfg(inst.state);
    initControllerTable(inst.midicfg); midiPrimeControllerMapping(inst.midicfg);
    inst.synth = allocTonegen(); initToneGenerator(inst.synth, inst.midicfg);
    initWhirl(inst.whirl, inst.midicfg, 44100.0);
    w = inst.whirl;
    printf("hornSpacing"); for (i = 0; i < 6; i++) printf(" %.6f", w->hornSpacing[i]); printf("\n");
    printf("drumSpacing"); for (i = 0; i < 6; i++) printf(" %.6f", w->drumSpacing[i]); printf("\n");
    printf("hornPhase"); for (i = 0; i < 6; i++) printf(" %d", w->hornPhase[i]); printf("\n");
    printf("drumPhase"); for (i = 0; i < 6; i++) printf(" %d", w->drumPhase[i]); printf("\n");
    printf("hornLevel %.6f leakage %.6f\n", w->hornLevel, w->leakage);
    printf("hafw"); for (i = 0; i < 7; i++) printf(" %.9f", w->hafw[i]); printf("\n");
    printf("hbfw"); for (i = 0; i < 7; i++) printf(" %.9f", w->hbfw[i]); printf("\n");
    printf("drfL"); for (i = 0; i < 7; i++) printf(" %.9f", w->drfL[i]); printf("\n");
    printf("drfR"); for (i = 0; i < 7; i++) printf(" %.9f", w->drfR[i]); printf("\n");
    for (i = 0; i < 1024; i++) {
        const struct _bw *f = &w->bfw[i * step], *b = &w->bbw[i * step];
        printf("t %d %.5f %.5f %.5f %.5f", i, w->hnFwdDispl[i * step], w->hnBwdDispl[i * step], w->drFwdDispl[i * step], w->drBwdDispl[i * step]);
        for (j = 0; j < 5; j++) printf(" %.6f", f->b[j]);
        for (j = 0; j < 5; j++) printf(" %.6f", b->b[j]);
        printf("\n");
    }
    return 0;
}
