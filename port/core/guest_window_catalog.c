#include "guest_window_catalog.h"
__attribute__((weak,noinline)) uint32_t tribes_is_desktop_window(uint64_t b,uint32_t h){(void)b;(void)h;return 0;}
#include "guest_heap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Standalone graphics fixtures need no guest TEB/module loader. Missing
   identity is retained as unknown and cannot produce successful ID queries. */
__attribute__((weak,noinline)) int tribes_teb_get_ids(uint64_t b,uint32_t *p,uint32_t *t){(void)b;(void)p;(void)t;return 0;}
__attribute__((weak,noinline)) uint32_t tribes_get_module_handle(uint64_t b,uint32_t h){(void)b;(void)h;return 0;}
__attribute__((weak,noinline)) void tribes_sdl_refresh_window_catalog(uint64_t base){(void)base;}
__attribute__((weak,noinline)) void tribes_user_messages_remove_window(uint64_t b,uint32_t h){(void)b;(void)h;}
__attribute__((weak,noinline)) int tribes_user_messages_ensure(uint64_t b){(void)b;return 1;}
typedef struct Window {uint64_t base;uint32_t hwnd,instance,style,pid,tid;int focused;struct Window *next;} Window;
static Window *windows;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Window *find(uint64_t b,uint32_t h){for(Window *w=windows;w;w=w->next)if(w->base==b&&w->hwnd==h)return w;return NULL;}
static _Noreturn void unsupported(void){fputs("TribesPad: unsupported guest window catalog operation/identity\n",stderr);abort();}
int tribes_window_add(uint64_t b,uint32_t h,uint32_t instance,uint32_t style){
 if(!h||style&0x40000000u)return 0;Window *w=calloc(1,sizeof *w);if(!w)return 0;
 w->base=b;w->hwnd=h;w->instance=instance;w->style=style;
 if(!instance)w->instance=tribes_get_module_handle(b,0);
 (void)tribes_teb_get_ids(b,&w->pid,&w->tid);
 if(w->tid&&!tribes_user_messages_ensure(b)){free(w);return 0;}
 pthread_mutex_lock(&lock);if(find(b,h)){pthread_mutex_unlock(&lock);free(w);return 0;}w->next=windows;windows=w;pthread_mutex_unlock(&lock);return 1;
}
void tribes_window_remove(uint64_t b,uint32_t h){pthread_mutex_lock(&lock);Window **p=&windows;while(*p&&((*p)->base!=b||(*p)->hwnd!=h))p=&(*p)->next;if(!*p)abort();Window *w=*p;*p=w->next;pthread_mutex_unlock(&lock);tribes_user_messages_remove_window(b,h);free(w);}
void tribes_window_update(uint64_t b,uint32_t h,uint32_t style,int focused){
 pthread_mutex_lock(&lock);Window *w=find(b,h);if(!w)abort();w->style=style;
 if(focused&&!w->focused){Window **p=&windows;while(*p!=w)p=&(*p)->next;*p=w->next;w->next=windows;windows=w;}
 if(focused)for(Window *other=windows;other;other=other->next)if(other->base==b&&other->tid==w->tid)other->focused=0;
 w->focused=!!focused;pthread_mutex_unlock(&lock);
}
uint32_t tribes_window_known(uint64_t b,uint32_t h){pthread_mutex_lock(&lock);uint32_t r=find(b,h)!=NULL;pthread_mutex_unlock(&lock);return r;}
uint32_t tribes_get_top_window(uint64_t b,uint32_t parent){
 tribes_sdl_refresh_window_catalog(b);pthread_mutex_lock(&lock);uint32_t r=0;
 if(!parent||tribes_is_desktop_window(b,parent)){for(Window *w=windows;w;w=w->next)if(w->base==b){r=w->hwnd;break;}}
 else if(!find(b,parent))tribes_set_last_error(1400); /* All owned windows are top-level; no children. */
 pthread_mutex_unlock(&lock);return r;
}
uint32_t tribes_get_window(uint64_t b,uint32_t h,uint32_t command){
 if(command!=2)unsupported();tribes_sdl_refresh_window_catalog(b);pthread_mutex_lock(&lock);Window *w=find(b,h);uint32_t r=0;
 if(!w)tribes_set_last_error(1400);else for(w=w->next;w;w=w->next)if(w->base==b){r=w->hwnd;break;}
 pthread_mutex_unlock(&lock);return r;
}
uint32_t tribes_get_window_thread_process_id(uint64_t b,uint32_t h,uint32_t output){
 pthread_mutex_lock(&lock);Window *w=find(b,h);uint32_t pid=0,tid=0;if(w){pid=w->pid;tid=w->tid;}pthread_mutex_unlock(&lock);
 if(!w){tribes_set_last_error(1400);return 0;}if(!pid||!tid)unsupported();
 if(output){if(output>UINT32_MAX-3||b>UINTPTR_MAX-output-3)unsupported();memcpy((void *)(uintptr_t)(b+output),&pid,4);}return tid;
}
uint32_t tribes_get_window_long_a(uint64_t b,uint32_t h,uint32_t index){
 if(index!=(uint32_t)-6&&index!=(uint32_t)-16)unsupported();tribes_sdl_refresh_window_catalog(b);pthread_mutex_lock(&lock);Window *w=find(b,h);uint32_t r=0;
 if(!w)tribes_set_last_error(1400);else if(index==(uint32_t)-6){r=w->instance;if(!r)unsupported();}else r=w->style;
 pthread_mutex_unlock(&lock);return r;
}
uint32_t tribes_get_active_window(uint64_t b){
 tribes_sdl_refresh_window_catalog(b);uint32_t pid=0,tid=0;if(!tribes_teb_get_ids(b,&pid,&tid))unsupported();uint32_t r=0;
 pthread_mutex_lock(&lock);for(Window *w=windows;w;w=w->next)if(w->base==b&&w->focused&&w->tid==tid){r=w->hwnd;break;}pthread_mutex_unlock(&lock);return r;
}
