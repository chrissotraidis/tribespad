/* Provider policy/dispatch component test; context and module services are
   controlled fixtures. Actual rendering is exercised by the full Apple app. */
#include <assert.h>
#include <stdio.h>
#include "../port/core/guest_graphics.c"
#include "fixtures/graphics_availability_reference.h"
#include "graphics-test-functions.inc"
static uint32_t last_error,current_context,registered;
static unsigned context_queries;
uint32_t tribes_get_last_error(void){return last_error;}
uint32_t tribes_set_last_error(uint32_t value){last_error=value;return value;}
uint32_t tribes_sdl_gl_get_current_context(_cpu *cpu){(void)cpu;context_queries++;return current_context;}
bool SDL_SetError(const char *fmt,...){(void)fmt;return false;}
int tribes_module_define_native(uint64_t base,uint32_t image,const char *path,const TribesNativeExport *exports,uint32_t count){
    (void)base;(void)path;uint32_t n=0;
    for(unsigned i=0;i<sizeof graphics_exports/sizeof *graphics_exports;i++){
        const GraphicsExport *e=&graphics_exports[i];
        if(e->module==image&&reference_available(e)){
            assert(n<count&&!strcmp(exports[n].name,e->name)&&exports[n].address==e->address);n++;
        }
    }
    assert(n==count);registered+=count;return 1;
}
int tribes_module_retain_native(uint64_t base,uint32_t image){(void)base;(void)image;return 1;}
uint32_t tribes_get_proc_address(uint64_t base,uint32_t image,uint32_t name){(void)base;(void)image;(void)name;return 0;}
uint32_t tribes_free_library(uint64_t base,uint32_t image){(void)base;(void)image;return 1;}
extern unsigned fallback_calls;
extern uint32_t fallback_address;
extern void *fallback_result;
int main(void){
    char memory[512]={0};_cpu cpu={0};cpu._pointer_offset=(uintptr_t)memory;
    unsigned supported=0;
    for(unsigned i=0;i<sizeof graphics_exports/sizeof *graphics_exports;i++){
        const GraphicsExport *e=&graphics_exports[i];int expected=reference_available(e);
        assert(available(e)==expected);supported+=expected;
        fallback_calls=0;fallback_result=(void *)&fallback_calls;
        assert(tribes_graphics_native_lookup(e->address)==(expected?e->function:fallback_result));
        assert(fallback_calls==(unsigned)!expected);
        if(!expected)assert(fallback_address==e->address);
        /* Every byte inside every slot except its exact entry is rejected. */
        for(unsigned j=1;j<16;j++){
            fallback_calls=0;
            assert(tribes_graphics_native_lookup(e->address+j)==fallback_result);
            assert(fallback_calls==1&&fallback_address==e->address+j);
        }
        strcpy(memory+16,e->name);
        for(unsigned context=0;context<2;context++){
            current_context=context;context_queries=0;last_error=123;
            uint32_t result=context&&expected&&e->module==gl_module?e->address:0;
            assert(tribes_graphics_get_proc(&cpu,16)==result&&last_error==123&&context_queries==1);
            context_queries=0;
            assert(tribes_wgl_get_proc(&cpu,16)==result&&context_queries==1);
            assert(last_error==(result?123:context?127:6));
        }
    }
    assert(tribes_graphics_configure(cpu._pointer_offset)&&registered==supported);
    uint32_t invalid[]={0,1,0xfff9ffff,0xffffffff,0xfffa0000+sizeof graphics_exports/sizeof *graphics_exports*16};
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++){
        fallback_calls=0;assert(tribes_graphics_native_lookup(invalid[i])==fallback_result);
        assert(fallback_calls==1&&fallback_address==invalid[i]);
        fallback_result=0;assert(tribes_graphics_native_lookup(invalid[i])==0);
        fallback_result=&fallback_calls;
    }
    strcpy(memory+16,"glNotImplemented");current_context=1;last_error=99;
    assert(!tribes_graphics_get_proc(&cpu,16)&&last_error==99);
    assert(!tribes_wgl_get_proc(&cpu,16)&&last_error==127);
    assert(!tribes_wgl_get_proc(&cpu,0)&&last_error==87);
    printf("PASS: %zu exports, %u available; slot bytes, fallthrough, registration, context and errors\n",sizeof graphics_exports/sizeof *graphics_exports,supported);
}
