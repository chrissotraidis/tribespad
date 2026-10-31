/* Unicode (W) window, message and class services for Tribes: Vengeance, whose
 * Unreal window driver (WinDrv) uses them throughout. For ASCII text the W forms
 * differ from the A forms only in their string arguments, so each narrows its
 * strings and delegates to the A service TribesPad and Starsiege already provide
 * (window_classes.c, guest_user_messages.c, port/starsiege/starsiege_windows.c).
 * Narrowed strings live in a guest page for the duration of the call: window
 * procedures run on the guest stack below the caller, so stack scratch would
 * be overwritten. Window properties and registered messages are new here.
 * Non-ASCII text fails with ERROR_NOT_SUPPORTED, as TribesPad's other W services. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../llasm-support/llasm_cpu.h"
#include "../core/guest_virtual_memory.h"
#include "../core/window_classes.h"

extern uint32_t tribes_set_last_error(uint32_t value);
extern uint32_t tribes_get_last_error(void);
extern uint32_t ss_create_window_ex_a(_cpu *cpu,uint32_t exstyle,uint32_t class_name,uint32_t title,uint32_t style,uint32_t x,uint32_t y,uint32_t width,uint32_t height,uint32_t parent,uint32_t menu,uint32_t instance,uint32_t parameter);
extern uint32_t ss_set_window_text_a(_cpu *cpu,uint32_t handle,uint32_t text);
extern uint32_t tribes_load_cursor(uint64_t base,uint32_t instance,uint32_t name);
extern uint32_t ss_load_icon_a(uint64_t base,uint32_t instance,uint32_t name);
extern uint32_t tribes_get_key_state(uint64_t base,uint32_t key);
extern uint32_t tribes_get_top_window(uint64_t base,uint32_t parent);
extern uint32_t tribes_is_window(uint64_t base,uint32_t handle);

#define GUEST(base,address) ((void *)(uintptr_t)((base)+(address)))
static _Noreturn void unsupported(const char *what,uint32_t value){
 printf("{\"diagnostic_stop\":true,\"kind\":\"windows\",\"name\":\"%s\",\"index\":%u}\n",what,value);fflush(stdout);_Exit(75);
}
static int in_guest(uint64_t base,uint32_t address,uint32_t bytes){return address&&address<=UINT32_MAX-bytes&&base<=UINTPTR_MAX-address-bytes;}
/* A committed guest page for narrowed strings; released after the call. */
static uint32_t page(uint64_t base){uint32_t p=tribes_virtual_alloc(base,0,65536,0x3000,4);if(!p)unsupported("scratch page",0);return p;}
static void release(uint64_t base,uint32_t p){if(p)tribes_virtual_free(base,p,0,0x8000);}
/* Narrows a guest wide string (or passes an atom through) to dest; 0 on failure. */
static int narrow(uint64_t base,uint32_t source,uint32_t dest,uint32_t capacity,uint32_t *out){
 if(source<=0xffff){*out=source;return 1;}
 char *d=GUEST(base,dest);
 for(uint32_t i=0;i<capacity;i++){
  if(!in_guest(base,source+2*i,2)){tribes_set_last_error(87);return 0;}
  uint16_t v;memcpy(&v,GUEST(base,source+2*i),2);
  if(v>127){tribes_set_last_error(50);return 0;}
  d[i]=(char)v;if(!v){*out=dest;return 1;}
 }
 tribes_set_last_error(206);return 0;
}

/* HWND CreateWindowExW: Starsiege's CreateWindowExA with narrowed class and title. */
uint32_t vg_create_window_ex_w(_cpu *cpu,uint32_t exstyle,uint32_t class_name,uint32_t title,uint32_t style,uint32_t x,uint32_t y,uint32_t width,uint32_t height,uint32_t parent,uint32_t menu,uint32_t instance,uint32_t parameter){
 uint64_t base=cpu->_pointer_offset;uint32_t p=page(base),c=0,t=0;
 if(!narrow(base,class_name,p,256,&c)||(title&&!narrow(base,title,p+256,2048,&t))){release(base,p);return 0;}
 uint32_t window=ss_create_window_ex_a(cpu,exstyle,c,title?t:0,style,x,y,width,height,parent,menu,instance,parameter);
 release(base,p);return window;
}
/* ATOM RegisterClassExW / RegisterClassW: WNDCLASS(EX)W as WNDCLASSA with a
   narrowed name (menu names stay unsupported, as for RegisterClassA). */
