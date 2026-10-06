#include "../port/core/guest_window_catalog.h"
#include "../port/core/guest_callback.h"
#include "../port/core/guest_teb.h"
#include "../port/core/guest_modules.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t),tribes_return_to_host(_cpu *);
extern void tbimp_GetTopWindow(void),tbimp_GetWindow(void),tbimp_GetWindowThreadProcessId(void),tbimp_GetWindowLongA(void),tbimp_IsWindow(void),tbimp_GetActiveWindow(void);
void *tribes_lookup(uint32_t a){switch(a){case 1:return tbimp_GetTopWindow;case 2:return tbimp_GetWindow;case 3:return tbimp_GetWindowThreadProcessId;case 4:return tbimp_GetWindowLongA;case 5:return tbimp_IsWindow;case 6:return tbimp_GetActiveWindow;case 0xfffff000:return tribes_return_to_host;default:abort();}}
static unsigned char *memory;
static uint32_t call(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args){_cpu before=*cpu;cpu->_esp-=4*count;memcpy(memory+cpu->_esp,args,4*count);tribes_enter(cpu,target);uint32_t r=cpu->_eax;before._eax=r;assert(!memcmp(cpu,&before,sizeof before));return r;}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory+0x100000,0x100000,PROT_READ|PROT_WRITE));uint64_t base=(uintptr_t)memory;
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));assert(tribes_teb_configure(base,0x100000,0x200000,0x180000,51,73,0x101000));assert(tribes_module_register(base,0x400000,"C:\\TribesPad\\Game\\Tribes2.exe",1));
 _cpu cpu={0};cpu._pointer_offset=base;cpu._esp=0x1f0000;cpu.stack_bottom=memory+0x180000;cpu.stack_top=memory+0x200000;cpu._eflags=0x246;cpu._eax=771;
 uint32_t zero=0;assert(!call(&cpu,1,1,&zero));assert(tribes_window_add(base,100,0x22220000,0));assert(tribes_window_add(base,101,0,0x90000000));assert(tribes_window_add(base,102,0x33330000,0));assert(!tribes_window_add(base,101,0,0));
 assert(call(&cpu,1,1,&zero)==102);uint32_t args[]={102,2};assert(call(&cpu,2,2,args)==101);args[0]=101;assert(call(&cpu,2,2,args)==100);args[0]=100;tribes_set_last_error(345);assert(!call(&cpu,2,2,args)&&tribes_get_last_error()==345);
 tribes_window_update(base,101,0x90000000,1);assert(call(&cpu,1,1,&zero)==101);assert(call(&cpu,6,0,NULL)==101);args[0]=101;assert(call(&cpu,2,2,args)==102);
 tribes_window_update(base,102,0,1);assert(call(&cpu,6,0,NULL)==102);tribes_window_update(base,102,0,0);assert(!call(&cpu,6,0,NULL));tribes_window_update(base,101,0x90000000,1);
 args[0]=101;args[1]=(uint32_t)-6;assert(call(&cpu,4,2,args)==0x400000);args[1]=(uint32_t)-16;assert(call(&cpu,4,2,args)==0x90000000);
 args[0]=100;args[1]=(uint32_t)-6;assert(call(&cpu,4,2,args)==0x22220000);
 args[0]=101;args[1]=0x102001;memset(memory+0x102000,0xaa,8);
 if(argc>1&&!strcmp(argv[1],"readonly"))assert(tribes_memory_permissions_set(0x102000,4096,1));
 assert(call(&cpu,3,2,args)==73);uint32_t pid;memcpy(&pid,memory+0x102001,4);assert(pid==51&&memory[0x102000]==0xaa&&memory[0x102005]==0xaa);args[1]=0;assert(call(&cpu,3,2,args)==73);
 tribes_guest_thread_finalize(&cpu);assert(tribes_teb_configure(base,0x100000,0x200000,0x180000,51,74,0x101000));assert(!call(&cpu,6,0,NULL));assert(call(&cpu,3,2,args)==73);assert(tribes_window_add(base,103,0,0));args[0]=103;assert(call(&cpu,3,2,args)==74);
 tribes_window_remove(base,103);tribes_window_remove(base,101);uint32_t removed=101;assert(!call(&cpu,5,1,&removed));assert(call(&cpu,1,1,&zero)==102);args[0]=101;args[1]=0x102001;memset(memory+0x102001,0xcc,4);assert(!call(&cpu,3,2,args)&&tribes_get_last_error()==1400);assert(memory[0x102001]==0xcc);
 tribes_window_remove(base,102);tribes_window_remove(base,100);assert(!call(&cpu,1,1,&zero));puts("PASS: finite window discovery ABI, order/focus, instance/style, creator identity, stale handles and CPU preservation");return 0;
}
