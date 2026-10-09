/* ANGLE owns EGL contexts/surfaces; SDL owns the UIKit window and Metal view.
   One surface per window lets shared contexts use the same drawable safely. */
#define TRIBES_ANGLE_IMPLEMENTATION
#define EGL_EGLEXT_PROTOTYPES
#include "angle_sdl.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct Surface {SDL_Window *window;SDL_MetalView view;EGLSurface egl;unsigned refs;struct Surface *next;} Surface;
typedef struct Context {Surface *surface;EGLContext egl;int interval;struct Context *next;} Context;
static EGLDisplay display=EGL_NO_DISPLAY;
static EGLConfig config;
static Surface *surfaces;
static Context *contexts,*current;
static bool main_thread(void){return pthread_main_np() || SDL_SetError("ANGLE context operations require the main thread");}
static bool failure(const char *operation){return SDL_SetError("ANGLE %s failed: EGL 0x%x",operation,eglGetError());}
static Context *lookup(SDL_GLContext token){for(Context *c=contexts;c;c=c->next)if((SDL_GLContext)c==token)return c;return NULL;}
static bool initialize(void){
 if(display!=EGL_NO_DISPLAY)return true;
 const EGLint attrs[]={EGL_PLATFORM_ANGLE_TYPE_ANGLE,EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,EGL_NONE};
 EGLDisplay d=eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE,NULL,attrs);
 if(d==EGL_NO_DISPLAY || !eglInitialize(d,NULL,NULL))return failure("initialize Metal");
 const EGLint cfg[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_STENCIL_SIZE,8,EGL_NONE};
 EGLint count=0;
 if(!eglChooseConfig(d,cfg,&config,1,&count)||count!=1){failure("choose config");eglTerminate(d);return false;}
 display=d;return true;
}
static void release_surface(Surface *s){
 if(--s->refs)return;
 Surface **p=&surfaces;while(*p!=s)p=&(*p)->next;*p=s->next;
 if(!eglDestroySurface(display,s->egl))abort();
 SDL_Metal_DestroyView(s->view);free(s);
}
SDL_GLContext tribes_angle_create_context(SDL_Window *window){
 if(!main_thread()||!window||!initialize())return NULL;
 int share=0;if(!SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,&share))return NULL;
 if(share&&!current){SDL_SetError("No current ANGLE context to share");return NULL;}
 Surface *s=surfaces;while(s&&s->window!=window)s=s->next;
 if(!s){
  s=calloc(1,sizeof *s);if(!s){SDL_OutOfMemory();return NULL;}s->window=window;s->view=SDL_Metal_CreateView(window);
  if(!s->view){free(s);return NULL;}
  s->egl=eglCreateWindowSurface(display,config,(EGLNativeWindowType)SDL_Metal_GetLayer(s->view),NULL);
  if(s->egl==EGL_NO_SURFACE){failure("create Metal surface");SDL_Metal_DestroyView(s->view);free(s);return NULL;}
  s->next=surfaces;surfaces=s;
 }
 s->refs++;
 Context *c=calloc(1,sizeof *c);if(!c){release_surface(s);SDL_OutOfMemory();return NULL;}
 /* GLES3 provides real GPU fences. ANGLE explicitly supports client arrays,
    which GL4ES uses for its checked, private vertex staging. */
 const EGLint attrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_CONTEXT_CLIENT_ARRAYS_ENABLED_ANGLE,EGL_TRUE,EGL_NONE};
 c->egl=eglCreateContext(display,config,share?current->egl:EGL_NO_CONTEXT,attrs);c->surface=s;c->interval=1;
 if(c->egl==EGL_NO_CONTEXT){failure("create context");release_surface(s);free(c);return NULL;}
 if(!eglMakeCurrent(display,s->egl,s->egl,c->egl)){failure("make current");eglDestroyContext(display,c->egl);release_surface(s);free(c);return NULL;}
 c->next=contexts;contexts=c;current=c;return (SDL_GLContext)c;
}
bool tribes_angle_make_current(SDL_Window *window,SDL_GLContext token){
 if(!main_thread())return false;
 if(!token){if(display!=EGL_NO_DISPLAY&&!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT))return failure("clear current");current=NULL;return true;}
 Context *c=lookup(token);if(!c||c->surface->window!=window)return SDL_SetError("Invalid ANGLE context/window pair");
 if(!eglMakeCurrent(display,c->surface->egl,c->surface->egl,c->egl))return failure("make current");current=c;return true;
}
bool tribes_angle_destroy_context(SDL_GLContext token){
 if(!main_thread())return false;if(!token)return SDL_SetError("Invalid ANGLE context");
 Context *c=lookup(token);if(!c)return SDL_SetError("Invalid ANGLE context");
 if(c==current&&!tribes_angle_make_current(NULL,NULL))return false;
 if(!eglDestroyContext(display,c->egl))return failure("destroy context");
 Context **p=&contexts;while(*p!=c)p=&(*p)->next;*p=c->next;release_surface(c->surface);free(c);
 if(!contexts){if(!eglTerminate(display))abort();display=EGL_NO_DISPLAY;config=NULL;}
 return true;
}
SDL_GLContext tribes_angle_current_context(void){return main_thread()?(SDL_GLContext)current:NULL;}
SDL_Window *tribes_angle_current_window(void){return main_thread()&&current?current->surface->window:NULL;}
bool tribes_angle_swap_window(SDL_Window *window){
 if(!main_thread())return false;if(!current||current->surface->window!=window)return SDL_SetError("ANGLE window is not current");
 return eglSwapBuffers(display,current->surface->egl)||failure("swap buffers");
}
bool tribes_angle_set_swap_interval(int value){
 if(!main_thread())return false;if(!current)return SDL_SetError("No current ANGLE context");
 EGLint lo,hi;if(!eglGetConfigAttrib(display,config,EGL_MIN_SWAP_INTERVAL,&lo)||!eglGetConfigAttrib(display,config,EGL_MAX_SWAP_INTERVAL,&hi))return failure("query swap interval range");
 if(value<lo||value>hi)return SDL_SetError("Unsupported ANGLE swap interval");
 if(!eglSwapInterval(display,value))return failure("swap interval");current->interval=value;return true;
}
bool tribes_angle_get_swap_interval(int *value){
 if(!main_thread())return false;if(!current||!value)return SDL_SetError("No current ANGLE context or interval output");*value=current->interval;return true;
}
bool tribes_angle_get_attribute(SDL_GLAttr attr,int *value){
 if(!main_thread())return false;if(!value)return SDL_SetError("Missing ANGLE attribute output");
 if(!current)return SDL_GL_GetAttribute(attr,value);
 EGLint key=0;
 switch(attr){
 case SDL_GL_RED_SIZE:key=EGL_RED_SIZE;break;case SDL_GL_GREEN_SIZE:key=EGL_GREEN_SIZE;break;case SDL_GL_BLUE_SIZE:key=EGL_BLUE_SIZE;break;case SDL_GL_ALPHA_SIZE:key=EGL_ALPHA_SIZE;break;
 case SDL_GL_DEPTH_SIZE:key=EGL_DEPTH_SIZE;break;case SDL_GL_STENCIL_SIZE:key=EGL_STENCIL_SIZE;break;case SDL_GL_BUFFER_SIZE:key=EGL_BUFFER_SIZE;break;
 case SDL_GL_MULTISAMPLEBUFFERS:key=EGL_SAMPLE_BUFFERS;break;case SDL_GL_MULTISAMPLESAMPLES:key=EGL_SAMPLES;break;
 case SDL_GL_DOUBLEBUFFER:{EGLint v;if(!eglQuerySurface(display,current->surface->egl,EGL_RENDER_BUFFER,&v))return failure("query render buffer");*value=v==EGL_BACK_BUFFER;return true;}
 case SDL_GL_CONTEXT_MAJOR_VERSION:return eglQueryContext(display,current->egl,EGL_CONTEXT_CLIENT_VERSION,value)||failure("query context version");
 default:return SDL_GL_GetAttribute(attr,value);
 }
 return eglGetConfigAttrib(display,config,key,value)||failure("query config attribute");
}
SDL_FunctionPointer tribes_angle_get_proc(const char *name){return (SDL_FunctionPointer)eglGetProcAddress(name);}
