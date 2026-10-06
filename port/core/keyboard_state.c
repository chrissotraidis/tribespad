#include "keyboard_state.h"
#include "guest_heap.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Profile {uint64_t base;unsigned char initial[256];struct Profile *next;} Profile;
typedef struct Local {uint64_t base;unsigned char state[256];struct Local *next;} Local;
static Profile *profiles;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t local_key;static pthread_once_t once=PTHREAD_ONCE_INIT;
static int toggle_key(unsigned vk){return vk==0x14||vk==0x90||vk==0x91;}
static void destroy(void *pointer){Local *local=pointer;while(local){Local *next=local->next;free(local);local=next;}}
static void make_key(void){if(pthread_key_create(&local_key,destroy))abort();}
static void aggregate(unsigned char state[256]) {for(unsigned i=0;i<3;i++)state[0x10+i]=(state[0xa0+i*2]|state[0xa1+i*2])&128;}
int tribes_keyboard_configure(uint64_t base,const unsigned char initial[256]) {
    if(!initial){errno=EINVAL;return 0;}Profile *created=calloc(1,sizeof *created);if(!created)return 0;created->base=base;
    for(unsigned i=0;i<256;i++)created->initial[i]=initial[i]&(toggle_key(i)?129:128);aggregate(created->initial);
    pthread_mutex_lock(&lock);for(Profile *p=profiles;p;p=p->next)if(p->base==base){pthread_mutex_unlock(&lock);free(created);errno=EEXIST;return 0;}
    created->next=profiles;profiles=created;pthread_mutex_unlock(&lock);return 1;
}
static Local *state_for(uint64_t base) {
    if(pthread_once(&once,make_key))abort();Local *head=pthread_getspecific(local_key);for(Local *local=head;local;local=local->next)if(local->base==base)return local;
    Local *local=calloc(1,sizeof *local);if(!local)abort();local->base=base;
    pthread_mutex_lock(&lock);Profile *profile=profiles;while(profile&&profile->base!=base)profile=profile->next;
    if(profile)memcpy(local->state,profile->initial,256);pthread_mutex_unlock(&lock);
    if(!profile){free(local);fprintf(stderr,"TribesPad: processed keyboard state is not configured\n");abort();}
    local->next=head;if(pthread_setspecific(local_key,local)){free(local);abort();}return local;
}
int tribes_keyboard_apply(uint64_t base,uint32_t vk,int down) {
    if(vk>255||vk==0x10||vk==0x11||vk==0x12){errno=EINVAL;return 0;}Local *local=state_for(base);int was_down=!!(local->state[vk]&128);
    unsigned toggle=local->state[vk]&1;if(down&&!was_down&&toggle_key(vk))toggle^=1;local->state[vk]=(down?128:0)|toggle;
    if(vk>=0xa0&&vk<=0xa5)aggregate(local->state);return 1;
}
void tribes_keyboard_snapshot(uint64_t base,unsigned char state[256]) {memcpy(state,state_for(base)->state,256);}
uint32_t tribes_get_key_state(uint64_t base,uint32_t vk) {
    if(vk>255)return 0;unsigned value=state_for(base)->state[vk];return (uint32_t)(int32_t)(int16_t)(((value&128)?0x8000:0)|(value&1));
}
uint32_t tribes_get_keyboard_state(uint64_t base,uint32_t output) {
    if(!output||output>UINT32_MAX-255||base>UINTPTR_MAX-output-255){tribes_set_last_error(87);return 0;}
    tribes_keyboard_snapshot(base,(unsigned char *)(uintptr_t)(base+output));return 1;
}
