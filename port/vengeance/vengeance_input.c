/* Keyboard and frame pacing for Tribes: Vengeance's input. WinDrv reads keys as
 * window messages (WM_KEYDOWN/WM_KEYUP, WM_CHAR from TranslateMessage) and the
 * mouse through DirectInput 8. SDL events come through Starsiege's input layer
 * (port/starsiege/starsiege_input.c), which feeds DirectInput's mouse and
 * keyboard and calls back here for each key change; the key then goes to the
 * focused game window as a message, with the thread's key state updated (US
 * layout, TribesPad's keyboard tables). The layer is pumped once per presented
 * frame (port/vengeance/vengeance_d3d9_host.m), which also counts frames for
 * scripted input (STARSIEGE_INPUT). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../llasm-support/llasm_cpu.h"
#include "../core/keyboard_state.h"

extern void (*ss_key_hook)(unsigned dik,int down);
extern void ss_input_pump(_cpu *cpu);
extern void ss_count_frame(void);
extern uint32_t ss_get_focus(void);
extern uint32_t ss_get_foreground_window(void);
extern uint32_t tribes_post_message(uint64_t base,uint32_t window,uint32_t message,uint32_t wp,uint32_t lp);
extern uint32_t tribes_map_virtual_key(uint32_t code,uint32_t type);
extern uint32_t tribes_to_ascii(uint64_t base,uint32_t vk,uint32_t scan,uint32_t keys,uint32_t output,uint32_t flags);
extern uint32_t tribes_global_alloc(uint64_t base,uint32_t flags,uint32_t bytes);
extern uint32_t tribes_get_last_error(void);
extern uint32_t tribes_set_last_error(uint32_t value);
extern uint64_t vengeance_guest_base(void);
#define GUEST(base,address) ((void *)(uintptr_t)((base)+(address)))
enum {WM_KEYDOWN=0x100,WM_KEYUP=0x101,WM_CHAR=0x102,WM_SYSKEYDOWN=0x104,WM_SYSKEYUP=0x105,WM_SYSCHAR=0x106};

static int alt_down;
/* A DirectInput key code (scan code, | 0x80 for E0-prefixed keys) as a message. */
static void key(unsigned dik,int down){
 uint64_t base=vengeance_guest_base();
 unsigned scan=dik&0x7f,extended=(dik&0x80)!=0;
 uint32_t code=extended?0xe000u|scan:scan,side=tribes_map_virtual_key(code,3),vk=tribes_map_virtual_key(code,1);
 if(!vk)return;
 if(side==0xa4||side==0xa5)alt_down=down;
 tribes_keyboard_apply(base,side?side:vk,down);
 uint32_t window=ss_get_focus();if(!window)window=ss_get_foreground_window();if(!window)return;
 uint32_t lp=1u|(scan<<16)|(extended<<24)|(down?0u:0xc0000000u);
 uint32_t message=(alt_down||vk==0x12)?(down?WM_SYSKEYDOWN:WM_SYSKEYUP):(down?WM_KEYDOWN:WM_KEYUP);
 tribes_post_message(base,window,message,vk,lp);
}
/* The mouse, while WinDrv holds it without exclusivity (menus): the Windows
   pointer moves with it and the window gets mouse messages, as on Windows. */
extern void (*ss_pointer_hook)(int32_t dx,int32_t dy,int32_t dz);
extern void (*ss_button_hook)(unsigned button,int down);
extern uint32_t ss_pointer_to_client(int32_t dx,int32_t dy,int32_t *cx,int32_t *cy);
static uint32_t held;   /* MK_LBUTTON 1, MK_RBUTTON 2, MK_MBUTTON 0x10 */
static uint32_t point(int32_t x,int32_t y){return ((uint32_t)(uint16_t)y<<16)|(uint16_t)x;}
static void pointer(int32_t dx,int32_t dy,int32_t dz){
 int32_t x,y;uint32_t window=ss_pointer_to_client(dx,dy,&x,&y);if(!window)return;uint64_t base=vengeance_guest_base();
 if(dx||dy)tribes_post_message(base,window,0x0200,held,point(x,y));                          /* WM_MOUSEMOVE */
 if(dz)tribes_post_message(base,window,0x020a,((uint32_t)(int16_t)dz<<16)|held,point(x,y));   /* WM_MOUSEWHEEL */
}
static void button(unsigned b,int down){
 static const uint32_t flag[3]={1,2,0x10},msg[3]={0x0201,0x0204,0x0207};    /* left, right, middle */
 if(b>2)return;int32_t x,y;uint32_t window=ss_pointer_to_client(0,0,&x,&y);if(!window)return;
 if(down)held|=flag[b];else held&=~flag[b];
 tribes_post_message(vengeance_guest_base(),window,msg[b]+(down?0:1),held,point(x,y));
}
__attribute__((constructor)) static void install(void){ss_key_hook=key;ss_pointer_hook=pointer;ss_button_hook=button;}

/* Once per presented frame, on the game's thread. */
void vengeance_input_frame(_cpu *cpu){ss_count_frame();ss_input_pump(cpu);}

/* BOOL TranslateMessage(const MSG *): a key down also posts its character, as a
   US keyboard types it with the current key state. */
uint32_t vg_translate_message(uint64_t base,uint32_t msg){
 if(!msg)return 0;uint32_t m[4];memcpy(m,GUEST(base,msg),16);
 if(m[1]!=WM_KEYDOWN&&m[1]!=WM_SYSKEYDOWN)return m[1]==WM_KEYUP||m[1]==WM_SYSKEYUP;
 if(m[1]==WM_SYSKEYDOWN)return 1;                       /* Alt combinations type nothing here */
 static uint32_t out;if(!out){uint32_t e=tribes_get_last_error();out=tribes_global_alloc(base,0x40,4);tribes_set_last_error(e);}
 if(out&&tribes_to_ascii(base,m[2],(m[3]>>16)&0xff,0,out,0)==1){
  uint16_t c;memcpy(&c,GUEST(base,out),2);tribes_post_message(base,m[0],WM_CHAR,c,m[3]);
 }
 return 1;
}

