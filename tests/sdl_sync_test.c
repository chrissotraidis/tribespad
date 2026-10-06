#include "../port/core/guest_sdl_sync.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
static atomic_int try_done;
static uint32_t mutex,condition;static unsigned turn,count;
static void *worker(void *arg){
 (void)arg;assert(!tribes_sdl_try_lock_mutex(1,mutex));atomic_store(&try_done,1);
 tribes_sdl_lock_mutex(1,mutex);
 for(unsigned i=0;i<2000;i++){
  while(turn!=1)assert(tribes_sdl_wait_condition_timeout(1,condition,mutex,-1));
  assert(count==i);count++;turn=0;tribes_sdl_signal_condition(1,condition);
 }
 tribes_sdl_unlock_mutex(1,mutex);return NULL;
}
#ifndef TRIBESPAD_SIMULATOR
static void fault(unsigned n){
 pid_t p=fork();assert(p>=0);if(!p){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);
  if(n==0)tribes_sdl_lock_mutex(1,0x1234);
  if(n==1)tribes_sdl_lock_mutex(1,condition);
  if(n==2)tribes_sdl_lock_mutex(2,mutex);
  if(n==3)tribes_sdl_unlock_mutex(1,mutex);
  if(n==4){uint32_t t=tribes_sdl_create_mutex(1);tribes_sdl_destroy_mutex(1,t);tribes_sdl_destroy_mutex(1,t);}
  if(n==5){tribes_sdl_lock_mutex(1,mutex);tribes_sdl_destroy_mutex(1,mutex);}
  if(n==6)tribes_sdl_wait_condition_timeout(1,condition,mutex,0);
  if(n==7){tribes_sdl_lock_mutex(1,mutex);tribes_sdl_lock_mutex(1,mutex);tribes_sdl_wait_condition_timeout(1,condition,mutex,0);}
  _exit(99);
 }int status;assert(waitpid(p,&status,0)==p&&WIFSIGNALED(status)&&WTERMSIG(status)==6);
}
#endif
int main(void){
 mutex=tribes_sdl_create_mutex(1);condition=tribes_sdl_create_condition(1);assert(mutex&&condition&&mutex!=condition);
 assert(tribes_sdl_try_lock_mutex(1,0));tribes_sdl_lock_mutex(1,0);tribes_sdl_unlock_mutex(1,0);tribes_sdl_destroy_mutex(1,0);tribes_sdl_destroy_condition(1,0);tribes_sdl_signal_condition(1,0);tribes_sdl_broadcast_condition(1,0);assert(tribes_sdl_wait_condition_timeout(1,0,mutex,0));
 tribes_sdl_lock_mutex(1,mutex);assert(tribes_sdl_try_lock_mutex(1,mutex));tribes_sdl_unlock_mutex(1,mutex);
 assert(!tribes_sdl_wait_condition_timeout(1,condition,mutex,1));
 pthread_t t;assert(!pthread_create(&t,NULL,worker,NULL));
 /* Hold the recursive mutex through worker's unsuccessful try-lock. */
 while(!atomic_load(&try_done))sched_yield();
 for(unsigned i=0;i<2000;i++){
  assert(count==i);turn=1;tribes_sdl_broadcast_condition(1,condition);
  while(turn)assert(tribes_sdl_wait_condition_timeout(1,condition,mutex,-1));
 }
 tribes_sdl_unlock_mutex(1,mutex);assert(!pthread_join(t,NULL));assert(count==2000);
 #ifndef TRIBESPAD_SIMULATOR
 for(unsigned i=0;i<8;i++)fault(i);
 #endif
 tribes_sdl_destroy_condition(1,condition);tribes_sdl_destroy_mutex(1,mutex);
 puts("PASS: native recursive SDL mutex and condition tokens;2000 two-thread handoffs, timeout/reacquisition, null contracts; host-only ownership/lifetime faults");return 0;
}