static uint32_t register_class(uint64_t base,uint32_t descriptor,int ex){
 uint32_t bytes=ex?48:40;if(!in_guest(base,descriptor,bytes)){tribes_set_last_error(87);return 0;}
 uint32_t f[12];memcpy(f,GUEST(base,descriptor),bytes);
 if(ex&&f[0]!=48){tribes_set_last_error(87);return 0;}
 uint32_t *w=ex?f+1:f; /* WNDCLASSW fields: style, proc, cls, wnd, instance, icon, cursor, brush, menu, name */
 uint32_t p=page(base),name=0;
 if(!narrow(base,w[9],p+64,256,&name)){release(base,p);return 0;}
 uint32_t a[10];memcpy(a,w,40);a[9]=name;memcpy(GUEST(base,p),a,40);
 uint32_t atom=tribes_register_class(base,p);
 if(!atom)fprintf(stderr,"vengeance: RegisterClass%sW \"%s\" (style 0x%x, instance 0x%x, menu 0x%x) failed: %u\n",ex?"Ex":"",(const char *)GUEST(base,name),a[0],a[4],a[8],tribes_get_last_error());
 release(base,p);return atom;
}
uint32_t vg_register_class_ex_w(uint64_t base,uint32_t descriptor){return register_class(base,descriptor,1);}
uint32_t vg_register_class_w(uint64_t base,uint32_t descriptor){return register_class(base,descriptor,0);}
uint32_t vg_unregister_class_w(uint64_t base,uint32_t name,uint32_t instance){
 uint32_t p=page(base),n=0;if(!narrow(base,name,p,256,&n)){release(base,p);return 0;}
 uint32_t ok=tribes_unregister_class(base,n,instance);release(base,p);return ok;
}
/* BOOL GetClassInfoEx(HINSTANCE, name, LPWNDCLASSEX): from TribesPad's registry.
   A NULL instance also finds the program's own classes, as on Windows, where
   user32 looks a NULL instance up as the executable's module.
   Unreal's Window.dll superclasses the system controls (WControl::
   RegisterWindowClass asks for "BUTTON", "EDIT"...). TribesPad has no system
   controls and the game draws its interface itself, so a system class answers
   with vg_control_proc as its procedure: such windows are plain windows that
   keep their title and ignore control messages (the log window's edit box). */
extern uint32_t tribes_get_module_handle(uint64_t base,uint32_t pointer);
extern uint32_t vengeance_control_proc(void);
extern uint32_t ss_def_window_proc_a(_cpu *cpu,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp);
uint32_t vg_control_proc(_cpu *cpu,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp){
 switch(message){
 case 0x0081:case 0x000C:case 0x0010:case 0x0014:case 0x0084:case 0x0086:case 0x0021:case 0x0047:case 0x0112:
  return ss_def_window_proc_a(cpu,handle,message,wp,lp);
 }
 return 0;
}
static const char *const system_classes[]={"Button","Edit","Static","ListBox","ComboBox","ScrollBar","ComboLBox","MDIClient",
 "RichEdit","RICHEDIT20A","RICHEDIT20W","msctls_trackbar32","msctls_progress32","msctls_updown32","SysListView32","SysTreeView32",
 "SysTabControl32","SysHeader32","tooltips_class32","ToolbarWindow32","msctls_statusbar32","ReBarWindow32"};
