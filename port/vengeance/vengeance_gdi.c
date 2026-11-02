/* GDI objects and the small desktop services Tribes: Vengeance's windowing
 * (Window.dll, WinDrv.dll) asks for, beyond Starsiege's windows. The game draws
 * everything through Direct3D: GDI here creates and tracks brushes, pens and
 * fonts and paints nothing on screen. Objects this file creates have handles
 * from 0xd8000000; any other handle goes to TribesPad's GDI (gdi_memory.c).
 * System parameters and colours are those of a default Windows XP desktop.
 * Requests outside what is modeled stop with their values. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "../llasm-support/llasm_cpu.h"

extern uint32_t tribes_set_last_error(uint32_t value);
extern uint32_t tribes_delete_gdi_object(uint64_t base,uint32_t handle);
extern uint32_t tribes_select_gdi_object(uint64_t base,uint32_t dc,uint32_t object);
extern uint32_t tribes_get_window_dc(uint64_t base,uint32_t window);
extern uint32_t tribes_release_window_dc(uint64_t base,uint32_t window,uint32_t dc);
extern uint32_t tribes_get_system_metrics(uint64_t base,uint32_t index);
extern uint32_t ss_get_client_rect(uint64_t base,uint32_t handle,uint32_t output);
extern uint32_t ss_validate_rect(uint32_t handle,uint32_t rect);
extern uint32_t ss_get_window_long_a(uint64_t base,uint32_t handle,uint32_t index);
extern uint32_t ss_client_to_screen(uint64_t base,uint32_t handle,uint32_t point);
extern uint32_t ss_screen_to_client(uint64_t base,uint32_t handle,uint32_t point);
extern uint32_t ss_set_foreground_window(_cpu *cpu,uint32_t handle);
extern uint32_t ss_get_foreground_window(void);

#define GUEST(base,address) ((void *)(uintptr_t)((base)+(address)))
static _Noreturn void unsupported(const char *what,uint32_t value){fprintf(stderr,"TribesPad: unsupported Vengeance desktop service: %s 0x%x\n",what,value);abort();}
static uint32_t rd(uint64_t base,uint32_t a){uint32_t v;memcpy(&v,GUEST(base,a),4);return v;}
static void wr(uint64_t base,uint32_t a,uint32_t v){memcpy(GUEST(base,a),&v,4);}

/* ---- brushes, pens, fonts ---- */
enum {BRUSH=1,PEN=2,FONT=3,BITMAP=4};
enum {FIRST=0xd8000000u,SYSTEM_BRUSH=0xd7000000u};
typedef struct Object {uint32_t handle,kind,color,width,height,bits;struct Object *next;} Object;
typedef struct Selection {uint32_t dc,brush,pen,font,bk_mode,rop2;struct Selection *next;} Selection;
static Object *objects;static Selection *selections;static uint32_t next_handle=FIRST;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
/* Permanent objects: system colour brushes at SYSTEM_BRUSH + index, stock pens,
   fonts and DC brush/pen at SYSTEM_BRUSH + 0x100 + index. */
static int ours(uint32_t h){return (h>=FIRST&&h<FIRST+0x1000000u)||(h>=SYSTEM_BRUSH&&h<SYSTEM_BRUSH+31)||(h>=SYSTEM_BRUSH+0x100&&h<SYSTEM_BRUSH+0x100+20);}
static uint32_t permanent_kind(uint32_t h){
 if(h<SYSTEM_BRUSH+0x100)return BRUSH;uint32_t i=h-SYSTEM_BRUSH-0x100;
 return i==18?BRUSH:(i>=6&&i<=8)||i==19?PEN:i==15?0:FONT;
}
/* HGDIOBJ GetStockObject(index): brushes 0-5 are TribesPad's; pens, fonts and
   the DC brush and pen are permanent objects here. */
