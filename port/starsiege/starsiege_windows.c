/* Win32 top-level windows for Starsiege: Tribes (goal loop G2).
 * Tribes 1 draws into its own Win32 window through WGL, unlike Tribes 2, whose
 * community client moved windowing onto SDL. This is the small USER subset it
 * uses: creation with WH_CBT hooks (the game binds its C++ window object and
 * subclasses the window in HCBT_CREATEWND), window procedures and subclassing,
 * geometry, focus and painting. Class registration, the window catalog and
 * the posted-message queue are TribesPad's own (window_classes.c,
 * guest_window_catalog.c, guest_user_messages.c). Calls about Tribes 2's
 * hidden helper windows go to hidden_windows.c unchanged.
 * Unknown messages and modes stop loudly; there are no success stubs. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/guest_callback.h"
#include "../core/guest_memory_checks.h"
#include "../core/guest_window_catalog.h"
#include "../core/hidden_windows.h"
#include "../core/window_classes.h"
#include "../core/guest_display.h"

extern uint32_t tribes_set_last_error(uint32_t value);
extern void *tribes_lookup(uint32_t address);
extern int tribes_teb_get_ids(uint64_t base,uint32_t *pid,uint32_t *tid);

#define WS_POPUP 0x80000000u
#define WS_CHILD 0x40000000u
#define WS_VISIBLE 0x10000000u
#define WS_DISABLED 0x08000000u
#define WS_CAPTION 0x00C00000u
#define WS_BORDER 0x00800000u
#define WS_DLGFRAME 0x00400000u
#define WS_THICKFRAME 0x00040000u
#define WS_EX_DLGMODALFRAME 0x1u
#define CW_USEDEFAULT 0x80000000u

static int32_t cursor_x,cursor_y,cursor_count;
typedef struct Window {
 uint64_t base;uint32_t handle,atom,class_proc,proc,style,exstyle,instance,parent,id,userdata;
 int32_t x,y,width,height;int visible,destroying;unsigned char *extra;uint32_t extra_bytes;char title[256];
 struct Window *next;
} Window;
static Window *windows;
static uint32_t next_handle=0xc3000001;
static uint32_t focus,foreground,capture;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
/* WH_CBT is the one hook kind Starsiege installs; one per thread is enough. */
static struct {uint32_t handle,proc,tid;} cbt;

static Window *find(uint64_t base,uint32_t handle){for(Window *w=windows;w;w=w->next)if(w->base==base&&w->handle==handle)return w;return NULL;}
uint32_t ss_destroy_window(_cpu *cpu,uint32_t handle);
static _Noreturn void unsupported(const char *what,uint32_t value){fprintf(stderr,"TribesPad: unsupported Starsiege window operation: %s 0x%x\n",what,value);abort();}
static void *guest(uint64_t base,uint32_t address,uint32_t bytes,uint32_t access,uint32_t *token){
 if(!address||(uint64_t)address+bytes>UINT64_C(0x100000000))unsupported("guest pointer",address);
 void *p=(void *)(uintptr_t)(base+address);*token=tribes_memory_access_begin(p,bytes,access);return p;
}
static void frame(uint32_t style,uint32_t exstyle,int32_t *dx,int32_t *dy,int32_t *caption){
 int32_t border=0;
 if(style&WS_THICKFRAME)border=4;
 else if((style&WS_DLGFRAME)||(exstyle&WS_EX_DLGMODALFRAME))border=3;
 else if(style&WS_BORDER)border=1;
 *dx=border;*dy=border;*caption=(style&WS_CAPTION)==WS_CAPTION?19:0;
}
/* Guest callbacks run below this host frame's scratch space on the guest stack. */
static uint32_t call_proc(_cpu *cpu,uint32_t proc,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp,uint32_t stack_top){
 uint32_t args[]={handle,message,wp,lp};(void)tribes_lookup(proc);
 return tribes_call_guest(cpu,proc,4,args,stack_top);
}
static uint32_t send(_cpu *cpu,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp){
 pthread_mutex_lock(&lock);Window *w=find(cpu->_pointer_offset,handle);uint32_t proc=w?w->proc:0;pthread_mutex_unlock(&lock);
 if(!proc)return 0;
 return call_proc(cpu,proc,handle,message,wp,lp,cpu->_esp-256);
}
static void move_messages(_cpu *cpu,Window *snapshot){
 int32_t dx,dy,cap;frame(snapshot->style,snapshot->exstyle,&dx,&dy,&cap);
 uint32_t cw=(uint32_t)(snapshot->width-2*dx),ch=(uint32_t)(snapshot->height-2*dy-cap);
 send(cpu,snapshot->handle,0x0005,0,(ch<<16)|(cw&0xffff));                                    /* WM_SIZE */
 send(cpu,snapshot->handle,0x0003,0,((uint32_t)(snapshot->y+dy+cap)<<16)|((uint32_t)(snapshot->x+dx)&0xffff)); /* WM_MOVE */
}
static void activate(_cpu *cpu,uint32_t handle){
 uint64_t base=cpu->_pointer_offset;
 pthread_mutex_lock(&lock);Window *w=find(base,handle);uint32_t style=w?w->style:0;int changed=w&&foreground!=handle;if(w){foreground=handle;focus=handle;}pthread_mutex_unlock(&lock);
 if(!changed)return;
 tribes_window_update(base,handle,style,1);
 uint32_t pid,tid=0;(void)tribes_teb_get_ids(base,&pid,&tid);
 send(cpu,handle,0x001C,1,tid);  /* WM_ACTIVATEAPP */
 send(cpu,handle,0x0086,1,0);    /* WM_NCACTIVATE */
 send(cpu,handle,0x0006,1,0);    /* WM_ACTIVATE: WA_ACTIVE */
 send(cpu,handle,0x0007,0,0);    /* WM_SETFOCUS */
}

