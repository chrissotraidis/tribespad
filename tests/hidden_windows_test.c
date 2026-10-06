#include "../port/core/hidden_windows.h"
#include "../port/core/window_classes.h"
#include "../port/core/guest_display.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[8][16384];static _Thread_local unsigned mode,stage,depth;static _Thread_local uint32_t live_window;
extern void fixture_callback(_cpu *);
extern void tribes_return_to_host(_cpu *);
const uint32_t tribes_dispatch_count=1,tribes_dispatch_vas[]={0x550000};
void (*const tribes_dispatch_fns[])(_cpu *)={fixture_callback};
uint32_t fixture_body(_cpu *cpu,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp) {
    uint64_t base=cpu->_pointer_offset;(void)wp;
    if(mode==3){cpu->_ecx=0xdeadbeef;cpu->_ebx=0xfeedfeed;cpu->_eflags=0x246;cpu->_st_cw=0xb7f;cpu->_st_sw_cond=0x21;cpu->_st_exp[2]=0x8001;return handle^message^wp^lp;}
    if(mode==4){if(depth++==0){uint32_t args[]={1,2,3,4};uint32_t nested=tribes_call_guest(cpu,0x550000,4,args,cpu->_esp-128);depth--;assert(nested==10);return 20;}depth--;return handle+message+wp+lp;}
    assert(tribes_is_window(base,handle));live_window=handle;
    if(message==0x81){stage=1;uint32_t fields[12];memcpy(fields,(void *)(uintptr_t)(base+lp),48);assert(fields[1]==0x400000&&fields[0]==0x1234);if(mode==1)return 0;}
    else if(message==0x83){assert(stage==1);stage=2;uint32_t rect[4];memcpy(rect,(void *)(uintptr_t)(base+lp),16);assert(!rect[0]&&!rect[1]&&!rect[2]&&!rect[3]);}
    else if(message==1){assert(stage==2);stage=3;if(mode==2)return UINT32_MAX;}
    else if(message==2){stage=4;}
    else if(message==0x82){stage=5;}
    return tribes_default_window_proc(base,handle,message,wp,lp);
}
static void setup(unsigned i,_cpu *cpu) {
    memset(cpu,0,sizeof *cpu);cpu->_pointer_offset=(uintptr_t)memory[i]-0x20000;cpu->_esp=0x23000;cpu->_eax=0x123;cpu->_ecx=0x456;cpu->_ebx=0x789;cpu->_eflags=0x202;cpu->_st_cw=0x37f;cpu->_st_extended=1;
    cpu->stack_bottom=memory[i]+4096;cpu->stack_top=memory[i]+sizeof memory[i];strcpy((char *)memory[i]+512,"HiddenFixture");strcpy((char *)memory[i]+768,"Copied title");uint32_t fields[]={0,0x550000,0,0,0x400000,0,0,0,0,0x20200};memcpy(memory[i]+17,fields,40);
}
static uint32_t create(_cpu *cpu) { return tribes_create_hidden_window(cpu,0,0x20200,0x20300,0,0,0,0,0,0,0,0x400000,0x1234); }
static void *worker(void *arg){unsigned i=(unsigned)(uintptr_t)arg;_cpu cpu;setup(i,&cpu);uint32_t atom=tribes_register_class(cpu._pointer_offset,0x20011);assert(atom);for(unsigned n=0;n<1000;n++){mode=0;uint32_t h=create(&cpu);assert(h&&stage==3);assert(tribes_destroy_hidden_window(&cpu,h)&&stage==5);assert(!tribes_is_window(cpu._pointer_offset,h));}assert(tribes_unregister_class(cpu._pointer_offset,atom,0x400000));return NULL;}
int main(void) {
    _cpu cpu;setup(0,&cpu);assert(tribes_display_configure(cpu._pointer_offset,4,4));uint32_t desktop=tribes_get_desktop_window(cpu._pointer_offset);assert(tribes_is_window(cpu._pointer_offset,desktop));assert(!tribes_destroy_hidden_window(&cpu,desktop)&&tribes_get_last_error()==5&&tribes_is_window(cpu._pointer_offset,desktop));uint32_t atom=tribes_register_class(cpu._pointer_offset,0x20011);assert(atom);_cpu original=cpu;
    for(unsigned m=0;m<3;m++){mode=m;uint32_t h=create(&cpu);assert(!memcmp(&cpu,&original,sizeof cpu));if(!m){assert(h&&stage==3);assert(!tribes_unregister_class(cpu._pointer_offset,atom,0x400000)&&tribes_get_last_error()==1412);assert(tribes_destroy_hidden_window(&cpu,h)&&stage==5);}else assert(!h&&stage==5&&!tribes_is_window(cpu._pointer_offset,live_window));}
    mode=3;uint32_t args[]={1,2,3,4};assert(tribes_call_guest(&cpu,0x550000,4,args,cpu._esp-128)==(1^2^3^4));assert(!memcmp(&cpu,&original,36));assert(cpu._st_cw==0xb7f&&cpu._st_sw_cond==0x21&&cpu._st_exp[2]==0x8001);
    mode=4;assert(tribes_call_guest(&cpu,0x550000,4,args,cpu._esp-128)==20);assert(!memcmp(&cpu,&original,36));assert(tribes_unregister_class(cpu._pointer_offset,atom,0x400000));
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: desktop-root IsWindow identity and protected destruction; real native fastcc callbacks, caller register preservation, shared FP state, nested callbacks, creation rejection/cleanup and 8000 concurrent hidden-window lifetimes");return 0;
}
