/* SDL's EAGL drawable is a borrowed framebuffer, whereas desktop GL uses zero.
   Keep that mapping in the pinned GL4ES state; SDL retains drawable ownership.
   These private structure accesses require the exact dependency revision in
   build-gl4es-mobile.py. No library-generated procedure reaches the guest. */
#include "gl4es_context.h"
#ifdef TB_ANGLE
#include "angle_sdl.h"
#endif
#include "gl4esinit.h"
#include "gl/glstate.h"
#include "gl/gl4es.h"
#include "gl/framebuffers.h"
#include "glx/hardext.h"
#include <pthread.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "content_area.h"
#endif
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern void *NewGLState(void *, int);
extern void ActivateGLState(void *);
extern void DeleteGLState(void *);

struct TribesGL4ESContext {
    SDL_Window *window;
    SDL_GLContext native;
    glstate_t *state;
    bool frame_timing;
    Uint64 timing_start, timing_last, timing_frames;
    Uint64 timing_swap_total, timing_swap_max, timing_interval_max;
#ifdef TB_ANGLE
    void *stream_state;
#endif
};
static int initialized;

static void require_current(TribesGL4ESContext *context) {
    if (!pthread_main_np() || !context || SDL_GL_GetCurrentContext()!=context->native ||
        SDL_GL_GetCurrentWindow()!=context->window) abort();
}

#ifdef TB_ANGLE
/* ---- Client-array streaming ------------------------------------------------
   GL4ES draws immediate-mode geometry from GL client arrays. ANGLE's Metal
   backend streams client arrays through a small buffer pool and blocks in
   -[MTLCommandBuffer waitUntilCompleted] whenever that pool's oldest buffer is
   still in flight (measured ~6.5 ms per frame; 27% of the frame with unbatched
   glBegin/glEnd). Copy the exact client bytes each draw reads into our own ring
   buffers with unsynchronized maps; per-segment fences block only when the GPU
   falls a whole ring behind. GL4ES's view of bindings and pointers is
   preserved; anything unusual falls back to the original client-array draw.
   GL is used on the main thread only (require_current). */
