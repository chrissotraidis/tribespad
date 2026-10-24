/* WGL and GDI pixel formats for Starsiege: Tribes, on TribesPad's renderer.
 * The game asks for an OpenGL pixel format on its window's device context and
 * creates a WGL context. Here each game window's GL drawable is a native SDL
 * window driven through TribesPad's SDL backend (guest_sdl_backend.c), so the
 * guest's GL calls reach the same GL4ES -> ANGLE -> Metal renderer as Tribes 2.
 * One pixel format is offered: RGBA8, 24-bit depth, 8-bit stencil, double
 * buffered, the closest match to any OpenGL request.
 * Headless runs (STARSIEGE_HEADLESS=1) keep the SDL window hidden and SDL in
 * background mode, and can write frames to PNG (STARSIEGE_CAPTURE=folder). */
#include <SDL3/SDL.h>
#include <TargetConditionals.h>
#include <ImageIO/ImageIO.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/guest_callback.h"
#include "../core/guest_memory_checks.h"
#include "../core/guest_display.h"
#include <GL/gl.h>

extern uint32_t tribes_set_last_error(uint32_t value);
extern uint32_t tribes_sdl_init(_cpu *cpu,uint32_t flags);
extern uint32_t tribes_sdl_create_window(_cpu *cpu,uint32_t title,uint32_t width,uint32_t height,uint32_t low,uint32_t high);
extern uint32_t tribes_sdl_gl_set_attribute(_cpu *cpu,uint32_t attribute,uint32_t value);
extern uint32_t tribes_sdl_gl_create_context(_cpu *cpu,uint32_t window);
extern uint32_t tribes_sdl_gl_destroy_context(_cpu *cpu,uint32_t context);
extern uint32_t tribes_sdl_gl_make_current(_cpu *cpu,uint32_t window,uint32_t context);
extern uint32_t tribes_sdl_gl_swap_window(_cpu *cpu,uint32_t window);
extern uint32_t ss_window_client_size(uint64_t base,uint32_t handle,int32_t *width,int32_t *height);
extern uint32_t ss_owns_window(uint64_t base,uint32_t handle);

typedef struct {uint32_t dc,window,format,sdl_window;} DeviceContext;
typedef struct {uint32_t handle,dc;} Context;
static DeviceContext dcs[16];
static Context contexts[8];
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int sdl_started;
static uint32_t current_dc;
static uint64_t swaps;
static int has_context(void);

static _Noreturn void unsupported(const char *what,uint32_t value){fprintf(stderr,"TribesPad: unsupported Starsiege GL operation: %s 0x%x\n",what,value);abort();}
static int headless(void){const char *v=getenv("STARSIEGE_HEADLESS");return v&&!strcmp(v,"1");}
static DeviceContext *dc_of(uint32_t dc){for(unsigned i=0;i<16;i++)if(dcs[i].dc&&dcs[i].dc==dc)return &dcs[i];return NULL;}

/* Start SDL video once, with the one pixel format's GL attributes. */
static int start_sdl(_cpu *cpu){
 if(sdl_started)return 1;
 if(headless())SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP,"1");
 if(!tribes_sdl_init(cpu,SDL_INIT_VIDEO)){fprintf(stderr,"TribesPad: SDL video failed: %s\n",SDL_GetError());return 0;}
 /* DirectSound (guest_dsound.c) plays through SDL audio. Starsiege opens it
    after its window, so audio starts with video. Silent if it fails. */
 if(!tribes_sdl_init(cpu,SDL_INIT_AUDIO))fprintf(stderr,"TribesPad: SDL audio failed: %s\n",SDL_GetError());
 tribes_sdl_gl_set_attribute(cpu,SDL_GL_DOUBLEBUFFER,1);tribes_sdl_gl_set_attribute(cpu,SDL_GL_DEPTH_SIZE,24);tribes_sdl_gl_set_attribute(cpu,SDL_GL_STENCIL_SIZE,8);
 sdl_started=1;return 1;
}
/* HDC GetDC(HWND) / int ReleaseDC(HWND, HDC). A game window's DC is its SDL
   drawable, created on first use, in the SDL backend's own convention (window
   token + 0x02000000), so backend WGL entry points accept it unchanged. */