static uint32_t system_class(uint64_t base,uint32_t name,uint32_t output){
 if(name<=0xffff)return 0;
 for(unsigned i=0;i<sizeof system_classes/sizeof system_classes[0];i++)if(!strcasecmp((const char *)GUEST(base,name),system_classes[i])){
  uint32_t record[12]={48,0x8,vengeance_control_proc(),0,0,0,0,0,0,0,0,0}; /* CS_DBLCLKS */
  memcpy(GUEST(base,output),record,48);fprintf(stderr,"vengeance: system class %s as a plain window\n",system_classes[i]);
  return 0xc000u-1-i;
 }
 return 0;
}
static uint32_t class_info(uint64_t base,uint32_t instance,uint32_t name,uint32_t output,int wide){
 if(!in_guest(base,output,48)){tribes_set_last_error(87);return 0;}
 uint32_t p=page(base),n=name;if(wide&&!narrow(base,name,p,256,&n)){release(base,p);return 0;}
 uint32_t fields[10];uint32_t atom=tribes_class_acquire(base,n,instance,fields);release(base,p);
 if(!atom&&!instance){uint32_t exe=tribes_get_module_handle(base,0);if(exe){p=page(base);n=name;
  if(!wide||narrow(base,name,p,256,&n))atom=tribes_class_acquire(base,n,exe,fields);release(base,p);}}
 if(!atom&&!instance){p=page(base);n=name;
  uint32_t s=(!wide||narrow(base,name,p,256,&n))?system_class(base,n,output):0;release(base,p);
  if(s){uint32_t record[12];memcpy(record,GUEST(base,output),48);record[10]=name;memcpy(GUEST(base,output),record,48);return s;}}
 if(!atom){tribes_set_last_error(1411);return 0;}
 tribes_class_release(base,atom);
 uint32_t record[12]={48,fields[0],fields[1],fields[2],fields[3],fields[4],fields[5],fields[6],fields[7],0,name,0};
 memcpy(GUEST(base,output),record,48);return atom;
}
uint32_t vg_get_class_info_ex_a(uint64_t base,uint32_t instance,uint32_t name,uint32_t output){return class_info(base,instance,name,output,0);}
uint32_t vg_get_class_info_ex_w(uint64_t base,uint32_t instance,uint32_t name,uint32_t output){return class_info(base,instance,name,output,1);}
uint32_t vg_set_window_text_w(_cpu *cpu,uint32_t handle,uint32_t text){
 uint64_t base=cpu->_pointer_offset;uint32_t p=page(base),t=0;if(text&&!narrow(base,text,p,4000,&t)){release(base,p);return 0;}
 uint32_t ok=ss_set_window_text_a(cpu,handle,t);release(base,p);return ok;
}
/* LoadCursorW / LoadIconW: resource ordinals are the same as the A forms. */
uint32_t vg_load_cursor_w(uint64_t base,uint32_t instance,uint32_t name){
 if(name<=0xffff)return tribes_load_cursor(base,instance,name);
 uint32_t p=page(base),n=0;if(!narrow(base,name,p,256,&n)){release(base,p);return 0;}uint32_t r=tribes_load_cursor(base,instance,n);release(base,p);return r;
}
uint32_t vg_load_icon_w(uint64_t base,uint32_t instance,uint32_t name){
 if(name<=0xffff)return ss_load_icon_a(base,instance,name);
 uint32_t p=page(base),n=0;if(!narrow(base,name,p,256,&n)){release(base,p);return 0;}uint32_t r=ss_load_icon_a(base,instance,n);release(base,p);return r;
}

/* HWND FindWindowEx: TribesPad runs one guest process, so a search before any
   window exists (a single-instance check at start-up) finds nothing. Searching
   among existing windows is not modeled yet and stops. */
static uint32_t find_window(uint64_t base,uint32_t parent,uint32_t after){
 if(parent||after)unsupported("FindWindowEx with a parent or start window",parent?parent:after);
 if(tribes_get_top_window(base,0))unsupported("FindWindowEx among existing windows",0);
 tribes_set_last_error(0);return 0;
}
uint32_t vg_find_window_ex_a(uint64_t base,uint32_t parent,uint32_t after,uint32_t name,uint32_t title){(void)name;(void)title;return find_window(base,parent,after);}
uint32_t vg_find_window_ex_w(uint64_t base,uint32_t parent,uint32_t after,uint32_t name,uint32_t title){(void)name;(void)title;return find_window(base,parent,after);}
uint32_t vg_find_window_a(uint64_t base,uint32_t name,uint32_t title){(void)name;(void)title;return find_window(base,0,0);}

