#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#ifdef TB_CHECKED_RESULT
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#ifdef TB_CHECKED_RESULT_LOADER
#include "../port/core/guest_address_space.h"
#endif
#endif
extern uint32_t halopad_x87_result(_cpu *,const int64_t *);
static uint64_t base;
static void *worker(void *argument) {
    uint64_t id=(uintptr_t)argument;_cpu states[2]={0};states[0]._pointer_offset=base;states[1]._pointer_offset=base;
    uint32_t addresses[2]={0};
    for(unsigned i=0;i<1000;i++) {
        int64_t first=(int64_t)((id<<56)|i),second=~first;
        uint32_t a=halopad_x87_result(&states[0],&first),b=halopad_x87_result(&states[1],&second);assert(a!=b&&a>=0x2f000000&&b<0x30000000&&a%8==0&&b%8==0);
        if(i)assert(a==addresses[0]&&b==addresses[1]);addresses[0]=a;addresses[1]=b;
        int64_t actual;memcpy(&actual,(void *)(uintptr_t)(base+a),8);assert(actual==first);memcpy(&actual,(void *)(uintptr_t)(base+b),8);assert(actual==second);
    }
    return NULL;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(window!=MAP_FAILED);base=(uintptr_t)window;
#ifdef TB_CHECKED_RESULT
    assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
    tribes_memory_external_configure(base,UINT64_C(0x100000000));
#ifdef TB_CHECKED_RESULT_LOADER
    assert(!mprotect((void *)(uintptr_t)(base+0x200000),65536,PROT_READ|PROT_WRITE));
    assert(tribes_address_space_begin(base));
    assert(tribes_address_space_declare(base,0x200000,65536,0x200000,4,0x1000,4,0x20000));
    assert(tribes_address_space_seal(base));
    TribesCheckedMemoryBackend backend={tribes_memory_initialize_static,tribes_memory_protect_pages};
    const void *targets[]={(const void *)tribes_memory_initialize_static,(const void *)tribes_memory_protect_pages};
    tribes_memory_register_targets(targets,2);assert(tribes_address_space_enable_checked(base,&backend));
#endif
    tribes_memory_external_enable();
#ifdef TB_EXPECT_MANAGER_STOP
    _cpu rejected={0};rejected._pointer_offset=base;int64_t rejected_value=42;
    halopad_x87_result(&rejected,&rejected_value);return 90;
#endif
#endif
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
#ifdef TB_CHECKED_RESULT
    _cpu state={0};state._pointer_offset=base;int64_t value=42;
    uint32_t address=halopad_x87_result(&state,&value);int64_t *result=(void *)(uintptr_t)(base+address);
#ifdef TB_CHECKED_RESULT_LOADER
    uint32_t *query=(void *)(uintptr_t)(base+0x200000);uint32_t page=(uint32_t)sysconf(_SC_PAGESIZE),start=address&~(page-1);
    assert(tribes_virtual_query(base,address,0x200000,28)==28);
    assert(query[1]==0x2f000000&&query[2]==4&&query[4]==0x1000&&query[5]==4&&query[6]==0x20000);
    assert(tribes_address_space_commit_scratch(base,start,page)&&*result==42);
    assert(!tribes_address_space_commit_scratch(base,start+1,page));
    assert(!tribes_address_space_commit_scratch(base,0x30000000,page));
    assert(tribes_virtual_query(base,start+page,0x200000,28)==28&&query[4]==0x10000);
    assert(tribes_virtual_protect(base,start,page,2,0x200000));
    assert(!tribes_address_space_commit_scratch(base,start,page)&&*result==42);
    assert(tribes_virtual_query(base,address,0x200000,28)==28&&query[5]==2);
    assert(tribes_virtual_protect(base,start,page,4,0x200000));
    puts("PASS: scratch commit matches checked permissions and VirtualQuery; neighboring pages and later read-only protection preserved");
#endif
    for(unsigned mode=0;mode<2;mode++){
        pid_t child=fork();assert(child>=0);
        if(!child){
            assert(tribes_memory_permissions_set(address&~4095u,4096,mode?0:1));
            value=99;halopad_x87_result(&state,mode?result:&value);_exit(90);
        }
        int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
        assert(*result==42);
    }
    puts("PASS: checked source/destination faults preserve shared result");
#endif
    uint32_t application=tribes_global_alloc(base,0,1);assert(application==0x30000000);assert(!tribes_global_free(base,application));
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: 16000 wide results, stable/distinct per-CPU pointers, concurrent isolation, separate nonexecutable scratch arena");return 0;
}
