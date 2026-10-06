#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
extern void tbimp_ReadFile(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){
 if(address==1)return tbimp_ReadFile;
 if(address==0xfffff000)return tribes_return_to_host;
 abort();
}
ssize_t unsafe_read_output(int fd,void *output,size_t bytes){return read(fd,output,bytes);}
_Noreturn void tribes_unimplemented_dinput(void *cpu,uint32_t method){(void)cpu;(void)method;abort();}