/* HWND CreateWindowExA(...). The all-zero geometry form with no styles is
   Tribes 2's hidden-window contract and stays with hidden_windows.c. */
uint32_t ss_create_window_ex_a(_cpu *cpu,uint32_t exstyle,uint32_t class_name,uint32_t title,uint32_t style,uint32_t x,uint32_t y,uint32_t width,uint32_t height,uint32_t parent,uint32_t menu,uint32_t instance,uint32_t parameter){
 uint64_t base=cpu->_pointer_offset;
 if(!exstyle&&!style&&!x&&!y&&!width&&!height&&!parent&&!menu)return tribes_create_hidden_window(cpu,exstyle,class_name,title,style,x,y,width,height,parent,menu,instance,parameter);
 if(style&WS_CHILD)unsupported("child window style",style);
 if(exstyle&~(uint32_t)(0x1|0x8|0x40000|0x100|0x200))unsupported("extended window style",exstyle);
 uint32_t fields[10];uint32_t atom=tribes_class_acquire(base,class_name,instance,fields);
 if(!atom){tribes_set_last_error(1407);return 0;}
 if(x==CW_USEDEFAULT){x=0;y=0;}
 if(width==CW_USEDEFAULT){width=640;height=480;}
 Window *w=calloc(1,sizeof *w);if(!w){tribes_class_release(base,atom);tribes_set_last_error(8);return 0;}
 if(fields[3]){w->extra=calloc(1,fields[3]);w->extra_bytes=fields[3];if(!w->extra){free(w);tribes_class_release(base,atom);tribes_set_last_error(8);return 0;}}
 w->base=base;w->atom=atom;w->class_proc=w->proc=fields[1];w->style=style&~WS_VISIBLE;w->exstyle=exstyle;w->instance=instance?instance:fields[4];
 w->parent=parent;w->id=menu;w->x=(int32_t)x;w->y=(int32_t)y;w->width=(int32_t)width;w->height=(int32_t)height;
 pthread_mutex_lock(&lock);w->handle=next_handle++;w->next=windows;windows=w;uint32_t handle=w->handle;pthread_mutex_unlock(&lock);
 if(!tribes_window_add(base,handle,w->instance,w->style))unsupported("window catalog",handle);
 uint32_t cs=cpu->_esp-160,cbt_create=cpu->_esp-176,rect=cpu->_esp-200,token;
 uint32_t create[]={parameter,instance,menu,parent,height,width,y,x,style,title,class_name,exstyle};
 memcpy(guest(base,cs,48,2,&token),create,48);tribes_memory_access_end(token);
 uint32_t pair[]={cs,0};memcpy(guest(base,cbt_create,8,2,&token),pair,8);tribes_memory_access_end(token);
 pthread_mutex_lock(&lock);uint32_t hook=cbt.proc;pthread_mutex_unlock(&lock);
 if(hook){fprintf(stderr,"TribesPad: CBT hook HCBT_CREATEWND window=0x%x\n",handle);
  uint32_t args[]={3,handle,cbt_create};(void)tribes_lookup(hook);
  if(tribes_call_guest(cpu,hook,3,args,cpu->_esp-256)){tribes_set_last_error(1400);goto failed;}}
 if(!send(cpu,handle,0x0081,0,cs))goto failed;                                                  /* WM_NCCREATE */
 int32_t r[4]={(int32_t)x,(int32_t)y,(int32_t)(x+width),(int32_t)(y+height)};
 memcpy(guest(base,rect,16,2,&token),r,16);tribes_memory_access_end(token);
 send(cpu,handle,0x0083,0,rect);                                                                /* WM_NCCALCSIZE */
 if(send(cpu,handle,0x0001,0,cs)==UINT32_MAX)goto failed;                                      /* WM_CREATE */
 fprintf(stderr,"TribesPad: Starsiege window 0x%x %dx%d at %d,%d style 0x%x \"%s\"\n",handle,(int)width,(int)height,(int)x,(int)y,style,w->title);
 if(style&WS_VISIBLE){
  pthread_mutex_lock(&lock);w=find(base,handle);if(w){w->style|=WS_VISIBLE;w->visible=1;}Window snapshot=w?*w:(Window){0};pthread_mutex_unlock(&lock);
  if(!snapshot.handle)return 0;
  send(cpu,handle,0x0018,1,0);move_messages(cpu,&snapshot);activate(cpu,handle);
 }
 return handle;
failed:
 send(cpu,handle,0x0082,0,0);
 pthread_mutex_lock(&lock);Window **p=&windows;while(*p&&(*p)->handle!=handle)p=&(*p)->next;if(*p){Window *gone=*p;*p=gone->next;tribes_class_release(base,gone->atom);free(gone->extra);free(gone);}pthread_mutex_unlock(&lock);
 tribes_window_remove(base,handle);return 0;
}

