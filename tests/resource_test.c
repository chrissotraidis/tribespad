#include "../port/core/guest_modules.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;
#include "resources.inc"
static uint32_t call(unsigned slot,unsigned target,const uint32_t *args,unsigned n,uint32_t error){
 _cpu c;memset(&c,0xa5,sizeof c);c._pointer_offset=base;c._esp=0x10003+slot*4096;uint32_t sp=c._esp;
 memset(memory+sp-68,0x71,64);memset(memory+sp+4*n,0x71,64);memcpy(memory+sp,args,4*n);_cpu before=c;
 tribes_set_last_error(12345);tribes_enter(&c,target);before._eax=c._eax;before._esp+=4*n;
 assert(!memcmp(&before,&c,sizeof c)&&!memcmp(memory+sp,args,4*n));
 for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x71&&memory[sp+4*n+i]==0x71);
 assert(tribes_get_last_error()==error);return c._eax;
}

static void *worker(void *arg){unsigned slot=(unsigned)(uintptr_t)arg;
 for(unsigned n=0;n<1000;n++){
  uint32_t find[]={0x10000000,102,10};uint32_t h=call(slot,1,find,3,12345);
  assert(h==0x10000000+resources[0].entry_rva);
  uint32_t args[]={0x10000000,h};assert(call(slot,2,args,2,12345)==21234);
  uint32_t data=call(slot,3,args,2,12345);assert(data==0x104760f0);
  assert(call(slot,4,&data,1,12345)==data);
 }
 return NULL;
}
int main(int argc,char **argv){
 assert(argc==3);memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));assert(!mprotect(memory+0x10000000,IMAGE_SIZE,PROT_READ|PROT_WRITE));
 FILE *in=fopen(argv[1],"rb");assert(in);assert(fread(memory+0x10000000,1,MAPPED_SIZE,in)==MAPPED_SIZE);assert(!fclose(in));
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 assert(tribes_module_register_image(base,0x10000000,IMAGE_SIZE,"C:\\IFC22.dll",1));
 TribesImageResource bad=resources[0];bad.data_rva=IMAGE_SIZE-1;
 assert(!tribes_module_register_resources(base,0x10000000,&bad,1)&&errno==EINVAL);
 bad=resources[0];bad.entry_rva=IMAGE_SIZE-15;assert(!tribes_module_register_resources(base,0x10000000,&bad,1));
 bad=resources[0];bad.data_rva=bad.entry_rva;assert(!tribes_module_register_resources(base,0x10000000,&bad,1));
 TribesImageResource duplicates[]={resources[0],resources[0]};duplicates[1].language=0;assert(!tribes_module_register_resources(base,0x10000000,duplicates,2));
 assert(!tribes_module_register_resources(base,0x20000000,resources,3));
 assert(tribes_module_register_resources(base,0x10000000,resources,3));
 assert(!tribes_module_register_resources(base,0x10000000,resources,3)&&errno==EALREADY);
 for(unsigned i=0;i<3;i++){
  uint32_t args[]={0,resources[i].name,resources[i].type};uint32_t h=call(0,1,args,3,12345);assert(h==0x10000000+resources[i].entry_rva);
  assert(!call(0,4,&h,1,12345));uint32_t data=0x10000000+resources[i].data_rva;assert(!call(0,4,&data,1,12345));
  uint32_t pair[]={0,h};assert(call(0,2,pair,2,12345)==resources[i].size);assert(call(0,3,pair,2,12345)==data);assert(call(0,4,&data,1,12345)==data);
  pair[0]=0x20000000;assert(!call(0,2,pair,2,6));assert(!call(0,3,pair,2,6));pair[0]=0;pair[1]=data;assert(!call(0,2,pair,2,6));assert(!call(0,3,pair,2,6));
 }
 uint32_t absent[]={0,999,10};assert(!call(0,1,absent,3,1814));absent[2]=999;assert(!call(0,1,absent,3,1813));absent[0]=123;assert(!call(0,1,absent,3,6));
 uint32_t zero=0;assert(!call(0,4,&zero,1,12345));assert(!tribes_lock_resource(base+1,0x104760f0));
 /* The catalog is private, even if the guest changes its PE directory copy. */
 for(unsigned i=0;i<3;i++)memset(memory+0x10000000+resources[i].entry_rva,0xff,16);
 strcpy((char *)memory+0x4001,"IFC22.dll");assert(tribes_load_library(base,0x4001)==0x10000000);assert(tribes_free_library(base,0x10000000));
 pthread_t threads[4];for(uintptr_t i=1;i<=4;i++)assert(!pthread_create(threads+i-1,NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 FILE *out=fopen(argv[2],"wb");assert(out);uint32_t ptr=tribes_lock_resource(base,0x104760f0);assert(ptr);assert(fwrite(memory+ptr,1,21234,out)==21234);assert(!fclose(out));
 puts("PASS: four emitted resource ABIs, real mapped ZIP, three catalog entries, 4000 concurrent sequences, CPU/stack/LastError preservation, invalid typed handles and modules, missing IDs, private bounds, permanent mapped-image lifetime");return 0;
}
