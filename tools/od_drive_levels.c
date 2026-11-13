/* Measures the level with the overdrive on against off, five keys at swell 80, per drive step: the ratios behind OD_GOUT in organ_od.h.
 * cc -O1 -Ifirmware/src -o od_drive_levels tools/od_drive_levels.c -lm  (run from the repository root) */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#define FM1_TICKS_PER_US 24u
static uint32_t tk; static uint32_t fm1_ticks(void){return tk+=7919u;}
#include "organ_tab.h"
#include "organ_core.h"
static double run(int od,int drv,int sw,int nk){ static int32_t out[512]; int i,b; double s=0; long n=0;
 P.od=od; P.drv=drv; P.les=0; P.rev=0; fx_bypass=1; P.swell=sw;
 for(i=0;i<nk;i++) key_set(&mk_lo,&mk_hi,20+i*3,1);
 for(b=0;b<200;b++){memset(out,0,sizeof out);app_audio(out,256); if(b>60) for(i=0;i<256;i++){s+=(double)out[i]*out[i]; n++;}}
 for(i=0;i<nk;i++) key_set(&mk_lo,&mk_hi,20+i*3,0);
 for(b=0;b<100;b++) app_audio(out,256);
 return __builtin_sqrt(s/n);}
int main(){ int d; double off=run(0,0,80,5);
 for(d=0;d<=16;d++){ double r=run(1,d,80,5)/off; printf("%d %.4f %.0f\n",d,r,28820/r);} }
