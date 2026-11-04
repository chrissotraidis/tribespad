/* Keyboard and frame pacing for Tribes: Vengeance's input. WinDrv reads keys as
 * window messages (WM_KEYDOWN/WM_KEYUP, WM_CHAR from TranslateMessage) and the
 * mouse through DirectInput 8. SDL events come through Starsiege's input layer
 * (port/starsiege/starsiege_input.c), which feeds DirectInput's mouse and
 * keyboard and calls back here for each key change; the key then goes to the
 * focused game window as a message, with the thread's key state updated (US
 * layout, TribesPad's keyboard tables). The layer is pumped once per presented
 * frame (port/vengeance/vengeance_d3d9_host.m), which also counts frames for
 * scripted input (STARSIEGE_INPUT). */
#include <TargetConditionals.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../llasm-support/llasm_cpu.h"
#include "../core/keyboard_state.h"
#include <mach/mach.h>

extern void (*ss_key_hook)(unsigned dik,int down);
extern void ss_input_pump(_cpu *cpu);
extern void ss_count_frame(void);
extern uint64_t ss_frames(void);
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
/* The character each key down types, decided when the key goes down. The iPad's
   keyboard sends Shift, the letter and Shift up within one pump, before the game
   translates the message: translated then, a capital came out in lower case. */
static struct {uint32_t vk;uint16_t c;uint8_t typed;} typed[32];
static unsigned typed_head,typed_count;
static uint32_t char_buffer(uint64_t base){
 static uint32_t out;if(!out){uint32_t e=tribes_get_last_error();out=tribes_global_alloc(base,0x40,4);tribes_set_last_error(e);}return out;
}
static void note_typed(uint64_t base,uint32_t vk,unsigned scan){
 uint32_t out=char_buffer(base);if(!out)return;
 unsigned at=(typed_head+typed_count)%32;if(typed_count==32){typed_head=(typed_head+1)%32;typed_count--;}
 typed[at].vk=vk;typed[at].typed=tribes_to_ascii(base,vk,scan,0,out,0)==1;
 if(typed[at].typed)memcpy(&typed[at].c,GUEST(base,out),2);
 typed_count++;
}
/* The character noted for this key down: 1 if one was noted (*c, or none typed). */
static int take_typed(uint32_t vk,uint16_t *c,int *any){
 for(unsigned i=0;i<typed_count;i++){unsigned at=(typed_head+i)%32;if(typed[at].vk!=vk)continue;
  *c=typed[at].c;*any=typed[at].typed;
  for(unsigned j=i;j>0;j--)typed[(typed_head+j)%32]=typed[(typed_head+j-1)%32];   /* drop it, keep the order */
  typed_head=(typed_head+1)%32;typed_count--;return 1;}
 return 0;
}
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
 if(message==WM_KEYDOWN)note_typed(base,vk,scan);
 uint32_t posted=tribes_post_message(base,window,message,vk,lp);
 static int trace=-1;if(trace<0)trace=getenv("VENGEANCE_TRACE_KEYS")!=NULL;
 if(trace)fprintf(stderr,"vengeance key: dik 0x%x vk 0x%x %s to 0x%x (posted %u, state 0x%x) thread %p\n",dik,vk,down?"down":"up",window,posted,tribes_get_key_state(base,vk),(void *)pthread_self());
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
extern void ss_input_use_vengeance_pad(void);
extern const char *ss_window_title;
/* Playing or in a menu: the game's GUI controller (recorded by a lift fixup,
   port/vengeance/vengeance_x86.c) is an Interaction whose bVisible flag (bit 1 of
   the flags after UObject's 0x28 bytes) is set while a menu or loading screen is
   up: the main menu, the Escape menu, briefings. Checked on the Mac: 0xf in
   menus, 0x9 in play. Drives the controller layout and TribesPad's overlay. */
extern uint32_t vengeance_gui_controller(void);
extern int (*ss_play_state_hook)(void);
static int playing(void){
 uint32_t g=vengeance_gui_controller();if(!g)return 0;
 uint32_t flags;memcpy(&flags,GUEST(vengeance_guest_base(),g+0x28),4);return !(flags&2);
}
/* A page made for typing is open in the game's GUI, so TribesPad brings up the
   on-screen keyboard: a new profile's name, a server or admin password, a saved
   game's name, the GameSpy login. GUIController.ActivePage is at 0xac (checked on
   the Mac: the profile name popup appears there on CREATE). An object's class is
   at 0x24 and a class's name at 0x20, an index into FName::Names (Core.dll),
   whose entries hold the name as UTF-16 after 12 bytes. VENGEANCE_TRACE_GUI logs
   each page as it opens. */
extern uint32_t vengeance_export(const char *name);
/* Guest memory read without trusting the pointer: the kernel reports an unmapped
   address instead of the read faulting (the controller's fields are not set yet
   early on, and a class or name pointer may be stale). */
