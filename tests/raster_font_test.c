#include "../port/apple/font.h"
#include "../port/core/gdi_memory.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <CoreText/CoreText.h>
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
#ifdef CHECKED_TEST
extern void raster_guest_pointer(void *),raster_wrong_abi(void),raster_unreviewed(void),raster_wrong_convention(void),raster_wrong_rect(void);
#endif
static unsigned char *memory;static uint64_t base;static uint32_t dc,font;
static uint32_t invoke(uint32_t id,const uint32_t *args,unsigned count){
 _cpu cpu;memset(&cpu,0xa9,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0xc003;
 memcpy(memory+cpu._esp,args,count*4);_cpu expected=cpu;tribes_set_last_error(12345);tribes_enter(&cpu,id);
 expected._eax=cpu._eax;expected._esp+=count*4;
 assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0xc003,args,count*4)&&tribes_get_last_error()==12345);return cpu._eax;
}
static uint32_t create(int32_t height){uint32_t args[14]={0};args[0]=(uint32_t)height;args[13]=0x1000;return invoke(1,args,14);}
static uint32_t glyph_outline(uint32_t c,uint32_t cap,uint32_t buffer,uint32_t gm){uint32_t a[]={dc,c,6,gm,cap,buffer,0x1100};return invoke(5,a,7);}
static void native_reference(void){
 void *positive=tb_apple_font_create("Helvetica",24);assert(positive);TBFontMetrics metrics;assert(tb_apple_font_metrics(positive,&metrics)&&metrics.height<=24&&metrics.height>20);tb_apple_font_release(positive);
 void *native=tb_apple_font_create("Helvetica",-24);assert(native);
 CTFontRef independent=CTFontCreateWithName(CFSTR("Helvetica"),24,NULL);assert(independent);
 assert(tb_apple_font_metrics(native,&metrics));assert(metrics.ascent==(int32_t)ceil(CTFontGetAscent(independent))&&metrics.descent==(int32_t)ceil(CTFontGetDescent(independent))&&metrics.height==metrics.ascent+metrics.descent);
 for(unsigned c=32;c<=126;c++){
  UniChar character=c;CGGlyph glyph=0;CGSize advance;assert(CTFontGetGlyphsForCharacters(independent,&character,&glyph,1));CTFontGetAdvancesForGlyphs(independent,kCTFontOrientationHorizontal,&glyph,&advance,1);
  CGRect bounds=CTFontGetBoundingRectsForGlyphs(independent,kCTFontOrientationHorizontal,&glyph,NULL,1);
  TBGlyphMetrics gm;unsigned char *pixels=NULL;uint32_t bytes=99;assert(tb_apple_font_glyph(native,c,&gm,&pixels,&bytes));
  assert(gm.advance_x==(int16_t)floor(advance.width+.5)&&!gm.advance_y);
  if(bounds.size.width>0&&bounds.size.height>0){
   assert(gm.x==(int32_t)floor(bounds.origin.x)&&gm.y==(int32_t)ceil(bounds.origin.y+bounds.size.height));
   assert(gm.width==(uint32_t)(ceil(bounds.origin.x+bounds.size.width)-floor(bounds.origin.x))&&gm.height==(uint32_t)(ceil(bounds.origin.y+bounds.size.height)-floor(bounds.origin.y)));
   unsigned stride=(gm.width+3)&~3u;assert(bytes==stride*gm.height&&pixels);unsigned ink=0,partial=0;
   for(unsigned y=0;y<gm.height;y++)for(unsigned x=0;x<stride;x++){unsigned v=pixels[y*stride+x];assert(v<=64);if(x>=gm.width)assert(!v);else{ink+=v;partial+=v&&v<64;}}
   assert(ink);if(c=='F'){
    unsigned top=0,bottom=0;for(unsigned y=0;y<3;y++)for(unsigned x=0;x<gm.width;x++){top+=pixels[y*stride+x];bottom+=pixels[(gm.height-1-y)*stride+x];}
    assert(top>bottom*2&&partial); /* Independent asymmetric orientation check. */
   }
   if(c=='g')assert((int32_t)gm.height>gm.y); /* Actual descender below baseline. */
  }else assert(!bytes&&!pixels&&!gm.width&&!gm.height&&c==' ');
  free(pixels);
 }
 CFRelease(independent);tb_apple_font_release(native);
}
static void *worker(void *arg){
 uint32_t at=0x40000+(uint32_t)(uintptr_t)arg*0x2000;tribes_set_last_error(9876);
 uint32_t local=tribes_create_memory_dc(base,0);assert(local);tribes_select_gdi_object(base,local,font);
 for(unsigned i=0;i<16;i++){assert(tribes_get_text_metrics_a(base,local,at));assert(tribes_get_glyph_outline_a(base,local,'g',6,at+64,1024,at+128,0x1100)!=UINT32_MAX);assert(tribes_get_last_error()==9876);}
 assert(tribes_delete_memory_dc(base,local));return NULL;
}
#ifdef CHECKED_TEST
static void fault(unsigned mode){
 memset(memory+0x18000,0xa5,0x8000);unsigned char saved[0x8000];memcpy(saved,memory+0x18000,sizeof saved);pid_t child=fork();assert(child>=0);
 if(!child){
  if(mode==0){assert(tribes_memory_permissions_set(0x18000,4096,1));tribes_get_text_metrics_a(base,dc,0x18000);}
  if(mode==1){assert(tribes_memory_permissions_set(0x19000,4096,1));tribes_get_text_metrics_a(base,dc,0x18ff0);}
  if(mode==2){assert(tribes_memory_permissions_set(0x18000,4096,1));glyph_outline('F',1024,0x20000,0x18000);}
  if(mode==3){assert(tribes_memory_permissions_set(0x20000,4096,1));glyph_outline('F',1024,0x20000,0x18000);}
  if(mode==4){assert(tribes_memory_permissions_set(0x19000,4096,1));glyph_outline('F',1024,0x18fff,0x18000);}
  if(mode==5){assert(tribes_memory_permissions_set(0x1000,4096,0));create(24);}
  if(mode==6){assert(tribes_memory_permissions_set(0x1000,4096,0));glyph_outline('F',1024,0x20000,0x18000);}
  if(mode==7){uint32_t a[]={dc,0x1000,5,0x18000};assert(tribes_memory_permissions_set(0x1000,4096,0));invoke(6,a,4);}
  if(mode==8){uint32_t a[]={dc,0x1000,5,0x18000};assert(tribes_memory_permissions_set(0x18000,4096,1));invoke(6,a,4);}
  if(mode==9)raster_guest_pointer(memory+0x18000);
  if(mode==10)raster_wrong_abi();if(mode==11)raster_unreviewed();if(mode==12)raster_wrong_convention();if(mode==13)raster_wrong_rect();
  if(mode==14){uint32_t a[14]={24,1};invoke(1,a,14);}
  if(mode==15)tribes_get_glyph_outline_a(base,dc,'F',2,0x18000,1024,0x20000,0x1100);
  if(mode==16){memory[0x1100]=1;glyph_outline('F',1024,0x20000,0x18000);}
  _exit(90);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(saved,memory+0x18000,sizeof saved));
}
#endif
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 strcpy((char *)memory+0x1000,"Helvetica");int32_t matrix[]={0x10000,0,0,0x10000};memcpy(memory+0x1100,matrix,sizeof matrix);
 native_reference();strcpy((char *)memory+0x1000,"Univers");font=create(24);assert(font);dc=tribes_create_memory_dc(base,0);assert(dc);assert(!tribes_select_gdi_object(base,dc,font));assert(!tribes_delete_gdi_object(base,font));
 uint32_t colors[]={dc,0x123456};assert(invoke(2,colors,2)==0xffffff);assert(invoke(3,colors,2)==0);assert(invoke(2,colors,2)==0x123456);
 memset(memory+0x18000,0xa5,4096);uint32_t tm[]={dc,0x18001};assert(invoke(4,tm,2));TBFontMetrics metrics;memcpy(&metrics,memory+0x18001,sizeof metrics);assert(metrics.height==metrics.ascent+metrics.descent&&metrics.height>0&&metrics.height<=24);assert(memory[0x18000]==0xa5&&memory[0x18001+56]==0xa5);
 assert(!tribes_get_text_metrics_a(base+1,dc,0x18001)&&!tribes_get_text_metrics_a(base,dc,0xfffffffe));
 for(unsigned c=32;c<256;c++){
  memset(memory+0x18000,0xa5,4096);uint32_t bytes=glyph_outline(c,0,0,0x18001);assert(bytes!=UINT32_MAX);TBGlyphMetrics gm;memcpy(&gm,memory+0x18001,sizeof gm);assert(memory[0x18000]==0xa5&&memory[0x18001+20]==0xa5);
  assert(bytes==((gm.width+3)&~3u)*gm.height&&bytes<4096);memset(memory+0x20000,0xa5,0x4000);
#ifdef CHECKED_TEST
  assert(tribes_memory_permissions_set(0x21000,4096,1));
#endif
  uint32_t start=0x21000-bytes;assert(glyph_outline(c,65536,start,0x18001)==bytes);assert(memory[start-1]==0xa5&&memory[0x21000]==0xa5);
  for(unsigned i=0;i<bytes;i++)assert(memory[start+i]<=64);
  if(bytes){memset(memory+0x18000,0xa5,4096);assert(glyph_outline(c,bytes-1,0x20000,0x18001)==UINT32_MAX);assert(memory[0x18001]==0xa5&&memory[0x20000]==0xa5);}
 }
 uint32_t textargs[]={dc,0x1000,7,0x18001};memset(memory+0x18000,0xa5,4096);assert(invoke(6,textargs,4));int32_t extent[2];memcpy(extent,memory+0x18001,8);assert(extent[0]>0&&extent[1]==metrics.height&&memory[0x18000]==0xa5&&memory[0x18009]==0xa5);
 uint32_t other=tribes_create_memory_dc(base,0);assert(other);tribes_select_gdi_object(base,other,font);assert(tribes_delete_memory_dc(base,other)&&!tribes_delete_gdi_object(base,font));
 pthread_t threads[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
#ifdef CHECKED_TEST
 assert(tribes_memory_permissions_set(0x21000,4096,3));for(unsigned i=0;i<17;i++)fault(i);
#endif
 assert(tribes_delete_memory_dc(base,dc)&&tribes_delete_gdi_object(base,font)&&!tribes_delete_gdi_object(base,font));
 puts("PASS: real CoreText bounds/advances and asymmetric top-down glyphs; original Univers height24 request with real Helvetica fallback, 224 ANSI characters, gray8 levels/padding/space, positive cell height, emitted six-call ABI/CPU/LastError, exact output spans/protected tails, short-buffer preservation, shared font/DC ownership and 64 concurrent rasterizations; 17 fault stops in checked mode");return 0;
}
