#include "../port/core/guest_scheduling.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>
static int failure,native_nice,calls;
int scheduling_injected_setpriority(int which,id_t who,int nice){assert(which==PRIO_PROCESS&&!who);native_nice=nice;calls++;errno=failure;return failure?-1:0;}
int main(void){
 const unsigned classes[]={0x40,0x4000,0x20,0x8000,0x80};const int nice[]={19,5,0,-5,-10};
 const int errors[]={0,EACCES,EPERM,ESRCH,EINVAL,ENOTSUP,ENOSYS,EIO};const unsigned mapped[]={123,5,5,6,87,50,50,31};
 for(unsigned p=0;p<5;p++)for(unsigned e=0;e<8;e++){
  failure=errors[e];tribes_set_last_error(123);unsigned result=tribes_set_priority_class(UINT32_MAX,classes[p]);assert(result==!failure&&tribes_get_last_error()==mapped[e]&&native_nice==nice[p]);
 }
 assert(calls==40);assert(!tribes_set_priority_class(1,0x20));assert(!tribes_set_priority_class(UINT32_MAX,0x100));assert(!tribes_set_priority_class(UINT32_MAX,0));assert(calls==40);
 puts("PASS: 40 injected scheduling outcomes preserve actual native success/error, map classes and never target another process");
}
