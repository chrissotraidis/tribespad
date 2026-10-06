#include <stdint.h>
#include <stdlib.h>
extern void tbimp_NtQueryInformationFile(void),tbimp_NtQueryVolumeInformationFile(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){
 if(address==1)return tbimp_NtQueryInformationFile;
 if(address==2)return tbimp_NtQueryVolumeInformationFile;
 if(address==0xfffff000)return tribes_return_to_host;
 abort();
}
_Noreturn void tribes_unimplemented_dinput(void *cpu,uint32_t method){(void)cpu;(void)method;abort();}
