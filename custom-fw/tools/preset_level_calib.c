/* IMP-02: measure each factory preset and print the preset_vol that lands it on the target */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pa_config.h"
#include "pa_audio.h"
#include "pa_fx.h"
#include "pa_preset.h"
#include "pa_audio_core.h"
extern pa_fx_chain_t g_fx_chain;
void pa_firmware_init(void);
uint32_t plat_ticks24(void){return 0;} static uint32_t s_ms; uint32_t plat_ms(void){return s_ms+=1;}
void st7735s_fill_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t c){} void st7735s_init(void){}
void plat_backlight_set(uint8_t pct){(void)pct;} void plat_wdt_feed(void){} int plat_battery_is_brownout(void){return 0;}
int  plat_battery_mv(void){return 4050;}
/* The board layer is not linked here: the tool only measures the audio, so
 * the panel and the audio state are stubs. */
pa_audio_state_t g_pa_audio;
void ui_draw_str(uint16_t x,uint16_t y,const char*st,uint16_t fg,uint16_t bg){(void)x;(void)y;(void)st;(void)fg;(void)bg;}
void ui_draw_str_scaled(uint16_t x,uint16_t y,const char*st,uint16_t fg,uint16_t bg,uint8_t sc){(void)x;(void)y;(void)st;(void)fg;(void)bg;(void)sc;}
void ui_draw_char(uint16_t x,uint16_t y,char c,uint16_t fg,uint16_t bg){(void)x;(void)y;(void)c;(void)fg;(void)bg;}
void ui_draw_char_scaled(uint16_t x,uint16_t y,char c,uint16_t fg,uint16_t bg,uint8_t sc){(void)x;(void)y;(void)c;(void)fg;(void)bg;(void)sc;}
void pa_smp_init(pa_fx_chain_t*c){(void)c;} void pa_audio_alnk_init(void){}
static float *load_raw(const char *p,int *n){FILE*f=fopen(p,"rb");fseek(f,0,2);long sz=ftell(f);fseek(f,44,0);int cnt=(sz-44)/2;short*s=malloc(cnt*2);if(fread(s,2,cnt,f)){};fclose(f);float*o=malloc(cnt*4);for(int i=0;i<cnt;i++)o[i]=s[i]/32768.f;*n=cnt;return o;}
static double run(float*x,int n,float g,double*pk_out){int32_t rx[128],tx[128];double sum=0;long cnt=0;int32_t mx=0;
  for(int off=0;off+64<=n;off+=64){for(int i=0;i<64;i++){rx[2*i]=((int32_t)lrintf(x[off+i]*g*8388607.f))&0xFFFFFF;rx[2*i+1]=0;}
   pa_audio_core_process_half(rx,tx);for(int i=0;i<64;i++){int32_t v=(tx[2*i]<<8)>>8;int32_t a=v<0?-v:v;if(a>mx)mx=a;sum+=(double)v*v;cnt++;}}
  *pk_out=20*log10(mx/8388608.0+1e-12); return 20*log10(sqrt(sum/cnt)/8388608.0+1e-12);}
int main(int argc,char**argv){
  const char*gtr[3]={argv[1],argv[2],argv[3]}; const char*bass=argv[4]; double target=atof(argv[5]);
  int mode=argc>6?atoi(argv[6]):0; /* 0 = measure+suggest, 1 = verify with stored vol */
  pa_firmware_init(); double lo=99,hi=-99;
  for(int p=0;p<40;p++){
    pa_preset_t pr; pa_preset_load(p,&pr); int bassp=(pr.genre_tag==PA_GENRE_BASS);
    double acc=0,pkmax=-99;int k=0;
    for(int f=0;f<(bassp?1:3);f++){int n;float*x=load_raw(bassp?bass:gtr[f],&n);float pk=0;for(int i=0;i<n;i++)if(fabsf(x[i])>pk)pk=fabsf(x[i]);
      pa_preset_t q=pr; if(!mode) q.preset_vol=70; pa_preset_apply_to_chain(&q,&g_fx_chain); double pko; acc+=run(x,n,powf(10.f,-10.f/20.f)/pk,&pko);if(pko>pkmax)pkmax=pko;k++;free(x);}
    double r=acc/k; if(r<lo)lo=r; if(r>hi)hi=r;
    int v=(int)lrint(70*pow(10,(target-r)/20)); if(v>100)v=100; if(v<30)v=30;
    if(mode) printf("P%02d %-22s vol=%3d rms=%6.1f peak=%6.1f\n",p+1,pr.name,pr.preset_vol,r,pkmax);
    else printf("%d,",v);
  }
  printf("\nspread: %.1f .. %.1f dBFS RMS (%.1f dB)\n",lo,hi,hi-lo);
}
