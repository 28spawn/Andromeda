/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Andromeda contributors */

/* tools/amp_level_calib.c : AMP(GAIN 50 % = 0 dB, flat) + matching CAB, DI peak -10 dBFS.
 * Build with -DPA_AMP_TRIM_DISABLE; prints k_amp_out_trim_db[] for target -18 dBFS RMS. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pa_config.h"
#include "pa_audio.h"
#include "pa_fx.h"
#include "pa_preset.h"
extern pa_fx_chain_t g_fx_chain;
void pa_firmware_init(void);
uint32_t plat_ticks24(void){return 0;} static uint32_t s_ms; uint32_t plat_ms(void){return s_ms+=1;}
void st7735s_fill_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t c){} void st7735s_init(void){}
static float *load_raw(const char *p,int *n){FILE*f=fopen(p,"rb");fseek(f,0,2);long sz=ftell(f);fseek(f,44,0);int cnt=(sz-44)/2;short*s=malloc(cnt*2);if(fread(s,2,cnt,f)){};fclose(f);float*o=malloc(cnt*4);for(int i=0;i<cnt;i++)o[i]=s[i]/32768.f;*n=cnt;return o;}
static double run(float*x,int n,float g){int32_t rx[128],tx[128];double sum=0;long cnt=0;
  for(int off=0;off+64<=n;off+=64){for(int i=0;i<64;i++){rx[2*i]=((int32_t)lrintf(x[off+i]*g*8388607.f))&0xFFFFFF;rx[2*i+1]=0;}
   pa_audio_process_half(rx,tx);for(int i=0;i<64;i++){int32_t v=(tx[2*i]<<8)>>8;sum+=(double)v*v;cnt++;}}
  return 20*log10(sqrt(sum/cnt)/8388608.0+1e-12);}
int main(int argc,char**argv){
  const char*gtr[3]={argv[1],argv[2],argv[3]}; const char*bass=argv[4]; double target=atof(argv[5]);
  pa_firmware_init();
  printf("raw: "); int tr[20];
  for(int a=0;a<20;a++){ double acc=0;int k=0;
    for(int f=0;f<(a>=15?1:3);f++){int n;float*x=load_raw(a>=15?bass:gtr[f],&n);float pk=0;for(int i=0;i<n;i++)if(fabsf(x[i])>pk)pk=fabsf(x[i]);
      pa_preset_t p; pa_preset_load(40,&p); p.slot_enable_mask=(1u<<PA_SLOT_AMP)|(1u<<PA_SLOT_CAB);
      p.amp_model_idx=a;p.cab_model_idx=a;p.preset_vol=100;for(int q=0;q<6;q++)p.params[PA_SLOT_AMP][q]=32768;
      pa_preset_apply_to_chain(&p,&g_fx_chain); acc+=run(x,n,powf(10.f,-0.5f)/pk);k++;free(x);}
    double r=acc/k; printf("%.1f ",r); int t=(int)lrint(target-r); if(t>30)t=30; if(t<-12)t=-12; tr[a]=t;}
  printf("\nk_amp_out_trim_db = { "); for(int a=0;a<20;a++)printf("%d%s",tr[a],a<19?", ":" };\n");
}
