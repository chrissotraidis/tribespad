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
/* For TribesPad's Mac menu (port/apple/mac_menu.m): 1 in play, 0 in a menu or loading. */
int vengeance_in_play(void){return playing();}
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
 /* Each page change is logged on iOS (the session log), on the Mac with VENGEANCE_TRACE_GUI. */
 static int trace=-1;if(trace<0)trace=TARGET_OS_IPHONE||getenv("VENGEANCE_TRACE_GUI")!=NULL;
 if(trace)fprintf(stderr,"TribesPad: game page %s%s\n",name[0]?name:"(none)",typing?" (typing)":"");
 return typing;
}
__attribute__((constructor)) static void install(void){ss_key_hook=key;ss_pointer_hook=pointer;ss_button_hook=button;ss_input_use_vengeance_pad();ss_play_state_hook=playing;ss_window_title="Tribes: Vengeance";}

/* Console commands TribesPad runs itself (Play with Bots, Bot Difficulty, the bot
   views): the game's own command handler, the viewport's Exec (UWindowsViewport::Exec,
   Engine.dll: what the console calls, so the player's exec functions, the cheat
   manager and the engine's SET and OPEN), called on the game thread at a presented
   frame. Unlike typed commands it is instant and the console never opens. The game's
   replies go to its log, which TribesPad's log copies for two seconds after each
   (vengeance_win32.c). The viewport is GUIController.ViewportOwner (Interaction,
   0x2c, after its flags at 0x28). VENGEANCE_COMMANDS names a file of
   "<frame> <command>" lines for scripted checks. */
#include <time.h>
/* port/core/guest_callback.h (not included: it reaches llasm_cpu.h by another path) */
extern uint32_t tribes_call_guest(_cpu *parent,uint32_t target,uint32_t count,const uint32_t *arguments,uint32_t stack_top);
extern double vengeance_echo_all_until;
static pthread_mutex_t command_lock=PTHREAD_MUTEX_INITIALIZER;
static char *commands[512];static unsigned command_head,command_tail;
static void queue_command(const char *line){
 pthread_mutex_lock(&command_lock);unsigned next=(command_tail+1)%512;
 if(next!=command_head){commands[command_tail]=strdup(line);command_tail=next;}
 else fprintf(stderr,"TribesPad command dropped (queue full): %s\n",line);
 pthread_mutex_unlock(&command_lock);
}
/* TRIBESai's bot classes (BaseBotsSTS.pkg, every class under BotAISettings): each
   copies its multiplayer aim spread and reaction time when it spawns, so the
   difficulty is set on each class (and on the bots already playing). */
static const char *const bot_classes[]={
 "BotAISettings","Buckler","CustomBots","Defender","DefenderMB","DefenderMI","DefenderMP","DefenderMedium",
 "Duelist","DuelistHB","DuelistHI","DuelistHP","DuelistHeavy","DuelistLB","DuelistLI","DuelistLP",
 "DuelistLight","DuelistMB","DuelistMI","DuelistMP","DuelistMedium","Glorax","Grenadier","GrenadierLB",
 "GrenadierLI","GrenadierLP","GrenadierLight","GrenadierMB","GrenadierMI","GrenadierMP","GrenadierMedium",
 "Grunt","GruntHB","GruntHI","GruntHP","GruntHeavy","GruntLB","GruntLI","GruntLP","GruntLight","GruntMB",
 "GruntMI","GruntMP","GruntMedium","HDB","HDI","HDP","HOB","HOI","HOP","Heavy","HeavyD","HeavyO","LDB","LDI",
 "LDP","LEB","LEI","LEP","LGB","LGI","LGP","LOB","LOI","LOP","LSB","LSI","LSP","Light","LightDefender",
 "LightEnergy","LightGrenadier","LightHunter","MBB","MBI","MBP","MDB","MDI","MDP","MOB","MOI","MOP","Medium",
 "MediumD","MediumO","PilotMD","PilotMI","PilotMP","PilotMedium","Protect","ProtectMB","ProtectMI",
 "ProtectMP","ProtectMedium","Rocketier","RocketierMB","RocketierMI","RocketierMP","RocketierMedium",
 "Sniper","Sniper2","SniperLB","SniperLI","SniperLP","Spammer","SpammerHB","SpammerHI","SpammerHP",
 "SpammerHeavy","pilot",
};
static void bot_skill(const char *skill){
 /* TRIBESai's own: 2 degrees of spread either way (snipers 1, the Glorax 0), 0.5 s to react. */
 double spread=2,sniper=1,glorax=0,react=0.5;
 if(!strcmp(skill,"easy")){spread=9;sniper=5;glorax=9;react=1.5;}
 else if(strcmp(skill,"hard")){spread=5;sniper=3;glorax=5;react=0.9;}
 char line[160];
 for(size_t i=0;i<sizeof bot_classes/sizeof bot_classes[0];i++){const char *c=bot_classes[i];
  double s=!strncmp(c,"Sniper",6)?sniper:!strcmp(c,"Glorax")?glorax:spread;
  snprintf(line,sizeof line,"set BaseBotsSTS.%s shotAngularDeviationMP %g",c,s);queue_command(line);
  snprintf(line,sizeof line,"set BaseBotsSTS.%s reactionDelayMP %g",c,react);queue_command(line);}
 snprintf(line,sizeof line,"set BaseAICharacter shotAngularDeviation %g",spread);queue_command(line);
 snprintf(line,sizeof line,"set BaseAICharacter reactionDelay %g",react);queue_command(line);
 fprintf(stderr,"TribesPad bots: difficulty %s: aim spread %g degrees (snipers %g), reaction %g s\n",skill,spread,sniper,react);
}
/* TribesPad's own commands, from the ··· menu and the Mac menu:
   "tribespad bots on <skill> [small|medium|full]"
                                the host joins Blood Eagle, the difficulty, then the
                                scripts each base's console runs to switch its bots on.
                                On the four bot maps a base's scripts are, in order: two
                                flag defenders; two repair gunners; a defender and a flag
                                grabber; two grabbers; a duelist and a spammer; a spammer and
                                a rocketeer; three vehicle bots. Small (6 a side) is the
                                defenders and grabbers, medium (10) adds the fighters, full
                                (15) is all seven. Every bot costs frames: they are the
                                game's own scripted AI, run through translated code.
   "tribespad botskill <skill>" the difficulty for the bots playing and those to come;
   "tribespad botlog on|off"    every bot's decisions (logTyrion) copied to TribesPad's log.
   SET and the map's scripts need the game's developer tools, on only meanwhile. */
