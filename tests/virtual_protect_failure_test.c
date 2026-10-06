#include "../port/core/guest_address_space.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
extern void checked_write(void *,uint64_t);
static unsigned count;
static int inject;
static struct {void *address;size_t size;int access;} changes[8];
int tribes_test_mprotect(void *address,size_t size,int access){assert(count<8);changes[count++]=(typeof(changes[0])){address,size,access};if(inject&&count==2){errno=ENOMEM;return -1;}return mprotect(address,size,access);}
int main(void){uint32_t page=(uint32_t)sysconf(_SC_PAGESIZE);unsigned char *memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);uint64_t base=(uintptr_t)memory;assert(!mprotect(memory+0x100000,0x10000,PROT_READ|PROT_WRITE)&&!mprotect(memory+0x400000,2*page,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));assert(tribes_address_space_begin(base));assert(tribes_address_space_declare(base,0x100000,0x10000,0x100000,4,0x1000,4,0x20000));assert(tribes_address_space_declare(base,0x400000,2*page,0x400000,4,0x1000,4,0x1000000));assert(tribes_address_space_seal(base));const TribesCheckedMemoryBackend backend={tribes_memory_initialize_static,tribes_memory_protect_pages};assert(tribes_address_space_enable_checked(base,&backend));
 uint32_t sentinel=0x93478216;memcpy(memory+0x100080,&sentinel,4);inject=1;assert(!tribes_virtual_protect(base,0x400000,2*page,2,0x100080)&&tribes_get_last_error()==5);assert(!memcmp(memory+0x100080,&sentinel,4));assert(count==4);
 for(unsigned i=0;i<4;i++){assert(changes[i].address==memory+0x400000+(i&1)*page&&changes[i].size==page);assert(changes[i].access==(i<2?PROT_READ:PROT_READ|PROT_WRITE));}
 for(unsigned i=0;i<2;i++){uint32_t address=0x400000+i*page;assert(tribes_virtual_query(base,address,0x100100,28)==28);uint32_t fields[7];memcpy(fields,memory+0x100100,28);assert(fields[5]==4);checked_write(memory+address,UINT64_C(0x83916482)+i);}
 inject=0;assert(tribes_virtual_protect(base,0x400000,2*page,2,0x100080));uint32_t old;memcpy(&old,memory+0x100080,4);assert(old==4&&count==6);assert(tribes_virtual_query(base,0x400000,0x100100,28)==28);uint32_t fields[7];memcpy(fields,memory+0x100100,28);assert(fields[5]==2);
 puts("PASS: injected second-native-page protection failure restores both native groups, retains software write access and loader metadata, preserves output, and permits a successful retry");return 0;}
