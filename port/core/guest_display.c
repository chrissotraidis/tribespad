#include "guest_display.h"
#include "guest_heap.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Desktop {uint64_t base;uint32_t window,width,height;int32_t work[4];int work_defined;size_t bytes;unsigned char *pixels;struct Desktop *next;} Desktop;
typedef struct DC {uint64_t base;uint32_t handle,window;pthread_t owner;Desktop *desktop;struct DC *next,*thread_next;} DC;
static Desktop *desktops;static DC *contexts;static uint32_t next_window=0xc3000001,next_dc=0xd7000001;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;static pthread_key_t key;static pthread_once_t once=PTHREAD_ONCE_INIT;
static Desktop *find_desktop(uint64_t base){for(Desktop *d=desktops;d;d=d->next)if(d->base==base)return d;return NULL;}
static DC *find_dc(uint64_t base,uint32_t handle){for(DC *dc=contexts;dc;dc=dc->next)if(dc->base==base&&dc->handle==handle)return dc;return NULL;}
static void remove_dc(DC *dc){DC **p=&contexts;while(*p!=dc)p=&(*p)->next;*p=dc->next;free(dc);}
static void cleanup(void *pointer){pthread_mutex_lock(&lock);DC *dc=pointer;while(dc){DC *next=dc->thread_next;remove_dc(dc);dc=next;}pthread_mutex_unlock(&lock);}
static void make_key(void){if(pthread_key_create(&key,cleanup))abort();}
int tribes_display_configure(uint64_t base,uint32_t width,uint32_t height){
    uint64_t bytes=(uint64_t)width*height*4;if(!width||!height||width>32768||height>32768||bytes>256*1024*1024){errno=EINVAL;return 0;}
    Desktop *d=calloc(1,sizeof *d);if(!d)return 0;d->pixels=calloc(1,(size_t)bytes);if(!d->pixels){free(d);return 0;}for(size_t i=3;i<bytes;i+=4)d->pixels[i]=255;d->base=base;d->width=width;d->height=height;d->bytes=(size_t)bytes;
    pthread_mutex_lock(&lock);if(find_desktop(base)||next_window>=0xc4000000){pthread_mutex_unlock(&lock);free(d->pixels);free(d);errno=EEXIST;return 0;}d->window=next_window++;d->next=desktops;desktops=d;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_get_desktop_window(uint64_t base){pthread_mutex_lock(&lock);Desktop *d=find_desktop(base);uint32_t window=d?d->window:0;pthread_mutex_unlock(&lock);if(!window){fprintf(stderr,"TribesPad: Apple display geometry is not configured\n");abort();}return window;}
int tribes_display_configure_work_area(uint64_t base,int32_t left,int32_t top,int32_t right,int32_t bottom){
 pthread_mutex_lock(&lock);Desktop *d=find_desktop(base);
 if(!d||d->work_defined||left<0||top<0||right<left||bottom<top||(uint32_t)right>d->width||(uint32_t)bottom>d->height){pthread_mutex_unlock(&lock);return 0;}
 const int32_t work[]={left,top,right,bottom};memcpy(d->work,work,sizeof work);d->work_defined=1;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_monitor_from_window(uint64_t base,uint32_t window,uint32_t flags){
 if(flags>2){tribes_set_last_error(87);return 0;}
 pthread_mutex_lock(&lock);Desktop *d=find_desktop(base);
 if(!d){pthread_mutex_unlock(&lock);tribes_set_last_error(50);return 0;}
 if(window&&window!=d->window){pthread_mutex_unlock(&lock);fputs("TribesPad: monitor geometry for non-desktop windows not implemented\n",stderr);abort();}
 /* c4000001..c4ffffff: typed monitor tokens, distinct from desktop HWNDs. */
 uint32_t monitor=(window||flags)?d->window+0x01000000:0;
 pthread_mutex_unlock(&lock);return monitor;
}
static int monitor_range(uint64_t base,uint32_t p,uint32_t n){return base&&p&&(uint64_t)p+n<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-p-(n-1);}
static uint32_t monitor_info(uint64_t base,uint32_t monitor,uint32_t output,int wide){
 uint32_t words[10];pthread_mutex_lock(&lock);Desktop *d=find_desktop(base);
 if(!d||monitor!=d->window+0x01000000){pthread_mutex_unlock(&lock);tribes_set_last_error(1461);return 0;}
 if(!d->work_defined){pthread_mutex_unlock(&lock);tribes_set_last_error(50);return 0;}
 words[0]=40;words[1]=words[2]=0;words[3]=d->width;words[4]=d->height;
 memcpy(words+5,d->work,16);words[9]=1;pthread_mutex_unlock(&lock);
 if(!monitor_range(base,output,4)){tribes_set_last_error(87);return 0;}
 uint32_t size;memcpy(&size,(void *)(uintptr_t)(base+output),4);
 if((size!=40&&size!=(wide?104u:72u))||!monitor_range(base,output,size)){tribes_set_last_error(87);return 0;}
 unsigned char bytes[104]={0};words[0]=size;memcpy(bytes,words,40);
 if(size!=40){const char name[]="\\\\.\\DISPLAY1";for(unsigned i=0;i<sizeof name;i++)bytes[40+i*(wide?2:1)]=(unsigned char)name[i];}
 memcpy((void *)(uintptr_t)(base+output),bytes,size);return 1;
}
uint32_t tribes_get_monitor_info_a(uint64_t base,uint32_t monitor,uint32_t output){return monitor_info(base,monitor,output,0);}
uint32_t tribes_get_monitor_info_w(uint64_t base,uint32_t monitor,uint32_t output){return monitor_info(base,monitor,output,1);}
uint32_t tribes_is_desktop_window(uint64_t base,uint32_t window){pthread_mutex_lock(&lock);Desktop *d=find_desktop(base);uint32_t valid=d&&d->window==window;pthread_mutex_unlock(&lock);return valid;}
uint32_t tribes_get_window_dc(uint64_t base,uint32_t window){
    if(pthread_once(&once,make_key))abort();pthread_mutex_lock(&lock);Desktop *d=find_desktop(base);if(!d||(window&&window!=d->window)){pthread_mutex_unlock(&lock);tribes_set_last_error(1400);return 0;}
    if(next_dc>=0xd8000000){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}DC *dc=calloc(1,sizeof *dc);if(!dc){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}dc->base=base;dc->window=window;dc->handle=next_dc++;dc->desktop=d;dc->owner=pthread_self();dc->thread_next=pthread_getspecific(key);
    if(pthread_setspecific(key,dc)){free(dc);pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}dc->next=contexts;contexts=dc;uint32_t handle=dc->handle;pthread_mutex_unlock(&lock);return handle;
}
uint32_t tribes_release_window_dc(uint64_t base,uint32_t window,uint32_t handle){
    if(pthread_once(&once,make_key))abort();pthread_mutex_lock(&lock);DC *dc=find_dc(base,handle);if(!dc||dc->window!=window||!pthread_equal(dc->owner,pthread_self())){pthread_mutex_unlock(&lock);return 0;}
    DC *head=pthread_getspecific(key),**p=&head;while(*p!=dc)p=&(*p)->thread_next;*p=dc->thread_next;if(pthread_setspecific(key,head))abort();remove_dc(dc);pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_get_display_caps(uint64_t base,uint32_t handle,uint32_t index){
    pthread_mutex_lock(&lock);DC *dc=find_dc(base,handle);if(!dc){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
    uint32_t value;if(index==8)value=dc->desktop->width;else if(index==10)value=dc->desktop->height;else if(index==12)value=32;else{pthread_mutex_unlock(&lock);fprintf(stderr,"TribesPad: display capability %u is not implemented\n",index);abort();}pthread_mutex_unlock(&lock);return value;
}
int tribes_display_surface_snapshot(uint64_t base,uint32_t handle,uint32_t *width,uint32_t *height,void *pixels,size_t capacity){
    if(!width||!height||!pixels)return 0;pthread_mutex_lock(&lock);DC *dc=find_dc(base,handle);if(!dc||capacity<dc->desktop->bytes){pthread_mutex_unlock(&lock);return 0;}*width=dc->desktop->width;*height=dc->desktop->height;memcpy(pixels,dc->desktop->pixels,dc->desktop->bytes);pthread_mutex_unlock(&lock);return 1;
}

/* Primary-screen pixels share the configured desktop geometry. This runtime
   implements single-byte CP1252/US text only, so DBCS support is absent.
   Other metrics remain strict gates rather than fabricated window geometry. */
uint32_t tribes_get_system_metrics(uint64_t base,uint32_t index){
    /* SM_CLEANBOOT: this guest launches the normal service/startup profile.
       It does not implement Windows fail-safe boot or omit startup services.
       https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getsystemmetrics */
    if(index==67)return 0;
    if(index==42)return 0;
    if(index!=0&&index!=1){fprintf(stderr,"TribesPad: system metric %u is not implemented\n",index);abort();}
    pthread_mutex_lock(&lock);Desktop *d=find_desktop(base);uint32_t value=d?(index==0?d->width:d->height):0;pthread_mutex_unlock(&lock);
    if(!value){fprintf(stderr,"TribesPad: Apple display geometry is not configured\n");abort();}return value;
}