extern uint32_t tribes_get_stock_object(uint64_t base,uint32_t index);
uint32_t vg_get_stock_object(uint64_t base,uint32_t index){
 if(index<=5)return tribes_get_stock_object(base,index);
 if(index<=19&&index!=9&&index!=15)return SYSTEM_BRUSH+0x100+index;
 return 0;
}
static uint32_t create(uint32_t kind,uint32_t color){
 Object *o=calloc(1,sizeof *o);if(!o){tribes_set_last_error(8);return 0;}
 pthread_mutex_lock(&lock);o->handle=next_handle++;o->kind=kind;o->color=color;o->next=objects;objects=o;uint32_t h=o->handle;pthread_mutex_unlock(&lock);return h;
}
static Object *find(uint32_t h){for(Object *o=objects;o;o=o->next)if(o->handle==h)return o;return NULL;}
static Selection *selection(uint32_t dc){
 for(Selection *s=selections;s;s=s->next)if(s->dc==dc)return s;
 Selection *s=calloc(1,sizeof *s);if(!s)abort();s->dc=dc;s->bk_mode=2;s->rop2=13;s->next=selections;selections=s;return s;
}
uint32_t vg_create_solid_brush(uint32_t color){return create(BRUSH,color&0xffffff);}
uint32_t vg_create_pen(uint32_t style,uint32_t width,uint32_t color){(void)style;(void)width;return create(PEN,color&0xffffff);}
/* HFONT CreateFontW(height, width, ..., face): a font object the game can select; text is never rasterized here. */
uint32_t vg_create_font_w(uint32_t h,uint32_t w,uint32_t e,uint32_t o,uint32_t weight,uint32_t i,uint32_t u,uint32_t s,uint32_t cs,uint32_t op,uint32_t cp,uint32_t q,uint32_t p,uint32_t face){
 (void)h;(void)w;(void)e;(void)o;(void)weight;(void)i;(void)u;(void)s;(void)cs;(void)op;(void)cp;(void)q;(void)p;(void)face;return create(FONT,0);
}
uint32_t vg_delete_object(uint64_t base,uint32_t h){
 if(!ours(h))return tribes_delete_gdi_object(base,h);
 if(h<FIRST)return 1; /* system colour brushes are permanent */
 pthread_mutex_lock(&lock);Object **p=&objects;while(*p&&(*p)->handle!=h)p=&(*p)->next;
 if(!*p){pthread_mutex_unlock(&lock);return 0;}Object *o=*p;*p=o->next;free(o);pthread_mutex_unlock(&lock);return 1;
}
/* HBITMAP CreateDIBitmap(dc, BITMAPINFOHEADER *, init, bits, info, usage): a
   bitmap of the header's size and depth (the startup splash's logo). Its
   pixels are not kept: nothing draws GDI bitmaps on screen. */
uint32_t vg_create_di_bitmap(uint64_t base,uint32_t dc,uint32_t header,uint32_t init,uint32_t bits,uint32_t info,uint32_t usage){
 (void)dc;(void)init;(void)bits;(void)info;(void)usage;if(!header){tribes_set_last_error(87);return 0;}
 uint32_t h=create(BITMAP,0);if(!h)return 0;
 pthread_mutex_lock(&lock);Object *o=find(h);o->width=rd(base,header+4);int32_t height=(int32_t)rd(base,header+8);o->height=(uint32_t)(height<0?-height:height);
 uint16_t depth;memcpy(&depth,GUEST(base,header+14),2);o->bits=depth;pthread_mutex_unlock(&lock);return h;
}
/* int GetObjectA(handle, size, out): BITMAP for this file's bitmaps. */
uint32_t vg_get_object_a(uint64_t base,uint32_t h,uint32_t size,uint32_t out){
 pthread_mutex_lock(&lock);Object *o=ours(h)?find(h):NULL;
 if(!o||o->kind!=BITMAP){pthread_mutex_unlock(&lock);unsupported("GetObject of a handle that is not a bitmap",h);}
 uint32_t v[6]={0,o->width,o->height,((o->width*o->bits+15)/16)*2,1u|((uint32_t)o->bits<<16),0};pthread_mutex_unlock(&lock);
 if(!out)return 24;if(size<24){tribes_set_last_error(87);return 0;}memcpy(GUEST(base,out),v,24);return 24;
}
/* CreateDialog*: TribesPad has no dialog manager. The game's one modeless
   dialog is the startup splash, on its own thread, which simply ends when the
   dialog cannot be created; the failure is logged. */