typedef void (*PFNBindBuffer)(GLenum,GLuint);
/* ES 3.0 / ARB_sync values; GL4ES's desktop headers predate some of them. */
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_RANGE_BIT
#define GL_MAP_INVALIDATE_RANGE_BIT 0x0004
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
#define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif
typedef void (*PFNVertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void *);
typedef void (*PFNAttribArray)(GLuint);
typedef void (*PFNDrawArrays)(GLenum,GLint,GLsizei);
typedef void (*PFNDrawElements)(GLenum,GLsizei,GLenum,const void *);
typedef void (*PFNGenBuffers)(GLsizei,GLuint *);
typedef void (*PFNBufferData)(GLenum,GLsizeiptr,const void *,GLenum);
typedef void *(*PFNMapBufferRange)(GLenum,GLintptr,GLsizeiptr,GLbitfield);
typedef GLboolean (*PFNUnmapBuffer)(GLenum);
typedef GLsync (*PFNFenceSync)(GLenum,GLbitfield);
typedef GLenum (*PFNClientWaitSync)(GLsync,GLbitfield,GLuint64);
typedef void (*PFNDeleteSync)(GLsync);
static struct {
    PFNBindBuffer bind_buffer;PFNVertexAttribPointer attrib_pointer;PFNAttribArray enable,disable;
    PFNDrawArrays draw_arrays;PFNDrawElements draw_elements;PFNGenBuffers gen_buffers;PFNBufferData buffer_data;
    PFNMapBufferRange map_range;PFNUnmapBuffer unmap;PFNFenceSync fence;PFNClientWaitSync client_wait;PFNDeleteSync delete_sync;
} real;
#define STREAM_ATTRIBS 32
#define STREAM_SEGMENTS 8
typedef struct { int enabled,client;GLint size;GLenum type;GLboolean normalized;GLsizei stride;const void *pointer; } StreamAttrib;
typedef struct { GLuint buffer;GLenum target;size_t size,head;unsigned segment;GLsync fences[STREAM_SEGMENTS]; } StreamRing;
typedef struct { StreamAttrib attribs[STREAM_ATTRIBS];GLuint array_binding,element_binding;StreamRing vertices,indices;int ready; } StreamState;
static StreamState *stream;           /* current context's state */
static int streaming=-1;
static int streaming_enabled(void){
    if(streaming<0){const char *v=getenv("TRIBES_GL_STREAMING");streaming=!(v&&!strcmp(v,"0"));}
    return streaming;
}
static void *real_proc(const char *name){void *p=(void *)SDL_GL_GetProcAddress(name);if(!p)abort();return p;}
static void load_real(void){
    if(real.draw_arrays)return;
    real.bind_buffer=(PFNBindBuffer)real_proc("glBindBuffer");real.attrib_pointer=(PFNVertexAttribPointer)real_proc("glVertexAttribPointer");
    real.enable=(PFNAttribArray)real_proc("glEnableVertexAttribArray");real.disable=(PFNAttribArray)real_proc("glDisableVertexAttribArray");
    real.draw_elements=(PFNDrawElements)real_proc("glDrawElements");real.gen_buffers=(PFNGenBuffers)real_proc("glGenBuffers");
    real.buffer_data=(PFNBufferData)real_proc("glBufferData");real.map_range=(PFNMapBufferRange)real_proc("glMapBufferRange");
    real.unmap=(PFNUnmapBuffer)real_proc("glUnmapBuffer");real.fence=(PFNFenceSync)real_proc("glFenceSync");
    real.client_wait=(PFNClientWaitSync)real_proc("glClientWaitSync");real.delete_sync=(PFNDeleteSync)real_proc("glDeleteSync");
    real.draw_arrays=(PFNDrawArrays)real_proc("glDrawArrays");
}
static size_t type_bytes(GLenum type){
    switch(type){case GL_BYTE:case GL_UNSIGNED_BYTE:return 1;case GL_SHORT:case GL_UNSIGNED_SHORT:case GL_HALF_FLOAT:return 2;
    case GL_FLOAT:case GL_FIXED:case GL_INT:case GL_UNSIGNED_INT:return 4;default:return 0;}
}
static int ring_ready(StreamRing *r,GLenum target,size_t size){
    if(r->buffer)return 1;
    r->target=target;r->size=size;real.gen_buffers(1,&r->buffer);if(!r->buffer)return 0;
    real.bind_buffer(target,r->buffer);real.buffer_data(target,(GLsizeiptr)size,NULL,GL_STREAM_DRAW);return 1;
}
/* Reserve bytes in the ring; the region's previous GPU use has completed. */
static int ring_reserve(StreamRing *r,size_t bytes,size_t *offset){
    size_t segment_size=r->size/STREAM_SEGMENTS;if(!bytes||bytes>segment_size)return 0;
    size_t head=(r->head+15)&~(size_t)15;
    unsigned segment=(unsigned)(head/segment_size);
    if(head>=r->size||(head+bytes-1)/segment_size!=segment){segment=(segment+1)%STREAM_SEGMENTS;head=(size_t)segment*segment_size;}
    if(segment!=r->segment){
        if(!r->fences[r->segment])r->fences[r->segment]=real.fence(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
        if(r->fences[segment]){
            while(real.client_wait(r->fences[segment],GL_SYNC_FLUSH_COMMANDS_BIT,UINT64_C(1000000000))==GL_TIMEOUT_EXPIRED){}
            real.delete_sync(r->fences[segment]);r->fences[segment]=NULL;
        }
        r->segment=segment;
    }
    *offset=head;r->head=head+bytes;return 1;
}
static void *ring_map(StreamRing *r,size_t offset,size_t bytes){
    real.bind_buffer(r->target,r->buffer);
    return real.map_range(r->target,(GLintptr)offset,(GLsizeiptr)bytes,GL_MAP_WRITE_BIT|GL_MAP_UNSYNCHRONIZED_BIT|GL_MAP_INVALIDATE_RANGE_BIT);
}
static void stream_note_unpack(GLuint buffer);
static void stream_bind_buffer(GLenum target,GLuint buffer){
    if(stream){if(target==GL_ARRAY_BUFFER)stream->array_binding=buffer;else if(target==GL_ELEMENT_ARRAY_BUFFER)stream->element_binding=buffer;else if(target==0x88EC)stream_note_unpack(buffer);}
    real.bind_buffer(target,buffer);
}
static void stream_attrib_pointer(GLuint index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void *pointer){
    if(stream&&index<STREAM_ATTRIBS){
        StreamAttrib *a=stream->attribs+index;
        a->client=!stream->array_binding&&pointer&&type_bytes(type)&&size>=1&&size<=4&&stride>=0;
        a->size=size;a->type=type;a->normalized=normalized;a->stride=stride;a->pointer=pointer;
        if(a->client)return;
    }
    real.attrib_pointer(index,size,type,normalized,stride,pointer);
}
static void stream_enable(GLuint index){if(stream&&index<STREAM_ATTRIBS)stream->attribs[index].enabled=1;real.enable(index);}
static void stream_disable(GLuint index){if(stream&&index<STREAM_ATTRIBS)stream->attribs[index].enabled=0;real.disable(index);}
static size_t attrib_stride(const StreamAttrib *a){return a->stride?(size_t)a->stride:(size_t)a->size*type_bytes(a->type);}
static int has_client(void){
    if(!stream)return 0;
    for(unsigned i=0;i<STREAM_ATTRIBS;i++)if(stream->attribs[i].enabled&&stream->attribs[i].client)return 1;
    return 0;
}
/* Hand ANGLE the recorded client pointers for an unmodified fallback draw. */
static void restore_client_pointers(void){
    real.bind_buffer(GL_ARRAY_BUFFER,0);
    for(unsigned i=0;i<STREAM_ATTRIBS;i++){StreamAttrib *a=stream->attribs+i;if(a->enabled&&a->client)real.attrib_pointer(i,a->size,a->type,a->normalized,a->stride,a->pointer);}
    real.bind_buffer(GL_ARRAY_BUFFER,stream->array_binding);
}
/* Upload vertices [first,first+count) of every enabled client attribute,
   rebased so that vertex first becomes vertex zero. */
static int upload_vertices(GLint first,GLsizei count){
    size_t total=0,sizes[STREAM_ATTRIBS]={0};
    for(unsigned i=0;i<STREAM_ATTRIBS;i++){StreamAttrib *a=stream->attribs+i;if(!a->enabled||!a->client)continue;
        size_t stride=attrib_stride(a),element=(size_t)a->size*type_bytes(a->type);
        sizes[i]=(size_t)(count-1)*stride+element;total+=(sizes[i]+15)&~(size_t)15;}
    size_t offset;
    if(!ring_ready(&stream->vertices,GL_ARRAY_BUFFER,32u<<20)||!ring_reserve(&stream->vertices,total,&offset))return 0;
    unsigned char *map=ring_map(&stream->vertices,offset,total);if(!map){real.bind_buffer(GL_ARRAY_BUFFER,stream->array_binding);return 0;}
    size_t cursor=0;
    for(unsigned i=0;i<STREAM_ATTRIBS;i++){StreamAttrib *a=stream->attribs+i;if(!sizes[i])continue;
        memcpy(map+cursor,(const unsigned char *)a->pointer+(size_t)first*attrib_stride(a),sizes[i]);
        real.attrib_pointer(i,a->size,a->type,a->normalized,a->stride,(const void *)(uintptr_t)(offset+cursor));
        cursor+=(sizes[i]+15)&~(size_t)15;}
    real.unmap(GL_ARRAY_BUFFER);real.bind_buffer(GL_ARRAY_BUFFER,stream->array_binding);
    return 1;
}
/* Metal has no triangle fans or line loops; ANGLE generates their index
   buffers per draw from a capped pool, which blocks when that pool is in
   flight. Expand them here into ring-resident GL_TRIANGLES/GL_LINES indices:
   fan triangle i is (v0,vi,vi+1) and loop segment i is (vi,vi+1), (vn-1,v0),
   preserving vertex order and the provoking (last) vertex of each primitive. */
static int expanded_count(GLenum mode,GLsizei count){
    if(mode==GL_TRIANGLE_FAN)return count>=3?3*(count-2):0;
    if(mode==GL_LINE_LOOP)return count>=2?2*count:0;
    return -1;
}
static uint32_t source_index(const void *indices,GLenum type,GLsizei i,uint32_t first){
    if(!indices)return first+(uint32_t)i;
    return type==GL_UNSIGNED_BYTE?((const uint8_t *)indices)[i]:type==GL_UNSIGNED_SHORT?((const uint16_t *)indices)[i]:((const uint32_t *)indices)[i];
}
/* Draw a fan/loop as an indexed list. indices==NULL means arrays from first.
   rebase is subtracted from every vertex index (vertices already uploaded). */
static int draw_expanded(GLenum mode,GLsizei count,const void *indices,GLenum type,uint32_t first,uint32_t rebase){
    int n=expanded_count(mode,count);if(n<=0)return n==0;
    uint32_t top=0;for(GLsizei i=0;i<count;i++){uint32_t v=source_index(indices,type,i,first)-rebase;if(v>top)top=v;}
    GLenum out=top<65535?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT;size_t width=out==GL_UNSIGNED_SHORT?2:4,bytes=(size_t)n*width,offset;
    if(!ring_ready(&stream->indices,GL_ELEMENT_ARRAY_BUFFER,8u<<20)||!ring_reserve(&stream->indices,bytes,&offset))return 0;
    unsigned char *map=ring_map(&stream->indices,offset,bytes);if(!map){real.bind_buffer(GL_ELEMENT_ARRAY_BUFFER,stream->element_binding);return 0;}
    size_t k=0;
#define PUT(v) do{uint32_t value=(v)-rebase;if(out==GL_UNSIGNED_SHORT)((uint16_t *)map)[k++]=(uint16_t)value;else ((uint32_t *)map)[k++]=value;}while(0)
    if(mode==GL_TRIANGLE_FAN){uint32_t hub=source_index(indices,type,0,first);
        for(GLsizei i=1;i+1<count;i++){PUT(hub);PUT(source_index(indices,type,i,first));PUT(source_index(indices,type,i+1,first));}}
    else for(GLsizei i=0;i<count;i++){PUT(source_index(indices,type,i,first));PUT(source_index(indices,type,(i+1)%count,first));}
#undef PUT
    real.unmap(GL_ELEMENT_ARRAY_BUFFER);
    real.draw_elements(mode==GL_TRIANGLE_FAN?GL_TRIANGLES:GL_LINES,n,out,(const void *)(uintptr_t)offset);
    real.bind_buffer(GL_ELEMENT_ARRAY_BUFFER,stream->element_binding);
    return 1;
}
static void stream_draw_arrays(GLenum mode,GLint first,GLsizei count){
    int client=has_client(),expand=stream&&expanded_count(mode,count)>0;
    if(first<0||count<=0||(!client&&!expand)){if(client)restore_client_pointers();real.draw_arrays(mode,first,count);return;}
    if(client&&!upload_vertices(first,count)){restore_client_pointers();real.draw_arrays(mode,first,count);return;}
    if(expand&&draw_expanded(mode,count,NULL,GL_UNSIGNED_INT,(uint32_t)first,client?(uint32_t)first:0))return;
    if(client)real.draw_arrays(mode,0,count);else real.draw_arrays(mode,first,count);
}
static void stream_draw_elements(GLenum mode,GLsizei count,GLenum type,const void *indices){
    int client=has_client();
    if(count<=0||!stream||stream->element_binding||!indices||(type!=GL_UNSIGNED_BYTE&&type!=GL_UNSIGNED_SHORT&&type!=GL_UNSIGNED_INT)){
        if(client)restore_client_pointers();real.draw_elements(mode,count,type,indices);return;}
    size_t width=type_bytes(type);uint32_t low=UINT32_MAX,high=0,restart=type==GL_UNSIGNED_BYTE?0xffu:type==GL_UNSIGNED_SHORT?0xffffu:0xffffffffu;
    for(GLsizei i=0;i<count;i++){
        uint32_t v=type==GL_UNSIGNED_BYTE?((const uint8_t *)indices)[i]:type==GL_UNSIGNED_SHORT?((const uint16_t *)indices)[i]:((const uint32_t *)indices)[i];
        if(v==restart){if(client)restore_client_pointers();real.draw_elements(mode,count,type,indices);return;}
        if(v<low)low=v;if(v>high)high=v;}
    if(client&&!upload_vertices((GLint)low,(GLsizei)(high-low+1))){restore_client_pointers();real.draw_elements(mode,count,type,indices);return;}
    if(!client)low=0;
    if(expanded_count(mode,count)>0&&draw_expanded(mode,count,indices,type,0,low))return;
    size_t bytes=(size_t)count*width,offset;
    if(!ring_ready(&stream->indices,GL_ELEMENT_ARRAY_BUFFER,8u<<20)||!ring_reserve(&stream->indices,bytes,&offset)){
        if(client)restore_client_pointers();real.bind_buffer(GL_ELEMENT_ARRAY_BUFFER,stream->element_binding);real.draw_elements(mode,count,type,indices);return;}
    unsigned char *map=ring_map(&stream->indices,offset,bytes);
    if(!map){real.bind_buffer(GL_ELEMENT_ARRAY_BUFFER,stream->element_binding);if(client)restore_client_pointers();real.draw_elements(mode,count,type,indices);return;}
    if(!low)memcpy(map,indices,bytes);
    else for(GLsizei i=0;i<count;i++){
        if(type==GL_UNSIGNED_BYTE)map[i]=(uint8_t)(((const uint8_t *)indices)[i]-low);
        else if(type==GL_UNSIGNED_SHORT)((uint16_t *)map)[i]=(uint16_t)(((const uint16_t *)indices)[i]-low);
        else ((uint32_t *)map)[i]=((const uint32_t *)indices)[i]-low;}
    real.unmap(GL_ELEMENT_ARRAY_BUFFER);
    real.draw_elements(mode,count,type,(const void *)(uintptr_t)offset);
    real.bind_buffer(GL_ELEMENT_ARRAY_BUFFER,stream->element_binding);
}

/* Texture updates: route glTexSubImage2D pixels through a fenced
   GL_PIXEL_UNPACK_BUFFER ring so ANGLE copies them on the GPU instead of
   writing texture memory from the CPU (a synchronous XPC per upload in the
   iOS Simulator, and a potential in-use stall on devices). The bytes GL would
   read from client memory are copied exactly; unusual unpack state falls back. */
typedef void (*PFNTexSubImage2D)(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void *);
typedef void (*PFNPixelStorei)(GLenum,GLint);
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_UNPACK_SKIP_ROWS
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#endif
#ifndef GL_UNPACK_SKIP_PIXELS
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#endif
#ifndef GL_UNPACK_IMAGE_HEIGHT
#define GL_UNPACK_IMAGE_HEIGHT 0x806E
#endif
#ifndef GL_UNPACK_SKIP_IMAGES
#define GL_UNPACK_SKIP_IMAGES 0x806D
#endif
static PFNTexSubImage2D real_tex_sub_image;static PFNPixelStorei real_pixel_store;
typedef struct { GLint alignment,unusual;GLuint unpack_binding;StreamRing pixels; } PixelState;
static PixelState *pixel_state(void){
    /* One per stream state; allocated alongside it on first use. */
    static PixelState *states[8];static StreamState *owners[8];
    if(!stream)return NULL;
    for(unsigned i=0;i<8;i++)if(owners[i]==stream)return states[i];
    for(unsigned i=0;i<8;i++)if(!owners[i]){
        PixelState *n=calloc(1,sizeof(PixelState));if(!n)return NULL;
        /* Start from the context's actual unpack state, not GL defaults. */
        void (*get)(GLenum,GLint *)=(void (*)(GLenum,GLint *))real_proc("glGetIntegerv");
        GLint v=4,u=0,binding=0;get(GL_UNPACK_ALIGNMENT,&v);n->alignment=v;
        const GLenum names[]={GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_IMAGES};
        for(unsigned k=0;k<5;k++){GLint value=0;get(names[k],&value);u|=value!=0;}
        n->unusual=u;get(0x88EF/*GL_PIXEL_UNPACK_BUFFER_BINDING*/,&binding);n->unpack_binding=(GLuint)binding;
        states[i]=n;owners[i]=stream;return n;}
    return NULL;
}
static void stream_pixel_store(GLenum name,GLint value){
    PixelState *p=pixel_state();
    if(p){if(name==GL_UNPACK_ALIGNMENT)p->alignment=value;
        else if(name==GL_UNPACK_ROW_LENGTH||name==GL_UNPACK_SKIP_ROWS||name==GL_UNPACK_SKIP_PIXELS||name==GL_UNPACK_IMAGE_HEIGHT||name==GL_UNPACK_SKIP_IMAGES){
            /* Track any nonzero value of these; fall back while set. */
            static GLint values[5];unsigned k=name==GL_UNPACK_ROW_LENGTH?0:name==GL_UNPACK_SKIP_ROWS?1:name==GL_UNPACK_SKIP_PIXELS?2:name==GL_UNPACK_IMAGE_HEIGHT?3:4;
            values[k]=value;p->unusual=values[0]||values[1]||values[2]||values[3]||values[4];}}
    real_pixel_store(name,value);
}
static size_t pixel_bytes(GLenum format,GLenum type){
    size_t channels=format==GL_RGBA||format==0x80E1/*BGRA*/?4:format==GL_RGB?3:format==GL_LUMINANCE_ALPHA?2:format==GL_LUMINANCE||format==GL_ALPHA?1:0;
    if(type==GL_UNSIGNED_BYTE)return channels;
    if(type==GL_UNSIGNED_SHORT_5_6_5)return format==GL_RGB?2:0;
    if(type==GL_UNSIGNED_SHORT_4_4_4_4||type==GL_UNSIGNED_SHORT_5_5_5_1)return format==GL_RGBA?2:0;
    return 0;
}
static void stream_tex_sub_image(GLenum target,GLint level,GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,const void *pixels){
    PixelState *p=pixel_state();size_t bpp=pixel_bytes(format,type);
    if(!p||!pixels||p->unusual||p->unpack_binding||width<=0||height<=0||!bpp||(p->alignment!=1&&p->alignment!=2&&p->alignment!=4&&p->alignment!=8)){
        real_tex_sub_image(target,level,x,y,width,height,format,type,pixels);return;}
    size_t row=(size_t)width*bpp,stride=(row+(size_t)p->alignment-1)/(size_t)p->alignment*(size_t)p->alignment,bytes=(size_t)(height-1)*stride+row,offset;
    if(!ring_ready(&p->pixels,GL_PIXEL_UNPACK_BUFFER,32u<<20)||!ring_reserve(&p->pixels,bytes,&offset)){
        real.bind_buffer(GL_PIXEL_UNPACK_BUFFER,0);real_tex_sub_image(target,level,x,y,width,height,format,type,pixels);return;}
    unsigned char *map=ring_map(&p->pixels,offset,bytes);
    if(!map){real.bind_buffer(GL_PIXEL_UNPACK_BUFFER,0);real_tex_sub_image(target,level,x,y,width,height,format,type,pixels);return;}
    memcpy(map,pixels,bytes);real.unmap(GL_PIXEL_UNPACK_BUFFER);
    real_tex_sub_image(target,level,x,y,width,height,format,type,(const void *)(uintptr_t)offset);
    real.bind_buffer(GL_PIXEL_UNPACK_BUFFER,0);
}

static void stream_note_unpack(GLuint buffer){PixelState *p=pixel_state();if(p)p->unpack_binding=buffer;}
static void *stream_proc(const char *name){
    if(!streaming_enabled())return NULL;
    load_real();
    if(!strcmp(name,"glBindBuffer"))return (void *)stream_bind_buffer;
    if(!strcmp(name,"glVertexAttribPointer"))return (void *)stream_attrib_pointer;
    if(!strcmp(name,"glEnableVertexAttribArray"))return (void *)stream_enable;
    if(!strcmp(name,"glDisableVertexAttribArray"))return (void *)stream_disable;
    if(!strcmp(name,"glDrawArrays"))return (void *)stream_draw_arrays;
    if(!strcmp(name,"glDrawElements"))return (void *)stream_draw_elements;
    if(!strcmp(name,"glTexSubImage2D")){real_tex_sub_image=(PFNTexSubImage2D)real_proc("glTexSubImage2D");return (void *)stream_tex_sub_image;}
    if(!strcmp(name,"glPixelStorei")){real_pixel_store=(PFNPixelStorei)real_proc("glPixelStorei");return (void *)stream_pixel_store;}
    return NULL;
}
#endif


static void *native_proc(const char *name) {
#ifdef TB_ANGLE
    void *wrapped=stream_proc(name);if(wrapped)return wrapped;
#endif
    return (void *)SDL_GL_GetProcAddress(name);
}
static void framebuffer_size(int *width, int *height) {
    SDL_Window *window=SDL_GL_GetCurrentWindow();
#if TARGET_OS_IPHONE
    if (!window || !tb_content_pixels(window,width,height)) abort();
#else
    if (!window || !SDL_GetWindowSizeInPixels(window,width,height)) abort();
#endif
}

void tribes_gl4es_context_activate(TribesGL4ESContext *context) {
    require_current(context);
    int switched=glstate!=context->state;
    ActivateGLState(context->state);
#ifdef TB_ANGLE
    stream=context->stream_state;
#endif
    if (switched) {
        /* GL4ES shares its texture-binding cache with shared objects, but
           native EAGL binding state belongs to each individual context. */
        void (*getInteger)(GLenum,GLint *)=(void (*)(GLenum,GLint *))SDL_GL_GetProcAddress("glGetIntegerv");
        void (*activeTexture)(GLenum)=(void (*)(GLenum))SDL_GL_GetProcAddress("glActiveTexture");
        if (!getInteger || !activeTexture) abort();
        GLint active;
        getInteger(GL_ACTIVE_TEXTURE,&active);
        for (int i=0;i<hardext.maxtex;i++) {
            GLint texture;
            activeTexture(GL_TEXTURE0+i);
            getInteger(GL_TEXTURE_BINDING_2D,&texture);
            context->state->actual_tex2d[i]=(GLuint)texture;
        }
        activeTexture((GLenum)active);
        context->state->bound_changed=hardext.maxtex;
    }
#ifdef TB_ANGLE
    /* EGL owns its real default framebuffer zero on the SDL Metal surface. */
    Sint64 framebuffer=0;
#else
    Sint64 framebuffer=SDL_GetNumberProperty(SDL_GetWindowProperties(context->window),
        SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER,0);
#endif
    int width,height;
    framebuffer_size(&width,&height);
#ifndef TB_ANGLE
    if (framebuffer<=0 || framebuffer>UINT32_MAX) abort();
#endif
    if (width<=0 || height<=0) abort();
    /* mainfbo_fbo maps GL default zero to SDL's current native drawable.
       globals4es.usefbo stays false: GL4ES must not allocate/delete this FBO. */
    context->state->fbo.mainfbo_fbo=(GLuint)framebuffer;
    context->state->fbo.mainfbo_width=width;
    context->state->fbo.mainfbo_height=height;
    context->state->fbo.fbo_0->width=width;
    context->state->fbo.fbo_0->height=height;
    gl4es_setCurrentFBO();
}

TribesGL4ESContext *tribes_gl4es_context_create(SDL_Window *window, SDL_GLContext native,
                                               TribesGL4ESContext *shared) {
    TribesGL4ESContext *context=calloc(1,sizeof *context);
    if (!context) return NULL;
    context->window=window; context->native=native;
    const char *timing=getenv("TRIBES_FRAME_TIMING");
    context->frame_timing=timing && !strcmp(timing,"1");
#ifdef TB_ANGLE
    context->stream_state=calloc(1,sizeof(StreamState));
    if (!context->stream_state) { free(context); return NULL; }
#endif
    require_current(context);
    if (!initialized) {
        set_getprocaddress(native_proc);
        set_getmainfbsize(framebuffer_size);
        setenv("LIBGL_FBOFORCETEX","0",1);
        setenv("LIBGL_FB","0",1);
        initialize_gl4es();
        initialized=1;
    }
    context->state=NewGLState(shared ? shared->state : NULL,0);
    if (!context->state) abort();
    tribes_gl4es_context_activate(context);
    return context;
}

/* Presented frames since launch, for the optional on-screen FPS counter. */
static _Atomic uint64_t presented_frames;
uint64_t tribes_gl4es_presented_frames(void) { return atomic_load_explicit(&presented_frames,memory_order_relaxed); }

bool tribes_gl4es_context_swap(TribesGL4ESContext *context) {
    require_current(context);
    if (glstate!=context->state) abort();
    Uint64 begin=context->frame_timing?SDL_GetTicksNS():0;
    gl4es_glFlush();
    bool result=SDL_GL_SwapWindow(context->window);
    tribes_gl4es_context_activate(context);
    if (result) atomic_fetch_add_explicit(&presented_frames,1,memory_order_relaxed);
    if (context->frame_timing && result) {
        /* Opt-in native submission timing, independent of screen recording.
           Includes flush/swap/reactivation; it is not GPU completion timing. */
        Uint64 end=SDL_GetTicksNS();
        if (context->timing_last) {
            Uint64 interval=end-context->timing_last, swap=end-begin;
            context->timing_frames++;
            context->timing_swap_total+=swap;
            if (swap>context->timing_swap_max) context->timing_swap_max=swap;
            if (interval>context->timing_interval_max) context->timing_interval_max=interval;
            Uint64 elapsed=end-context->timing_start;
            if (elapsed>=UINT64_C(30000000000)) {
                fprintf(stderr,"TribesPad frame timing: end_ns=%llu frames=%llu seconds=%.6f fps=%.3f swap_mean_ms=%.3f swap_max_ms=%.3f interval_max_ms=%.3f\n",
                    (unsigned long long)end,(unsigned long long)context->timing_frames,
                    elapsed/1e9,context->timing_frames*1e9/elapsed,
                    context->timing_swap_total/1e6/context->timing_frames,
                    context->timing_swap_max/1e6,context->timing_interval_max/1e6);
                context->timing_start=end;
                context->timing_frames=context->timing_swap_total=0;
                context->timing_swap_max=context->timing_interval_max=0;
            }
        } else context->timing_start=end;
        context->timing_last=end;
    }
    return result;
}

void tribes_gl4es_context_destroy(TribesGL4ESContext *context) {
    require_current(context);
    if (glstate!=context->state) abort();
    gl4es_glFinish();
    /* Do not let library cleanup treat the borrowed drawable as its own. */
    context->state->fbo.mainfbo_fbo=0;
    DeleteGLState(context->state);
    ActivateGLState(NULL);
#ifdef TB_ANGLE
    if (stream==context->stream_state) stream=NULL;
    free(context->stream_state);
#endif
    free(context);
}
