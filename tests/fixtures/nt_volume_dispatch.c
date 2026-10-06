#include <stdint.h>
#include <stdlib.h>
#include <sys/mount.h>
extern void tbimp_NtQueryVolumeInformationFile(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){
 if(address==1)return tbimp_NtQueryVolumeInformationFile;
 if(address==0xfffff000)return tribes_return_to_host;
 abort();
}
int unsafe_volume_output(int fd,void *out){return fstatfs(fd,out);}
_Noreturn void tribes_unimplemented_dinput(void *cpu,uint32_t method){(void)cpu;(void)method;abort();}
