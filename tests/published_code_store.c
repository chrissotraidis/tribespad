#include <stdint.h>
#include <string.h>
void emitted_store(uint32_t *p,uint32_t value){memcpy(p,&value,sizeof value);}
void emitted_copy(void *destination,const void *source,uint64_t size){memcpy(destination,source,size);}