/* LRESULT DefWindowProcA. Each handled message has its documented default. */
uint32_t ss_def_window_proc_a(_cpu *cpu,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp){
 uint64_t base=cpu->_pointer_offset;
 pthread_mutex_lock(&lock);Window *w=find(base,handle);int ours=w!=NULL;pthread_mutex_unlock(&lock);
 if(!ours)return tribes_default_window_proc(base,handle,message,wp,lp);
 switch(message){
 case 0x0081:{ /* WM_NCCREATE: keep the title */
  uint32_t token,name;memcpy(&name,guest(base,lp+36,4,1,&token),4);tribes_memory_access_end(token);
  if(name){const char *text=guest(base,name,1,1,&token);tribes_memory_access_end(token);pthread_mutex_lock(&lock);w=find(base,handle);if(w){strncpy(w->title,text,255);w->title[255]=0;}pthread_mutex_unlock(&lock);}
  return 1;}
 case 0x000C:{ /* WM_SETTEXT */
  const char *text="";uint32_t token=0;if(lp){text=guest(base,lp,1,1,&token);tribes_memory_access_end(token);}
  pthread_mutex_lock(&lock);w=find(base,handle);if(w){strncpy(w->title,text,255);w->title[255]=0;}pthread_mutex_unlock(&lock);return 1;}
 case 0x0010:return ss_destroy_window(cpu,handle),0;            /* WM_CLOSE */
 case 0x0014:return 1;                                          /* WM_ERASEBKGND */
 case 0x0084:return 1;                                          /* WM_NCHITTEST: HTCLIENT */
 case 0x0086:return 1;                                          /* WM_NCACTIVATE */
 case 0x0021:return 1;                                          /* WM_MOUSEACTIVATE: MA_ACTIVATE */
 case 0x0047:{                                                  /* WM_WINDOWPOSCHANGED */
  pthread_mutex_lock(&lock);w=find(base,handle);Window snapshot=w?*w:(Window){0};pthread_mutex_unlock(&lock);
  if(snapshot.handle)move_messages(cpu,&snapshot);return 0;}
 case 0x0112:if((wp&0xfff0)==0xF060)send(cpu,handle,0x0010,0,0);return 0; /* WM_SYSCOMMAND: SC_CLOSE */
 case 0x0001:case 0x0002:case 0x0003:case 0x0005:case 0x0006:case 0x0007:case 0x0008:case 0x000F:
 case 0x0018:case 0x001C:case 0x0020:case 0x0024:case 0x0046:case 0x0082:case 0x0083:
 case 0x0100:case 0x0101:case 0x0102:case 0x0104:case 0x0105:case 0x0106:
 case 0x0200:case 0x0201:case 0x0202:case 0x0203:case 0x0204:case 0x0205:case 0x0206:case 0x0207:case 0x0208:case 0x020A:
  return 0; /* WM_CREATE..WM_SETCURSOR, keyboard and mouse: nothing further by default */
 default:unsupported("default window message",message);
 }
}
uint32_t ss_destroy_window(_cpu *cpu,uint32_t handle){
 uint64_t base=cpu->_pointer_offset;
 pthread_mutex_lock(&lock);Window *w=find(base,handle);int ours=w!=NULL;if(w&&w->destroying){pthread_mutex_unlock(&lock);return 0;}if(w)w->destroying=1;pthread_mutex_unlock(&lock);
 if(!ours)return tribes_destroy_hidden_window(cpu,handle);
 send(cpu,handle,0x0002,0,0);send(cpu,handle,0x0082,0,0);
 pthread_mutex_lock(&lock);Window **p=&windows;while(*p&&((*p)->base!=base||(*p)->handle!=handle))p=&(*p)->next;
 if(*p){w=*p;*p=w->next;tribes_class_release(base,w->atom);free(w->extra);free(w);}
 if(focus==handle)focus=0;if(foreground==handle)foreground=0;if(capture==handle)capture=0;pthread_mutex_unlock(&lock);
 tribes_window_remove(base,handle);return 1;
}
uint32_t ss_dispatch_message_a(_cpu *cpu,uint32_t address){
 extern uint32_t tribes_dispatch_message(_cpu *cpu,uint32_t address);
 uint64_t base=cpu->_pointer_offset;uint32_t token,m[7];memcpy(m,guest(base,address,28,1,&token),28);tribes_memory_access_end(token);
 pthread_mutex_lock(&lock);Window *w=m[1]!=0x0113?find(base,m[0]):NULL;uint32_t proc=w?w->proc:0;pthread_mutex_unlock(&lock);
 if(!proc)return tribes_dispatch_message(cpu,address);
 return call_proc(cpu,proc,m[0],m[1],m[2],m[3],cpu->_esp-64);
}
uint32_t ss_send_message_a(_cpu *cpu,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp){
 pthread_mutex_lock(&lock);int ours=find(cpu->_pointer_offset,handle)!=NULL;pthread_mutex_unlock(&lock);
 if(!ours){
  if(tribes_is_window(cpu->_pointer_offset,handle))return tribes_dispatch_hidden_message(cpu,handle,message,wp,lp);
  tribes_set_last_error(1400);return 0;
 }
 return send(cpu,handle,message,wp,lp);
}
uint32_t ss_call_window_proc_a(_cpu *cpu,uint32_t proc,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp){
 if(!proc)return 0;return call_proc(cpu,proc,handle,message,wp,lp,cpu->_esp-64);
}

