#include "guest_system.h"
#include "guest_heap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* Original x86 SYSTEM_INFO is 36 bytes, including DWORD pointer/mask fields.
   CPU identity matches x86_cpuid's family 6/model 3/stepping 3. It does not
   describe the physical Apple CPU or advertise additional instruction support.
   The 32-bit group exposes up to 32 online native processors, consistent with
   the native critical-section service's single/multiprocessor distinction.
   https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-system_info */
/* GetNativeSystemInfo uses the same profile: this runtime exposes x86 only,
   with no x64 execution environment. Windows likewise reports x86 for an
   x86-only compatibility environment on ARM64. It must not expose Apple
   pointers, backing-page size, or ARM64 as guest execution capabilities.
   https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getnativesysteminfo */
void tribes_get_system_info(uint64_t base,uint32_t output) {
    if(!output||(uint64_t)output+36>UINT64_C(0x100000000)||base>UINTPTR_MAX-(uint64_t)output-36){fprintf(stderr,"TribesPad: invalid x86 SYSTEM_INFO output\n");abort();}
    long online=sysconf(_SC_NPROCESSORS_ONLN);
    if(online<1){fprintf(stderr,"TribesPad: native processor count unavailable\n");abort();}
    uint32_t processors=online>32?32:(uint32_t)online;
    uint32_t mask=processors==32?UINT32_MAX:(1u<<processors)-1;
    uint32_t words[9]={0,TRIBES_GUEST_PAGE_SIZE,TRIBES_GUEST_MINIMUM_ADDRESS,
        TRIBES_GUEST_MAXIMUM_ADDRESS,mask,processors,586,
        TRIBES_GUEST_ALLOCATION_GRANULARITY,0x03030006};
    memcpy((void *)(uintptr_t)(base+output),words,sizeof words);
}

/* One guest group and one UMA node, using actual native CPU samples. A sample
   is not an affinity guarantee: the host can migrate the thread immediately. */
static uint32_t processor_count(void) {
    long count=sysconf(_SC_NPROCESSORS_ONLN);
    if(count<1){fprintf(stderr,"TribesPad: native processor count unavailable\n");abort();}
    return count>32?32:(uint32_t)count;
}
static int field_range(uint64_t base,uint32_t pointer,uint32_t size) {
    return pointer&&(uint64_t)pointer+size<=UINT64_C(0x100000000)&&base<=UINTPTR_MAX-pointer-(size-1);
}
void tribes_get_current_processor_number_ex(uint64_t base,uint32_t output) {
    if(!field_range(base,output,4)){fprintf(stderr,"TribesPad: invalid PROCESSOR_NUMBER output\n");abort();}
    size_t number;
    if(pthread_cpu_number_np(&number)||number>=processor_count()) { fprintf(stderr,"TribesPad: native CPU sample outside supported guest group\n");abort(); }
    unsigned char bytes[4]={0,0,(unsigned char)number,0};memcpy((void *)(uintptr_t)(base+output),bytes,4);
}
uint32_t tribes_get_numa_processor_node_ex(uint64_t base,uint32_t processor,uint32_t output) {
    if(!field_range(base,processor,4)||!field_range(base,output,2)){tribes_set_last_error(87);return 0;}
    unsigned char bytes[4];memcpy(bytes,(void *)(uintptr_t)(base+processor),4);
    int valid=!bytes[0]&&!bytes[1]&&bytes[2]<processor_count()&&!bytes[3];uint16_t node=valid?0:UINT16_MAX;
    memcpy((void *)(uintptr_t)(base+output),&node,2);if(!valid)tribes_set_last_error(87);return valid;
}
uint32_t tribes_get_numa_processor_node(uint64_t base,uint32_t processor,uint32_t output) {
    if(!field_range(base,output,1)){tribes_set_last_error(87);return 0;}
    int valid=(uint8_t)processor<processor_count();uint8_t node=valid?0:UINT8_MAX;
    memcpy((void *)(uintptr_t)(base+output),&node,1);if(!valid)tribes_set_last_error(87);return valid;
}
uint32_t tribes_get_numa_node_processor_mask_ex(uint64_t base,uint32_t node,uint32_t output) {
    if((uint16_t)node||!field_range(base,output,12)){tribes_set_last_error(87);return 0;}
    uint32_t count=processor_count(),words[3]={count==32?UINT32_MAX:(1u<<count)-1,0,0};
    memcpy((void *)(uintptr_t)(base+output),words,12);return 1;
}

/* Highest identifier, not a count. The declared UMA topology has node zero. */
uint32_t tribes_get_numa_highest_node_number(uint64_t base,uint32_t output) {
    if(!field_range(base,output,4)){tribes_set_last_error(87);return 0;}
    uint32_t highest=0;memcpy((void *)(uintptr_t)(base+output),&highest,4);return 1;
}
