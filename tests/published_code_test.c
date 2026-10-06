#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
extern void emitted_store(uint32_t *,uint32_t);
extern void emitted_copy(void *,const void *,uint64_t);
static unsigned char *memory;
static unsigned rejected;
static void fault(unsigned mode){
 unsigned char before[8192];memcpy(before,memory,sizeof before);
 pid_t child=fork();assert(child>=0);
 if(!child){
  if(mode==0)emitted_store((uint32_t *)(memory+4094),0);
  if(mode==1)emitted_store((uint32_t *)(memory+4086),0);
  if(mode==2)emitted_copy(memory+4080,before,64);
  if(mode==3){assert(tribes_memory_permissions_set(0,8192,3));emitted_store((uint32_t *)(memory+4094),0);}
  _exit(0);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 assert(!memcmp(before,memory,sizeof before));rejected++;
}
int main(void){
 memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 uint64_t base=(uintptr_t)memory;assert(tribes_memory_checks_configure(base,65536));
 for(unsigned i=0;i<8192;i++)memory[i]=(unsigned char)(i*29);
 unsigned char expected[32];memcpy(expected,memory+4088,sizeof expected);
 TribesPublishedCodeRange range={4088,sizeof expected,expected};
 assert(!tribes_memory_code_published(base));assert(!tribes_memory_publish_code(base,NULL,1));
 assert(!tribes_memory_publish_code(base,&range,0));
 TribesPublishedCodeRange invalid={4088,32,memory+4088};assert(!tribes_memory_publish_code(base,&invalid,1));
 TribesPublishedCodeRange overlaps[2]={range,range};assert(!tribes_memory_publish_code(base,overlaps,2));
 expected[31]^=1;assert(!tribes_memory_publish_code(base,&range,1));assert(!tribes_memory_code_published(base));expected[31]^=1;
 assert(tribes_memory_permissions_set(4096,4096,0));assert(!tribes_memory_publish_code(base,&range,1));assert(tribes_memory_permissions_set(4096,4096,3));
 emitted_store((uint32_t *)(memory+4094),0x12345678);memcpy(expected,memory+4088,sizeof expected);
 assert(tribes_memory_publish_code(base,&range,1));assert(tribes_memory_code_published(base));
 assert(!tribes_memory_publish_code(base,&range,1));
 /* Caller storage is not retained. Protection transactions cannot unfreeze it. */
 range.address=0;memset(expected,0,sizeof expected);
 emitted_store((uint32_t *)(memory+4084),0xabababab);emitted_store((uint32_t *)(memory+4120),0xcdcdcdcd);
 uint32_t token=tribes_memory_access_begin(memory+4088,32,1);assert(token==1);tribes_memory_access_end(token);
 fault(0);fault(1);fault(2);fault(3);
 puts("PASS: atomic code-byte publication, failed validation/retry, immutable owned intervals, neighbor writes, four emitted pre-write faults");
 return rejected==4?0:1;
}