/* GetWindowLongA/SetWindowLongA: GWL_WNDPROC -4, GWL_HINSTANCE -6, GWL_ID -12,
   GWL_STYLE -16, GWL_EXSTYLE -20, GWL_USERDATA -21, and class extra bytes. */
uint32_t ss_get_window_long_a(uint64_t base,uint32_t handle,uint32_t index){
 pthread_mutex_lock(&lock);Window *w=find(base,handle);
 if(!w){pthread_mutex_unlock(&lock);return tribes_get_window_long_a(base,handle,index);}
 uint32_t value;
 switch((int32_t)index){
 case -4:value=w->proc;break;case -6:value=w->instance;break;case -12:value=w->id;break;
 case -16:value=w->style;break;case -20:value=w->exstyle;break;case -21:value=w->userdata;break;
 default:if((int32_t)index<0||index+4>w->extra_bytes){pthread_mutex_unlock(&lock);tribes_set_last_error(1413);return 0;}memcpy(&value,w->extra+index,4);
 }
 pthread_mutex_unlock(&lock);return value;
}
uint32_t ss_set_window_long_a(uint64_t base,uint32_t handle,uint32_t index,uint32_t value){
 if((int32_t)index==-4&&value)(void)tribes_lookup(value);
 pthread_mutex_lock(&lock);Window *w=find(base,handle);
 if(!w){pthread_mutex_unlock(&lock);unsupported("SetWindowLongA on a hidden window",handle);}
 uint32_t old;
 switch((int32_t)index){
 case -4:old=w->proc;w->proc=value;break;case -12:old=w->id;w->id=value;break;
 case -16:old=w->style;w->style=value;break;case -20:old=w->exstyle;w->exstyle=value;break;case -21:old=w->userdata;w->userdata=value;break;
 default:if((int32_t)index<0||index+4>w->extra_bytes){pthread_mutex_unlock(&lock);tribes_set_last_error(1413);return 0;}memcpy(&old,w->extra+index,4);memcpy(w->extra+index,&value,4);
 }
 pthread_mutex_unlock(&lock);tribes_set_last_error(0);return old;
}
uint32_t ss_is_window(uint64_t base,uint32_t handle){
 pthread_mutex_lock(&lock);int ours=find(base,handle)!=NULL;pthread_mutex_unlock(&lock);return ours||tribes_is_window(base,handle);
}

