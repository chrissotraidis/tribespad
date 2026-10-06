/* Fixed legacy guest API identity, not the native host OS version.
   https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversionexa */
#include "guest_version.h"
#include "guest_heap.h"
#include <string.h>
enum { guest_major=5, guest_minor=1, guest_build=2600 };
/* Packed NT version of the same declared legacy profile as GetVersionExA.
   https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversion */
uint32_t tribes_get_version(void){return (guest_build<<16)|(guest_minor<<8)|guest_major;}
uint32_t tribes_get_version_ex(uint64_t base,uint32_t output) {
    if(!output||output>UINT32_MAX-3||base>UINTPTR_MAX-output-3){tribes_set_last_error(87);return 0;}
    unsigned char *destination=(void *)(uintptr_t)(base+output);uint32_t size;
    memcpy(&size,destination,4);
    if((size!=148&&size!=156)||output>UINT32_MAX-(size-1)||base>UINTPTR_MAX-output-(size-1)){tribes_set_last_error(87);return 0;}
    unsigned char value[156]={0};uint32_t fields[]={size,guest_major,guest_minor,guest_build,2};memcpy(value,fields,sizeof fields);
    memcpy(value+20,"Service Pack 3",15);
    if(size==156){uint16_t sp=3,suite=0x100;memcpy(value+148,&sp,2);memcpy(value+152,&suite,2);value[154]=1;}
    memcpy(destination,value,size);return 1;
}