static uint32_t no_dialog(uint32_t name){fprintf(stderr,"vengeance: dialog %u not shown (no dialog manager)\n",name);tribes_set_last_error(1813);return 0;}
uint32_t vg_create_dialog_param_w(uint32_t instance,uint32_t name,uint32_t parent,uint32_t proc,uint32_t param){(void)instance;(void)parent;(void)proc;(void)param;return no_dialog(name);}
uint32_t vg_create_dialog_param_a(uint32_t instance,uint32_t name,uint32_t parent,uint32_t proc,uint32_t param){(void)instance;(void)parent;(void)proc;(void)param;return no_dialog(name);}
uint32_t vg_select_object(uint64_t base,uint32_t dc,uint32_t h){
 if(!ours(h))return tribes_select_gdi_object(base,dc,h);
 pthread_mutex_lock(&lock);uint32_t kind=h<FIRST?permanent_kind(h):0;Object *o=find(h);if(o)kind=o->kind;
 if(!kind){pthread_mutex_unlock(&lock);return 0;}
 Selection *s=selection(dc);uint32_t *slot=kind==BRUSH?&s->brush:kind==PEN?&s->pen:&s->font,old=*slot;*slot=h;pthread_mutex_unlock(&lock);
 return old?old:0xd6000000u+(kind==BRUSH?0:kind==PEN?7:13); /* the stock objects a new DC starts with */
}
uint32_t vg_set_bk_mode(uint32_t dc,uint32_t mode){pthread_mutex_lock(&lock);Selection *s=selection(dc);uint32_t old=s->bk_mode;s->bk_mode=mode;pthread_mutex_unlock(&lock);return old;}
uint32_t vg_set_rop2(uint32_t dc,uint32_t mode){pthread_mutex_lock(&lock);Selection *s=selection(dc);uint32_t old=s->rop2;s->rop2=mode;pthread_mutex_unlock(&lock);return old;}

/* ---- system colours (Windows XP default scheme) ---- */
static const uint32_t sys_colors[31]={0xc8d0d4,0x984e00,0x6a240a,0x808080,0xc8d0d4,0xffffff,0x000000,0x000000,0x000000,0xffffff,
 0xc8d0d4,0xc8d0d4,0x808080,0x6a240a,0xffffff,0xc8d0d4,0x808080,0x808080,0x000000,0xc8d0d4,0xffffff,0x404040,0xc8d0d4,0x000000,
 0xe1ffff,0xb5b5b5,0x800000,0xf0caa6,0xc0c0c0,0xff9933,0xc8d0d4};
uint32_t vg_get_sys_color(uint32_t index){return index<31?sys_colors[index]:0;}

/* int GetDeviceCaps(HDC, index): a 96 DPI, 60 Hz, 32-bit display; sizes and depth from TribesPad's desktop. */
extern uint32_t tribes_get_display_caps(uint64_t base,uint32_t dc,uint32_t index);
uint32_t vg_get_device_caps(uint64_t base,uint32_t dc,uint32_t index){
 switch(index){
 case 2: return 1;                 /* TECHNOLOGY: DT_RASDISPLAY */
 case 14: return 1;                /* PLANES */
 case 24: return 0xffffffffu;      /* NUMCOLORS: more than 8 bits per pixel */
 case 88: case 90: return 96;      /* LOGPIXELSX/Y */
 case 104: case 106: return 0;     /* SIZEPALETTE, NUMRESERVED */
 case 116: return 60;              /* VREFRESH */
 }
 return tribes_get_display_caps(base,dc,index);
}
uint32_t vg_get_sys_color_brush(uint32_t index){return index<31?SYSTEM_BRUSH+index:0;}