/* Hooks: WH_CBT only, on the calling thread. */
uint32_t ss_set_windows_hook_ex_a(uint64_t base,uint32_t kind,uint32_t proc,uint32_t module,uint32_t tid){
 (void)module;uint32_t pid,self=0;(void)tribes_teb_get_ids(base,&pid,&self);
 if(kind!=5||tid!=self||!proc)unsupported("SetWindowsHookExA kind/thread",kind);
 (void)tribes_lookup(proc);pthread_mutex_lock(&lock);
 if(cbt.proc){pthread_mutex_unlock(&lock);unsupported("second WH_CBT hook",proc);}
 cbt.proc=proc;cbt.tid=tid;cbt.handle=0xc6000001;pthread_mutex_unlock(&lock);return cbt.handle;
}
uint32_t ss_unhook_windows_hook_ex(uint32_t handle){
 pthread_mutex_lock(&lock);uint32_t ok=handle&&handle==cbt.handle;if(ok)cbt.proc=cbt.handle=0;pthread_mutex_unlock(&lock);
 if(!ok)tribes_set_last_error(1404);return ok;
}
/* No other hooks are installed, so the chain ends here (0: allow). */
uint32_t ss_call_next_hook_ex(uint32_t hook,uint32_t code,uint32_t wp,uint32_t lp){(void)hook;(void)code;(void)wp;(void)lp;return 0;}

