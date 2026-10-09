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
#include <stdlib.h>
#include <string.h>
typedef struct {const char *name;uint32_t address,module;void *function;} GraphicsExport;
#include "graphics-exports.inc"
/* Only implemented mobile entry points are exposed. */
static int available(const GraphicsExport *e){
#if TARGET_OS_IPHONE
#ifdef TB_GL4ES
    if(!strcmp(e->name,"glDrawBuffer"))return 1; /* bounded default-back selection */
    static const char *legacy[]={"glPrimitiveRestartIndex","glFenceSync","glIsSync","glClientWaitSync","glGetSynciv","glDeleteSync","glDrawArrays","glTexCoord2fv","glLoadMatrixf","glTexGeni","glTexGenfv","glPolygonMode","glAlphaFunc","glFogCoordPointerEXT","glVertex3fv","glFogi","glFogf","glFogfv","glLightfv","glLightf","glLightModelfv","glRotatef","glTexCoord2f","glTranslatef","glColor3ub","glVertex3f","glColor4f","glVertex2i","glPushMatrix","glPopMatrix","glScalef","glFrustum","glMultMatrixf","glMultMatrixd","glColor4fv","glMaterialfv","glTexEnvfv","glTexEnvf","glNormalPointer","glBegin","glVertex2f","glEnd","glTexEnvi","glColor4ub","glEnableClientState","glDisableClientState","glColorPointer","glVertexPointer","glTexCoordPointer","glDrawElements","glMatrixMode","glLoadIdentity","glLoadMatrixd","glClientActiveTextureARB","glMultiTexCoord2fARB","glMultiTexCoord2fvARB","glMultiTexCoord3fvARB"};
    for(unsigned i=0;i<sizeof legacy/sizeof *legacy;i++)if(!strcmp(e->name,legacy[i]))return 1;
#endif
    if(!strcmp(e->name,"glDrawArrays"))return 0;
    if(!strcmp(e->name,"glTexCoord2fv")||!strcmp(e->name,"glPrimitiveRestartIndex"))return 0;
    if(!strcmp(e->name,"glLoadMatrixf")||!strcmp(e->name,"glTexGeni")||!strcmp(e->name,"glTexGenfv")||!strcmp(e->name,"glPolygonMode")||!strcmp(e->name,"glAlphaFunc")||!strcmp(e->name,"glFogCoordPointerEXT")||!strcmp(e->name,"glVertex3fv"))return 0;
    if(!strcmp(e->name,"glFogi")||!strcmp(e->name,"glFogf")||!strcmp(e->name,"glFogfv"))return 0;
    if(!strcmp(e->name,"glLightfv")||!strcmp(e->name,"glLightf")||!strcmp(e->name,"glLightModelfv"))return 0;
    if(!strcmp(e->name,"glRotatef")||!strcmp(e->name,"glTexCoord2f")||!strcmp(e->name,"glTranslatef")||!strcmp(e->name,"glColor3ub")||!strcmp(e->name,"glVertex3f")||!strcmp(e->name,"glColor4f")||!strcmp(e->name,"glVertex2i"))return 0;
    if(!strcmp(e->name,"glPushMatrix")||!strcmp(e->name,"glPopMatrix")||!strcmp(e->name,"glScalef")||!strcmp(e->name,"glFrustum")||!strcmp(e->name,"glMultMatrixf")||!strcmp(e->name,"glMultMatrixd")||!strcmp(e->name,"glColor4fv")||!strcmp(e->name,"glMaterialfv")||!strcmp(e->name,"glTexEnvfv")||!strcmp(e->name,"glTexEnvf")||!strcmp(e->name,"glNormalPointer"))return 0;
    return strcmp(e->name,"glIsSync")!=0 && strcmp(e->name,"glGetSynciv")!=0 && strcmp(e->name,"glFenceSync")!=0 && strcmp(e->name,"glClientWaitSync")!=0 && strcmp(e->name,"glDeleteSync")!=0 && strcmp(e->name,"glBegin")!=0 && strcmp(e->name,"glVertex2f")!=0 && strcmp(e->name,"glEnd")!=0 && strcmp(e->name,"glTexImage3D")!=0 && strcmp(e->name,"glTexSubImage3D")!=0 && strcmp(e->name,"glTexEnvi")!=0 && strcmp(e->name,"glColor4ub")!=0 && strcmp(e->name,"glEnableClientState")!=0 && strcmp(e->name,"glDisableClientState")!=0 && strcmp(e->name,"glColorPointer")!=0 && strcmp(e->name,"glVertexPointer")!=0 && strcmp(e->name,"glTexCoordPointer")!=0 && strcmp(e->name,"glDrawElements")!=0 && strcmp(e->name,"glMatrixMode")!=0 && strcmp(e->name,"glLoadIdentity")!=0 && strcmp(e->name,"glLoadMatrixd")!=0 && strcmp(e->name,"glBeginQuery")!=0 && strcmp(e->name,"glEndQuery")!=0 && strcmp(e->name,"glGetQueryObjectui64v")!=0 && strcmp(e->name,"glGenQueries")!=0 && strcmp(e->name,"glDrawBuffer")!=0 && strcmp(e->name,"glClientActiveTextureARB")!=0 && strncmp(e->name,"glMultiTexCoord",14)!=0;
#else
    (void)e;return 1;
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
    for(unsigned i=0;i<sizeof graphics_exports/sizeof *graphics_exports;i++)if(graphics_exports[i].address==address&&available(&graphics_exports[i]))return graphics_exports[i].function;
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
    if(!result)SDL_SetError("%s","No implemented native export for this SDL function");
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
    if(!result)SDL_SetError("%s","No implemented native GL entry point for this procedure");
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
    tribes_set_last_error(127);return 0;
}
