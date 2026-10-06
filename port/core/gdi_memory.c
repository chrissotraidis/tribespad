/* Actual memory DCs and monochrome bitmap storage for Tribes' font-cache setup.
   https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatibledc
   https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatiblebitmap
   Original raster fonts use private CoreText storage; arbitrary drawing,
   window/screen DCs and color DIBs remain separate gates. */
#include "gdi_memory.h"
#include "guest_heap.h"
#include "guest_memory_checks.h"
#include "../apple/font.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { DC=1,BITMAP=2,BRUSH=3,FONT=4 };
typedef struct Object { uint64_t base;uint32_t handle,kind,selected,stock,width,height,stride,brush,style,color,font,bk_color,text_color;void *native_font;unsigned char *pixels;struct Object *next; } Object;
typedef struct Owned { uint64_t base;uint32_t handle;struct Owned *next; } Owned;
static Object *objects;
static uint32_t next_handle=0xd1000001;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t key_once=PTHREAD_ONCE_INIT;
static pthread_key_t owner_key;
static Object *find(uint64_t base,uint32_t handle,int kind) {
    for(Object *o=objects;o;o=o->next)if(o->base==base&&o->handle==handle&&(!kind||o->kind==(uint32_t)kind))return o;
    return NULL;
}
static void remove_object(Object *o) {
    Object **p=&objects;while(*p!=o)p=&(*p)->next;*p=o->next;if(o->native_font)tb_apple_font_release(o->native_font);free(o->pixels);free(o);
}
static void destroy_dc(Object *dc) {
    Object *selected=find(dc->base,dc->selected,BITMAP);if(selected)selected->selected=0;
    Object *font=find(dc->base,dc->font,FONT);if(font)font->selected--;
    Object *stock=find(dc->base,dc->stock,BITMAP);if(stock)remove_object(stock);remove_object(dc);
}
static void release_owned(void *data) {
    Owned *p=data;pthread_mutex_lock(&lock);while(p){Owned *next=p->next;Object *dc=find(p->base,p->handle,DC);if(dc)destroy_dc(dc);free(p);p=next;}pthread_mutex_unlock(&lock);
}
static void make_key(void) { if(pthread_key_create(&owner_key,release_owned))abort(); }
static Object *allocate(uint64_t base,int kind) {
    if(next_handle>=0xd2000000){tribes_set_last_error(8);return NULL;}
    Object *o=calloc(1,sizeof *o);if(!o){tribes_set_last_error(8);return NULL;}
    o->base=base;o->kind=(uint32_t)kind;o->handle=next_handle++;o->next=objects;objects=o;return o;
}
static Object *stock_brush(uint64_t base,uint32_t index) {
    uint32_t handle=0xd6000000+index;Object *brush=find(base,handle,BRUSH);if(brush)return brush;
    brush=calloc(1,sizeof *brush);if(!brush){tribes_set_last_error(8);return NULL;}
    const uint32_t colors[]={0xffffff,0xc0c0c0,0x808080,0x404040,0,0};brush->base=base;brush->kind=BRUSH;brush->handle=handle;brush->stock=1;brush->style=index==5?1:0;brush->color=colors[index];brush->next=objects;objects=brush;return brush;
}
uint32_t tribes_get_stock_object(uint64_t base,uint32_t index) {
    if(index>5){if(index<=19&&index!=9){fprintf(stderr,"TribesPad: stock pens/fonts/palette/DC brush are not implemented\n");abort();}return 0;}
    pthread_mutex_lock(&lock);Object *brush=stock_brush(base,index);uint32_t handle=brush?brush->handle:0;
#ifdef TRIBESPAD_BRUSH_OBSERVER
    extern void tribes_brush_observation(uint32_t,uint32_t,uint32_t,uint32_t);
    if(brush)tribes_brush_observation(index,handle,brush->style,brush->color);
#endif
    pthread_mutex_unlock(&lock);return handle;
}
int tribes_brush_snapshot(uint64_t base,uint32_t handle,uint32_t *style,uint32_t *color) {
    if(!style||!color)return 0;pthread_mutex_lock(&lock);Object *brush=find(base,handle,BRUSH);if(brush){*style=brush->style;*color=brush->color;}pthread_mutex_unlock(&lock);return !!brush;
}
static Object *bitmap(uint64_t base,uint32_t w,uint32_t h) {
    if(w>INT32_MAX||h>INT32_MAX){tribes_set_last_error(87);return NULL;}
    if(!w||!h)w=h=1;uint64_t stride=(((uint64_t)w+31)/32)*4,size=stride*h;
    if(size>64*1024*1024){tribes_set_last_error(8);return NULL;}
    unsigned char *pixels=calloc(1,(size_t)size);if(!pixels){tribes_set_last_error(8);return NULL;}
    Object *o=allocate(base,BITMAP);if(!o){free(pixels);return NULL;}o->width=w;o->height=h;o->stride=(uint32_t)stride;o->pixels=pixels;return o;
}
uint32_t tribes_create_memory_dc(uint64_t base,uint32_t compatible) {
    Owned *owned=NULL;
    if(!compatible){pthread_once(&key_once,make_key);owned=malloc(sizeof *owned);if(!owned){tribes_set_last_error(8);return 0;}}
    pthread_mutex_lock(&lock);
    if(compatible&&!find(base,compatible,DC)){pthread_mutex_unlock(&lock);free(owned);tribes_set_last_error(6);return 0;}
    Object *dc=allocate(base,DC),*stock=dc?bitmap(base,1,1):NULL;
    if(!stock){if(dc)remove_object(dc);pthread_mutex_unlock(&lock);free(owned);return 0;}
    Object *brush=stock_brush(base,0);if(!brush){remove_object(stock);remove_object(dc);pthread_mutex_unlock(&lock);free(owned);return 0;}dc->brush=brush->handle;
    dc->bk_color=0xffffff;dc->selected=dc->stock=stock->handle;stock->selected=dc->handle;uint32_t result=dc->handle;
    if(owned){owned->base=base;owned->handle=result;owned->next=pthread_getspecific(owner_key);if(pthread_setspecific(owner_key,owned))abort();}
    pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_create_compatible_bitmap(uint64_t base,uint32_t handle,uint32_t w,uint32_t h) {
    pthread_mutex_lock(&lock);Object *dc=find(base,handle,DC);if(!dc){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
    Object *o=bitmap(base,w,h);uint32_t result=o?o->handle:0;pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_select_gdi_object(uint64_t base,uint32_t handle,uint32_t object) {
    pthread_mutex_lock(&lock);Object *dc=find(base,handle,DC),*o=find(base,object,0);
    if(dc&&o&&o->kind==BRUSH){uint32_t old=dc->brush;dc->brush=object;pthread_mutex_unlock(&lock);return old;}
    if(dc&&o&&o->kind==FONT){uint32_t old=dc->font;Object *previous=find(base,old,FONT);if(previous)previous->selected--;dc->font=object;o->selected++;pthread_mutex_unlock(&lock);return old;}
    if(!dc||!o||o->kind!=BITMAP||(o->selected&&o->selected!=handle)){pthread_mutex_unlock(&lock);return 0;}
    uint32_t old=dc->selected;Object *previous=find(base,old,BITMAP);if(previous)previous->selected=0;o->selected=handle;dc->selected=object;
    pthread_mutex_unlock(&lock);return old;
}
uint32_t tribes_delete_gdi_object(uint64_t base,uint32_t handle) {
    pthread_mutex_lock(&lock);Object *o=find(base,handle,0);if(!o){pthread_mutex_unlock(&lock);return 0;}
    if(o->kind==BRUSH&&o->stock){pthread_mutex_unlock(&lock);return 1;}
    if(o->kind==DC){destroy_dc(o);pthread_mutex_unlock(&lock);return 1;}
    if(o->selected){pthread_mutex_unlock(&lock);return 0;}
    /* The original stock bitmap remains owned by its DC even when deselected. */
    for(Object *p=objects;p;p=p->next)if(p->kind==DC&&p->base==base&&p->stock==handle){pthread_mutex_unlock(&lock);return 1;}
    remove_object(o);pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_delete_memory_dc(uint64_t base,uint32_t handle) {
    pthread_mutex_lock(&lock);Object *dc=find(base,handle,DC);if(!dc){pthread_mutex_unlock(&lock);return 0;}destroy_dc(dc);pthread_mutex_unlock(&lock);return 1;
}
int tribes_bitmap_snapshot(uint64_t base,uint32_t handle,uint32_t *w,uint32_t *h,uint32_t *stride,uint32_t *bits,void *pixels,size_t capacity) {
    if(!w||!h||!stride||!bits||!pixels)return 0;pthread_mutex_lock(&lock);Object *o=find(base,handle,BITMAP);
    if(!o||capacity<(size_t)o->stride*o->height){pthread_mutex_unlock(&lock);return 0;}
    *w=o->width;*h=o->height;*stride=o->stride;*bits=1;memcpy(pixels,o->pixels,(size_t)o->stride*o->height);pthread_mutex_unlock(&lock);return 1;
}
static void *guest_range(uint64_t base,uint32_t address,uint32_t bytes){
    if(!base||!address||(uint64_t)address+bytes>UINT64_C(0x100000000)||base>UINTPTR_MAX-((uint64_t)address+bytes))return NULL;
    return (void *)(uintptr_t)(base+address);
}
static void unsupported_font(void){fputs("TribesPad: unsupported raster-font attributes/transform/format\n",stderr);abort();}
uint32_t tribes_create_font_a(uint64_t base,uint32_t height,uint32_t width,uint32_t escapement,uint32_t orientation,uint32_t weight,uint32_t italic,uint32_t underline,uint32_t strikeout,uint32_t charset,uint32_t out_precision,uint32_t clip_precision,uint32_t quality,uint32_t pitch,uint32_t face){
    /* The reached original cache requests ANSI, default upright regular fonts.
       Other attributes need their real mapper/raster semantics before entry. */
    if(width||escapement||orientation||(weight&&weight!=400)||italic||underline||strikeout||charset||out_precision||clip_precision||quality||pitch)unsupported_font();
    char name[32]={0};
    if(face){
        unsigned i;for(i=0;i<sizeof name;i++){
            if(face>UINT32_MAX-i){tribes_set_last_error(87);return 0;}
            const char *p=guest_range(base,face+i,1);if(!p){tribes_set_last_error(87);return 0;}
            memcpy(name+i,p,1);if(!name[i])break;
        }
        if(i==sizeof name){tribes_set_last_error(87);return 0;}
    }
    void *native=tb_apple_font_create(name,(int32_t)height);if(!native)return 0;
    pthread_mutex_lock(&lock);Object *f=allocate(base,FONT);if(f)f->native_font=native;
    uint32_t result=f?f->handle:0;pthread_mutex_unlock(&lock);if(!f)tb_apple_font_release(native);return result;
}
static uint32_t set_color(uint64_t base,uint32_t handle,uint32_t color,int text){
    pthread_mutex_lock(&lock);Object *dc=find(base,handle,DC);if(!dc){pthread_mutex_unlock(&lock);return UINT32_MAX;}
    uint32_t *slot=text?&dc->text_color:&dc->bk_color,previous=*slot;*slot=color&0xffffff;pthread_mutex_unlock(&lock);return previous;
}
uint32_t tribes_set_bk_color(uint64_t base,uint32_t dc,uint32_t color){return set_color(base,dc,color,0);}
uint32_t tribes_set_text_color(uint64_t base,uint32_t dc,uint32_t color){return set_color(base,dc,color,1);}
uint32_t tribes_get_text_metrics_a(uint64_t base,uint32_t handle,uint32_t output){
    void *p=guest_range(base,output,sizeof(TBFontMetrics));if(!p)return 0;
    uint32_t bracket=tribes_memory_access_begin(p,sizeof(TBFontMetrics),2);
    pthread_mutex_lock(&lock);Object *dc=find(base,handle,DC),*f=dc?find(base,dc->font,FONT):NULL;TBFontMetrics metrics;
    int result=f&&tb_apple_font_metrics(f->native_font,&metrics);if(result)memcpy(p,&metrics,sizeof metrics);
    pthread_mutex_unlock(&lock);tribes_memory_access_end(bracket);return result;
}
uint32_t tribes_get_glyph_outline_a(uint64_t base,uint32_t handle,uint32_t character,uint32_t format,uint32_t output,uint32_t capacity,uint32_t buffer,uint32_t transform){
    if((format!=6&&format!=0)||character>255)unsupported_font();
    void *m=guest_range(base,transform,16),*p=guest_range(base,output,sizeof(TBGlyphMetrics));if(!m||!p)return UINT32_MAX;
    int32_t matrix[4];memcpy(matrix,m,16);if(matrix[0]!=0x10000||matrix[1]||matrix[2]||matrix[3]!=0x10000)unsupported_font();
    uint32_t metrics_bracket=tribes_memory_access_begin(p,sizeof(TBGlyphMetrics),2),buffer_bracket=0;
    pthread_mutex_lock(&lock);Object *dc=find(base,handle,DC),*f=dc?find(base,dc->font,FONT):NULL;
    TBGlyphMetrics metrics;unsigned char *pixels=NULL;uint32_t bytes=0,result=UINT32_MAX;
    if(f&&tb_apple_font_glyph(f->native_font,character,&metrics,&pixels,&bytes)){
        void *target=NULL;int copy=format==6&&capacity&&buffer&&bytes;
        if(!copy||(capacity>=bytes&&(target=guest_range(base,buffer,bytes)))){
            if(copy)buffer_bracket=tribes_memory_access_begin(target,bytes,2);
            if(copy)memcpy(target,pixels,bytes);memcpy(p,&metrics,sizeof metrics);result=format?bytes:0;
        }
    }
    free(pixels);pthread_mutex_unlock(&lock);tribes_memory_access_end(buffer_bracket);tribes_memory_access_end(metrics_bracket);return result;
}
uint32_t tribes_get_text_extent_point32_a(uint64_t base,uint32_t handle,uint32_t text,uint32_t count,uint32_t output){
    void *p=guest_range(base,output,8);if(!p||count>65536)return 0;
    void *input=count?guest_range(base,text,count):NULL;if(count&&!input)return 0;
    unsigned char *copy=count?malloc(count):NULL;if(count&&!copy)return 0;if(count)memcpy(copy,input,count);
    uint32_t bracket=tribes_memory_access_begin(p,8,2);
    pthread_mutex_lock(&lock);Object *dc=find(base,handle,DC),*f=dc?find(base,dc->font,FONT):NULL;int32_t size[2];
    int result=f&&tb_apple_font_extent(f->native_font,copy,count,size);if(result)memcpy(p,size,8);
    pthread_mutex_unlock(&lock);tribes_memory_access_end(bracket);free(copy);return result;
}