/* ---- SystemParametersInfo: the settings Unreal reads and restores ---- */
static uint32_t system_parameters(uint64_t base,uint32_t action,uint32_t ui,uint32_t pv,uint32_t ini){
 (void)ui;(void)ini;
 switch(action){
 case 0x10: if(pv)wr(base,pv,0);return 1;                     /* SPI_GETSCREENSAVEACTIVE: off */
 case 0x11: case 0x0f: case 0x3b: case 0x35: case 0x33: return 1;   /* screen saver, sticky/toggle/filter keys: accepted */
 case 0x30: if(!pv)break; wr(base,pv,0);wr(base,pv+4,0);         /* SPI_GETWORKAREA: the whole primary display */
  wr(base,pv+8,tribes_get_system_metrics(base,0));wr(base,pv+12,tribes_get_system_metrics(base,1));return 1;
 case 0x3a: case 0x34: if(!pv)break; wr(base,pv+4,0x2);return 1;    /* STICKYKEYS / TOGGLEKEYS: available, off (cbSize stays) */
 case 0x32: if(!pv)break; memset((uint8_t *)GUEST(base,pv)+4,0,20);wr(base,pv+4,0x2);return 1; /* FILTERKEYS */
 case 0x68: if(pv)wr(base,pv,0);return 1;                      /* SPI_GETSNAPTODEFBUTTON: off */
 }
 unsupported("SystemParametersInfo action",action);
}
uint32_t vg_system_parameters_info_a(uint64_t base,uint32_t action,uint32_t ui,uint32_t pv,uint32_t ini){return system_parameters(base,action,ui,pv,ini);}
uint32_t vg_system_parameters_info_w(uint64_t base,uint32_t action,uint32_t ui,uint32_t pv,uint32_t ini){return system_parameters(base,action,ui,pv,ini);}

/* ---- painting: the window's DC, the client area as the paint rectangle, validated ---- */
/* GetDC / ReleaseDC: Vengeance's windows paint through GDI only for Window.dll's
   log window and controls, which are never shown; their DCs are screen DCs
   (TribesPad's desktop), so no native drawable is made for them. The game's
   Direct3D window gets its drawable from the device (port/vengeance/vengeance_d3d9_host.m). */
uint32_t vg_get_dc(uint64_t base,uint32_t window){(void)window;return tribes_get_window_dc(base,0);}
uint32_t vg_release_dc(uint64_t base,uint32_t window,uint32_t dc){(void)window;return tribes_release_window_dc(base,0,dc);}
uint32_t vg_begin_paint(uint64_t base,uint32_t window,uint32_t ps){
 uint32_t dc=vg_get_dc(base,window);if(!dc||!ps)return 0;
 memset(GUEST(base,ps),0,64);wr(base,ps,dc);ss_get_client_rect(base,window,ps+8);ss_validate_rect(window,0);return dc;
}
uint32_t vg_end_paint(uint64_t base,uint32_t window,uint32_t ps){if(ps)vg_release_dc(base,window,rd(base,ps));return 1;}
uint32_t vg_fill_rect(uint32_t dc,uint32_t rect,uint32_t brush){(void)dc;(void)rect;(void)brush;return 1;}
uint32_t vg_get_update_rect(uint64_t base,uint32_t window,uint32_t rect,uint32_t erase){(void)window;(void)erase;if(rect)memset(GUEST(base,rect),0,16);return 0;}

