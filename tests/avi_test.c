#include "../port/core/guest_avi.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t);
static void *worker(void *arg){(void)arg;for(unsigned i=0;i<1000;i++){tribes_avi_file_init();tribes_avi_file_exit();}return NULL;}
int main(int argc,char **argv){
 (void)argv;unsigned char *memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));
#ifdef CHECKED
 tribes_memory_external_configure((uintptr_t)memory,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 _cpu cpu;memset(&cpu,0x7d,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x8000;_cpu before=cpu;
 if(argc>1){tribes_enter(&cpu,2);assert(0);}
 assert(!tribes_avi_reference_count());tribes_enter(&cpu,1);tribes_enter(&cpu,1);assert(tribes_avi_reference_count()==2&&!memcmp(&cpu,&before,sizeof cpu));
 pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(tribes_avi_reference_count()==2);
 tribes_enter(&cpu,2);assert(tribes_avi_reference_count()==1);tribes_enter(&cpu,2);assert(!tribes_avi_reference_count()&&!memcmp(&cpu,&before,sizeof cpu));
 assert(!munmap(memory,UINT64_C(0x100000000)));puts("PASS: emitted AVI void ABI preserves CPU; nested/concurrent library lifetimes balance; no media-open/decode claim");return 0;
}
