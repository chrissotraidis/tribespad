#include "../port/core/guest_virtual_memory.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
extern void vm_store(unsigned char *,unsigned char);
extern unsigned char vm_load(const unsigned char *);
extern void vm_copy(unsigned char *,const unsigned char *,uint32_t);
static uint64_t base;static unsigned char *memory;static uint32_t region;
static void query(uint32_t address,uint32_t size,uint32_t state,uint32_t protection){
 uint32_t q[7];assert(tribes_virtual_memory_query(base,address,q));
 assert(q[0]==address&&q[3]==size&&q[4]==state&&q[5]==protection);
}
static uint32_t alloc(uint32_t address,uint32_t size,uint32_t type,uint32_t protection){
 tribes_set_last_error(123);uint32_t p=tribes_virtual_alloc(base,address,size,type,protection);assert(p&&tribes_get_last_error()==123);return p;
}
static void zero(uint32_t address){for(unsigned i=0;i<4096;i++)assert(!vm_load(memory+address+i));}
static void nt(uint32_t address,uint32_t protection){
 uint32_t size=4096;memcpy(memory+0x100000,&address,4);memcpy(memory+0x100004,&size,4);
 tribes_set_last_error(321);assert(!tribes_nt_allocate_virtual_memory_ex(base,UINT32_MAX,0x100000,0x100004,0x1000,protection,0,0));assert(tribes_get_last_error()==321);
 uint32_t out;memcpy(&out,memory+0x100000,4);assert(out==address);memcpy(&out,memory+0x100004,4);assert(out==4096);
}
static atomic_int writer_started,writer_finished;
static void *nested_writer(void *unused){
 (void)unused;atomic_store(&writer_started,1);
 assert(tribes_virtual_free(base,region+4096,4096,0x4000));atomic_store(&writer_finished,1);return NULL;
}
static void nested_read_regression(void){
 uint32_t outer=tribes_memory_access_begin(memory+region,1,1);assert(outer);
 pthread_t writer;assert(!pthread_create(&writer,NULL,nested_writer,NULL));
 while(!atomic_load(&writer_started))sched_yield();
 /* Let the competing writer queue behind the held reader. The old recursive
    pthread read lock deadlocks here on Darwin with writer preference. */
 usleep(30000);assert(!atomic_load(&writer_finished));
 for(unsigned i=0;i<100;i++){assert(vm_load(memory+region)==0x33);unsigned char byte;vm_copy(&byte,memory+region,1);assert(byte==0x33);}
 assert(!atomic_load(&writer_finished));tribes_memory_access_end(outer);
 assert(!pthread_join(writer,NULL)&&atomic_load(&writer_finished));
 alloc(region+4096,4096,0x1000,4);zero(region+4096);
}
static void *toggle(void *unused){
 (void)unused;
 for(unsigned i=0;i<600;i++){
  assert(tribes_virtual_free(base,region+4096,4096,0x4000));
  assert(tribes_virtual_alloc(base,region+4096,4096,0x1000,4)==region+4096);
  assert(!vm_load(memory+region+4096));vm_store(memory+region+4096,0x82);
 }
 return NULL;
}
static void *peer(void *unused){
 (void)unused;for(unsigned i=0;i<6000;i++){vm_store(memory+region,(unsigned char)i);assert(vm_load(memory+region)==(unsigned char)i);assert(vm_load(memory+region+12288)==0x51);}return NULL;
}
static void *small_allocations(void *unused){
 (void)unused;for(unsigned i=0;i<100;i++){uint32_t p=alloc(0,4096,0x3000,4);zero(p);vm_store(memory+p,0xa7);assert(tribes_virtual_free(base,p,0,0x8000));}return NULL;
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x100000,65536,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
 tribes_memory_external_configure(base,UINT64_C(0x100000000));
 const TribesVirtualMemoryBackend backend={tribes_memory_vm_initialize,tribes_memory_vm_change};
 const void *targets[]={(const void *)tribes_memory_vm_initialize,(const void *)tribes_memory_vm_change};tribes_memory_register_targets(targets,2);tribes_memory_external_enable();
 assert(tribes_virtual_memory_enable_checked(base,&backend));assert(!tribes_virtual_memory_enable_checked(base,&backend));
 uint32_t p=alloc(0,4096,0x3000,4);assert(p==0x60030000);query(p,4096,0x1000,4);query(p+4096,(uint32_t)sysconf(_SC_PAGESIZE)-4096,0x2000,0);zero(p);
 if(argc>1&&!strcmp(argv[1],"padding")){vm_store(memory+p+4096,3);return 99;}
 assert(tribes_virtual_free(base,p,0,0x8000));
 if(argc>1&&!strcmp(argv[1],"released")){(void)vm_load(memory+p);return 99;}
 region=alloc(0,65536,0x2000,4);assert(region==p);query(region,65536,0x2000,0);
 alloc(region,4096,0x1000,4);alloc(region+4096,4096,0x1000,4);nt(region+12288,4);vm_store(memory+region,0x33);vm_store(memory+region+4096,0x44);vm_store(memory+region+12288,0x51);nt(region+12288,2);
 query(region,8192,0x1000,4);query(region+8192,4096,0x2000,0);query(region+12288,4096,0x1000,2);
 if(argc>1&&!strcmp(argv[1],"reserved")){(void)vm_load(memory+region+8192);return 99;}
 if(argc>1&&!strcmp(argv[1],"readonly")){vm_store(memory+region+12288,9);return 99;}
 if(argc>1&&!strcmp(argv[1],"cross-page")){unsigned char bytes[16]={0};vm_copy(memory+region+8192-8,bytes,16);return 99;}
 assert(alloc(region+12288,4096,0x1000,4)==region+12288);query(region+12288,4096,0x1000,2);assert(vm_load(memory+region+12288)==0x51);
 if(argc>1&&!strcmp(argv[1],"recommit-readonly")){vm_store(memory+region+12288,9);return 99;}
 assert(tribes_virtual_free(base,region+4097,2,0x4000));query(region+4096,8192,0x2000,0);assert(vm_load(memory+region)==0x33&&vm_load(memory+region+12288)==0x51);
 if(argc>1&&!strcmp(argv[1],"decommitted")){(void)vm_load(memory+region+4096);return 99;}
 alloc(region+4097,1,0x1000,4);zero(region+4096);
 /* NT changes protection without resetting committed contents. */
 nt(region,1);nt(region,4);assert(vm_load(memory+region)==0x33);
 /* Zero-fill a new no-access page before its first readable transition. */
 alloc(region+8192,4096,0x1000,1);query(region+8192,4096,0x1000,1);nt(region+8192,4);zero(region+8192);
 /* A range crossing native groups retains all existing committed bytes. */
 alloc(region+16384,8192,0x1000,4);vm_store(memory+region+16384,0x61);assert(tribes_virtual_free(base,region+8192,4096*3,0x4000));
 assert(vm_load(memory+region)==0x33&&vm_load(memory+region+4096)==0&&vm_load(memory+region+20480)==0);nt(region+12288,4);zero(region+12288);vm_store(memory+region+12288,0x51);nt(region+12288,2);
 nested_read_regression();
 pthread_t threads[4];assert(!pthread_create(threads,NULL,toggle,NULL));assert(!pthread_create(threads+1,NULL,peer,NULL));assert(!pthread_create(threads+2,NULL,small_allocations,NULL));assert(!pthread_create(threads+3,NULL,small_allocations,NULL));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 assert(tribes_virtual_free(base,region,0,0x8000));p=alloc(0,65536,0x3000,4);assert(p==region);for(unsigned i=0;i<65536;i++)assert(!vm_load(memory+p+i));assert(tribes_virtual_free(base,p,0,0x8000));
 assert(!munmap(memory,UINT64_C(0x100000000)));puts("PASS: original 4KiB request; guarded page isolation, query metadata, zero-fill, NT/classic recommit, cross-group decommit, nested read with queued writer, and concurrent neighbor access (600 toggles, 6000 peer writes, 200 allocations)");return 0;
}