/* Window properties: per window, keyed by atom or case-insensitive name. */
typedef struct Prop {uint64_t base;uint32_t window,atom,value;char name[256];struct Prop *next;} Prop;
static Prop *props;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int prop_key(uint64_t base,uint32_t key,int wide,uint32_t *atom,char name[256]){
 *atom=key<=0xffff?key:0;name[0]=0;if(*atom)return 1;
 for(uint32_t i=0;i<256;i++){
  uint32_t at=key+i*(wide?2:1);if(!in_guest(base,at,2)){tribes_set_last_error(87);return 0;}
  uint16_t v;if(wide)memcpy(&v,GUEST(base,at),2);else v=*(const unsigned char *)GUEST(base,at);
  if(v>127){tribes_set_last_error(50);return 0;}name[i]=(char)v;if(!v)return 1;
 }
 tribes_set_last_error(206);return 0;
}
static Prop **prop_find(uint64_t base,uint32_t window,uint32_t atom,const char *name){
 for(Prop **p=&props;*p;p=&(*p)->next)if((*p)->base==base&&(*p)->window==window&&(atom?(*p)->atom==atom:!(*p)->atom&&!strcasecmp((*p)->name,name)))return p;
 return NULL;
}
static uint32_t set_prop(uint64_t base,uint32_t window,uint32_t key,uint32_t value,int wide){
 if(!tribes_is_window(base,window)){tribes_set_last_error(1400);return 0;}
 uint32_t atom;char name[256];if(!prop_key(base,key,wide,&atom,name))return 0;
 pthread_mutex_lock(&lock);Prop **p=prop_find(base,window,atom,name);
 if(p)(*p)->value=value;
 else{Prop *n=calloc(1,sizeof *n);if(!n){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
  n->base=base;n->window=window;n->atom=atom;n->value=value;strcpy(n->name,name);n->next=props;props=n;}
 pthread_mutex_unlock(&lock);return 1;
}
static uint32_t get_prop(uint64_t base,uint32_t window,uint32_t key,int wide,int remove){
 uint32_t atom;char name[256];if(!prop_key(base,key,wide,&atom,name))return 0;
 pthread_mutex_lock(&lock);Prop **p=prop_find(base,window,atom,name);uint32_t value=p?(*p)->value:0;
 if(p&&remove){Prop *gone=*p;*p=gone->next;free(gone);}
 pthread_mutex_unlock(&lock);return value;
}
uint32_t vg_set_prop_a(uint64_t base,uint32_t window,uint32_t key,uint32_t value){return set_prop(base,window,key,value,0);}
uint32_t vg_set_prop_w(uint64_t base,uint32_t window,uint32_t key,uint32_t value){return set_prop(base,window,key,value,1);}
uint32_t vg_get_prop_a(uint64_t base,uint32_t window,uint32_t key){return get_prop(base,window,key,0,0);}
uint32_t vg_get_prop_w(uint64_t base,uint32_t window,uint32_t key){return get_prop(base,window,key,1,0);}
uint32_t vg_remove_prop_a(uint64_t base,uint32_t window,uint32_t key){return get_prop(base,window,key,0,1);}
uint32_t vg_remove_prop_w(uint64_t base,uint32_t window,uint32_t key){return get_prop(base,window,key,1,1);}

/* UINT RegisterWindowMessage: one id per case-insensitive name, from 0xC000. */
static struct {char name[256];} messages[256];static unsigned message_count;
static uint32_t register_message(uint64_t base,uint32_t text,int wide){
 uint32_t atom;char name[256];if(!text||text<=0xffff){tribes_set_last_error(87);return 0;}
 if(!prop_key(base,text,wide,&atom,name))return 0;
 pthread_mutex_lock(&lock);uint32_t id=0;
 for(unsigned i=0;i<message_count;i++)if(!strcasecmp(messages[i].name,name))id=0xc000+i;
 if(!id){if(message_count==256)unsupported("registered window messages",256);strcpy(messages[message_count].name,name);id=0xc000+message_count++;}
 pthread_mutex_unlock(&lock);return id;
}
uint32_t vg_register_window_message_a(uint64_t base,uint32_t text){return register_message(base,text,0);}
uint32_t vg_register_window_message_w(uint64_t base,uint32_t text){return register_message(base,text,1);}

/* SHORT GetAsyncKeyState(int): the key's current up/down state (high bit). The
   "pressed since the last call" bit is not tracked (Windows documents it as
   unreliable). HWND GetCapture: the window SetCapture recorded. HCURSOR
   SetCursor: TribesPad draws no system cursor; the previous cursor is returned. */
uint32_t vg_get_async_key_state(uint64_t base,uint32_t key){return tribes_get_key_state(base,key)&0x8000u;}
static uint32_t current_cursor;
uint32_t vg_set_cursor(uint32_t cursor){pthread_mutex_lock(&lock);uint32_t old=current_cursor;current_cursor=cursor;pthread_mutex_unlock(&lock);return old;}
uint32_t vg_get_cursor(void){pthread_mutex_lock(&lock);uint32_t c=current_cursor;pthread_mutex_unlock(&lock);return c;}
extern uint32_t ss_set_capture(uint32_t handle);
uint32_t vg_get_capture(void){uint32_t current=ss_set_capture(0);ss_set_capture(current);return current;}
