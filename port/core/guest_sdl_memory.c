/* Pinned SDL 3.2.18 SDL_SetMemoryFunctions contract; see config/sdl-reference.json. */
#include "guest_sdl_memory.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
typedef struct Allocator { uint64_t base;uint32_t functions[4];struct Allocator *next; } Allocator;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Allocator *allocators;
static _Thread_local const char *error="";
static Allocator *find(uint64_t base){for(Allocator *a=allocators;a;a=a->next)if(a->base==base)return a;return NULL;}
const char *tribes_sdl_memory_error(void){return error;}
void tribes_sdl_memory_out_of_memory(void){error="Out of memory";}
uint32_t tribes_sdl_set_memory_functions(uint64_t base,uint32_t malloc_pc,uint32_t calloc_pc,uint32_t realloc_pc,uint32_t free_pc){
    if(!malloc_pc){error="Parameter 'malloc_func' is invalid";return 0;}
    if(!calloc_pc){error="Parameter 'calloc_func' is invalid";return 0;}
    if(!realloc_pc){error="Parameter 'realloc_func' is invalid";return 0;}
    if(!free_pc){error="Parameter 'free_func' is invalid";return 0;}
    pthread_mutex_lock(&lock);Allocator *a=find(base);
    if(!a){a=calloc(1,sizeof *a);if(!a){pthread_mutex_unlock(&lock);tribes_sdl_memory_out_of_memory();return 0;}a->base=base;a->next=allocators;allocators=a;}
    uint32_t values[]={malloc_pc,calloc_pc,realloc_pc,free_pc};memcpy(a->functions,values,sizeof values);pthread_mutex_unlock(&lock);return 1;
}
int tribes_sdl_memory_functions(uint64_t base,uint32_t functions[4]){
    if(!functions)return 0;pthread_mutex_lock(&lock);Allocator *a=find(base);if(a)memcpy(functions,a->functions,sizeof a->functions);pthread_mutex_unlock(&lock);return a!=NULL;
}
