/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Dumps setBfree's compiled key contribution matrix (tonewheel taper + crosstalk/leakage, default model),
 * upper manual: one line "key bus wheel gain" (wheel 1-based). Output: data/setbfree-contrib.txt.
 * Build against a setBfree checkout (tonegen.c is linked in, everything else it needs is stubbed here):
 *   S=<setBfree>/src; gcc -O1 -I$S -I<setBfree> -I<setBfree>/b_overdrive -I<setBfree>/b_reverb -I<setBfree>/b_whirl \
 *       -DHAVE_CONFIG_H tools/dump_contrib.c $S/tonegen.c -lm -o dump_contrib && ./dump_contrib > data/setbfree-contrib.txt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tonegen.h"
#include "midi.h"
#include "cfgParser.h"
#include "vibrato.h"
void useMIDIControlFunction(void*m,const char*c,void(*f)(void*,unsigned char),void*d){}
void notifyControlChangeByName(void*m,const char*c,unsigned char v){}
void notifyControlChangeById(void*m,int i,unsigned char v){}
void vibratoProc(struct b_vibrato*v,float const*i,float*o,size_t n){}
void resetVibrato(void*t){}
void showConfigfileContext(ConfigContext*c,const char*m){}
int getConfigParameter_d(const char*p,ConfigContext*c,double*d){return 0;}
int getConfigParameter_i(const char*p,ConfigContext*c,int*d){return 0;}
int getConfigParameter_f(const char*p,ConfigContext*c,float*d){return 0;}
int getConfigParameter_ir(const char*p,ConfigContext*c,int*d,int a,int b){return 0;}
int getConfigParameter_dr(const char*p,ConfigContext*c,double*d,double a,double b){return 0;}
int getConfigParameter_fr(const char*p,ConfigContext*c,float*d,float a,float b){return 0;}
double SampleRateD = 44100.0;
int SampleRateI=44100;
void configDoubleUnparsable(ConfigContext*c){}
void configIntUnparsable(ConfigContext*c){}
void configIntOutOfRange(ConfigContext*c,int a,int b){}
const char* getConfigValue(ConfigContext*c){return "";}
void setConfigInt(int*v,ConfigContext*c){}
void setConfigRangeInt(int*v,ConfigContext*c){}
void setConfigDouble(double*v,ConfigContext*c){}

#include "global_inst.h"
int main(int argc,char**argv){
  struct b_tonegen*t=allocTonegen();initToneGenerator(t,NULL);
  printf("floor %g min %g\n",t->contributionFloorLevel,t->contributionMinLevel);
  for(int k=0;k<61;k++){for(ListElement*l=t->keyContrib[k];l;l=l->next){
    if(l->u.ssf.sb<9)printf("%d %d %d %.6f\n",k,l->u.ssf.sb,l->u.ssf.sa,l->u.ssf.fc);}}
  return 0;}
