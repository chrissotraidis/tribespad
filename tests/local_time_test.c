#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>
extern void tbimp_GetLocalTime(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static unsigned char *memory;
static void invoke(uint32_t output){
 _cpu cpu;memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x100804;
 memcpy(memory+0x100800,&output,4);memset(memory+0x1007bc,0x79,64);memset(memory+0x100804,0x79,64);
 _cpu before=cpu;struct timeval first,last;assert(!gettimeofday(&first,NULL));
 tribes_set_last_error(123);cpu._esp-=4;tribes_enter(&cpu,1);assert(!gettimeofday(&last,NULL));
 assert(!memcmp(&cpu,&before,sizeof cpu)&&tribes_get_last_error()==123);
 assert(!memcmp(memory+0x100800,&output,4));for(unsigned i=0;i<64;i++)assert(memory[0x1007bc+i]==0x79&&memory[0x100804+i]==0x79);
 uint16_t fields[8];memcpy(fields,memory+output,16);
 assert(fields[0]>=1601&&fields[1]>=1&&fields[1]<=12&&fields[2]<=6&&fields[3]>=1&&fields[3]<=31&&fields[4]<24&&fields[5]<60&&fields[6]<60&&fields[7]<1000);
 /* Independently invert the returned calendar, including the host timezone
    and daylight-saving rule, and bracket its millisecond wall-clock sample. */
 struct tm calendar={0};calendar.tm_year=fields[0]-1900;calendar.tm_mon=fields[1]-1;calendar.tm_mday=fields[3];calendar.tm_hour=fields[4];calendar.tm_min=fields[5];calendar.tm_sec=fields[6];calendar.tm_isdst=-1;
 time_t seconds=mktime(&calendar);assert(seconds!=(time_t)-1&&calendar.tm_wday==fields[2]);
 int64_t stamp=(int64_t)seconds*1000+fields[7];assert(stamp>=(int64_t)first.tv_sec*1000+first.tv_usec/1000&&stamp<=(int64_t)last.tv_sec*1000+last.tv_usec/1000);
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(!mprotect(memory+0x100000,4096,PROT_READ|PROT_WRITE)&&!mprotect(memory+0x200000,4096,PROT_READ|PROT_WRITE));
 assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));
 assert(tribes_memory_permissions_set(0,UINT64_C(0x100000000),0));
 assert(tribes_memory_permissions_set(0x100000,4096,3)&&tribes_memory_permissions_set(0x200000,4096,3));
 const void *targets[]={(void *)tbimp_GetLocalTime,(void *)tribes_return_to_host};tribes_memory_register_targets(targets,2);
 tribes_memory_external_configure((uintptr_t)memory,UINT64_C(0x100000000));tribes_memory_external_enable();
 if(argc>1){uint32_t output=0x200001;if(!strcmp(argv[1],"null"))output=0;else if(!strcmp(argv[1],"short"))output=0x200ff1;else if(!strcmp(argv[1],"readonly")){assert(!mprotect(memory+0x200000,4096,PROT_READ));assert(tribes_memory_permissions_set(0x200000,4096,1));}invoke(output);return 99;}
 tzset();for(unsigned i=0;i<16;i++){
  memset(memory+0x200000,0x79,4096);invoke(0x200001);assert(memory[0x200000]==0x79&&memory[0x200011]==0x79);
  memset(memory+0x200000,0x79,4096);invoke(0x200ff0);assert(memory[0x200fef]==0x79);
 }
 puts("PASS: real local calendar/milliseconds, independent wall-clock bracket, unaligned and page-end 16-byte output, void stdcall/full CPU/stack/LastError preservation");
 assert(!munmap(memory,UINT64_C(0x100000000)));return 0;
}
