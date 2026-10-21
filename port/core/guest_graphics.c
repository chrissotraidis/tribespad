#include <stdio.h>
/* Finite graphics providers backed by native queries. Unknown exports return
   failure; there are no placeholder successful rendering functions. */
#include "guest_graphics.h"
#include "guest_modules.h"
#include "guest_sdl_backend.h"
#include "guest_heap.h"
#include "guest_openal.h"
/* Graphics-only component builds do not link the audio provider. */
__attribute__((weak,noinline)) uint32_t tribes_openal_load(uint64_t base,const char *name){(void)base;(void)name;return 0;}
__attribute__((weak,noinline)) void *tribes_openal_native_lookup(uint32_t address){(void)address;return 0;}
#include <SDL3/SDL.h>
#include <TargetConditionals.h>
/* Guest GL is served by OpenGL ES (iOS, or GL4ES on any Apple platform)
   instead of the Mac's desktop OpenGL. */
#if TARGET_OS_IPHONE || defined(TB_GL4ES)
#define TB_GLES_HOST 1
#else
#define TB_GLES_HOST 0
#endif
#include <stdlib.h>
#include <string.h>
typedef struct {const char *name;uint32_t address,module;void *function;uint32_t platforms;} GraphicsExport;
#include "graphics-exports.inc"
/* Only implemented mobile entry points are exposed. */
static int available(const GraphicsExport *e){
#if !TB_GLES_HOST
    return (e->platforms & 1u) != 0;
#elif defined(TB_GL4ES)
    return (e->platforms & 4u) != 0;
#else
    return (e->platforms & 2u) != 0;
#endif
}
static const uint32_t gl_module=0x20040000,glu_module=0x20050000;
int tribes_graphics_configure(uint64_t base){
    for(unsigned m=0;m<2;m++){
        uint32_t module=m?glu_module:gl_module;TribesNativeExport exports[sizeof graphics_exports/sizeof *graphics_exports];uint32_t n=0;
        for(unsigned i=0;i<sizeof graphics_exports/sizeof *graphics_exports;i++){
            const GraphicsExport *e=&graphics_exports[i];if(e->module==module&&available(e))exports[n++]=(TribesNativeExport){e->name,0,e->address};
        }
        if(!tribes_module_define_native(base,module,m?"C:\\Windows\\System32\\glu32.dll":"C:\\Windows\\System32\\opengl32.dll",exports,n))return 0;
    }return 1;
}
void *tribes_graphics_native_lookup(uint32_t address){
    /* Generated slots are contiguous, but arbitrary/unaligned guest addresses
       still fall through to the next finite provider. No dynamic proc cache. */
    uint32_t offset=address-GRAPHICS_EXPORT_BASE;
    if(offset%GRAPHICS_EXPORT_STRIDE==0 && offset/GRAPHICS_EXPORT_STRIDE<sizeof graphics_exports/sizeof *graphics_exports){
        const GraphicsExport *e=&graphics_exports[offset/GRAPHICS_EXPORT_STRIDE];
        if(e->address==address&&available(e))return e->function;
    }
    return tribes_openal_native_lookup(address);
}
static void name_copy(_cpu *cpu,uint32_t address,char text[256]){
    if(!address||address>UINT32_MAX-255||cpu->_pointer_offset>UINTPTR_MAX-address-255)abort();
    for(unsigned i=0;i<256;i++){text[i]=*(const char *)(uintptr_t)(cpu->_pointer_offset+address+i);if(!text[i])return;}abort();
}
uint32_t tribes_graphics_load_object(_cpu *cpu,uint32_t name){
    uint32_t error=tribes_get_last_error();char text[256];name_copy(cpu,name,text);uint32_t module=0;
    if(!strcmp(text,"glu32")||!strcmp(text,"glu32.dll"))module=glu_module;
    else if(!strcmp(text,"opengl32")||!strcmp(text,"opengl32.dll"))module=gl_module;
    else module=tribes_openal_load(cpu->_pointer_offset,text);
    if(module&&!tribes_module_retain_native(cpu->_pointer_offset,module))module=0;
    if(!module)SDL_SetError("%s","No implemented native provider for this SDL object");
    tribes_set_last_error(error);return module;
}
uint32_t tribes_graphics_load_function(_cpu *cpu,uint32_t module,uint32_t name){
    uint32_t error=tribes_get_last_error();char text[256];name_copy(cpu,name,text);(void)text;
    uint32_t result=(module==gl_module||module==glu_module||module==TB_OPENAL_MODULE)?tribes_get_proc_address(cpu->_pointer_offset,module,name):0;
    if(!result){SDL_SetError("%s","No implemented native export for this SDL function");fprintf(stderr,"TribesPad: no native GL export %s\n",text);}
    tribes_set_last_error(error);return result;
}
void tribes_graphics_unload_object(_cpu *cpu,uint32_t module){
    uint32_t error=tribes_get_last_error();if(module&&(module!=gl_module&&module!=glu_module&&module!=TB_OPENAL_MODULE))abort();
    if(module&&!tribes_free_library(cpu->_pointer_offset,module))abort();tribes_set_last_error(error);
}
uint32_t tribes_graphics_get_proc(_cpu *cpu,uint32_t name){
    uint32_t error=tribes_get_last_error();char text[256];name_copy(cpu,name,text);uint32_t result=0;
    if(tribes_sdl_gl_get_current_context(cpu))
        for(unsigned i=0;i<sizeof graphics_exports/sizeof *graphics_exports;i++)
            if(available(&graphics_exports[i])&&graphics_exports[i].module==gl_module&&!strcmp(graphics_exports[i].name,text)){result=graphics_exports[i].address;break;}
    if(!result){SDL_SetError("%s","No implemented native GL entry point for this procedure");fprintf(stderr,"TribesPad: no native GL procedure %s\n",text);}
    tribes_set_last_error(error);return result;
}

/* WGL uses Win32 errors, not SDL's error channel. Only precompiled guest
   procedures backed by the current platform are eligible. Failure codes are
   adapter diagnostics; Windows drivers need not choose these exact codes. */
uint32_t tribes_wgl_get_proc(_cpu *cpu,uint32_t name){
    uint32_t error=tribes_get_last_error();
    if(!name){tribes_set_last_error(87);return 0;}
    char text[256];name_copy(cpu,name,text);
    if(!tribes_sdl_gl_get_current_context(cpu)){tribes_set_last_error(6);return 0;}
    for(unsigned i=0;i<sizeof graphics_exports/sizeof *graphics_exports;i++){
        const GraphicsExport *e=&graphics_exports[i];
        if(e->module==gl_module&&available(e)&&!strcmp(e->name,text)){
            tribes_set_last_error(error);return e->address;
        }
    }
    fprintf(stderr,"TribesPad: no native WGL procedure %s\n",text);
    tribes_set_last_error(127);return 0;
}

/* Called by the generated no-op stubs for OpenGL entry points that have no
   native provider (gl-unprovided.json): name each one once in the log. */
void tribes_gl_unprovided(const char *name,int *seen){
    if(*seen)return;*seen=1;fprintf(stderr,"TribesPad: unprovided GL function called: %s (ignored)\n",name);
}
