#include "../port/core/guest_messages.h"
#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../port/core/system_messages.inc"
extern void tbimp_FormatMessageA(void),tbimp_FormatMessageW(void),tbimp_LocalAlloc(void),tbimp_LocalFree(void),tbimp_LocalSize(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t address){switch(address){case 1:return (void *)tbimp_FormatMessageA;case 2:return (void *)tbimp_FormatMessageW;case 3:return (void *)tbimp_LocalAlloc;case 4:return (void *)tbimp_LocalFree;case 5:return (void *)tbimp_LocalSize;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args){
 uint32_t top=cpu->_esp;unsigned char *memory=(void *)(uintptr_t)cpu->_pointer_offset;cpu->_esp-=count*4;memcpy(memory+cpu->_esp,args,count*4);_cpu saved=*cpu;
 tribes_enter(cpu,target);saved._eax=cpu->_eax;saved._esp=top;assert(!memcmp(cpu,&saved,sizeof saved)&&!memcmp(memory+top-count*4,args,count*4));return cpu->_eax;
}
static unsigned char *memory;static uint64_t base;static unsigned checks;
static uint32_t call(unsigned width,uint32_t flags,uint32_t id,uint32_t lang,uint32_t out,uint32_t n){return (width==1?tribes_format_message_a:tribes_format_message_w)(base,flags,0xfffffff0,id,lang,out,n,0xfffffff0);}
static void unsupported(uint32_t flags,uint32_t id){
 memset(memory+0x10000,0xa7,4096);pid_t child=fork();assert(child>=0);if(!child){call(1,flags,id,0x409,0x10101,256);_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);for(unsigned i=0;i<4096;i++)assert(memory[0x10000+i]==0xa7);
}
static void fault(unsigned width,int allocated){
 memset(memory+0x10000,0xa7,4096);assert(tribes_memory_permissions_set(0x10000,4096,1));
 pid_t child=fork();assert(child>=0);if(!child){call(width,0x1200|(allocated?0x100:0),126,0x409,0x10000,256);_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 for(unsigned i=0;i<4096;i++)assert(memory[0x10000+i]==0xa7);assert(tribes_memory_permissions_set(0x10000,4096,3));
}
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(!mprotect(memory+0x10000,0x10000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
 for(unsigned width=1;width<=2;width++)for(unsigned i=0;i<sizeof system_messages/sizeof system_messages[0];i++)for(unsigned wrap=0;wrap<2;wrap++){
  const char *text=system_messages[i].text;size_t len=strlen(text)-(wrap?2:0);uint32_t id=system_messages[i].id;
  for(unsigned allocated=0;allocated<2;allocated++){
   uint32_t output=0x10101,flags=0x1200|(wrap?255:0)|(allocated?0x100:0);memset(memory+0x10000,0x7b,4096);tribes_set_last_error(12345);
   assert(call(width,flags,id,0x400,output,allocated?1024:(uint32_t)len+1)==len&&tribes_get_last_error()==12345);
   uint32_t destination=output;if(allocated){memcpy(&destination,memory+output,4);assert(destination>=0x30000000&&tribes_global_size(base,destination)>=1024*width);}
   for(size_t k=0;k<=len;k++){uint16_t unit=0;if(width==1)unit=memory[destination+k];else memcpy(&unit,memory+destination+2*k,2);assert(unit==(k<len?(unsigned char)text[k]:0));}
   assert(memory[output-1]==0x7b&&memory[output+(allocated?4:(len+1)*width)]==0x7b);
   if(allocated)assert(!tribes_global_free(base,destination));checks++;
  }
  memset(memory+0x10000,0x7b,4096);assert(!call(width,0x1200|(wrap?255:0),id,0x409,0x10101,(uint32_t)len)&&tribes_get_last_error()==122);for(unsigned k=0;k<4096;k++)assert(memory[0x10000+k]==0x7b);
 }
 assert(call(1,0x1200,0x8007007e,0,0x10101,256)==strlen("The specified module could not be found.\r\n"));
 assert(!call(1,0x1300,126,0x411,0x10101,0)&&tribes_get_last_error()==1815);
 assert(!call(1,0x1300,126,0x409,0xfffffffe,0)&&tribes_get_last_error()==87);
 assert(!call(2,0x1300,126,0x409,0x10101,UINT32_MAX)&&tribes_get_last_error()==8);
 assert(!call(1,0x1400,126,0x409,0x10101,256)&&tribes_get_last_error()==87);
 fault(1,0);fault(2,0);fault(1,1);fault(2,1);
 unsupported(0x1200,0xfefefefe);unsupported(0x1201,126);unsupported(0x600,126);
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x1e001;
 for(unsigned width=1;width<=2;width++){
  uint32_t args[7]={0x1300,0,126,0x400,0x10101,0,0};tribes_set_last_error(126);
  assert(invoke(&cpu,width,7,args)==strlen("The specified module could not be found.\r\n")&&tribes_get_last_error()==126);
  uint32_t pointer;memcpy(&pointer,memory+0x10101,4);assert(invoke(&cpu,5,1,&pointer)>=80);assert(!invoke(&cpu,4,1,&pointer));
 }
 uint32_t local_args[2]={0x40,37},local=invoke(&cpu,3,2,local_args);assert(local);for(unsigned i=0;i<37;i++)assert(!memory[local+i]);assert(!invoke(&cpu,4,1,&local));
 assert(!tribes_global_free(base,0));assert(tribes_global_free(base,0x123456)==0x123456&&tribes_get_last_error()==6);
 printf("PASS: %u catalog/output cases, ANSI/UTF16, minimum allocation, short-buffer preservation, language/HRESULT/errors, LocalFree storage contract and four checked pre-write faults\n",checks);return 0;
}