uint32_t ss_get_dc(_cpu *cpu,uint32_t window){
 uint64_t base=cpu->_pointer_offset;
 if(!window||!ss_owns_window(base,window))return tribes_get_window_dc(base,window);
 pthread_mutex_lock(&lock);unsigned free_slot=16;
 for(unsigned i=0;i<16;i++){if(dcs[i].dc&&dcs[i].window==window){uint32_t dc=dcs[i].dc;pthread_mutex_unlock(&lock);return dc;}if(!dcs[i].dc&&free_slot==16)free_slot=i;}
#if TARGET_OS_IPHONE
 /* iOS has one window per display, so every game window draws into the same
    SDL window: a later window takes over the existing drawable and its DC
    (Starsiege probes OpenGL on a hidden window, then moves to its main one). */
 for(unsigned i=0;i<16;i++)if(dcs[i].dc){dcs[i].window=window;uint32_t dc=dcs[i].dc;pthread_mutex_unlock(&lock);
  fprintf(stderr,"TribesPad: window 0x%x takes over the drawable (DC 0x%x)\n",window,dc);return dc;}
#endif
 pthread_mutex_unlock(&lock);
 if(free_slot==16)unsupported("device context table",window);
 if(!start_sdl(cpu)){tribes_set_last_error(2000);return 0;}
 int32_t width,height;if(!ss_window_client_size(base,window,&width,&height))unsupported("GL window",window);
 if(width<1)width=1;if(height<1)height=1;
 static const char title[]="Starsiege: Tribes";uint32_t guest_title=cpu->_esp-1024,token=tribes_memory_access_begin((void *)(uintptr_t)(base+guest_title),sizeof title,2);
 memcpy((void *)(uintptr_t)(base+guest_title),title,sizeof title);tribes_memory_access_end(token);
 uint64_t flags=SDL_WINDOW_OPENGL|(headless()?SDL_WINDOW_HIDDEN:0);
 uint32_t sdl_window=tribes_sdl_create_window(cpu,guest_title,(uint32_t)width,(uint32_t)height,(uint32_t)flags,(uint32_t)(flags>>32));
 if(!sdl_window){fprintf(stderr,"TribesPad: SDL window failed: %s\n",SDL_GetError());tribes_set_last_error(2000);return 0;}
 fprintf(stderr,"TribesPad: GL drawable %dx%d for window 0x%x%s\n",(int)width,(int)height,window,headless()?" (hidden)":"");
 pthread_mutex_lock(&lock);dcs[free_slot]=(DeviceContext){sdl_window+0x02000000u,window,0,sdl_window};uint32_t dc=dcs[free_slot].dc;pthread_mutex_unlock(&lock);
 return dc;
}
uint32_t ss_release_dc(uint64_t base,uint32_t window,uint32_t dc){
 pthread_mutex_lock(&lock);DeviceContext *d=dc_of(dc);pthread_mutex_unlock(&lock);
 if(d)return 1;return tribes_release_window_dc(base,window,dc);
}
/* PIXELFORMATDESCRIPTOR for format 1. */
static void describe(unsigned char p[40]){
 memset(p,0,40);uint16_t size=40,version=1;uint32_t flags=0x4|0x20|0x1; /* DRAW_TO_WINDOW, SUPPORT_OPENGL, DOUBLEBUFFER */
 memcpy(p,&size,2);memcpy(p+2,&version,2);memcpy(p+4,&flags,4);
 p[8]=0;p[9]=32;p[10]=8;p[11]=16;p[12]=8;p[13]=8;p[14]=8;p[15]=0;p[16]=8;p[17]=24;p[23]=24;p[24]=8;
}
uint32_t ss_choose_pixel_format(uint64_t base,uint32_t dc,uint32_t descriptor){
 pthread_mutex_lock(&lock);DeviceContext *d=dc_of(dc);pthread_mutex_unlock(&lock);
 if(!d||!descriptor){tribes_set_last_error(d?87:6);return 0;}
 uint32_t token,flags;memcpy(&flags,(unsigned char *)(uintptr_t)(base+descriptor)+4,4);token=tribes_memory_access_begin((void *)(uintptr_t)(base+descriptor),40,1);tribes_memory_access_end(token);
 fprintf(stderr,"TribesPad: ChoosePixelFormat DC 0x%x flags 0x%x\n",dc,flags);
 if(!(flags&0x20))unsupported("non-OpenGL pixel format request",flags);
 return 1;
}
uint32_t ss_describe_pixel_format(uint64_t base,uint32_t dc,uint32_t index,uint32_t bytes,uint32_t output){
 pthread_mutex_lock(&lock);DeviceContext *d=dc_of(dc);pthread_mutex_unlock(&lock);
 fprintf(stderr,"TribesPad: DescribePixelFormat DC 0x%x index %u bytes %u\n",dc,index,bytes);
 if(!d){tribes_set_last_error(6);return 0;}
 if(output){
  if(index!=1){tribes_set_last_error(87);return 0;}
  unsigned char p[40];describe(p);uint32_t n=bytes<40?bytes:40,token=tribes_memory_access_begin((void *)(uintptr_t)(base+output),n,2);
  memcpy((void *)(uintptr_t)(base+output),p,n);tribes_memory_access_end(token);
 }
 return 1;
}
uint32_t ss_set_pixel_format(uint32_t dc,uint32_t format,uint32_t descriptor){
 (void)descriptor;pthread_mutex_lock(&lock);DeviceContext *d=dc_of(dc);
 if(!d){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
 fprintf(stderr,"TribesPad: SetPixelFormat DC 0x%x format %u (current %u)\n",dc,format,d->format);
 if(format!=1||(d->format&&d->format!=format)){pthread_mutex_unlock(&lock);tribes_set_last_error(2000);return 0;}
 d->format=format;pthread_mutex_unlock(&lock);return 1;
}
uint32_t ss_get_pixel_format(uint32_t dc){pthread_mutex_lock(&lock);DeviceContext *d=dc_of(dc);uint32_t f=d?d->format:0;pthread_mutex_unlock(&lock);if(!d)tribes_set_last_error(6);return f;}

/* One native GL context per drawable. Starsiege deletes and recreates its
   WGL context while starting (OpenGL probe, then each video mode change);
   destroying and rebuilding GL4ES state that way hung GL4ES's render list on
   the iPad once in three launches. A deleted context is parked instead, and
   the next one created on the same drawable reuses it; the game sets its GL
   state and reloads its textures for a new context either way. */
static uint32_t parked_context,parked_window;
/* HGLRC wglCreateContext(HDC). */
uint32_t ss_wgl_create_context(_cpu *cpu,uint32_t dc){
 pthread_mutex_lock(&lock);DeviceContext *d=dc_of(dc);DeviceContext copy=d?*d:(DeviceContext){0};pthread_mutex_unlock(&lock);
 if(!copy.dc||!copy.format){tribes_set_last_error(copy.dc?2000:6);return 0;}
 if(!copy.sdl_window){tribes_set_last_error(2000);return 0;}
 uint32_t context=0;
 pthread_mutex_lock(&lock);if(parked_context&&parked_window==copy.sdl_window){context=parked_context;parked_context=parked_window=0;}pthread_mutex_unlock(&lock);
 if(context)fprintf(stderr,"TribesPad: WGL context 0x%x reused on DC 0x%x\n",context,dc);
 if(!context)context=tribes_sdl_gl_create_context(cpu,copy.sdl_window);
 if(!context){fprintf(stderr,"TribesPad: GL context failed: %s\n",SDL_GetError());tribes_set_last_error(2000);return 0;}
 pthread_mutex_lock(&lock);unsigned i=0;while(i<8&&contexts[i].handle)i++;if(i==8){pthread_mutex_unlock(&lock);unsupported("context table",context);}
 contexts[i]=(Context){context,dc};pthread_mutex_unlock(&lock);
 fprintf(stderr,"TribesPad: WGL context 0x%x on DC 0x%x\n",context,dc);
 return context;
}
uint32_t ss_wgl_make_current(_cpu *cpu,uint32_t dc,uint32_t context){
 fprintf(stderr,"TribesPad: wglMakeCurrent DC 0x%x context 0x%x\n",dc,context);
 if(!context){uint32_t r=tribes_sdl_gl_make_current(cpu,0,0);if(r){pthread_mutex_lock(&lock);current_dc=0;pthread_mutex_unlock(&lock);}return r;}
 pthread_mutex_lock(&lock);DeviceContext *d=dc_of(dc);uint32_t window=d?d->sdl_window:0;int known=0;for(unsigned i=0;i<8;i++)if(contexts[i].handle==context)known=1;pthread_mutex_unlock(&lock);
 if(!window||!known){tribes_set_last_error(2000);return 0;}
 uint32_t r=tribes_sdl_gl_make_current(cpu,window,context);
 if(r){pthread_mutex_lock(&lock);current_dc=dc;pthread_mutex_unlock(&lock);}
 return r;
}
/* HDC wglGetCurrentDC(void). */
uint32_t ss_wgl_get_current_dc(void){pthread_mutex_lock(&lock);uint32_t dc=current_dc;pthread_mutex_unlock(&lock);return dc;}
uint32_t ss_wgl_delete_context(_cpu *cpu,uint32_t context){
 fprintf(stderr,"TribesPad: wglDeleteContext 0x%x\n",context);
 pthread_mutex_lock(&lock);int known=0;uint32_t window=0;
 for(unsigned i=0;i<8;i++)if(contexts[i].handle==context){DeviceContext *d=dc_of(contexts[i].dc);window=d?d->sdl_window:0;contexts[i].handle=0;known=1;}
 int park=known&&window;uint32_t older=0;if(park){older=parked_context;parked_context=context;parked_window=window;}pthread_mutex_unlock(&lock);
 if(!known){tribes_set_last_error(6);return 0;}
 if(park){
  if(older&&older!=context)(void)tribes_sdl_gl_destroy_context(cpu,older); /* only the newest is kept */
  /* Windows makes a deleted current context not current. */
  if(has_context()&&!tribes_sdl_gl_make_current(cpu,0,0))return 0;
  pthread_mutex_lock(&lock);current_dc=0;pthread_mutex_unlock(&lock);return 1;
 }
 return tribes_sdl_gl_destroy_context(cpu,context);
}
static void capture(int width,int height){
 const char *folder=getenv("STARSIEGE_CAPTURE");if(!folder)return;
 int probing=getenv("STARSIEGE_GL_PROBE")&&!strcmp(getenv("STARSIEGE_GL_PROBE"),"1");
 if(probing&&swaps==1){ /* Probe: can this drawable be read back at all? */
  unsigned char before[4]={0},probe[4]={0};GLint viewport[4]={0},fbo=-1;
  glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);
  glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,before);
  glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,probe);
  fprintf(stderr,"TribesPad: readback probe viewport %d,%d %dx%d fbo %d before %u,%u,%u,%u after red clear %u,%u,%u,%u error 0x%x\n",
   viewport[0],viewport[1],viewport[2],viewport[3],fbo,before[0],before[1],before[2],before[3],probe[0],probe[1],probe[2],probe[3],glGetError());
 }
 if(probing&&(swaps==2||swaps==30)){
  GLint viewport[4],scissor[4];GLfloat projection[16],color[4];
  glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);glGetFloatv(GL_PROJECTION_MATRIX,projection);glGetFloatv(GL_CURRENT_COLOR,color);
  fprintf(stderr,"TribesPad: GL state frame %llu viewport %d,%d %dx%d scissor %s %d,%d %dx%d depth %d blend %d alpha %d texture2d %d cull %d lighting %d color %.2f,%.2f,%.2f,%.2f proj %.4f %.4f %.4f %.4f | %.4f %.4f\n",
   (unsigned long long)swaps,viewport[0],viewport[1],viewport[2],viewport[3],glIsEnabled(GL_SCISSOR_TEST)?"on":"off",scissor[0],scissor[1],scissor[2],scissor[3],
   glIsEnabled(GL_DEPTH_TEST),glIsEnabled(GL_BLEND),glIsEnabled(GL_ALPHA_TEST),glIsEnabled(GL_TEXTURE_2D),glIsEnabled(GL_CULL_FACE),glIsEnabled(GL_LIGHTING),
   color[0],color[1],color[2],color[3],projection[0],projection[5],projection[10],projection[15],projection[12],projection[13]);
 }
 if(probing&&swaps==30){
  GLint src=0,dst=0,env=0,tex=0,tw=0,th=0,tf=0;glGetIntegerv(GL_BLEND_SRC,&src);glGetIntegerv(GL_BLEND_DST,&dst);glGetTexEnviv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,&env);
  glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&tw);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&th);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&tf);
  unsigned char texel[4]={0};if(tw>0&&th>0&&tw<=2048&&th<=2048){unsigned char *all=malloc((size_t)tw*th*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,all);memcpy(texel,all+((size_t)(th/2)*tw+tw/2)*4,4);free(all);}
  fprintf(stderr,"TribesPad: GL error 0x%x blend 0x%x/0x%x env 0x%x texture %d %dx%d format 0x%x centre texel %u,%u,%u,%u\n",glGetError(),src,dst,env,tex,tw,th,tf,texel[0],texel[1],texel[2],texel[3]);
  /* Test quad: does GL4ES immediate mode reach this drawable? */
  glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,width,height,0,1,-1);glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
  glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glColor4f(1,0,0,1);
  glBegin(GL_QUADS);glVertex2f(10,10);glVertex2f(60,10);glVertex2f(60,60);glVertex2f(10,60);glEnd();
  unsigned char probe[4]={0};glReadPixels(30,height-30,1,1,GL_RGBA,GL_UNSIGNED_BYTE,probe);
  glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glEnable(GL_SCISSOR_TEST);
  fprintf(stderr,"TribesPad: test quad pixel %u,%u,%u,%u\n",probe[0],probe[1],probe[2],probe[3]);
 }
 const char *chosen=getenv("STARSIEGE_CAPTURE_FRAMES"); /* "a,b,c": only these frames */
 if(chosen){int hit=0;for(const char *s=chosen;*s;){char *end;unsigned long long f=strtoull(s,&end,10);if(end==s)break;if(f==swaps)hit=1;s=*end==','?end+1:end;}if(!hit)return;}
 else if(!(swaps==1||swaps==30||swaps==120||swaps%300==0))return;
 size_t stride=(size_t)width*4;unsigned char *pixels=malloc(stride*(size_t)height),*flipped=malloc(stride*(size_t)height);if(!pixels||!flipped)abort();
 glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
 for(int y=0;y<height;y++)memcpy(flipped+(size_t)y*stride,pixels+(size_t)(height-1-y)*stride,stride);
 char path[1024];snprintf(path,sizeof path,"%s/frame-%05llu.png",folder,(unsigned long long)swaps);
 CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
 CGContextRef bitmap=CGBitmapContextCreate(flipped,(size_t)width,(size_t)height,8,stride,space,kCGImageAlphaNoneSkipLast|kCGBitmapByteOrderDefault);
 CGImageRef image=CGBitmapContextCreateImage(bitmap);
 CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)path,(CFIndex)strlen(path),false);
 CGImageDestinationRef out=CGImageDestinationCreateWithURL(url,CFSTR("public.png"),1,NULL);
 if(out){CGImageDestinationAddImage(out,image,NULL);CGImageDestinationFinalize(out);CFRelease(out);fprintf(stderr,"TribesPad: captured %s\n",path);}
 CFRelease(url);CGImageRelease(image);CGContextRelease(bitmap);CGColorSpaceRelease(space);free(pixels);free(flipped);
}
/* BOOL SwapBuffers(HDC). */
uint32_t ss_swap_buffers(_cpu *cpu,uint32_t dc){
 uint64_t base=cpu->_pointer_offset;
 pthread_mutex_lock(&lock);DeviceContext *d=dc_of(dc);DeviceContext copy=d?*d:(DeviceContext){0};pthread_mutex_unlock(&lock);
 if(!copy.sdl_window){tribes_set_last_error(6);return 0;}
 swaps++;
 {extern void ss_gl_trace_report(uint64_t);if(swaps<=3||swaps==30||swaps==300||swaps==3000)ss_gl_trace_report(swaps);}
 int32_t width,height;if(ss_window_client_size(base,copy.window,&width,&height))capture(width,height);
 if(swaps==1||swaps%600==0)fprintf(stderr,"TribesPad: frames presented %llu\n",(unsigned long long)swaps);
 uint32_t presented=tribes_sdl_gl_swap_window(cpu,copy.sdl_window);
 {extern void ss_input_pump(_cpu *);ss_input_pump(cpu);}
 return presented;
}
/* For input: frames presented so far, and the game's SDL window token. */
uint64_t ss_frames(void){return swaps;}
uint32_t ss_sdl_window(void){pthread_mutex_lock(&lock);uint32_t w=0;for(unsigned i=0;i<16;i++)if(dcs[i].dc){w=dcs[i].sdl_window;break;}pthread_mutex_unlock(&lock);return w;}


