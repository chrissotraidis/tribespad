#include "../port/core/guest_callback.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_sdl_memory.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tbimp_SDL_aligned_alloc(void),tbimp_SDL_aligned_free(void);
extern void tbimp_SDL_SetMemoryFunctions(void),tbimp_SDL_malloc(void),tbimp_SDL_calloc(void),tbimp_SDL_realloc(void),tbimp_SDL_free(void);
extern void tbimp_FixtureMalloc(void),tbimp_FixtureCalloc(void),tbimp_FixtureRealloc(void),tbimp_FixtureFree(void),fixture_callback(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){case 1:return (void *)tbimp_FixtureMalloc;case 2:return (void *)tbimp_FixtureCalloc;case 3:return (void *)tbimp_FixtureRealloc;case 4:return (void *)tbimp_FixtureFree;case 5:return (void *)fixture_callback;case 6:return (void *)tbimp_SDL_SetMemoryFunctions;case 7:return (void *)tbimp_SDL_malloc;case 8:return (void *)tbimp_SDL_calloc;case 9:return (void *)tbimp_SDL_realloc;case 10:return (void *)tbimp_SDL_free;case 11:return (void *)tbimp_SDL_aligned_alloc;case 12:return (void *)tbimp_SDL_aligned_free;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
uint32_t fixture_body(_cpu *cpu,uint32_t a,uint32_t b,uint32_t c,uint32_t d){cpu->_ecx=0xdeadbeef;cpu->_eflags=0x246;cpu->_st_cw=0xb7f;cpu->_st_exp[2]=0x8001;cpu->_xmm[1][3]^=0x5a;cpu->_ymm_hi[6][11]^=0xa7;cpu->_mxcsr=0x3f80;return a+b+c+d;}
static _Thread_local uint32_t last_size,last_free,malloc_calls;
uint32_t fixture_malloc(_cpu *cpu,uint32_t size){
    last_size=size;++malloc_calls;
    if(size==999)return 0;
    uint32_t arguments[]={1,2,3,4};assert(tribes_call_guest(cpu,5,4,arguments,cpu->_esp-256)==10);
    uint32_t result=tribes_global_alloc(cpu->_pointer_offset,0,size);
    if(result)memset((void *)(uintptr_t)(cpu->_pointer_offset+result),0xa5,size);
    return result;
}
uint32_t fixture_calloc(_cpu *cpu,uint32_t count,uint32_t size){assert(count&&size&&count<=UINT32_MAX/size);return tribes_global_alloc(cpu->_pointer_offset,0x40,count*size);}
uint32_t fixture_realloc(_cpu *cpu,uint32_t ptr,uint32_t size){
    if(size==888)return 0;
    uint32_t newptr=tribes_global_alloc(cpu->_pointer_offset,0,size);assert(newptr);
    if(ptr){uint32_t oldsize=tribes_global_size(cpu->_pointer_offset,ptr);assert(oldsize);memcpy((void *)(uintptr_t)(cpu->_pointer_offset+newptr),(void *)(uintptr_t)(cpu->_pointer_offset+ptr),oldsize<size?oldsize:size);assert(!tribes_global_free(cpu->_pointer_offset,ptr));}return newptr;
}
void fixture_free(_cpu *cpu,uint32_t ptr){last_free=ptr;if(ptr)assert(!tribes_global_free(cpu->_pointer_offset,ptr));}
static unsigned char *memory;
static uint32_t invoke(_cpu *cpu,uint32_t pc,unsigned count,const uint32_t *args){
    uint32_t top=cpu->_esp-512,sp=top-4*count;memset(memory+sp-68,0x79,64);memset(memory+top,0x79,64);_cpu before=*cpu;
    uint32_t result=tribes_call_guest_cdecl(cpu,pc,count,args,top);
    /* The nested stdcall fixture intentionally changes thread FP state. */
    if((pc==1||pc==7||pc==11)&&result){before._st_cw=0xb7f;before._st_exp[2]=0x8001;before._xmm[1][3]^=0x5a;before._ymm_hi[6][11]^=0xa7;before._mxcsr=0x3f80;}
    assert(!memcmp(cpu,&before,sizeof before));
    for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x79&&memory[top+i]==0x79);
    if(count)assert(!memcmp(memory+sp,args,4*count));return result;
}
static void setup(_cpu *cpu,unsigned i){memset(cpu,0xa5,sizeof *cpu);cpu->_pointer_offset=(uintptr_t)memory;cpu->_esp=0x100000+i*0x10000+0xf003;cpu->stack_bottom=memory+0x100000+i*0x10000;cpu->stack_top=memory+0x100000+(i+1)*0x10000;}
static void *worker(void *context){
    _cpu cpu;setup(&cpu,(unsigned)(uintptr_t)context);
    for(unsigned i=0;i<250;i++){
        tribes_set_last_error(123);uint32_t size=37,p=invoke(&cpu,7,1,&size);assert(p);memset(memory+p,0x5a,size);
        uint32_t grow[]={p,71};uint32_t q=invoke(&cpu,9,2,grow);assert(q&&q!=p);for(unsigned k=0;k<size;k++)assert(memory[q+k]==0x5a);
        (void)invoke(&cpu,10,1,&q);uint32_t zero[]={7,13};p=invoke(&cpu,8,2,zero);assert(p);for(unsigned k=0;k<91;k++)assert(!memory[p+k]);(void)invoke(&cpu,10,1,&p);
        assert(tribes_get_last_error()==123);
        uint32_t functions[4],expected[]={1,2,3,4};assert(tribes_sdl_memory_functions(cpu._pointer_offset,functions)&&!memcmp(functions,expected,sizeof expected));
        uint32_t empty=0;p=invoke(&cpu,7,1,&empty);assert(p);(void)invoke(&cpu,10,1,&p);
        uint32_t countzero[]={0,13};p=invoke(&cpu,8,2,countzero);assert(p&&!memory[p]);(void)invoke(&cpu,10,1,&p);
        uint32_t nullresize[]={0,0};p=invoke(&cpu,9,2,nullresize);assert(p);uint32_t failresize[]={p,888};assert(!invoke(&cpu,9,2,failresize)&&!strcmp(tribes_sdl_memory_error(),"Out of memory"));(void)invoke(&cpu,10,1,&p);
        uint32_t failure=999;assert(!invoke(&cpu,7,1,&failure)&&!strcmp(tribes_sdl_memory_error(),"Out of memory"));(void)invoke(&cpu,10,1,&empty);
    }return NULL;
}
int main(int argc,char **argv){
    memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory+0x100000,0x80000,PROT_READ|PROT_WRITE));
    assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));
    if(argc>1){_cpu cpu;setup(&cpu,0);uint32_t args[]={1,2,3,4};
        if(!strcmp(argv[1],"cdecl-as-stdcall"))(void)tribes_call_guest(&cpu,2,2,args,cpu._esp-512);
        else if(!strcmp(argv[1],"stdcall-as-cdecl"))(void)tribes_call_guest_cdecl(&cpu,5,4,args,cpu._esp-512);
        else if(!strcmp(argv[1],"below-stack"))(void)tribes_call_guest_cdecl(&cpu,2,2,args,0x100003);
        else if(!strcmp(argv[1],"aligned-free-unreadable")){
            assert(tribes_sdl_set_memory_functions(cpu._pointer_offset,1,2,3,4));
            uint32_t allocation[]={16,65536};uint32_t p=invoke(&cpu,11,2,allocation);
            assert(p&&tribes_memory_permissions_set((p-4)&~4095u,4096,0));
            (void)invoke(&cpu,12,1,&p);
        }else abort();return 99;
    }
    _cpu cpu;setup(&cpu,0);uint32_t functions[]={1,2,3,4};assert(invoke(&cpu,6,4,functions));
    const char *names[]={"malloc_func","calloc_func","realloc_func","free_func"};
    for(unsigned mask=0;mask<15;mask++){
        uint32_t invalid[4];for(unsigned i=0;i<4;i++)invalid[i]=(mask&(1u<<i))?functions[i]:0;
        assert(!invoke(&cpu,6,4,invalid));uint32_t current[4];assert(tribes_sdl_memory_functions(cpu._pointer_offset,current)&&!memcmp(current,functions,sizeof current));
        unsigned first=0;while(mask&(1u<<first))first++;assert(strstr(tribes_sdl_memory_error(),names[first]));
    }
    assert(invoke(&cpu,6,4,functions)&&strstr(tribes_sdl_memory_error(),"malloc_func"));
    uint32_t alignments[]={0,1,3,4,5,7,16,64,4096};
    uint32_t sizes[]={0,1,15,16,37,65536};
    for(unsigned i=0;i<sizeof alignments/sizeof *alignments;i++)for(unsigned j=0;j<sizeof sizes/sizeof *sizes;j++){
        uint32_t alignment=alignments[i]<4?4:alignments[i],size=sizes[j],args[]={alignments[i],size};
        tribes_set_last_error(123);uint32_t p=invoke(&cpu,11,2,args);assert(p&&p%alignment==0);
        uint32_t original;memcpy(&original,memory+p-4,4);
        uint32_t padding=alignment-size%alignment;
        assert(last_size==size+alignment+4+padding&&original+4<p&&p+size+padding<=original+last_size);
        assert(tribes_global_size(cpu._pointer_offset,original)>=last_size);
        for(uint32_t k=0;k<padding;k++)assert(memory[p+size+k]==0);
        memset(memory+p,0x67,size);(void)invoke(&cpu,12,1,&p);
        assert(last_free==original&&tribes_get_last_error()==123);
    }
    uint32_t overflow[][2]={{16,UINT32_MAX},{UINT32_MAX,1},{0x80000000,0}};
    for(unsigned i=0;i<3;i++){uint32_t calls=malloc_calls;assert(!invoke(&cpu,11,2,overflow[i])&&malloc_calls==calls);}
    uint32_t failed[]={5,985};assert(!invoke(&cpu,11,2,failed)&&last_size==999&&!strcmp(tribes_sdl_memory_error(),"Out of memory"));
    uint32_t null_pointer=0,freed=last_free;(void)invoke(&cpu,12,1,&null_pointer);assert(last_free==freed);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(!munmap(memory,UINT64_C(0x100000000)));
    puts("PASS: 54 aligned allocations through registered callbacks with i686 metadata, zeroed padding, matching frees, overflow and failure; 2000 real native allocation/calloc/reallocation/free lifetimes through emitted ARM64 SDL memory wrappers and registered cdecl callbacks, nested stdcall calls, zero-size normalization, NULL-free no-op, allocation failure/error retention and all 15 invalid callback tables, eight concurrent pthreads, copied byte retention/zero fill, unaligned argument/stack guards, parent CPU/flags preserved and thread FP changes propagated; fixtures are not selected QoL/SDL execution");return 0;
}