/* Geometry and visibility. Screen coordinates are the guest desktop's. */
uint32_t ss_show_window(_cpu *cpu,uint32_t handle,uint32_t command){
 uint64_t base=cpu->_pointer_offset;
 pthread_mutex_lock(&lock);Window *w=find(base,handle);if(!w){pthread_mutex_unlock(&lock);unsupported("ShowWindow on a hidden window",handle);}
 int was=w->visible,show=command!=0;fprintf(stderr,"TribesPad: ShowWindow 0x%x command %u\n",handle,command);if(command==2||command==6||command==7||command==11)unsupported("ShowWindow minimize",command);
 w->visible=show;if(show)w->style|=WS_VISIBLE;else w->style&=~WS_VISIBLE;Window snapshot=*w;pthread_mutex_unlock(&lock);
 if(was!=show){send(cpu,handle,0x0018,(uint32_t)show,0);if(show){move_messages(cpu,&snapshot);activate(cpu,handle);}}
 return (uint32_t)was;
}
uint32_t ss_update_window(_cpu *cpu,uint32_t handle){
 pthread_mutex_lock(&lock);int ours=find(cpu->_pointer_offset,handle)!=NULL;pthread_mutex_unlock(&lock);
 if(!ours){tribes_set_last_error(1400);return 0;}send(cpu,handle,0x000F,0,0);return 1;
}
static void write_rect(uint64_t base,uint32_t address,int32_t l,int32_t t,int32_t r,int32_t b){
 uint32_t token;int32_t v[4]={l,t,r,b};memcpy(guest(base,address,16,2,&token),v,16);tribes_memory_access_end(token);
}
uint32_t ss_get_client_rect(uint64_t base,uint32_t handle,uint32_t output){
 pthread_mutex_lock(&lock);Window *w=find(base,handle);Window s=w?*w:(Window){0};pthread_mutex_unlock(&lock);
 if(!s.handle){if(tribes_is_desktop_window(base,handle)){write_rect(base,output,0,0,(int32_t)tribes_get_system_metrics(base,0),(int32_t)tribes_get_system_metrics(base,1));return 1;}tribes_set_last_error(1400);return 0;}
 int32_t dx,dy,cap;frame(s.style,s.exstyle,&dx,&dy,&cap);write_rect(base,output,0,0,s.width-2*dx,s.height-2*dy-cap);return 1;
}
uint32_t ss_get_window_rect(uint64_t base,uint32_t handle,uint32_t output){
 pthread_mutex_lock(&lock);Window *w=find(base,handle);Window s=w?*w:(Window){0};pthread_mutex_unlock(&lock);
 if(!s.handle){if(tribes_is_desktop_window(base,handle)){write_rect(base,output,0,0,(int32_t)tribes_get_system_metrics(base,0),(int32_t)tribes_get_system_metrics(base,1));return 1;}tribes_set_last_error(1400);return 0;}
 write_rect(base,output,s.x,s.y,s.x+s.width,s.y+s.height);return 1;
}
/* BOOL AdjustWindowRect(Ex)(LPRECT, DWORD style, BOOL menu[, DWORD exstyle]). */
uint32_t ss_adjust_window_rect_ex(uint64_t base,uint32_t address,uint32_t style,uint32_t menu,uint32_t exstyle){
 if(menu)unsupported("AdjustWindowRect with a menu",menu);
 uint32_t token;int32_t r[4];memcpy(r,guest(base,address,16,3,&token),16);
 int32_t dx,dy,cap;frame(style,exstyle,&dx,&dy,&cap);r[0]-=dx;r[1]-=dy+cap;r[2]+=dx;r[3]+=dy;
 memcpy((void *)(uintptr_t)(base+address),r,16);tribes_memory_access_end(token);return 1;
}
uint32_t ss_adjust_window_rect(uint64_t base,uint32_t address,uint32_t style,uint32_t menu){return ss_adjust_window_rect_ex(base,address,style,menu,0);}
static uint32_t place(_cpu *cpu,uint32_t handle,int32_t x,int32_t y,int32_t width,int32_t height,uint32_t flags){
 uint64_t base=cpu->_pointer_offset;
 pthread_mutex_lock(&lock);Window *w=find(base,handle);if(!w){pthread_mutex_unlock(&lock);tribes_set_last_error(1400);return 0;}
 if(!(flags&0x2)){w->x=x;w->y=y;}          /* SWP_NOMOVE */
 if(!(flags&0x1)){w->width=width;w->height=height;} /* SWP_NOSIZE */
 if(flags&0x40){w->visible=1;w->style|=WS_VISIBLE;} /* SWP_SHOWWINDOW */
 if(flags&0x80){w->visible=0;w->style&=~WS_VISIBLE;} /* SWP_HIDEWINDOW */
 Window snapshot=*w;pthread_mutex_unlock(&lock);
 move_messages(cpu,&snapshot);
 if(!(flags&0x10)&&snapshot.visible)activate(cpu,handle); /* not SWP_NOACTIVATE */
 return 1;
}
uint32_t ss_set_window_pos(_cpu *cpu,uint32_t handle,uint32_t after,uint32_t x,uint32_t y,uint32_t width,uint32_t height,uint32_t flags){
 (void)after;fprintf(stderr,"TribesPad: SetWindowPos 0x%x %d,%d %ux%u flags 0x%x\n",handle,(int)x,(int)y,width,height,flags);if(flags&~(uint32_t)(0x1|0x2|0x4|0x10|0x20|0x40|0x80|0x200|0x400))unsupported("SetWindowPos flags",flags);
 return place(cpu,handle,(int32_t)x,(int32_t)y,(int32_t)width,(int32_t)height,flags);
}
uint32_t ss_move_window(_cpu *cpu,uint32_t handle,uint32_t x,uint32_t y,uint32_t width,uint32_t height,uint32_t repaint){
 (void)repaint;return place(cpu,handle,(int32_t)x,(int32_t)y,(int32_t)width,(int32_t)height,0x10);
}
static uint32_t translate(uint64_t base,uint32_t handle,uint32_t address,int sign){
 pthread_mutex_lock(&lock);Window *w=find(base,handle);Window s=w?*w:(Window){0};pthread_mutex_unlock(&lock);
 if(!s.handle){tribes_set_last_error(1400);return 0;}
 int32_t dx,dy,cap;frame(s.style,s.exstyle,&dx,&dy,&cap);
 uint32_t token;int32_t p[2];memcpy(p,guest(base,address,8,3,&token),8);p[0]+=sign*(s.x+dx);p[1]+=sign*(s.y+dy+cap);
 memcpy((void *)(uintptr_t)(base+address),p,8);tribes_memory_access_end(token);return 1;
}
uint32_t ss_client_to_screen(uint64_t base,uint32_t handle,uint32_t point){return translate(base,handle,point,1);}
uint32_t ss_screen_to_client(uint64_t base,uint32_t handle,uint32_t point){return translate(base,handle,point,-1);}
uint32_t ss_set_focus(_cpu *cpu,uint32_t handle){
 pthread_mutex_lock(&lock);uint32_t old=focus;int ours=!handle||find(cpu->_pointer_offset,handle);pthread_mutex_unlock(&lock);
 if(!ours){tribes_set_last_error(1400);return 0;}
 if(handle&&handle!=old){pthread_mutex_lock(&lock);focus=handle;pthread_mutex_unlock(&lock);if(old)send(cpu,old,0x0008,handle,0);send(cpu,handle,0x0007,old,0);}
 return old;
}
uint32_t ss_set_foreground_window(_cpu *cpu,uint32_t handle){
 pthread_mutex_lock(&lock);int ours=find(cpu->_pointer_offset,handle)!=NULL;pthread_mutex_unlock(&lock);
 if(!ours)return 0;activate(cpu,handle);return 1;
}
uint32_t ss_get_foreground_window(void){pthread_mutex_lock(&lock);uint32_t r=foreground;pthread_mutex_unlock(&lock);return r;}
uint32_t ss_get_focus(void){pthread_mutex_lock(&lock);uint32_t r=focus;pthread_mutex_unlock(&lock);return r;}
uint32_t ss_is_iconic(uint32_t handle){(void)handle;return 0;}
uint32_t ss_set_capture(uint32_t handle){pthread_mutex_lock(&lock);uint32_t old=capture;capture=handle;pthread_mutex_unlock(&lock);return old;}
uint32_t ss_release_capture(void){pthread_mutex_lock(&lock);capture=0;pthread_mutex_unlock(&lock);return 1;}
uint32_t ss_get_parent(uint64_t base,uint32_t handle){pthread_mutex_lock(&lock);Window *w=find(base,handle);uint32_t r=w?w->parent:0;pthread_mutex_unlock(&lock);return r;}
uint32_t ss_set_window_text_a(_cpu *cpu,uint32_t handle,uint32_t text){return send(cpu,handle,0x000C,0,text);}
uint32_t ss_get_window_text_a(uint64_t base,uint32_t handle,uint32_t output,uint32_t capacity){
 pthread_mutex_lock(&lock);Window *w=find(base,handle);char title[256];if(w)strcpy(title,w->title);pthread_mutex_unlock(&lock);
 if(!w||!capacity)return 0;uint32_t n=(uint32_t)strlen(title);if(n>=capacity)n=capacity-1;
 uint32_t token;char *p=guest(base,output,n+1,2,&token);memcpy(p,title,n);p[n]=0;tribes_memory_access_end(token);return n;
}
uint32_t ss_get_window_text_length_a(uint64_t base,uint32_t handle){
 pthread_mutex_lock(&lock);Window *w=find(base,handle);uint32_t n=w?(uint32_t)strlen(w->title):0;pthread_mutex_unlock(&lock);return n;
}
/* Painting: the frame comes from GL, so there is nothing to invalidate. */
uint32_t ss_invalidate_rect(uint32_t handle,uint32_t rect,uint32_t erase){(void)handle;(void)rect;(void)erase;return 1;}
uint32_t ss_validate_rect(uint32_t handle,uint32_t rect){(void)handle;(void)rect;return 1;}
uint32_t ss_enable_window(uint64_t base,uint32_t handle,uint32_t enable){
 pthread_mutex_lock(&lock);Window *w=find(base,handle);uint32_t was=0;if(w){was=!!(w->style&WS_DISABLED);if(enable)w->style&=~WS_DISABLED;else w->style|=WS_DISABLED;}pthread_mutex_unlock(&lock);return was;
}
/* For the host: the visible game window, if any. */
uint32_t ss_window_client_size(uint64_t base,uint32_t handle,int32_t *width,int32_t *height){
 pthread_mutex_lock(&lock);Window *w=find(base,handle);Window s=w?*w:(Window){0};pthread_mutex_unlock(&lock);
 if(!s.handle)return 0;int32_t dx,dy,cap;frame(s.style,s.exstyle,&dx,&dy,&cap);*width=s.width-2*dx;*height=s.height-2*dy-cap;return 1;
}
uint32_t ss_owns_window(uint64_t base,uint32_t handle){pthread_mutex_lock(&lock);uint32_t r=find(base,handle)!=NULL;pthread_mutex_unlock(&lock);return r;}

