#include "../port/core/guest_display.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static uint32_t window,foreign_dc;
static void *worker(void *arg){(void)arg;tribes_set_last_error(123);for(unsigned i=0;i<10000;i++){assert(tribes_get_system_metrics(1,0)==16&&tribes_get_system_metrics(1,1)==8&&tribes_get_system_metrics(1,42)==0&&tribes_get_system_metrics(1,67)==0);uint32_t dc=tribes_get_window_dc(1,window);assert(dc&&tribes_get_display_caps(1,dc,12)==32&&tribes_get_display_caps(1,dc,8)==16&&tribes_get_display_caps(1,dc,10)==8);assert(!tribes_release_window_dc(1,0,dc));assert(tribes_release_window_dc(1,window,dc));assert(tribes_get_last_error()==123);}return NULL;}
static void *exiting(void *arg){(void)arg;foreign_dc=tribes_get_window_dc(1,window);assert(foreign_dc);return NULL;}
static void *wrong_thread(void *arg){uint32_t dc=(uint32_t)(uintptr_t)arg;assert(!tribes_release_window_dc(1,window,dc));return NULL;}
int main(void){assert(!tribes_display_configure(1,0,10)&&errno==EINVAL);assert(!tribes_display_configure(1,32768,32768));assert(tribes_display_configure(1,16,8));assert(!tribes_display_configure(1,16,8)&&errno==EEXIST);assert(tribes_display_configure(2,8,16));window=tribes_get_desktop_window(1);assert(window&&window==tribes_get_desktop_window(1)&&window!=tribes_get_desktop_window(2));assert(tribes_is_desktop_window(1,window)&&!tribes_is_desktop_window(2,window));
 tribes_set_last_error(123);assert(tribes_get_system_metrics(1,0)==16&&tribes_get_system_metrics(1,1)==8&&tribes_get_system_metrics(2,0)==8&&tribes_get_system_metrics(2,1)==16&&tribes_get_system_metrics(1,42)==0&&tribes_get_system_metrics(1,67)==0&&tribes_get_system_metrics(2,42)==0&&tribes_get_last_error()==123);
 uint32_t dc=tribes_get_window_dc(1,window),other=tribes_get_window_dc(1,0);assert(dc&&other&&dc!=other);uint32_t w=99,h=99;unsigned char pixels[514];memset(pixels,0xa5,sizeof pixels);assert(tribes_display_surface_snapshot(1,dc,&w,&h,pixels+1,512)&&w==16&&h==8&&pixels[0]==0xa5&&pixels[513]==0xa5);for(unsigned i=0;i<128;i++)assert(pixels[1+i*4]==0&&pixels[2+i*4]==0&&pixels[3+i*4]==0&&pixels[4+i*4]==255);
 w=h=99;assert(!tribes_display_surface_snapshot(1,dc,&w,&h,pixels,511)&&w==99&&h==99);assert(!tribes_display_surface_snapshot(2,dc,&w,&h,pixels,512));assert(!tribes_get_window_dc(2,window)&&tribes_get_last_error()==1400);assert(!tribes_release_window_dc(2,window,dc));pthread_t thread;assert(!pthread_create(&thread,NULL,wrong_thread,(void *)(uintptr_t)dc)&&!pthread_join(thread,NULL));assert(tribes_release_window_dc(1,window,dc)&&!tribes_release_window_dc(1,window,dc));assert(tribes_release_window_dc(1,0,other));assert(!tribes_get_display_caps(1,dc,8)&&tribes_get_last_error()==6);
 assert(!pthread_create(&thread,NULL,exiting,NULL)&&!pthread_join(thread,NULL));assert(!tribes_get_display_caps(1,foreign_dc,12)&&tribes_get_last_error()==6);
 pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));puts("PASS: 80000 concurrent owner-thread desktop-DC lifetimes, typed backing-store size/depth, shared root identity, NULL-screen alias, opaque BGRA8 surface/guards, mismatched window/process/thread releases, stale handles and thread-exit cleanup");}
