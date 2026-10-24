/* Starsiege's registry: TribesPad's registry service (port/core/
 * guest_registry.c, included unchanged so its reviewed hash stays valid) plus
 * ANSI value queries on the read-only hardware namespace, which Starsiege uses
 * for the CPU speed. The Starsiege build compiles this file in place of
 * guest_registry.c. */
#include "../core/guest_registry.c"

/* LONG RegQueryValueExA. Hardware keys use the same values as the UTF-16 path
   (ASCII strings convert exactly); settings keys use TribesPad's ANSI path. */
uint32_t ss_reg_query_value_ex_a(uint64_t base,uint32_t key,uint32_t name,uint32_t reserved,uint32_t type,uint32_t data,uint32_t size){
 pthread_mutex_lock(&mutex);Key *entry=find(base,key);int kind=entry?path_kind(entry->path):-1;uint32_t access=entry?entry->access:0;pthread_mutex_unlock(&mutex);
 if(!entry||kind<0)return tribes_reg_query_value_ex_a(base,key,name,reserved,type,data,size);
 if(reserved||(data&&!size)||(type&&!range(base,type,4))||(size&&!range(base,size,4)))return 87;
 if(!(access&KEY_QUERY_VALUE))return 5;
 char field[PATH_BYTES];int error=ansi_name(base,name,field,sizeof field);if(error)return (uint32_t)error;
 unsigned char bytes[512];uint32_t vtype,length;uint32_t status=value(field,bytes,&vtype,&length);if(status)return status;
 if(vtype==1){length/=2;for(uint32_t i=0;i<length;i++)bytes[i]=bytes[2*i];}
 uint32_t capacity=0;if(data)memcpy(&capacity,(void *)(uintptr_t)(base+size),4);
 if(data&&capacity>=length&&!range(base,data,length))return 87;
 if(type)put32(base,type,vtype);
 if(size)put32(base,size,length);
 if(!data)return 0;
 if(capacity<length)return 234;
 memcpy((void *)(uintptr_t)(base+data),bytes,length);return 0;
}