static int peek(uint64_t base,uint32_t address,void *out,size_t size){
 vm_size_t got=0;
 return address&&vm_read_overwrite(mach_task_self(),(vm_address_t)(base+address),size,(vm_address_t)out,&got)==KERN_SUCCESS&&got==size;
}
static int class_name(uint64_t base,uint32_t object,char *out,size_t size){
 static uint32_t names;if(!names)names=vengeance_export("?Names@FName@@0V?$TArray@PAUFNameEntry@@@@A");
 out[0]=0;if(!object||!names)return 0;
 uint32_t cls,index,table[2],entry;uint16_t text[64];
 if(!peek(base,object+0x24,&cls,4)||!peek(base,cls+0x20,&index,4)||!peek(base,names,table,8))return 0;
 if(!table[0]||index>=table[1]||!peek(base,table[0]+4*index,&entry,4)||!peek(base,entry+12,text,sizeof text))return 0;
 size_t n=0;for(;n+1<size&&n<64;n++){if(!text[n]||text[n]>127)break;out[n]=(char)text[n];}
 out[n]=0;return n>0;
}
int vengeance_text_focused(void){
 uint32_t g=vengeance_gui_controller();if(!g)return 0;uint64_t base=vengeance_guest_base();
 uint32_t page;if(!peek(base,g+0xac,&page,4))return 0;
 static uint32_t last_page;static int typing;
 if(page==last_page)return typing;
 last_page=page;char name[64];class_name(base,page,name,sizeof name);
 static const char *const typed[]={"TribesDefaultTextEntryPopup","TribesAdminLoginPopup","TribesGamespyLogin","TribesGamespyTeamLogin","TribesSaveGameMenu"};
 typing=0;for(unsigned i=0;i<sizeof typed/sizeof typed[0];i++)if(!strcmp(name,typed[i]))typing=1;
 static int trace=-1;if(trace<0)trace=getenv("VENGEANCE_TRACE_GUI")!=NULL;
 if(trace)fprintf(stderr,"vengeance gui: page %s%s\n",name[0]?name:"(none)",typing?" (typing)":"");
 return typing;
}
__attribute__((constructor)) static void install(void){ss_key_hook=key;ss_pointer_hook=pointer;ss_button_hook=button;ss_input_use_vengeance_pad();ss_play_state_hook=playing;ss_window_title="Tribes: Vengeance";}

#if TARGET_OS_IPHONE
/* TribesPad's overlay in the app (port/apple/mobile_controls.m): frames for its
   FPS counter, and whether the game is in play (no menu up). */
extern void tb_app_note_frame(void);
extern void tb_vengeance_report_state(int playing);
extern void tb_vengeance_report_typing(int typing);
#endif
int vengeance_text_focused(void);
/* Once per presented frame, on the game's thread. */
void vengeance_input_frame(_cpu *cpu){
 ss_count_frame();ss_input_pump(cpu);
#if TARGET_OS_IPHONE
 tb_app_note_frame();
 static int reported=-1;int now=playing();
 if(now!=reported){reported=now;tb_vengeance_report_state(now);fprintf(stderr,"TribesPad: Vengeance %s (frame %llu)\n",now?"in play":"in menus",(unsigned long long)ss_frames());}
 /* A text box focused in a menu: the app opens the keyboard (mobile_controls.m). */
 static int typing=-1;int text=!now&&vengeance_text_focused();
 if(text!=typing){typing=text;tb_vengeance_report_typing(text);fprintf(stderr,"TribesPad: Vengeance text box %s\n",text?"focused":"left");}
#else
 static int trace=-1;if(trace<0)trace=getenv("VENGEANCE_TRACE_GUI")!=NULL;
 if(trace)vengeance_text_focused();
#endif
}

/* BOOL TranslateMessage(const MSG *): a key down also posts its character, as a
   US keyboard types it with the current key state. */
uint32_t vg_translate_message(uint64_t base,uint32_t msg){
 if(!msg)return 0;uint32_t m[4];memcpy(m,GUEST(base,msg),16);
 if(m[1]!=WM_KEYDOWN&&m[1]!=WM_SYSKEYDOWN)return m[1]==WM_KEYUP||m[1]==WM_SYSKEYUP;
 if(m[1]==WM_SYSKEYDOWN)return 1;                       /* Alt combinations type nothing here */
 uint16_t c=0;int any=0;
 if(!take_typed(m[2],&c,&any)){uint32_t out=char_buffer(base);any=out&&tribes_to_ascii(base,m[2],(m[3]>>16)&0xff,0,out,0)==1;if(any)memcpy(&c,GUEST(base,out),2);}
 if(any){
  uint32_t posted=tribes_post_message(base,m[0],WM_CHAR,c,m[3]);
  static int trace=-1;if(trace<0)trace=getenv("VENGEANCE_TRACE_KEYS")!=NULL;
  if(trace)fprintf(stderr,"vengeance key: WM_CHAR 0x%x to 0x%x (posted %u)\n",c,m[0],posted);
 }
 return 1;
}

