#include "../port/core/guest_dinput.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_modules.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
static uint64_t base;static uint32_t shared;
const uint32_t tribes_dispatch_count=0,tribes_dispatch_vas[]={0};
void (*const tribes_dispatch_fns[])(void *)={0};
extern void *tribes_native_lookup(uint32_t);
static uint32_t read32(uint32_t address){uint32_t v;memcpy(&v,(void *)(uintptr_t)(base+address),4);return v;}
static void *worker(void *argument) {
    unsigned id=(unsigned)(uintptr_t)argument;uint32_t scratch=0x100000+id*4096,out=scratch+1,iid=scratch+33;
    const unsigned char unknown[]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};memcpy((void *)(uintptr_t)(base+iid),unknown,16);
    tribes_set_last_error(1000+id);
    for(unsigned i=0;i<1000;i++) {
        assert(!tribes_dinput_create(base,0x400000,(i&1)?0x300:0x500,out,0));uint32_t object=read32(out);assert(object>=0x30000000&&object<0x50000000);
        assert(read32(object)==object+4);for(unsigned j=0;j<9;j++)assert(read32(object+4+4*j)==TB_DI_METHOD(j)&&tribes_native_lookup(TB_DI_METHOD(j)));
        assert(tribes_dinput_addref(base,object)==2);assert(!tribes_dinput_query(base,object,iid,out)&&read32(out)==object);
        assert(tribes_dinput_release(base,object)==2);assert(tribes_dinput_release(base,object)==1);
        assert(!tribes_dinput_initialize(base,object,0x400000,0x500));assert(!tribes_dinput_release(base,object));
        for(unsigned j=0;j<4;j++){(void)tribes_dinput_addref(base,shared);(void)tribes_dinput_release(base,shared);}
        assert(tribes_get_last_error()==1000+id);
    }
    return NULL;
}
int main(void) {
    void *mapping=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(mapping!=MAP_FAILED);base=(uintptr_t)mapping;
    assert(!mprotect((char *)mapping+0x100000,0x10000,PROT_READ|PROT_WRITE));assert(tribes_dinput_configure(base));assert(!tribes_dinput_configure(base)&&errno==EALREADY);
    strcpy((char *)mapping+0x100001,"dinput");assert(!tribes_get_module_handle(base,0x100001));assert(tribes_load_library(base,0x100001)==TB_DINPUT_MODULE);
    strcpy((char *)mapping+0x100101,"DirectInputCreateA");assert(tribes_get_proc_address(base,TB_DINPUT_MODULE,0x100101)==TB_DI_CREATE&&tribes_native_lookup(TB_DI_CREATE));assert(!tribes_native_lookup(TB_DI_CREATE+1));
    uint32_t out=0x100301;assert(!tribes_dinput_create(base,0x400000,0x500,out,0));shared=read32(out);
    assert(tribes_dinput_create(base,0x400000,0x500,0,0)==0x80004003);
    assert(tribes_dinput_create(base,0x400000,0x500,out,1)==0x80040110&&read32(out)==0);
    assert(tribes_dinput_create(base,0,0x500,out,0)==0x80070057&&read32(out)==0);
    assert(tribes_dinput_create(base,0x400000,0,out,0)==0x80070015&&read32(out)==0);
    assert(tribes_dinput_create(base,0x400000,0x800,out,0)==0x8007047e&&read32(out)==0);
    unsigned char *guid=(void *)(uintptr_t)(base+0x100401);memset(guid,0xaa,16);assert(tribes_dinput_query(base,shared,0x100401,out)==0x80004002&&read32(out)==0);
    const unsigned char tail[]={0x8a,0xaa,0xcf,0x11,0xbf,0xc7,0x44,0x45,0x53,0x54,0,0};memcpy(guid+4,tail,12);
    for(unsigned i=0;i<2;i++){uint32_t first=i?0x5944e662:0x89521360;memcpy(guid,&first,4);assert(!tribes_dinput_query(base,shared,0x100401,out)&&read32(out)==shared);assert(tribes_dinput_release(base,shared)==1);}
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(!tribes_dinput_release(base,shared));assert(tribes_free_library(base,TB_DINPUT_MODULE));assert(!tribes_get_module_handle(base,0x100001));
    puts("PASS: 8000 native DirectInput root lifetimes, 32000 shared COM references, exact guest vtables, interface identity, argument errors and provider/export linkage");
}
