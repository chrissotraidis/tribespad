/* Trusted foreign-call boundary. Constructors register immutable AOT targets;
   the table is frozen before guest execution. No host library receives a guest
   pointer under a host-only contract. Unknown or indirect foreign calls stop. */
#include "guest_external_checks.h"
#include "guest_memory_checks.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static uintptr_t base;
static uint64_t span;
static uintptr_t *targets;
static size_t target_count;
static int enabled;
static _Noreturn void fault(const char *kind,const char *name){fprintf(stderr,"TribesPad: external memory contract stop: %s: %s\n",kind,name);abort();}
void tribes_memory_external_configure(uint64_t address,uint64_t size){if(enabled||base||!address||!size||size>UINTPTR_MAX-address)abort();base=(uintptr_t)address;span=size;}
void tribes_memory_register_targets(const void *const *values,uint64_t count){
 if(enabled||count>SIZE_MAX/sizeof *targets-target_count)abort();size_t next=target_count+(size_t)count;if(!next)return;
 uintptr_t *replacement=realloc(targets,next*sizeof *targets);if(!replacement)abort();targets=replacement;
 for(uint64_t i=0;i<count;i++)targets[target_count++]=(uintptr_t)values[i];
}
static int compare(const void *left,const void *right){uintptr_t a,b;memcpy(&a,left,sizeof a);memcpy(&b,right,sizeof b);return (a>b)-(a<b);}
void tribes_memory_external_enable(void){if(enabled||!base||!target_count)abort();qsort(targets,target_count,sizeof *targets,compare);enabled=1;}
void tribes_memory_external_gate(const char *name,uint32_t allowed){if(enabled&&!allowed)fault("unverified call",name);}
void tribes_memory_external_pointer(const char *name,const void *pointer){uintptr_t p=(uintptr_t)pointer;if(enabled&&p>=base&&p-base<span)fault("guest pointer passed to host-only call",name);}
void tribes_memory_indirect_target(const void *pointer){
 if(!enabled)return;uintptr_t target=(uintptr_t)pointer;size_t low=0,high=target_count;
 while(low<high){size_t middle=low+(high-low)/2;if(targets[middle]<target)low=middle+1;else high=middle;}
 if(low==target_count||targets[low]!=target)fault("unregistered indirect target","<indirect>");
}
void tribes_memory_callback_target(const void *pointer,uint32_t nullable){if(!pointer&&nullable)return;tribes_memory_indirect_target(pointer);}
static unsigned char string_byte(const void *pointer,uint64_t offset,const char *name){
 uintptr_t first=(uintptr_t)pointer;uint64_t remaining=UINT64_MAX;
 if(base&&first>=base&&first-base<span)remaining=span-(first-base);
 if(offset>=remaining||offset>UINTPTR_MAX-first)fault("string crosses guest address window",name);
 const volatile unsigned char *p=(const volatile unsigned char *)(first+(uintptr_t)offset);
 uint32_t token=tribes_memory_access_begin((const void *)p,1,1);unsigned char value=*p;tribes_memory_access_end(token);return value;
}
uint64_t tribes_memory_checked_strnlen(const void *pointer,uint64_t limit){
 for(uint64_t offset=0;offset<limit;offset++){
  if(!string_byte(pointer,offset,"strnlen"))return offset;
 }
 return limit;
}
uint64_t tribes_memory_checked_strlen(const void *pointer){return tribes_memory_checked_strnlen(pointer,UINT64_MAX);}
int32_t tribes_memory_checked_strncmp(const void *left,const void *right,uint64_t limit){
 for(uint64_t offset=0;offset<limit;offset++){
  unsigned char a=string_byte(left,offset,"strncmp"),b=string_byte(right,offset,"strncmp");
  if(a!=b)return (int32_t)a-(int32_t)b;if(!a)return 0;
 }
 return 0;
}
int32_t tribes_memory_checked_strcmp(const void *left,const void *right){return tribes_memory_checked_strncmp(left,right,UINT64_MAX);}
