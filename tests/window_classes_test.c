#include "../port/core/window_classes.h"
#include "../port/core/guest_heap.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* A real finite dispatch table with one native callback for this unit fixture. */
static void callback(_cpu *cpu){(void)cpu;}
const uint32_t tribes_dispatch_count=1,tribes_dispatch_vas[]={0x550000};
void (*const tribes_dispatch_fns[])(_cpu *)={callback};
void tribes_return_to_host(_cpu *cpu){(void)cpu;}
static unsigned char memory[8][1024];static uint64_t bases[8];
static uint32_t make(unsigned i,const char *name){strcpy((char *)memory[i]+512,name);uint32_t fields[]={0,0x550000,8,16,0x400000,0,0,0,0,0x20000+512};memcpy(memory[i]+17,fields,40);return tribes_register_class(bases[i],0x20000+17);}
static void *worker(void *arg){unsigned i=(unsigned)(uintptr_t)arg;for(unsigned n=0;n<1000;n++){uint32_t atom=make(i,"Concurrent"),fields[10];assert(atom&&tribes_class_acquire(bases[i],atom,0x400000,fields)==atom);assert(fields[1]==0x550000&&fields[9]==atom);tribes_class_release(bases[i],atom);assert(tribes_unregister_class(bases[i],atom,0x400000));}return NULL;}
int main(void){
    for(unsigned i=0;i<8;i++)bases[i]=(uintptr_t)memory[i]-0x20000;
    tribes_set_last_error(123);uint32_t atom=make(0,"Tribes2"),fields[10];assert(atom>=0xc000&&tribes_get_last_error()==123);assert(!make(0,"tRIBES2")&&tribes_get_last_error()==1410);
    assert(tribes_class_acquire(bases[0],atom,0x400000,fields)==atom);assert(!tribes_unregister_class(bases[0],atom,0x400000)&&tribes_get_last_error()==1412);assert(!tribes_class_acquire(bases[0],atom,0x999,fields));tribes_class_release(bases[0],atom);
    strcpy((char *)memory[0]+512,"source name changed");assert(tribes_class_acquire(bases[0],atom,0x400000,fields)==atom);tribes_class_release(bases[0],atom);assert(tribes_unregister_class(bases[0],atom,0x400000));assert(!tribes_unregister_class(bases[0],atom,0x400000)&&tribes_get_last_error()==1411);
    assert(!tribes_register_class(bases[0],0)&&tribes_get_last_error()==87);assert(!make(0,"caf\xe9")&&tribes_get_last_error()==50);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: actual finite native callback validation, copied class names, duplicate/busy errors, window retention and 8000 concurrent lifetimes");return 0;
}
