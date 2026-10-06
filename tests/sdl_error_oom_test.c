#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static _Atomic int fail_allocations,live_blocks;
static void *allocate(size_t n){if(atomic_load(&fail_allocations))return NULL;void *p=malloc(n);if(p)atomic_fetch_add(&live_blocks,1);return p;}
static void *allocate_zero(size_t n,size_t s){if(atomic_load(&fail_allocations))return NULL;void *p=calloc(n,s);if(p)atomic_fetch_add(&live_blocks,1);return p;}
static void *resize(void *p,size_t n){if(atomic_load(&fail_allocations))return NULL;int fresh=p==NULL;void *next=realloc(p,n);if(next&&fresh)atomic_fetch_add(&live_blocks,1);return next;}
static void release(void *p){if(p)atomic_fetch_sub(&live_blocks,1);free(p);}
static void *worker(void *arg){
 const char *message=arg;
 for(unsigned i=0;i<1000;i++){assert(!SDL_SetError("%s",message));assert(!strcmp(SDL_GetError(),message));assert(SDL_ClearError());assert(!*SDL_GetError());}
 SDL_CleanupTLS();return NULL;
}
int main(void){
 assert(SDL_SetMemoryFunctions(allocate,allocate_zero,resize,release));SDL_SetMainReady();
 atomic_store(&fail_allocations,1);assert(!SDL_Init(SDL_INIT_VIDEO));
 assert(!SDL_OutOfMemory()&&!strcmp(SDL_GetError(),"Out of memory"));assert(SDL_ClearError()&&!*SDL_GetError());
 pthread_t a,b;assert(!pthread_create(&a,NULL,worker,"worker-a")&&!pthread_create(&b,NULL,worker,"worker-b"));
 worker("main-thread");assert(!pthread_join(a,NULL)&&!pthread_join(b,NULL));
 atomic_store(&fail_allocations,0);assert(!SDL_SetError("%s","recovered"));assert(!strcmp(SDL_GetError(),"recovered"));
 assert(SDL_Init(SDL_INIT_EVENTS));SDL_Quit();SDL_CleanupTLS();assert(atomic_load(&live_blocks)==0);
 puts("PASS: persistent allocator failure returns without recursion; per-thread emergency errors survive reads/clears across 3000 concurrent cycles; TLS allocation recovers and cleans up with zero live custom-allocator blocks");return 0;
}
