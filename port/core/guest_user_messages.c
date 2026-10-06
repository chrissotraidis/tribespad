/* Owning-thread Win32 posted messages and low-priority coalesced USER timers.
   Native monotonic deadlines are serviced by PeekMessage/GetMessage; no timer
   callback runs on a foreign worker. Keyboard/native SDL events are separate.
   Contracts: config/runtime/user-messages.json. */
#include "guest_user_messages.h"
#include "guest_teb.h"
#include "guest_heap.h"
#include "guest_window_catalog.h"
#include "hidden_windows.h"
#include "performance_clock.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void *tribes_lookup(uint32_t);
#define WM_TIMER 0x113u
#define WM_QUIT 0x12u
typedef struct Message {uint32_t words[7];struct Message *next;} Message;
typedef struct Timer {uint32_t window,id,period,callback;uint64_t deadline;struct Timer *next;} Timer;
typedef struct Queue {uint64_t base;uint32_t tid,count;pthread_t owner;pthread_cond_t changed;Message *head,*tail;Timer *timers;struct Queue *next;} Queue;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Queue *queues;
static _Thread_local Queue *local;
static uint32_t next_id=1;
static void check(int result){if(result)abort();}
static _Noreturn void unsupported(const char *reason){fprintf(stderr,"TribesPad: unsupported USER message operation: %s\n",reason);abort();}
static void *ptr(uint64_t base,uint32_t address,uint32_t size){if(!address||(uint64_t)address+size>UINT64_C(0x100000000)||base>UINTPTR_MAX-address-(size-1))return NULL;return (void *)(uintptr_t)(base+address);}
static uint64_t now_ms(void){struct timespec t;check(clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
static Queue *find(uint64_t base,uint32_t tid){for(Queue *q=queues;q;q=q->next)if(q->base==base&&q->tid==tid)return q;return NULL;}
static void finalize(_cpu *cpu){
 (void)cpu;check(pthread_mutex_lock(&lock));Queue *q=local;if(q){Queue **p=&queues;while(*p!=q)p=&(*p)->next;*p=q->next;
  while(q->timers){Timer *t=q->timers;q->timers=t->next;free(t);}while(q->head){Message *m=q->head;q->head=m->next;free(m);}check(pthread_cond_destroy(&q->changed));free(q);local=NULL;}
 check(pthread_mutex_unlock(&lock));
}
/* Caller holds the registry lock. Queues die only on their owning thread. */
static Queue *current(uint64_t base){
 uint32_t pid,tid;if(!tribes_teb_get_ids(base,&pid,&tid))unsupported("thread without configured TEB");
 if(local){if(local->base!=base||local->tid!=tid||!pthread_equal(local->owner,pthread_self()))unsupported("changed queue owner");return local;}
 if(find(base,tid))unsupported("duplicate native owner for guest thread");
 Queue *q=calloc(1,sizeof *q);if(!q){tribes_set_last_error(8);return NULL;}
 if(!tribes_guest_finalizer_register(finalize)){free(q);tribes_set_last_error(8);return NULL;}
 check(pthread_cond_init(&q->changed,NULL));q->base=base;q->tid=tid;q->owner=pthread_self();q->next=queues;queues=q;local=q;return q;
}
int tribes_user_messages_ensure(uint64_t base){check(pthread_mutex_lock(&lock));int ok=current(base)!=NULL;check(pthread_mutex_unlock(&lock));return ok;}
static uint32_t window_owner(uint64_t base,uint32_t window){return tribes_get_window_thread_process_id(base,window,0);}
static int valid_window(Queue *q,uint32_t window){if(!window||window==UINT32_MAX)return 1;uint32_t tid=window_owner(q->base,window);if(tid!=q->tid){tribes_set_last_error(1400);return 0;}return 1;}
static Message *append(Queue *q,uint32_t window,uint32_t message,uint32_t wp,uint32_t lp){
 if(q->count>=10000){tribes_set_last_error(1816);return NULL;}Message *m=calloc(1,sizeof *m);if(!m){tribes_set_last_error(8);return NULL;}
 m->words[0]=window;m->words[1]=message;m->words[2]=wp;m->words[3]=lp;m->words[4]=tribes_tick_count();
 if(q->tail)q->tail->next=m;else q->head=m;q->tail=m;q->count++;check(pthread_cond_signal(&q->changed));return m;
}
uint32_t tribes_set_timer(uint64_t base,uint32_t window,uint32_t id,uint32_t period,uint32_t callback){
 if(callback)(void)tribes_lookup(callback);check(pthread_mutex_lock(&lock));Queue *q=current(base);if(!q){check(pthread_mutex_unlock(&lock));return 0;}
 if(window==UINT32_MAX||!valid_window(q,window)){check(pthread_mutex_unlock(&lock));return 0;}
 Timer *t=q->timers;while(t&&(t->window!=window||t->id!=id))t=t->next;
 if(period<10)period=10;if(period>0x7fffffff)period=0x7fffffff;
 if(!t){t=calloc(1,sizeof *t);if(!t){check(pthread_mutex_unlock(&lock));tribes_set_last_error(8);return 0;}
  if(!window){if(!next_id||next_id==UINT32_MAX)unsupported("timer identity exhausted");id=next_id++;}
  t->window=window;t->id=id;t->next=q->timers;q->timers=t;
 }
 t->period=period;t->callback=callback;t->deadline=now_ms()+period;check(pthread_cond_signal(&q->changed));check(pthread_mutex_unlock(&lock));
 fprintf(stderr,"TribesPad: USER timer registered thread=%u window=0x%x id=%u period=%u callback=0x%08x\n",q->tid,window,id,period,callback);return id?id:1;
}
uint32_t tribes_kill_timer(uint64_t base,uint32_t window,uint32_t id){
 check(pthread_mutex_lock(&lock));Queue *q=current(base);uint32_t result=0;if(q){Timer **p=&q->timers;while(*p&&((*p)->window!=window||(*p)->id!=id))p=&(*p)->next;if(*p){Timer *t=*p;*p=t->next;free(t);result=1;check(pthread_cond_signal(&q->changed));}}
 check(pthread_mutex_unlock(&lock));if(!result)tribes_set_last_error(87);return result;
}
void tribes_user_messages_remove_window(uint64_t base,uint32_t window){
 check(pthread_mutex_lock(&lock));for(Queue *q=queues;q;q=q->next)if(q->base==base){Timer **p=&q->timers;while(*p){if((*p)->window==window){Timer *t=*p;*p=t->next;free(t);}else p=&(*p)->next;}}check(pthread_mutex_unlock(&lock));
}
static int matches(uint32_t hwnd,uint32_t message,uint32_t window,uint32_t low,uint32_t high){
 if(message==WM_QUIT)return 1;if(window==UINT32_MAX){if(hwnd)return 0;}else if(window&&hwnd!=window)return 0;
 return (!low&&!high)||(message>=low&&message<=high);
}
static Message **select_message(Queue *q,uint32_t window,uint32_t low,uint32_t high){
 Message **p=&q->head;while(*p&&!matches((*p)->words[0],(*p)->words[1],window,low,high))p=&(*p)->next;return p;
}
static uint64_t generate_timers(Queue *q,uint32_t window,uint32_t low,uint32_t high){
 uint64_t now=now_ms(),next=UINT64_MAX;
 for(Timer *t=q->timers;t;t=t->next){
  if(!matches(t->window,WM_TIMER,window,low,high))continue;
  if(t->deadline<=now){int pending=0;for(Message *m=q->head;m;m=m->next)if(m->words[0]==t->window&&m->words[1]==WM_TIMER&&m->words[2]==t->id){pending=1;break;}
   if(!pending&&!append(q,t->window,WM_TIMER,t->id,t->callback))unsupported("timer message allocation failed");
   t->deadline=now+t->period;
  }
  if(t->deadline<next)next=t->deadline;
 }
 return next;
}
static uint32_t receive(uint64_t base,uint32_t output,uint32_t window,uint32_t low,uint32_t high,uint32_t remove,int wait){
 void *out=ptr(base,output,28);if(!out||((low||high)&&(low>high||high>65535))){tribes_set_last_error(87);return wait?UINT32_MAX:0;}
 if(remove&~3u)unsupported("PeekMessage queue category flags");check(pthread_mutex_lock(&lock));Queue *q=current(base);
 if(!q||!valid_window(q,window)){check(pthread_mutex_unlock(&lock));return wait?UINT32_MAX:0;}
 for(;;){
  Message **p=select_message(q,window,low,high);uint64_t deadline=UINT64_MAX;
  if(!*p){deadline=generate_timers(q,window,low,high);p=select_message(q,window,low,high);}
  if(*p){Message *m=*p;uint32_t code=m->words[1];memcpy(out,m->words,28);
   if(remove&1){*p=m->next;if(q->tail==m){q->tail=q->head;if(q->tail)while(q->tail->next)q->tail=q->tail->next;}q->count--;free(m);}
   check(pthread_mutex_unlock(&lock));return wait&&code==WM_QUIT?0:1;
  }
  if(!wait){check(pthread_mutex_unlock(&lock));return 0;}
  if(deadline==UINT64_MAX)check(pthread_cond_wait(&q->changed,&lock));
  else {uint64_t now=now_ms(),delay=deadline>now?deadline-now:0;struct timespec t={(time_t)(delay/1000),(long)(delay%1000)*1000000};int r=pthread_cond_timedwait_relative_np(&q->changed,&lock,&t);if(r!=ETIMEDOUT)check(r);}
 }
}
uint32_t tribes_peek_message(uint64_t b,uint32_t p,uint32_t w,uint32_t l,uint32_t h,uint32_t r){return receive(b,p,w,l,h,r,0);}
uint32_t tribes_get_message(uint64_t b,uint32_t p,uint32_t w,uint32_t l,uint32_t h){return receive(b,p,w,l,h,1,1);}
uint32_t tribes_dispatch_message(_cpu *cpu,uint32_t address){
 uint32_t m[7];void *p=ptr(cpu->_pointer_offset,address,28);if(!p){tribes_set_last_error(87);return 0;}memcpy(m,p,28);
 if(m[1]==WM_TIMER&&m[3]){uint32_t args[]={m[0],WM_TIMER,m[2],tribes_tick_count()};(void)tribes_lookup(m[3]);(void)tribes_call_guest(cpu,m[3],4,args,cpu->_esp);return 0;}
 if(!m[0])return 0;return tribes_dispatch_hidden_message(cpu,m[0],m[1],m[2],m[3]);
}
uint32_t tribes_translate_message(uint64_t base,uint32_t address){uint32_t m[7];void *p=ptr(base,address,28);if(!p)return 0;memcpy(m,p,28);if(m[1]>=0x100&&m[1]<=0x109)unsupported("keyboard translation requires native input conversion");return 0;}
uint32_t tribes_post_thread_message(uint64_t base,uint32_t tid,uint32_t message,uint32_t wp,uint32_t lp){
 check(pthread_mutex_lock(&lock));Queue *q=find(base,tid);uint32_t result=q&&append(q,0,message,wp,lp)!=NULL;check(pthread_mutex_unlock(&lock));if(!q)tribes_set_last_error(1444);return result;
}
uint32_t tribes_post_message(uint64_t base,uint32_t window,uint32_t message,uint32_t wp,uint32_t lp){
 if(window==0xffff)unsupported("broadcast messages");check(pthread_mutex_lock(&lock));Queue *q;
 if(!window)q=current(base);else {uint32_t tid=window_owner(base,window);q=tid?find(base,tid):NULL;}
 uint32_t result=q&&append(q,window,message,wp,lp)!=NULL;check(pthread_mutex_unlock(&lock));if(!q)tribes_set_last_error(1400);return result;
}
void tribes_post_quit_message(uint64_t base,uint32_t exit_code){
 check(pthread_mutex_lock(&lock));Queue *q=current(base);if(!q)unsupported("quit queue allocation failed");
 Message *m=q->head;while(m&&m->words[1]!=WM_QUIT)m=m->next;if(m)m->words[2]=exit_code;else if(!append(q,0,WM_QUIT,exit_code,0))unsupported("quit message allocation failed");check(pthread_mutex_unlock(&lock));
}
