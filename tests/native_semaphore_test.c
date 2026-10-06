#include "../port/core/native_handles.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static uint32_t gate;
static void *worker(void *unused){(void)unused;tribes_set_last_error(123);for(unsigned i=0;i<1000;i++){assert(tribes_wait_single_object(gate,UINT32_MAX)==0);assert(tribes_release_semaphore(0,gate,1,0));assert(tribes_get_last_error()==123);}return NULL;}
static void *waiter(void *arg){uint32_t h=(uint32_t)(uintptr_t)arg;tribes_set_last_error(123);assert(tribes_wait_single_object(h,UINT32_MAX)==0&&tribes_get_last_error()==123);return NULL;}
int main(void){
 unsigned char memory[32];memset(memory,0xa5,sizeof memory);uint64_t base=(uintptr_t)memory;tribes_set_last_error(123);
 assert(!tribes_create_semaphore(base,0,1,UINT32_MAX,0)&&tribes_get_last_error()==87);
 assert(!tribes_create_semaphore(base,0,0,0,0)&&tribes_get_last_error()==87);
 assert(!tribes_create_semaphore(base,0,UINT32_MAX,2,0));assert(!tribes_create_semaphore(base,0,3,2,0));
 tribes_set_last_error(123);uint32_t h=tribes_create_semaphore(base,0,1,2,0);assert(h&&tribes_get_last_error()==123);
 assert(tribes_wait_single_object(h,0)==0&&tribes_wait_single_object(h,0)==258&&tribes_get_last_error()==123);
 struct timespec start,end;assert(!clock_gettime(CLOCK_MONOTONIC,&start));assert(tribes_wait_single_object(h,20)==258);assert(!clock_gettime(CLOCK_MONOTONIC,&end));double elapsed=(end.tv_sec-start.tv_sec)+(end.tv_nsec-start.tv_nsec)*1e-9;assert(elapsed>=0.015&&elapsed<5);
 assert(tribes_release_semaphore(base,h,2,7));uint32_t previous;memcpy(&previous,memory+7,4);assert(previous==0&&memory[6]==0xa5&&memory[11]==0xa5&&tribes_get_last_error()==123);
 assert(!tribes_release_semaphore(base,h,1,7)&&tribes_get_last_error()==298);memcpy(&previous,memory+7,4);assert(previous==0);
 assert(!tribes_release_semaphore(base,h,0,7)&&tribes_get_last_error()==87);assert(!tribes_release_semaphore(base,h,UINT32_MAX,7)&&tribes_get_last_error()==87);
 assert(!tribes_release_semaphore(base,h,1,UINT32_MAX-2)&&tribes_get_last_error()==87);assert(tribes_wait_single_object(h,0)==0&&tribes_wait_single_object(h,0)==0&&tribes_wait_single_object(h,0)==258);
 tribes_set_last_error(123);assert(tribes_duplicate_handle(base,UINT32_MAX,h,UINT32_MAX,17,0,0,2));uint32_t copy;memcpy(&copy,memory+17,4);assert(copy&&copy!=h&&memory[16]==0xa5&&memory[21]==0xa5);assert(tribes_release_semaphore(base,copy,1,0)&&tribes_wait_single_object(h,0)==0);
 assert(tribes_duplicate_handle(base,UINT32_MAX,h,UINT32_MAX,17,0,0,3));uint32_t moved;memcpy(&moved,memory+17,4);assert(tribes_wait_single_object(h,0)==UINT32_MAX&&tribes_get_last_error()==6);assert(tribes_close_handle(copy));
 pthread_t thread;assert(!pthread_create(&thread,NULL,waiter,(void *)(uintptr_t)moved));assert(tribes_release_semaphore(base,moved,1,0));assert(!pthread_join(thread,NULL));assert(tribes_close_handle(moved));assert(!tribes_close_handle(moved)&&tribes_get_last_error()==6);assert(!tribes_release_semaphore(base,moved,1,0)&&tribes_get_last_error()==6);
 int fd=open("/dev/null",O_RDONLY);assert(fd>=0);uint32_t descriptor=tribes_fd_adopt(fd);assert(descriptor&&tribes_get_file_type(descriptor)==2);assert(!tribes_release_semaphore(base,descriptor,1,0)&&tribes_get_last_error()==6);assert(tribes_close_handle(descriptor));
 gate=tribes_create_semaphore(base,0,1,1,0);assert(gate);pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(tribes_wait_single_object(gate,0)==0&&tribes_wait_single_object(gate,0)==258);assert(tribes_close_handle(gate));
 puts("PASS: signed semaphore counts, immediate/finite/infinite waits, bounded release with guarded previous-count output, shared-count duplicate/source-close lifetimes, typed/stale handles and 8000 concurrent wait/release cycles");return 0;
}
