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
};
static int initialized;

static void require_current(TribesGL4ESContext *context) {
    if (!pthread_main_np() || !context || SDL_GL_GetCurrentContext()!=context->native ||
        SDL_GL_GetCurrentWindow()!=context->window) abort();
}
static void *native_proc(const char *name) { return (void *)SDL_GL_GetProcAddress(name); }
static void framebuffer_size(int *width, int *height) {
    SDL_Window *window=SDL_GL_GetCurrentWindow();
    if (!window || !SDL_GetWindowSizeInPixels(window,width,height)) abort();
}

void tribes_gl4es_context_activate(TribesGL4ESContext *context) {
    require_current(context);
    int switched=glstate!=context->state;
    ActivateGLState(context->state);
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

bool tribes_gl4es_context_swap(TribesGL4ESContext *context) {
    require_current(context);
    if (glstate!=context->state) abort();
    Uint64 begin=context->frame_timing?SDL_GetTicksNS():0;
    gl4es_glFlush();
    bool result=SDL_GL_SwapWindow(context->window);
    tribes_gl4es_context_activate(context);
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
    free(context);
}
