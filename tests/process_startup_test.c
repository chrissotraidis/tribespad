#include "../port/core/process_startup.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[8][128];static uint64_t bases[8];static uint32_t initial[3];
static uint32_t word(const unsigned char *p){uint32_t value;memcpy(&value,p,4);return value;}
static void *worker(void *arg){unsigned index=(unsigned)(uintptr_t)arg;for(unsigned i=0;i<2000;i++){memset(memory[index],0xa5,128);tribes_get_startup_info(bases[index],17);assert(word(memory[index]+17)==68);assert(word(memory[index]+61)==0x101);assert(memory[index][65]==1&&memory[index][66]==0);for(unsigned n=0;n<3;n++)assert(word(memory[index]+73+4*n)==initial[n]);for(unsigned n=0;n<17;n++)assert(memory[index][n]==0xa5);for(unsigned n=85;n<128;n++)assert(memory[index][n]==0xa5);for(unsigned n=4;n<44;n++)assert(memory[index][17+n]==0);}return NULL;}
int main(void){
    initial[0]=tribes_get_std_handle(0xfffffff6);initial[1]=tribes_get_std_handle(0xfffffff5);initial[2]=tribes_get_std_handle(0xfffffff4);
    for(unsigned i=0;i<8;i++){bases[i]=(uintptr_t)memory[i];assert(tribes_startup_configure(bases[i],1,initial));}
    assert(!tribes_startup_configure(bases[0],1,initial)&&errno==EALREADY);assert(!tribes_startup_configure(0,10,initial)&&errno==EINVAL);assert(!tribes_startup_configure(0,1,NULL)&&errno==EINVAL);
    assert(tribes_set_std_handle(0xfffffff5,0xdeadbeef));tribes_set_last_error(123);tribes_get_startup_info(bases[0],17);assert(tribes_get_last_error()==123&&word(memory[0]+77)==initial[1]);assert(tribes_set_std_handle(0xfffffff5,initial[1]));
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: immutable real launch handles, 32-bit STARTUPINFO fields, unaligned output guards and 16000 concurrent reads");return 0;
}