/* OpenGL 1.1 entry points Starsiege uses that Tribes 2 never reached (they
   were logged no-ops in gl-unprovided.json). Same stdcall marshalling as the
   SDL backend's helpers: doubles arrive as two words, pointers are guest
   addresses. With no current context they do nothing, as on Windows. */
/* Current-context state comes from wglMakeCurrent here: SDL's own query
   relies on thread storage the backend sets up only inside its entry frames. */
static int has_context(void){pthread_mutex_lock(&lock);int current=current_dc!=0;pthread_mutex_unlock(&lock);return current;}
static double f64(uint32_t low,uint32_t high){uint64_t bits=((uint64_t)high<<32)|low;double v;memcpy(&v,&bits,8);return v;}
static void *gptr(_cpu *cpu,uint32_t address,uint32_t bytes,uint32_t access,uint32_t *token){
 if(!address||(uint64_t)address+bytes>UINT64_C(0x100000000))unsupported("GL guest pointer",address);
 void *p=(void *)(uintptr_t)(cpu->_pointer_offset+address);*token=tribes_memory_access_begin(p,bytes?bytes:1,access);return p;
}
void ss_gl_ortho(_cpu *cpu,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5,uint32_t a6,uint32_t a7,uint32_t a8,uint32_t a9,uint32_t a10,uint32_t a11){
 (void)cpu;if(has_context())glOrtho(f64(a0,a1),f64(a2,a3),f64(a4,a5),f64(a6,a7),f64(a8,a9),f64(a10,a11));
}
void ss_gl_scissor(_cpu *cpu,uint32_t x,uint32_t y,uint32_t w,uint32_t h){(void)cpu;if(has_context())glScissor((GLint)x,(GLint)y,(GLsizei)w,(GLsizei)h);}
void ss_gl_vertex2fv(_cpu *cpu,uint32_t address){
 if(!has_context())return;uint32_t token;float v[2];memcpy(v,gptr(cpu,address,8,1,&token),8);tribes_memory_access_end(token);
 glVertex2f(v[0],v[1]);
}
void ss_gl_normal3fv(_cpu *cpu,uint32_t address){
 if(!has_context())return;uint32_t token;float v[3];memcpy(v,gptr(cpu,address,12,1,&token),12);tribes_memory_access_end(token);glNormal3f(v[0],v[1],v[2]);
}
void ss_gl_color_material(_cpu *cpu,uint32_t face,uint32_t mode){(void)cpu;if(has_context())glColorMaterial(face,mode);}
void ss_gl_color_mask(_cpu *cpu,uint32_t r,uint32_t g,uint32_t b,uint32_t a){(void)cpu;if(has_context())glColorMask((GLboolean)(r&255),(GLboolean)(g&255),(GLboolean)(b&255),(GLboolean)(a&255));}
void ss_gl_stencil_func(_cpu *cpu,uint32_t func,uint32_t ref,uint32_t mask){(void)cpu;if(has_context())glStencilFunc(func,(GLint)ref,mask);}
void ss_gl_stencil_mask(_cpu *cpu,uint32_t mask){(void)cpu;if(has_context())glStencilMask(mask);}
void ss_gl_stencil_op(_cpu *cpu,uint32_t fail,uint32_t zfail,uint32_t zpass){(void)cpu;if(has_context())glStencilOp(fail,zfail,zpass);}
void ss_gl_read_buffer(_cpu *cpu,uint32_t mode){(void)cpu;if(has_context())glReadBuffer(mode);}
void ss_gl_copy_tex_sub_image_2d(_cpu *cpu,uint32_t target,uint32_t level,uint32_t xo,uint32_t yo,uint32_t x,uint32_t y,uint32_t w,uint32_t h){
 (void)cpu;if(has_context())glCopyTexSubImage2D(target,(GLint)level,(GLint)xo,(GLint)yo,(GLint)x,(GLint)y,(GLsizei)w,(GLsizei)h);
}
static uint32_t pixel_bytes(uint32_t format,uint32_t type){
 uint32_t components=format==GL_RGBA||format==GL_BGRA?4:format==GL_RGB||format==GL_BGR?3:format==GL_LUMINANCE_ALPHA?2:
  (format==GL_LUMINANCE||format==GL_ALPHA||format==GL_RED||format==GL_DEPTH_COMPONENT||format==GL_STENCIL_INDEX)?1:0;
 if(!components)unsupported("pixel format",format);
 switch(type){
 case GL_UNSIGNED_BYTE:case GL_BYTE:return components;
 case GL_UNSIGNED_SHORT:case GL_SHORT:return 2*components;
 case GL_UNSIGNED_INT:case GL_INT:case GL_FLOAT:return 4*components;
 case GL_UNSIGNED_SHORT_5_6_5:case GL_UNSIGNED_SHORT_4_4_4_4:case GL_UNSIGNED_SHORT_5_5_5_1:return 2;
 default:unsupported("pixel type",type);
 }
}
static uint32_t image_bytes(uint32_t width,uint32_t height,uint32_t format,uint32_t type){
 GLint alignment=4;glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);if(alignment<1)alignment=1;
 uint64_t row=((uint64_t)width*pixel_bytes(format,type)+(uint64_t)alignment-1)/(uint64_t)alignment*(uint64_t)alignment;
 uint64_t total=row*height;if(total>0x40000000)unsupported("pixel transfer size",(uint32_t)(total>>20));return (uint32_t)total;
}
void ss_gl_read_pixels(_cpu *cpu,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t format,uint32_t type,uint32_t address){
 if(!has_context()||!w||!h)return;uint32_t token;void *p=gptr(cpu,address,image_bytes(w,h,format,type),2,&token);
 glReadPixels((GLint)x,(GLint)y,(GLsizei)w,(GLsizei)h,format,type,p);tribes_memory_access_end(token);
}
void ss_gl_get_tex_image(_cpu *cpu,uint32_t target,uint32_t level,uint32_t format,uint32_t type,uint32_t address){
 if(!has_context())return;GLint w=0,h=0;glGetTexLevelParameteriv(target,(GLint)level,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(target,(GLint)level,GL_TEXTURE_HEIGHT,&h);
 if(w<=0||h<=0)return;uint32_t token;void *p=gptr(cpu,address,image_bytes((uint32_t)w,(uint32_t)h,format,type),2,&token);
 glGetTexImage(target,(GLint)level,format,type,p);tribes_memory_access_end(token);
}
