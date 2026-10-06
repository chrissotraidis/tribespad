#include "guest_sdl_sync.h"
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
/* Guest SDL synchronization pointers are typed, nonreused tokens. Native
   primitives and their lifetimes never depend on a borrowed guest CPU frame. */
typedef struct Object { uint64_t base;uint32_t token,kind,refs,depth;pthread_t owner;void *native;struct Object *next; } Object;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static Object *objects;
static uint32_t next_token=0xd8000001u;
static _Noreturn void stop(void){fputs("TribesPad: invalid SDL synchronization ownership/lifetime\n",stderr);abort();}
static Object *find(uint64_t base,uint32_t token,uint32_t kind){
 for(Object *o=objects;o;o=o->next)if(o->token==token){if(o->base!=base||o->kind!=kind)stop();return o;}stop();
}
static Object *borrow(uint64_t base,uint32_t token,uint32_t kind){
 pthread_mutex_lock(&gate);Object *o=find(base,token,kind);if(o->refs==UINT32_MAX)stop();o->refs++;pthread_mutex_unlock(&gate);return o;
}
static void release(Object *o){pthread_mutex_lock(&gate);if(!o->refs)stop();o->refs--;pthread_mutex_unlock(&gate);}
static uint32_t create(uint64_t base,uint32_t kind){
 if(!base)stop();void *native=kind==1?(void *)SDL_CreateMutex():(void *)SDL_CreateCondition();if(!native)return 0;
 Object *o=calloc(1,sizeof *o);if(!o){if(kind==1)SDL_DestroyMutex(native);else SDL_DestroyCondition(native);SDL_OutOfMemory();return 0;}
 pthread_mutex_lock(&gate);if(next_token>=0xd9000000u)stop();o->base=base;o->token=next_token++;o->kind=kind;o->native=native;o->next=objects;objects=o;uint32_t token=o->token;pthread_mutex_unlock(&gate);return token;
}
static void destroy(uint64_t base,uint32_t token,uint32_t kind){
 if(!token)return;pthread_mutex_lock(&gate);Object *o=find(base,token,kind);if(o->refs||o->depth)stop();Object **slot=&objects;while(*slot!=o)slot=&(*slot)->next;*slot=o->next;pthread_mutex_unlock(&gate);
 if(kind==1)SDL_DestroyMutex(o->native);else SDL_DestroyCondition(o->native);free(o);
}
uint32_t tribes_sdl_create_mutex(uint64_t base){return create(base,1);}
uint32_t tribes_sdl_create_condition(uint64_t base){return create(base,2);}
void tribes_sdl_destroy_mutex(uint64_t base,uint32_t token){destroy(base,token,1);}
void tribes_sdl_destroy_condition(uint64_t base,uint32_t token){destroy(base,token,2);}
static uint32_t acquire(uint64_t base,uint32_t token,int try_only){
 if(!token)return 1;Object *o=borrow(base,token,1);int ok=1;
 if(try_only)ok=SDL_TryLockMutex(o->native);else SDL_LockMutex(o->native);
 if(ok){pthread_mutex_lock(&gate);if(o->depth&&(!pthread_equal(o->owner,pthread_self())||o->depth==UINT32_MAX))stop();o->owner=pthread_self();o->depth++;pthread_mutex_unlock(&gate);}
 release(o);return ok;
}
void tribes_sdl_lock_mutex(uint64_t base,uint32_t token){(void)acquire(base,token,0);}
uint32_t tribes_sdl_try_lock_mutex(uint64_t base,uint32_t token){return acquire(base,token,1);}
void tribes_sdl_unlock_mutex(uint64_t base,uint32_t token){
 if(!token)return;Object *o=borrow(base,token,1);pthread_mutex_lock(&gate);if(!o->depth||!pthread_equal(o->owner,pthread_self()))stop();o->depth--;pthread_mutex_unlock(&gate);SDL_UnlockMutex(o->native);release(o);
}
void tribes_sdl_signal_condition(uint64_t base,uint32_t token){if(!token)return;Object *o=borrow(base,token,2);SDL_SignalCondition(o->native);release(o);}
void tribes_sdl_broadcast_condition(uint64_t base,uint32_t token){if(!token)return;Object *o=borrow(base,token,2);SDL_BroadcastCondition(o->native);release(o);}
uint32_t tribes_sdl_wait_condition_timeout(uint64_t base,uint32_t condition,uint32_t mutex,int32_t milliseconds){
 if(!condition||!mutex)return 1;Object *c=borrow(base,condition,2),*m=borrow(base,mutex,1);
 pthread_mutex_lock(&gate);if(m->depth!=1||!pthread_equal(m->owner,pthread_self()))stop();m->depth=0;pthread_mutex_unlock(&gate);
 uint32_t ok=SDL_WaitConditionTimeout(c->native,m->native,milliseconds);
 pthread_mutex_lock(&gate);if(m->depth)stop();m->owner=pthread_self();m->depth=1;pthread_mutex_unlock(&gate);release(m);release(c);return ok;
}
