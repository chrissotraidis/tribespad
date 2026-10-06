/* Actual memory DCs and monochrome bitmap storage for Tribes' font-cache setup.
   https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatibledc
   https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatiblebitmap
   Fonts, drawing, window/screen DCs and color DIBs are separate unimplemented gates. */
#include "gdi_memory.h"
#include "guest_heap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { DC=1,BITMAP=2,BRUSH=3 };
typedef struct Object { uint64_t base;uint32_t handle,kind,selected,stock,width,height,stride,brush,style,color;unsigned char *pixels;struct Object *next; } Object;
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
    Object **p=&objects;while(*p!=o)p=&(*p)->next;*p=o->next;free(o->pixels);free(o);
}
static void destroy_dc(Object *dc) {
    Object *selected=find(dc->base,dc->selected,BITMAP);if(selected)selected->selected=0;
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
    dc->selected=dc->stock=stock->handle;stock->selected=dc->handle;uint32_t result=dc->handle;
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