extern int vengeance_echo_all;
void vengeance_run_command(const char *line){
 if(!line||!*line)return;
 if(strncmp(line,"tribespad ",10)){queue_command(line);return;}
 char what[16]="",arg[16]="",more[16]="",size[16]="";sscanf(line+10,"%15s %15s %15s %15s",what,arg,more,size);
 if(!strcmp(what,"bots")){
  queue_command("changeteam 0");queue_command("set GameEngine EnableDevTools True");bot_skill(more[0]?more:"normal");
  static const int small[]={1,3,4},medium[]={1,3,4,5,6},full[]={1,2,3,4,5,6,7};
  const int *chosen=full;size_t count=7;
  if(!strcmp(size,"small")){chosen=small;count=3;}else if(!strcmp(size,"medium")){chosen=medium;count=5;}
  fprintf(stderr,"TribesPad bots: %s game, %zu of each base's 7 bot scripts\n",size[0]?size:"full",count);
  char script[48];for(int team=1;team<=2;team++)for(size_t i=0;i<count;i++){snprintf(script,sizeof script,"runscript %02dConfig%02dScript",team,chosen[i]);queue_command(script);}
  queue_command("set GameEngine EnableDevTools False");
 } else if(!strcmp(what,"botskill")){
  queue_command("set GameEngine EnableDevTools True");bot_skill(arg);queue_command("set GameEngine EnableDevTools False");
 } else if(!strcmp(what,"botlog")){
  int on=!strcmp(arg,"on");if(on!=vengeance_echo_all){vengeance_echo_all=on;queue_command("logTyrion");}
  fprintf(stderr,"TribesPad bots: decisions log %s\n",on?"on":"off");
 } else fprintf(stderr,"TribesPad command not known: %s\n",line);
}
static void scripted_commands(void){
 static FILE *f;static int opened;static unsigned long long due;static char pending[1024];
 if(!opened){opened=1;const char *path=getenv("VENGEANCE_COMMANDS");if(path&&!(f=fopen(path,"r")))fprintf(stderr,"TribesPad: VENGEANCE_COMMANDS %s unreadable\n",path);}
 while(f){
  if(!pending[0]){char line[1100];if(!fgets(line,sizeof line,f)){fclose(f);f=NULL;return;}
   int at=0;if(sscanf(line,"%llu %n",&due,&at)!=1||!at)continue;
   snprintf(pending,sizeof pending,"%s",line+at);pending[strcspn(pending,"\r\n")]=0;}
  if(ss_frames()<due)return;
  vengeance_run_command(pending);pending[0]=0;
 }
}
static void run_commands(_cpu *cpu){
 scripted_commands();
 static uint32_t exec,glog;
 if(!exec){exec=vengeance_export("?Exec@UWindowsViewport@@UAEHPBGAAVFOutputDevice@@@Z");glog=vengeance_export("?GLog@@3PAVFOutputDevice@@A");}
 if(command_head==command_tail)return;
 static int told;if(!told){told=1;fprintf(stderr,"TribesPad command runner: game stack at 0x%x (%p to %p)\n",cpu->_esp,cpu->stack_bottom,cpu->stack_top);}
 /* At most about 6 ms a frame: a difficulty change is 220 commands. */
 struct timespec begun;clock_gettime(CLOCK_MONOTONIC,&begun);
 for(int n=0;n<128;n++){
  struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
  if(n&&(t.tv_sec-begun.tv_sec)*1000.0+(t.tv_nsec-begun.tv_nsec)/1e6>6)return;
  pthread_mutex_lock(&command_lock);char *line=command_head!=command_tail?commands[command_head]:NULL;
  if(line)command_head=(command_head+1)%512;pthread_mutex_unlock(&command_lock);
  if(!line)return;
  uint64_t base=vengeance_guest_base();uint32_t g=vengeance_gui_controller(),viewport=0,device=0;char kind[64]="";
  if(!exec||!glog||!g||!peek(base,g+0x2c,&viewport,4)||!class_name(base,viewport,kind,sizeof kind)||strcmp(kind,"WindowsViewport")||
     !peek(base,glog,&device,4)||!device){
   fprintf(stderr,"TribesPad command not run (no game viewport yet%s%s): %s\n",kind[0]?"; found ":"",kind,line);free(line);continue;}
  /* Exec is FExec's virtual (UPlayer derives from UObject and FExec): MSVC passes
     it the FExec part of the object. That part is the vtable slot holding Exec. */
  static uint32_t adjust=UINT32_MAX;
  if(adjust==UINT32_MAX){
   for(uint32_t off=0;off<0x100&&adjust==UINT32_MAX;off+=4){uint32_t table=0,entries[16];
    if(!peek(base,viewport+off,&table,4)||!peek(base,table,entries,sizeof entries))continue;
    for(int i=0;i<16;i++)if(entries[i]==exec){adjust=off;break;}}
   fprintf(stderr,"TribesPad command runner: Exec in the viewport's vtable at +0x%x\n",adjust);
  }
  if(adjust==UINT32_MAX){fprintf(stderr,"TribesPad command not run (Exec not found): %s\n",line);free(line);continue;}
  size_t len=strlen(line);if(len>900)len=900;
  uint32_t text=(cpu->_esp-2048)&~15u;uint16_t *wide=GUEST(base,text);
  for(size_t i=0;i<len;i++)wide[i]=(unsigned char)line[i];
  wide[len]=0;
  struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);vengeance_echo_all_until=now.tv_sec+now.tv_nsec/1e9+2;
  uint32_t saved=cpu->_ecx;cpu->_ecx=viewport+adjust;uint32_t arguments[2]={text,device};
  clock_gettime(CLOCK_MONOTONIC,&now);double started=now.tv_sec+now.tv_nsec/1e9;
  uint32_t handled=tribes_call_guest(cpu,exec,2,arguments,text-64);
  /* The game writes its log in 4 KB pieces: flush it so the replies show now. Flush
     is FOutputDevice's virtual; its slot is found in FOutputDevice's own vtable. */
  static int flush_slot=-2;
  if(flush_slot==-2){flush_slot=-1;uint32_t table=vengeance_export("??_7FOutputDevice@@6B@"),base_flush=vengeance_export("?Flush@FOutputDevice@@UAEXXZ"),entries[8];
   if(table&&base_flush&&peek(base,table,entries,sizeof entries))for(int i=0;i<8;i++)if(entries[i]==base_flush){flush_slot=i;break;}}
  uint32_t log_table=0,flush=0;
  if(flush_slot>=0&&peek(base,device,&log_table,4)&&peek(base,log_table+4u*(uint32_t)flush_slot,&flush,4)&&flush){
   cpu->_ecx=device;tribes_call_guest(cpu,flush,0,NULL,text-64);}
  cpu->_ecx=saved;
  clock_gettime(CLOCK_MONOTONIC,&now);
  fprintf(stderr,"TribesPad command: %s (%s, %.0f ms)\n",line,handled?"done":"not recognised",(now.tv_sec+now.tv_nsec/1e9-started)*1000);free(line);
 }
}