/* HICON LoadIconA(HINSTANCE, LPCSTR). Window icons are decorations the Apple
   host never shows, so any system (IDI_*) or module icon gets a stable opaque
   handle per request; nothing reads its pixels. */
static uint32_t icon_names[32],icon_instances[32];static unsigned icon_count;
uint32_t ss_load_icon_a(uint64_t base,uint32_t instance,uint32_t name){
 char label[64]="";if(name>0xffff){uint32_t token;const char *text=guest(base,name,1,1,&token);tribes_memory_access_end(token);strncpy(label,text,63);}
 pthread_mutex_lock(&lock);unsigned i=0;
 for(;i<icon_count;i++)if(icon_instances[i]==instance&&(icon_names[i]==name||(name>0xffff&&icon_names[i]>0xffff)))break;
 if(i==icon_count){if(icon_count==32){pthread_mutex_unlock(&lock);unsupported("icon table",name);}icon_instances[i]=instance;icon_names[i]=name;icon_count++;
  fprintf(stderr,"TribesPad: icon %s%s (decoration only)\n",name>0xffff?label:"#",name>0xffff?"":"");}
 pthread_mutex_unlock(&lock);return 0xc7000001+i;
}

/* For the host: the visible game window, if any. */

/* Small USER helpers. */
uint32_t ss_set_rect(uint64_t base,uint32_t address,uint32_t l,uint32_t t,uint32_t r,uint32_t b){
 if(!address)return 0;write_rect(base,address,(int32_t)l,(int32_t)t,(int32_t)r,(int32_t)b);return 1;
}
/* The guest cursor position in desktop coordinates; the host updates it from
   real pointer events (ss_host_pointer). ShowCursor keeps Windows' counter. */
