#include <stdint.h>
#include <string.h>
void vm_store(unsigned char *p,unsigned char value){*p=value;}
unsigned char vm_load(const unsigned char *p){return *p;}
void vm_copy(unsigned char *p,const unsigned char *q,uint32_t size){memcpy(p,q,size);}