/* ---- window state ---- */
uint32_t vg_is_zoomed(uint64_t base,uint32_t window){return (ss_get_window_long_a(base,window,(uint32_t)-16)&0x01000000u)!=0;}
uint32_t vg_is_window_enabled(uint64_t base,uint32_t window){return (ss_get_window_long_a(base,window,(uint32_t)-16)&0x08000000u)==0;}
uint32_t vg_set_active_window(_cpu *c,uint32_t window){uint32_t old=ss_get_foreground_window();ss_set_foreground_window(c,window);return old;}
uint32_t vg_bring_window_to_top(uint32_t window){(void)window;return 1;}
uint32_t vg_clip_cursor(uint32_t rect){(void)rect;return 1;}
uint32_t vg_track_mouse_event(uint32_t event){(void)event;return 1;}
/* int MapWindowPoints(from, to, points, count): through the screen; the offset as LOWORD/HIWORD. */
uint32_t vg_map_window_points(uint64_t base,uint32_t from,uint32_t to,uint32_t points,uint32_t count){
 int32_t x0=0,y0=0,x1=0,y1=0;
 for(uint32_t i=0;i<count;i++){
  uint32_t p=points+8*i;if(!i){x0=(int32_t)rd(base,p);y0=(int32_t)rd(base,p+4);}
  if(from)ss_client_to_screen(base,from,p);if(to)ss_screen_to_client(base,to,p);
  if(!i){x1=(int32_t)rd(base,p);y1=(int32_t)rd(base,p+4);}
 }
 return ((uint32_t)(x1-x0)&0xffffu)|((uint32_t)(y1-y0)<<16);
}
uint32_t vg_get_message_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000+t.tv_nsec/1000000);}

/* ---- kernel32 ---- */
uint32_t vg_mul_div(uint32_t a,uint32_t b,uint32_t c){
 if(!c)return 0xffffffffu;int64_t n=(int64_t)(int32_t)a*(int32_t)b,d=(int32_t)c;
 int64_t q=(n+((n<0)==(d<0)?d/2:-d/2))/d;if(q>INT32_MAX||q<INT32_MIN)return 0xffffffffu;return (uint32_t)(int32_t)q;
}
static int fold(unsigned v){return v>='a'&&v<='z'?v-32:v;}
static uint32_t compare_i(uint64_t base,uint32_t a,uint32_t b,int wide){
 for(uint32_t i=0;;i++){
  unsigned x=0,y=0;if(wide){uint16_t u,v;memcpy(&u,GUEST(base,a+2*i),2);memcpy(&v,GUEST(base,b+2*i),2);x=u;y=v;}
  else{x=*(uint8_t *)GUEST(base,a+i);y=*(uint8_t *)GUEST(base,b+i);}
  int d=fold(x)-fold(y);if(d)return d<0?0xffffffffu:1u;if(!x)return 0;
 }
}
uint32_t vg_lstrcmpi_a(uint64_t base,uint32_t a,uint32_t b){if(!a||!b)return a==b?0:a?1:0xffffffffu;return compare_i(base,a,b,0);}
uint32_t vg_lstrcmpi_w(uint64_t base,uint32_t a,uint32_t b){if(!a||!b)return a==b?0:a?1:0xffffffffu;return compare_i(base,a,b,1);}
void vg_get_system_time(uint64_t base,uint32_t out){
 struct timeval tv;gettimeofday(&tv,NULL);time_t s=tv.tv_sec;struct tm t;gmtime_r(&s,&t);
 uint16_t v[8]={(uint16_t)(t.tm_year+1900),(uint16_t)(t.tm_mon+1),(uint16_t)t.tm_wday,(uint16_t)t.tm_mday,(uint16_t)t.tm_hour,(uint16_t)t.tm_min,(uint16_t)t.tm_sec,(uint16_t)(tv.tv_usec/1000)};
 memcpy(GUEST(base,out),v,16);
}
uint32_t vg_get_system_default_lang_id(void){return 0x409;}
uint32_t vg_interlocked_compare_exchange(uint64_t base,uint32_t target,uint32_t exchange,uint32_t comparand){
 uint32_t expected=comparand;__atomic_compare_exchange_n((uint32_t *)GUEST(base,target),&expected,exchange,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return expected;
}
void vg_output_debug_string_a(uint64_t base,uint32_t text){if(text)fprintf(stderr,"vengeance debug: %.*s\n",1000,(const char *)GUEST(base,text));}

