/* Hidden notification windows with real class retention and native AOT callbacks.
   https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexa
   Visible Apple windows, cross-thread send and general default processing are gates. */
#include "hidden_windows.h"
#include "guest_window_catalog.h"
#include "window_classes.h"
#include "guest_display.h"
#include "guest_heap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Window { uint64_t base;uint32_t handle,atom,proc,destroying;pthread_t owner;char *title;unsigned char *extra;struct Window *next; } Window;
static Window *windows;
static uint32_t next_handle=0xc1000001;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Window *find(uint64_t base,uint32_t handle) { for(Window *w=windows;w;w=w->next)if(w->base==base&&w->handle==handle)return w;return NULL; }
static void remove_window(Window *w) { Window **p=&windows;while(*p!=w)p=&(*p)->next;*p=w->next;tribes_window_remove(w->base,w->handle);tribes_class_release(w->base,w->atom);free(w->title);free(w->extra);free(w); }
static uint32_t send(_cpu *cpu,uint32_t handle,uint32_t proc,uint32_t message,uint32_t wp,uint32_t lp) {
#ifdef TRIBESPAD_WINDOW_OBSERVER
    extern void tribes_window_observation(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
    tribes_window_observation(proc,handle,message,wp,lp);
#endif
    uint32_t args[]={handle,message,wp,lp};return tribes_call_guest(cpu,proc,4,args,cpu->_esp-128);
}
uint32_t tribes_create_hidden_window(_cpu *cpu,uint32_t extended,uint32_t class_name,uint32_t title,uint32_t style,uint32_t x,uint32_t y,uint32_t width,uint32_t height,uint32_t parent,uint32_t menu,uint32_t instance,uint32_t parameter) {
    if(extended||style||x||y||width||height||parent||menu){fprintf(stderr,"TribesPad: visible/general window creation requires the Apple window backend\n");abort();}
    uint64_t base=cpu->_pointer_offset;uint32_t fields[10],atom=tribes_class_acquire(base,class_name,instance,fields);
    if(!atom){tribes_set_last_error(1407);return 0;}
    if(cpu->_esp<256||(cpu->stack_bottom&&base+cpu->_esp-256<(uintptr_t)cpu->stack_bottom))abort();
    Window *w=calloc(1,sizeof *w);if(!w){tribes_class_release(base,atom);tribes_set_last_error(8);return 0;}
    if(fields[3]){w->extra=calloc(1,fields[3]);if(!w->extra){free(w);tribes_class_release(base,atom);tribes_set_last_error(8);return 0;}}
    w->base=base;w->atom=atom;w->proc=fields[1];w->owner=pthread_self();
    pthread_mutex_lock(&lock);if(next_handle>=0xc2000000){pthread_mutex_unlock(&lock);free(w->extra);free(w);tribes_class_release(base,atom);tribes_set_last_error(8);return 0;}
    w->handle=next_handle++;if(!tribes_window_add(base,w->handle,fields[4],style)){pthread_mutex_unlock(&lock);free(w->extra);free(w);tribes_class_release(base,atom);tribes_set_last_error(8);return 0;}w->next=windows;windows=w;uint32_t handle=w->handle;pthread_mutex_unlock(&lock);
    uint32_t cs=cpu->_esp-112,rect=cpu->_esp-64;uint32_t create[]={parameter,instance,menu,parent,height,width,y,x,style,title,class_name,extended};uint32_t rectangle[4]={x,y,x+width,y+height};
    memcpy((void *)(uintptr_t)(base+cs),create,48);memcpy((void *)(uintptr_t)(base+rect),rectangle,16);
    uint32_t ok=send(cpu,handle,fields[1],0x81,0,cs);
    if(ok&&tribes_is_window(base,handle)){send(cpu,handle,fields[1],0x83,0,rect);if(tribes_is_window(base,handle)&&send(cpu,handle,fields[1],1,0,cs)==UINT32_MAX)ok=0;}
    if(!tribes_is_window(base,handle))return 0;
    if(!ok){send(cpu,handle,fields[1],0x82,0,0);pthread_mutex_lock(&lock);w=find(base,handle);if(w)remove_window(w);pthread_mutex_unlock(&lock);return 0;}
    return handle;
}
uint32_t tribes_is_window(uint64_t base,uint32_t handle) { if(tribes_is_desktop_window(base,handle)||tribes_window_known(base,handle))return 1; pthread_mutex_lock(&lock);uint32_t found=find(base,handle)!=NULL;pthread_mutex_unlock(&lock);return found; }
uint32_t tribes_default_window_proc(uint64_t base,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp) {
    (void)wp;pthread_mutex_lock(&lock);Window *w=find(base,handle);if(!w){pthread_mutex_unlock(&lock);return 0;}
    if(message==0x81){uint32_t title;memcpy(&title,(void *)(uintptr_t)(base+lp+36),4);const char *text=title?(const char *)(uintptr_t)(base+title):"";size_t n=strnlen(text,65536);if(n==65536)abort();char *copy=malloc(n+1);if(!copy){pthread_mutex_unlock(&lock);return 0;}memcpy(copy,text,n+1);free(w->title);w->title=copy;pthread_mutex_unlock(&lock);return 1;}
    if(message==0x83||message==1||message==2||message==0x82||message==0x24){pthread_mutex_unlock(&lock);return 0;}
    pthread_mutex_unlock(&lock);fprintf(stderr,"TribesPad: unsupported default window message 0x%x\n",message);abort();
}
uint32_t tribes_destroy_hidden_window(_cpu *cpu,uint32_t handle) {
    uint64_t base=cpu->_pointer_offset;if(tribes_is_desktop_window(base,handle)){tribes_set_last_error(5);return 0;}pthread_mutex_lock(&lock);Window *w=find(base,handle);
    if(!w||!pthread_equal(w->owner,pthread_self())){pthread_mutex_unlock(&lock);tribes_set_last_error(w?5:1400);return 0;}
    if(w->destroying){pthread_mutex_unlock(&lock);return 0;}w->destroying=1;
    uint32_t proc=w->proc;pthread_mutex_unlock(&lock);send(cpu,handle,proc,2,0,0);send(cpu,handle,proc,0x82,0,0);pthread_mutex_lock(&lock);w=find(base,handle);if(w)remove_window(w);pthread_mutex_unlock(&lock);return 1;
}

uint32_t tribes_dispatch_hidden_message(_cpu *cpu,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp){
 pthread_mutex_lock(&lock);Window *w=find(cpu->_pointer_offset,handle);uint32_t proc=w?w->proc:0;
 if(w&&!pthread_equal(w->owner,pthread_self())){pthread_mutex_unlock(&lock);tribes_set_last_error(5);return 0;}pthread_mutex_unlock(&lock);
 if(!proc){if(tribes_window_known(cpu->_pointer_offset,handle)){fputs("TribesPad: unsupported native SDL window message dispatch\n",stderr);abort();}tribes_set_last_error(1400);return 0;}
 return send(cpu,handle,proc,message,wp,lp);
}