/* PeekMessageW (config/vengeance/bindings.json): the top of the game's frame, where
   its window messages (and so the console's keys) are handled, its stack is
   shallow, and no level is mid-update: TribesPad's commands run here. */
extern uint32_t tribes_peek_message(uint64_t base,uint32_t message,uint32_t window,uint32_t low,uint32_t high,uint32_t remove);
uint32_t vg_peek_message_w(_cpu *cpu,uint32_t message,uint32_t window,uint32_t low,uint32_t high,uint32_t remove){
 run_commands(cpu);
 return tribes_peek_message(cpu->_pointer_offset,message,window,low,high,remove);
}

#if TARGET_OS_IPHONE
/* TribesPad's overlay in the app (port/apple/mobile_controls.m): frames for its
   FPS counter, and whether the game is in play (no menu up). */
extern void tb_app_note_frame(void);
extern void tb_vengeance_report_state(int playing);
extern void tb_vengeance_report_typing(int typing);
extern void tb_vengeance_set_typer(void (*typer)(const char *line,int open_with_tab));
/* The command runner above, handed to the app (only the engine's entry point is
   visible outside it on iOS): the ··· menu's commands go through it. */
static void app_command(const char *line,int open_with_tab){(void)open_with_tab;vengeance_run_command(line);}
#endif
int vengeance_text_focused(void);
/* Once per presented frame, on the game's thread. */
void vengeance_input_frame(_cpu *cpu){
 ss_count_frame();ss_input_pump(cpu);
#if TARGET_OS_IPHONE
 tb_app_note_frame();
 static int handed;if(!handed){handed=1;tb_vengeance_set_typer(app_command);}
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