uint32_t ss_get_cursor_pos(uint64_t base,uint32_t output){
 uint32_t token;int32_t p[2];pthread_mutex_lock(&lock);p[0]=cursor_x;p[1]=cursor_y;pthread_mutex_unlock(&lock);
 memcpy(guest(base,output,8,2,&token),p,8);tribes_memory_access_end(token);
 {static int n;if(getenv("STARSIEGE_TRACE_INPUT")&&n<20){n++;fprintf(stderr,"TribesPad input: GetCursorPos %d,%d\n",p[0],p[1]);}}return 1;
}
uint32_t ss_set_cursor_pos(uint32_t x,uint32_t y){if(getenv("STARSIEGE_TRACE_INPUT"))fprintf(stderr,"TribesPad input: SetCursorPos %d,%d\n",(int)x,(int)y);pthread_mutex_lock(&lock);cursor_x=(int32_t)x;cursor_y=(int32_t)y;pthread_mutex_unlock(&lock);return 1;}
uint32_t ss_show_cursor(uint32_t show){if(getenv("STARSIEGE_TRACE_INPUT"))fprintf(stderr,"TribesPad input: ShowCursor %u\n",show);pthread_mutex_lock(&lock);cursor_count+=show?1:-1;uint32_t r=(uint32_t)cursor_count;pthread_mutex_unlock(&lock);return r;}
uint32_t ss_get_double_click_time(void){return 500;}
uint32_t ss_message_beep(uint32_t kind){(void)kind;return 1;}
/* int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT). Logged; answers
   IDOK, or IDYES for yes/no boxes. The app shows a native alert later. */
uint32_t ss_message_box_a(uint64_t base,uint32_t window,uint32_t text,uint32_t caption,uint32_t type){
 (void)window;uint32_t token;const char *t=text?guest(base,text,1,1,&token):"";if(text)tribes_memory_access_end(token);
 const char *c=caption?guest(base,caption,1,1,&token):"";if(caption)tribes_memory_access_end(token);
 fprintf(stderr,"TribesPad: MessageBox [%s] %s (type 0x%x)\n",c,t,type);
 uint32_t buttons=type&0xf;return buttons==4||buttons==3?6:1;
}
void ss_host_pointer(int32_t x,int32_t y){pthread_mutex_lock(&lock);cursor_x=x;cursor_y=y;pthread_mutex_unlock(&lock);}

/* HWND WindowFromPoint(POINT): the topmost visible game window under a
   desktop point; the desktop itself otherwise. */
uint32_t ss_window_from_point(uint64_t base,uint32_t x,uint32_t y){
 int32_t px=(int32_t)x,py=(int32_t)y;uint32_t r=0;pthread_mutex_lock(&lock);
 for(Window *w=windows;w;w=w->next)if(w->base==base&&w->visible&&!w->parent&&px>=w->x&&py>=w->y&&px<w->x+w->width&&py<w->y+w->height){r=w->handle;break;}
 pthread_mutex_unlock(&lock);return r?r:tribes_get_desktop_window(base);
}
/* For the host, while the game has handed the mouse back to Windows: move
   the desktop cursor by (dx, dy), kept inside the game window's client area
   (an Apple screen has nothing beside the game), and report the client point
   for a WM_MOUSEMOVE. Returns the window, or 0 when none is visible. */
uint32_t ss_pointer_to_client(int32_t dx,int32_t dy,int32_t *cx,int32_t *cy){
 pthread_mutex_lock(&lock);Window *w=windows;while(w&&!(w->visible&&!w->parent))w=w->next;
 if(!w){pthread_mutex_unlock(&lock);return 0;}
 int32_t fx,fy,cap;frame(w->style,w->exstyle,&fx,&fy,&cap);
 int32_t left=w->x+fx,top=w->y+fy+cap,width=w->width-2*fx,height=w->height-2*fy-cap;
 int32_t x=cursor_x+dx,y=cursor_y+dy;
 if(x<left)x=left;if(y<top)y=top;if(x>left+width-1)x=left+width-1;if(y>top+height-1)y=top+height-1;
 cursor_x=x;cursor_y=y;*cx=x-left;*cy=y-top;uint32_t handle=w->handle;pthread_mutex_unlock(&lock);return handle;
}
/* For the host: the visible game window, if any. */
uint32_t ss_main_window(int32_t *width,int32_t *height){
 pthread_mutex_lock(&lock);uint32_t r=0;for(Window *w=windows;w;w=w->next)if(w->visible){r=w->handle;*width=w->width;*height=w->height;break;}pthread_mutex_unlock(&lock);return r;
}
