/* Apple scheduling policy for the current guest process/thread only.
 * Ordinary Windows classes map to explicit POSIX niceness requests. This
 * preserves host permission failures; it is not Windows scheduler parity.
 * The guest group is unrestricted. Apple has no strict CPU-mask pinning API,
 * so SetThreadAffinityMask reports ERROR_NOT_SUPPORTED without changing it.
 * See config/runtime/scheduling.json for the declared policy and references. */
#include "guest_scheduling.h"
#include "guest_heap.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
static uint32_t fail(uint32_t error){tribes_set_last_error(error);return 0;}
uint32_t tribes_set_priority_class(uint32_t process,uint32_t priority){
 if(process!=UINT32_MAX)return fail(6);
 int nice;
 switch(priority){
 case 0x40:nice=19;break;
 case 0x4000:nice=5;break;
 case 0x20:nice=0;break;
 case 0x8000:nice=-5;break;
 case 0x80:nice=-10;break;
 case 0x100:case 0x100000:case 0x200000:return fail(50);
 default:return fail(87);
 }
 /* PRIO_PROCESS and who=0 cannot change another process or process group. */
 if(!setpriority(PRIO_PROCESS,0,nice))return 1;
 switch(errno){
 case EACCES:case EPERM:return fail(5);
 case ESRCH:return fail(6);
 case EINVAL:return fail(87);
 case ENOTSUP:case ENOSYS:return fail(50);
 default:return fail(31);
 }
}
static uint32_t available_mask(void){
 long n=sysconf(_SC_NPROCESSORS_ONLN);
 if(n<1)return 0;
 return n>=32?UINT32_MAX:(1u<<(unsigned)n)-1;
}
static int field(uint64_t base,uint32_t p){return base&&p&&(uint64_t)p+4<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-p-3;}
uint32_t tribes_get_process_affinity_mask(uint64_t base,uint32_t process,uint32_t process_mask,uint32_t system_mask){
 if(process!=UINT32_MAX)return fail(6);
 if(!field(base,process_mask)||!field(base,system_mask))return fail(87);
 uint32_t mask=available_mask();if(!mask)return fail(31);
 memcpy((void *)(uintptr_t)(base+process_mask),&mask,4);
 memcpy((void *)(uintptr_t)(base+system_mask),&mask,4);
 return 1;
}
uint32_t tribes_set_thread_affinity_mask(uint32_t thread,uint32_t mask){
 if(thread!=UINT32_MAX-1)return fail(6);
 uint32_t available=available_mask();if(!available)return fail(31);
 if(!mask||(mask&~available))return fail(87);
 return fail(50);
}
