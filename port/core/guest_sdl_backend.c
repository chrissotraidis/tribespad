#include "guest_sdl_backend.h"
#include "guest_sdl_tls.h"
#include "guest_sdl_memory.h"
#include "guest_memory_checks.h"
#include "guest_heap.h"
#include "guest_window_catalog.h"
#include "process_environment.h"
#include <SDL3/SDL.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#include <OpenGL/glu.h>
/* SDL declares extension types but omits legacy Apple prototypes. */
extern GLsync glFenceSync(GLenum,GLbitfield);
extern GLenum glClientWaitSync(GLsync,GLbitfield,GLuint64);
extern void glDeleteSync(GLsync);
extern GLboolean glIsSync(GLsync);
extern void glGetSynciv(GLsync,GLenum,GLsizei,GLsizei *,GLint *);
extern void glGenQueriesARB(GLsizei n,GLuint *ids);
extern void glBeginQueryARB(GLenum target,GLuint id);
extern void glEndQueryARB(GLenum target);
extern void glGetQueryObjectui64v(GLuint id,GLenum pname,GLuint64 *value);
extern const GLubyte *glGetStringi(GLenum name,GLuint index);
extern void glGenerateMipmap(GLenum target);
extern void glGenBuffers(GLsizei n,GLuint *ids);
extern void glDeleteBuffers(GLsizei n,const GLuint *ids);
extern void glBindBuffer(GLenum target,GLuint id);
extern void glBufferData(GLenum target,GLsizeiptr size,const void *data,GLenum usage);
extern void glBindFramebuffer(GLenum target,GLuint framebuffer);
extern void glTexImage3D(GLenum,GLint,GLint,GLsizei,GLsizei,GLsizei,GLint,GLenum,GLenum,const void *);
extern void glTexSubImage3D(GLenum,GLint,GLint,GLint,GLint,GLsizei,GLsizei,GLsizei,GLenum,GLenum,const void *);
extern void glBlendFuncSeparate(GLenum,GLenum,GLenum,GLenum);
extern GLuint glCreateShader(GLenum);
extern void glShaderSource(GLuint,GLsizei,const GLchar *const *,const GLint *);
extern void glCompileShader(GLuint);
extern void glGetShaderiv(GLuint,GLenum,GLint *);
extern void glGetShaderInfoLog(GLuint,GLsizei,GLsizei *,GLchar *);
extern void glDeleteShader(GLuint);
extern GLuint glCreateProgram(void);
extern void glAttachShader(GLuint,GLuint);
extern void glLinkProgram(GLuint);
extern void glGetProgramiv(GLuint,GLenum,GLint *);
extern void glGetProgramInfoLog(GLuint,GLsizei,GLsizei *,GLchar *);
extern void glDeleteProgram(GLuint);
extern void glGetActiveUniform(GLuint,GLuint,GLsizei,GLsizei *,GLint *,GLenum *,GLchar *);
extern void glGetActiveAttrib(GLuint,GLuint,GLsizei,GLsizei *,GLint *,GLenum *,GLchar *);
extern GLint glGetUniformLocation(GLuint,const GLchar *);
extern GLint glGetAttribLocation(GLuint,const GLchar *);
extern void glUniform1i(GLint,GLint);
extern void glUniform1f(GLint,GLfloat);
extern void glUniform4fv(GLint,GLsizei,const GLfloat *);
extern void glUseProgram(GLuint);
extern void glUniform2i(GLint,GLint,GLint);
extern void glUniform4i(GLint,GLint,GLint,GLint,GLint);
#endif
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native SDL owns its opaque allocations, including worker-thread objects.
   Only public results cross the 32-bit boundary through checked guest copies.
   Registered guest allocators remain authoritative for guest-visible storage;
   native SDL internal allocation counts/layouts are not guest ABI. */
typedef struct NativeFrame { _cpu *cpu; struct NativeFrame *previous; } NativeFrame;
typedef struct PublicBuffer { void *host; uint32_t address,size,gamepad_owner; int hint,mutable_pixels,surface_header,event_data; struct PublicBuffer *next; } PublicBuffer;
static PublicBuffer *public_buffers;
typedef struct AudioDevice {SDL_AudioDeviceID native;struct AudioDevice *next;} AudioDevice;
typedef struct AudioStream {SDL_AudioStream *native;uint32_t token;struct AudioStream *next;} AudioStream;
static AudioDevice *audio_devices;
static AudioStream *audio_streams;
static uint32_t next_audio_stream_token=0xd7000001;
static void clear_audio_records(void);
_Static_assert(sizeof(SDL_AudioSpec)==12&&offsetof(SDL_AudioSpec,channels)==4&&offsetof(SDL_AudioSpec,freq)==8,"SDL i686 audio spec ABI");
static int bound;
static _Thread_local int in_guest_allocator;
static uint64_t base;
static uint32_t callbacks[4];
static pthread_t owner;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int ready;
static _Thread_local NativeFrame *active_frame;
static uint32_t log_target,log_userdata;
static int application_ready,application_finished;
typedef struct DisplayMode {
    uint32_t display,token;
    int current;
    SDL_DisplayMode native;
    TribesSDLDisplayMode *public;
    struct DisplayMode *next;
} DisplayMode;
static DisplayMode *display_modes;
/* Caller-owned closest-mode copies retain only an opaque scalar identity. */
typedef struct ClosestMode {SDL_DisplayMode native;TribesSDLDisplayMode public;struct ClosestMode *next;} ClosestMode;
static ClosestMode *closest_modes;
static uint32_t next_mode_token=1;
static PublicBuffer *keyboard_state;
typedef struct Gamepad {
    SDL_Gamepad *native;
    uint32_t token,refs;
    PublicBuffer *name;
    struct Gamepad *next;
} Gamepad;
static Gamepad *gamepads;
static uint32_t next_gamepad_token=0xd9000001;
typedef struct Window {SDL_Window *native;uint32_t token;int swap_set,swap_interval;struct Window *next;} Window;
typedef struct AutoMipmap {GLuint name;struct AutoMipmap *next;} AutoMipmap;
typedef struct SyncObject {void *native;uint32_t token;struct SyncObject *next;} SyncObject;
#if !TARGET_OS_IPHONE
static uint32_t next_sync_token=0xdd000001;
#endif
typedef struct TextureState {unsigned refs;AutoMipmap *automatic;SyncObject *syncs;} TextureState;
typedef struct ClientArray {uint32_t address,size,type,stride;int enabled,defined;} ClientArray;
typedef struct GLContext {SDL_GLContext native;uint32_t token;int core;GLuint copy_bindings[2];ClientArray vertex,color,normal,texcoord[32];GLenum mipmap_hint;unsigned errors;TextureState *textures;struct GLContext *next;} GLContext;
typedef struct Properties {SDL_PropertiesID native;struct Properties *next;} Properties;
static Properties *properties;
static Window *windows;
static GLContext *contexts;
typedef struct Surface {SDL_Surface *native;PublicBuffer *header,*owned_pixels;uint32_t pixels,bytes;void *shadow;struct Surface *next;} Surface;
typedef struct Cursor {SDL_Cursor *native;uint32_t token;struct Cursor *next;} Cursor;
static Surface *surfaces;
static Cursor *cursors;
static uint32_t next_cursor_token=0xdc000001;
static void clear_surfaces_and_cursors(void);
static int graphics_active;
static uint32_t next_window_token=0xda000001,next_context_token=0xdb000001;
static void clear_graphics(void);
static uint32_t window_style(SDL_Window *w);

_Static_assert(sizeof(bool)==1&&sizeof(SDL_JoystickID)==4,"SDL input public ABI");
_Static_assert(sizeof(TribesSDLDisplayMode)==36,"SDL x86 mode size");
_Static_assert(offsetof(TribesSDLDisplayMode,internal)==32,"SDL x86 private slot");
#ifdef TRIBESPAD_SDL_TEST_OBSERVER
extern void sdl_test_metadata_observe(void);
#define OBSERVE_METADATA() sdl_test_metadata_observe()
extern void sdl_test_log_observe(void);
#define OBSERVE_LOG() sdl_test_log_observe()
#else
#define OBSERVE_METADATA() ((void)0)
#define OBSERVE_LOG() ((void)0)
#endif
static _Noreturn void stop(void) {
    fputs("TribesPad: unsupported SDL backend ownership/lifecycle\n",stderr);abort();
}
static void *guest(_cpu *cpu,uint32_t address,size_t bytes) {
    if(!address || bytes>UINT64_C(0x100000000)-address ||
       cpu->_pointer_offset>UINTPTR_MAX-address-bytes)stop();
    return (void *)(uintptr_t)(cpu->_pointer_offset+address);
}
static char *string(_cpu *cpu,uint32_t address) {
    if(!address)return NULL;
    /* Reject an unbounded guest string before passing any pointer to SDL. */
    size_t length=0;
    while(length<1048576 && length<UINT64_C(0x100000000)-address) {
        if(!*(const unsigned char *)guest(cpu,address+(uint32_t)length,1))break;
        length++;
    }
    if(length==1048576 || length==UINT64_C(0x100000000)-address)stop();
    char *copy=malloc(length+1);
    if(copy)memcpy(copy,guest(cpu,address,length+1),length+1);
    return copy;
}
static int seed_environment(_cpu *cpu) {
    SDL_Environment *env=SDL_GetEnvironment();
    if(!env)return 0;
    uint32_t snapshot=tribes_get_environment_strings_w(base);
    if(!snapshot)return 0;
    uint32_t bytes=tribes_environment_snapshot_size(base,snapshot,1);
    if(bytes<4 || bytes%2 || bytes>2097152)stop();
    const uint16_t *wide=guest(cpu,snapshot,bytes);
    char *text=malloc((size_t)bytes*2);
    if(!text){tribes_free_environment_strings_w(base,snapshot);return 0;}
    int ok=1;
    for(uint32_t i=0;i<bytes/2 && wide[i];) {
        size_t n=0;
        while(i<bytes/2 && wide[i]) {
            uint32_t ch=wide[i++];
            if(ch>=0xd800 && ch<=0xdbff) {
                if(i>=bytes/2 || wide[i]<0xdc00 || wide[i]>0xdfff)stop();
                ch=0x10000+((ch-0xd800)<<10)+(wide[i++]-0xdc00);
            } else if(ch>=0xdc00 && ch<=0xdfff)stop();
            if(ch<0x80)text[n++]=(char)ch;
            else if(ch<0x800){text[n++]=(char)(0xc0|(ch>>6));text[n++]=(char)(0x80|(ch&63));}
            else if(ch<0x10000){text[n++]=(char)(0xe0|(ch>>12));text[n++]=(char)(0x80|((ch>>6)&63));text[n++]=(char)(0x80|(ch&63));}
            else {text[n++]=(char)(0xf0|(ch>>18));text[n++]=(char)(0x80|((ch>>12)&63));text[n++]=(char)(0x80|((ch>>6)&63));text[n++]=(char)(0x80|(ch&63));}
        }
        if(i>=bytes/2)stop();
        i++;text[n]=0;
        char *equals=strchr(text,'=');
        if(equals && equals!=text){*equals++=0;if(!SDL_SetEnvironmentVariable(env,text,equals,true)){ok=0;break;}}
    }
    free(text);
    if(!tribes_free_environment_strings_w(base,snapshot))stop();
    return ok;
}
static void checked_copy(void *to,const void *from,size_t size) {
    uint32_t token=tribes_memory_transfer_begin(to,from,size);
    memcpy(to,from,size);tribes_memory_access_end(token);
}
static uint32_t allocate_guest(NativeFrame *frame,unsigned index,unsigned count,const uint32_t *args) {
    if(!frame||frame!=active_frame||in_guest_allocator||frame->cpu->_esp<128)stop();
    in_guest_allocator=1;
    uint32_t value=tribes_call_guest_cdecl(frame->cpu,callbacks[index],count,args,frame->cpu->_esp-128);
    in_guest_allocator=0;return value;
}
static PublicBuffer *public_new(NativeFrame *frame,uint32_t size) {
    if(!size)stop();
    PublicBuffer *b=calloc(1,sizeof *b);if(!b)return NULL;
    b->host=calloc(1,size);if(!b->host){free(b);return NULL;}
    uint32_t args[]={1,size};b->address=allocate_guest(frame,1,2,args);b->size=size;
    if(!b->address){free(b->host);free(b);return NULL;}
    for(PublicBuffer *v=public_buffers;v;v=v->next)
        if((uint64_t)b->address<v->address+v->size&&(uint64_t)v->address<b->address+size)stop();
    checked_copy(guest(frame->cpu,b->address,size),b->host,size);
    b->next=public_buffers;public_buffers=b;return b;
}
static PublicBuffer *public_find(void *host) {
    for(PublicBuffer *b=public_buffers;b;b=b->next)if(b->host==host)return b;
    stop();
}
static void public_publish(NativeFrame *frame,PublicBuffer *b) {
    checked_copy(guest(frame->cpu,b->address,b->size),b->host,b->size);
}
static void public_free(NativeFrame *frame,void *host) {
    PublicBuffer **slot=&public_buffers;while(*slot&&(*slot)->host!=host)slot=&(*slot)->next;
    if(!*slot)stop();PublicBuffer *b=*slot;*slot=b->next;
    (void)allocate_guest(frame,3,1,&b->address);free(b->host);free(b);
}
static void public_validate(_cpu *cpu) {
    for(PublicBuffer *b=public_buffers;b;b=b->next){
        if(b->mutable_pixels)continue;
        if(b->surface_header){
            int32_t refs;checked_copy(&refs,guest(cpu,b->address+24,4),4);
            if(refs<1)stop();((TribesSDLSurface *)b->host)->refcount=refs;
        }
        unsigned char bytes[4096];
        for(uint32_t offset=0;offset<b->size;){
            uint32_t n=b->size-offset;if(n>sizeof bytes)n=sizeof bytes;
            checked_copy(bytes,guest(cpu,b->address+offset,n),n);
            if(memcmp(bytes,(char *)b->host+offset,n))stop();offset+=n;
        }
    }
}
/* A native audio worker must never execute a guest callback using another
   thread's CPU/stack. Logs outside a guest call are copied into a FIFO and
   delivered on the next owner entry. Category, priority and callback identity
   are captured at emission; no native message pointer escapes its callback. */
typedef struct PendingLog { uint32_t target,userdata;int category;SDL_LogPriority priority;char *message;struct PendingLog *next; } PendingLog;
static pthread_mutex_t log_lock=PTHREAD_MUTEX_INITIALIZER;
static PendingLog *logs_head,*logs_tail;
static int draining_logs;
static void deliver_log(NativeFrame *frame,uint32_t target,uint32_t userdata,int category,SDL_LogPriority priority,const char *message) {
    _cpu *cpu=frame->cpu;uint32_t error=tribes_get_last_error();size_t length=strnlen(message,1048576);
    if(length==1048576||cpu->_esp<128)stop();
    uint32_t copy=tribes_global_alloc(base,0,(uint32_t)length+1);if(!copy)stop();
    checked_copy(guest(cpu,copy,length+1),message,length+1);
    uint32_t args[]={userdata,(uint32_t)category,(uint32_t)priority,copy};
    (void)tribes_call_guest_cdecl(cpu,target,4,args,cpu->_esp-128);
    if(tribes_global_free(base,copy))stop();tribes_set_last_error(error);
}
static void drain_logs(NativeFrame *frame) {
    if(draining_logs)return;draining_logs=1;
    pthread_mutex_lock(&log_lock);PendingLog *list=logs_head;logs_head=logs_tail=NULL;pthread_mutex_unlock(&log_lock);
    while(list){PendingLog *next=list->next;deliver_log(frame,list->target,list->userdata,list->category,list->priority,list->message);free(list->message);free(list);list=next;}
    draining_logs=0;
}
static int enter(_cpu *cpu,NativeFrame *frame) {
    uint32_t current[4];if(!cpu||!tribes_sdl_memory_functions(cpu->_pointer_offset,current))stop();
    pthread_mutex_lock(&lock);
    if(application_finished||in_guest_allocator)stop();
    if(!bound){base=cpu->_pointer_offset;owner=pthread_self();memcpy(callbacks,current,sizeof callbacks);bound=1;}
    if(base!=cpu->_pointer_offset||!pthread_equal(owner,pthread_self())||memcmp(callbacks,current,sizeof callbacks))stop();
    pthread_mutex_unlock(&lock);public_validate(cpu);
    *frame=(NativeFrame){cpu,active_frame};active_frame=frame;
    if(!ready){if(!seed_environment(cpu))return 0;ready=1;}
    drain_logs(frame);return 1;
}
static void leave(NativeFrame *frame) {
    if(active_frame!=frame)stop();drain_logs(frame);active_frame=frame->previous;
}
static void SDLCALL log_output(void *userdata,int category,SDL_LogPriority priority,const char *message) {
    if(userdata!=&log_target||!message)stop();
    pthread_mutex_lock(&log_lock);uint32_t target=log_target,data=log_userdata;
    if(!target){pthread_mutex_unlock(&log_lock);return;}
    if(active_frame){pthread_mutex_unlock(&log_lock);deliver_log(active_frame,target,data,category,priority,message);return;}
    size_t length=strnlen(message,1048576);if(length==1048576)stop();PendingLog *v=calloc(1,sizeof *v);if(!v)stop();
    v->message=malloc(length+1);if(!v->message)stop();memcpy(v->message,message,length+1);
    v->target=target;v->userdata=data;v->category=category;v->priority=priority;
    if(logs_tail)logs_tail->next=v;else logs_head=v;logs_tail=v;pthread_mutex_unlock(&log_lock);
}
void tribes_sdl_set_log_output(_cpu *cpu,uint32_t callback,uint32_t userdata) {
    uint32_t error=tribes_get_last_error();NativeFrame frame;
    if(!enter(cpu,&frame))stop();
    pthread_mutex_lock(&log_lock);log_target=callback;log_userdata=userdata;pthread_mutex_unlock(&log_lock);
    SDL_SetLogOutputFunction(callback?log_output:NULL,callback?&log_target:NULL);
    OBSERVE_LOG();leave(&frame);tribes_set_last_error(error);
}
void tribes_sdl_set_main_ready(_cpu *cpu) {
    uint32_t error=tribes_get_last_error();NativeFrame frame;
    if(!enter(cpu,&frame))stop();
    SDL_SetMainReady();leave(&frame);tribes_set_last_error(error);
}
void tribes_sdl_host_application_ready(void) {
    if(!pthread_main_np()||application_finished)stop();
    application_ready=1;
}
void tribes_sdl_host_application_finished(void) {
    if(!pthread_main_np())stop();
    pthread_mutex_lock(&lock);
    if(!application_ready||active_frame||ready||SDL_WasInit(0)||
       public_buffers||display_modes||closest_modes||keyboard_state||gamepads||windows||contexts||properties||surfaces||cursors||audio_devices||audio_streams)stop();
    application_ready=0;application_finished=1;
    pthread_mutex_lock(&log_lock);if(logs_head)stop();log_target=log_userdata=0;pthread_mutex_unlock(&log_lock);
    pthread_mutex_unlock(&lock);
}
/* Native Cocoa/UIKit video initialization runs only from the Apple host's
   main entry. This does not expose native windows or event unions to guests. */
static void event_flags(uint32_t flags) {
    if(flags&~(uint32_t)(SDL_INIT_EVENTS|SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK|SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC)){
        fputs("TribesPad: unsupported SDL subsystem initialization flags\n",stderr);abort();
    }
    if((flags&(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK|SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC))&&(!pthread_main_np()||!application_ready)){
        fputs("TribesPad: SDL video requires Apple main application entry\n",stderr);abort();
    }
}
/* Called within an owner allocator frame; SDL never sees the marshalled mode
   or a guest-controlled private token. Tokens are not reused across reinit. */
static void clear_display_modes(void) {
    while(closest_modes){ClosestMode *m=closest_modes;closest_modes=m->next;free(m);}
    while(display_modes){DisplayMode *m=display_modes;display_modes=m->next;
        public_free(active_frame,m->public);free(m);}
}
/* Native input objects never cross the32-bit ABI. Enumeration arrays are
   transferred to the registered guest allocator and owned by the caller;
   keyboard state and names remain borrowed, checked public snapshots. */
static void forget_gamepad(Gamepad **slot) {
    Gamepad *g=*slot;*slot=g->next;
    PublicBuffer *b=public_buffers;while(b){PublicBuffer *next=b->next;if(b->gamepad_owner==g->token)public_free(active_frame,b->host);b=next;}
    free(g);
}
static void clear_gamepads(void) {while(gamepads)forget_gamepad(&gamepads);}
static Gamepad **gamepad_slot(uint32_t token) {
    Gamepad **slot=&gamepads;while(*slot&&(*slot)->token!=token)slot=&(*slot)->next;
    if(!*slot)stop();return slot;
}
uint32_t tribes_sdl_get_keyboard_state(_cpu *cpu,uint32_t count_address) {
    event_flags(SDL_INIT_VIDEO);uint32_t error=tribes_get_last_error();NativeFrame frame;uint32_t result=0;int count=0;
    if(enter(cpu,&frame)) {
        const bool *native=SDL_GetKeyboardState(&count);
        if(!native||count<=0||count>65536)stop();
        if(!keyboard_state)keyboard_state=public_new(&frame,(uint32_t)count);
        if(keyboard_state){
            if(keyboard_state->size!=(uint32_t)count)stop();
            memcpy(keyboard_state->host,native,(size_t)count);public_publish(&frame,keyboard_state);result=keyboard_state->address;
        }else {SDL_OutOfMemory();count=0;}
    }
    if(count_address)checked_copy(guest(cpu,count_address,4),&count,4);
    leave(&frame);tribes_set_last_error(error);return result;
}
/* SDL3 keeps event strings through the poll cycle, until the next pump.
   Preserve that lifetime in checked guest storage; native pointers never
   appear in the 128-byte i686 union. Unknown event families fail explicitly. */
static int event_cycle_finished,event_active;
static void clear_event_data(void){
    PublicBuffer *b=public_buffers;while(b){PublicBuffer *next=b->next;if(b->event_data)public_free(active_frame,b->host);b=next;}
}
static uint32_t event_string(const char *text){
    if(!text)return 0;size_t n=strnlen(text,1048576);if(n==1048576)stop();
    PublicBuffer *b=public_new(active_frame,(uint32_t)n+1);if(!b)stop();b->event_data=1;
    memcpy(b->host,text,n+1);public_publish(active_frame,b);return b->address;
}
static uint32_t event_strings(const char *const *strings,int count){
    if(count<0||count>1024||(count&&!strings))stop();if(!strings)return 0;
    PublicBuffer *b=public_new(active_frame,((uint32_t)count+1)*4);if(!b)stop();b->event_data=1;
    for(int i=0;i<count;i++)((uint32_t *)b->host)[i]=event_string(strings[i]);
    public_publish(active_frame,b);return b->address;
}
static void refresh_keyboard(void){
    if(!keyboard_state)return;int n=0;const bool *keys=SDL_GetKeyboardState(&n);
    if(!keys||n<=0||(uint32_t)n!=keyboard_state->size)stop();
    memcpy(keyboard_state->host,keys,(size_t)n);public_publish(active_frame,keyboard_state);
}
static void event_to_guest(unsigned char out[128],const SDL_Event *e){
    memset(out,0,128);
#define EF(offset,member) memcpy(out+(offset),&e->member,sizeof e->member)
#define EP(offset,value) do{uint32_t p=(value);memcpy(out+(offset),&p,4);}while(0)
    EF(0,type);EF(8,common.timestamp);
    if(e->type>=SDL_EVENT_DISPLAY_FIRST&&e->type<=SDL_EVENT_DISPLAY_LAST){EF(16,display.displayID);EF(20,display.data1);EF(24,display.data2);return;}
    if(e->type>=SDL_EVENT_WINDOW_FIRST&&e->type<=SDL_EVENT_WINDOW_LAST){EF(16,window.windowID);EF(20,window.data1);EF(24,window.data2);return;}
    switch(e->type){
    case SDL_EVENT_QUIT:case SDL_EVENT_TERMINATING:case SDL_EVENT_LOW_MEMORY:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_WILL_ENTER_FOREGROUND:case SDL_EVENT_DID_ENTER_FOREGROUND:
    case SDL_EVENT_LOCALE_CHANGED:case SDL_EVENT_SYSTEM_THEME_CHANGED:case SDL_EVENT_KEYMAP_CHANGED:break;
    case SDL_EVENT_KEY_DOWN:case SDL_EVENT_KEY_UP:
        EF(16,key.windowID);EF(20,key.which);EF(24,key.scancode);EF(28,key.key);
        EF(32,key.mod);EF(34,key.raw);EF(36,key.down);EF(37,key.repeat);break;
    case SDL_EVENT_KEYBOARD_ADDED:case SDL_EVENT_KEYBOARD_REMOVED:EF(16,kdevice.which);break;
    case SDL_EVENT_MOUSE_ADDED:case SDL_EVENT_MOUSE_REMOVED:EF(16,mdevice.which);break;
    case SDL_EVENT_MOUSE_MOTION:
        EF(16,motion.windowID);EF(20,motion.which);EF(24,motion.state);EF(28,motion.x);EF(32,motion.y);EF(36,motion.xrel);EF(40,motion.yrel);break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:case SDL_EVENT_MOUSE_BUTTON_UP:
        EF(16,button.windowID);EF(20,button.which);EF(24,button.button);EF(25,button.down);EF(26,button.clicks);EF(28,button.x);EF(32,button.y);break;
    case SDL_EVENT_MOUSE_WHEEL:
        EF(16,wheel.windowID);EF(20,wheel.which);EF(24,wheel.x);EF(28,wheel.y);EF(32,wheel.direction);EF(36,wheel.mouse_x);EF(40,wheel.mouse_y);EF(44,wheel.integer_x);EF(48,wheel.integer_y);break;
    case SDL_EVENT_TEXT_INPUT:EF(16,text.windowID);EP(20,event_string(e->text.text));break;
    case SDL_EVENT_TEXT_EDITING:
        EF(16,edit.windowID);EP(20,event_string(e->edit.text));EF(24,edit.start);EF(28,edit.length);break;
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        EF(16,edit_candidates.windowID);EP(20,event_strings(e->edit_candidates.candidates,e->edit_candidates.num_candidates));
        EF(24,edit_candidates.num_candidates);EF(28,edit_candidates.selected_candidate);EF(32,edit_candidates.horizontal);break;
    case SDL_EVENT_DROP_BEGIN:case SDL_EVENT_DROP_FILE:case SDL_EVENT_DROP_TEXT:case SDL_EVENT_DROP_COMPLETE:case SDL_EVENT_DROP_POSITION:
        EF(16,drop.windowID);EF(20,drop.x);EF(24,drop.y);EP(28,event_string(e->drop.source));EP(32,event_string(e->drop.data));break;
    case SDL_EVENT_CLIPBOARD_UPDATE:
        EF(16,clipboard.owner);EF(20,clipboard.num_mime_types);EP(24,event_strings(e->clipboard.mime_types,e->clipboard.num_mime_types));break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:EF(16,jaxis.which);EF(20,jaxis.axis);EF(24,jaxis.value);break;
    case SDL_EVENT_JOYSTICK_BALL_MOTION:EF(16,jball.which);EF(20,jball.ball);EF(24,jball.xrel);EF(26,jball.yrel);break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:EF(16,jhat.which);EF(20,jhat.hat);EF(21,jhat.value);break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:case SDL_EVENT_JOYSTICK_BUTTON_UP:EF(16,jbutton.which);EF(20,jbutton.button);EF(21,jbutton.down);break;
    case SDL_EVENT_JOYSTICK_ADDED:case SDL_EVENT_JOYSTICK_REMOVED:case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE:EF(16,jdevice.which);break;
    case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:EF(16,jbattery.which);EF(20,jbattery.state);EF(24,jbattery.percent);break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:EF(16,gaxis.which);EF(20,gaxis.axis);EF(24,gaxis.value);break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:case SDL_EVENT_GAMEPAD_BUTTON_UP:EF(16,gbutton.which);EF(20,gbutton.button);EF(21,gbutton.down);break;
    case SDL_EVENT_GAMEPAD_ADDED:case SDL_EVENT_GAMEPAD_REMOVED:case SDL_EVENT_GAMEPAD_REMAPPED:
    case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE:case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:EF(16,gdevice.which);break;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
        EF(16,gtouchpad.which);EF(20,gtouchpad.touchpad);EF(24,gtouchpad.finger);EF(28,gtouchpad.x);EF(32,gtouchpad.y);EF(36,gtouchpad.pressure);break;
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
        EF(16,gsensor.which);EF(20,gsensor.sensor);EF(24,gsensor.data);EF(40,gsensor.sensor_timestamp);break;
    case SDL_EVENT_AUDIO_DEVICE_ADDED:case SDL_EVENT_AUDIO_DEVICE_REMOVED:case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:EF(16,adevice.which);EF(20,adevice.recording);break;
    case SDL_EVENT_FINGER_DOWN:case SDL_EVENT_FINGER_UP:case SDL_EVENT_FINGER_MOTION:case SDL_EVENT_FINGER_CANCELED:
        EF(16,tfinger.touchID);EF(24,tfinger.fingerID);EF(32,tfinger.x);EF(36,tfinger.y);EF(40,tfinger.dx);EF(44,tfinger.dy);EF(48,tfinger.pressure);EF(52,tfinger.windowID);break;
    default:fprintf(stderr,"TribesPad: unsupported SDL event type 0x%x\n",e->type);stop();
    }
#undef EF
#undef EP
}
uint32_t tribes_sdl_poll_event(_cpu *cpu,uint32_t address){
    event_flags(SDL_INIT_VIDEO);if(event_active||graphics_active)stop();event_active=graphics_active=1;
    uint32_t error=tribes_get_last_error();NativeFrame frame;if(!enter(cpu,&frame))stop();
    if(address){uint32_t token=tribes_memory_access_begin(guest(cpu,address,128),128,2);tribes_memory_access_end(token);}
    if(event_cycle_finished){clear_event_data();event_cycle_finished=0;}
    SDL_Event event={0};uint32_t result=SDL_PollEvent(address?&event:NULL);refresh_keyboard();
    if(result&&address){unsigned char output[128];event_to_guest(output,&event);checked_copy(guest(cpu,address,128),output,128);}
    if(!result)event_cycle_finished=1;
    leave(&frame);event_active=graphics_active=0;tribes_set_last_error(error);return result;
}
void tribes_sdl_pump_events(_cpu *cpu){
    event_flags(SDL_INIT_VIDEO);if(event_active||graphics_active)stop();event_active=graphics_active=1;
    uint32_t error=tribes_get_last_error();NativeFrame frame;if(!enter(cpu,&frame))stop();
    clear_event_data();event_cycle_finished=0;SDL_PumpEvents();refresh_keyboard();
    leave(&frame);event_active=graphics_active=0;tribes_set_last_error(error);
}
uint32_t tribes_sdl_get_gamepads(_cpu *cpu,uint32_t count_address) {
    event_flags(SDL_INIT_GAMEPAD);uint32_t error=tribes_get_last_error();NativeFrame frame;uint32_t result=0;int count=0;
    if(enter(cpu,&frame)) {
        SDL_JoystickID *native=SDL_GetGamepads(&count);
        if(native){
            if(count<0||(uint32_t)count>UINT32_MAX/4-1)stop();
            uint32_t args[]={1,((uint32_t)count+1)*4};result=allocate_guest(&frame,1,2,args);
            if(result)checked_copy(guest(cpu,result,args[1]),native,args[1]);
            else {SDL_OutOfMemory();count=0;}
            SDL_free(native);
        }
    }
    if(count_address)checked_copy(guest(cpu,count_address,4),&count,4);
    leave(&frame);tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_has_joystick(_cpu *cpu) {
    event_flags(SDL_INIT_JOYSTICK);uint32_t error=tribes_get_last_error();NativeFrame frame;
    uint32_t result=enter(cpu,&frame)?SDL_HasJoystick():0;leave(&frame);tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_open_gamepad(_cpu *cpu,uint32_t instance) {
    event_flags(SDL_INIT_GAMEPAD);uint32_t error=tribes_get_last_error();NativeFrame frame;uint32_t result=0;
    if(enter(cpu,&frame)) {
        SDL_Gamepad *native=SDL_OpenGamepad(instance);
        if(native){
            Gamepad *g=gamepads;while(g&&g->native!=native)g=g->next;
            if(g){if(g->refs==UINT32_MAX)stop();g->refs++;}
            else {
                g=calloc(1,sizeof *g);
                if(!g){SDL_CloseGamepad(native);SDL_OutOfMemory();}
                else {if(next_gamepad_token>=0xda000000)stop();g->native=native;g->refs=1;g->token=next_gamepad_token++;g->next=gamepads;gamepads=g;}
            }
            if(g)result=g->token;
        }
    }
    leave(&frame);tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_get_gamepad_name(_cpu *cpu,uint32_t token) {
    event_flags(SDL_INIT_GAMEPAD);uint32_t error=tribes_get_last_error();NativeFrame frame;uint32_t result=0;
    if(enter(cpu,&frame)) {
        Gamepad *g=token?*gamepad_slot(token):NULL;
        const char *native=SDL_GetGamepadName(g?g->native:NULL);
        if(native){
            size_t size=strlen(native)+1;if(size>UINT32_MAX)stop();
            if(!g)stop();
            if(!g->name||g->name->size!=size||memcmp(g->name->host,native,size)){
                PublicBuffer *copy=public_new(&frame,(uint32_t)size);
                if(copy){copy->gamepad_owner=g->token;memcpy(copy->host,native,size);public_publish(&frame,copy);g->name=copy;}
                else {SDL_OutOfMemory();leave(&frame);tribes_set_last_error(error);return 0;}
            }
            result=g->name->address;
        }
    }
    leave(&frame);tribes_set_last_error(error);return result;
}
void tribes_sdl_close_gamepad(_cpu *cpu,uint32_t token) {
    event_flags(SDL_INIT_GAMEPAD);uint32_t error=tribes_get_last_error();NativeFrame frame;
    if(!enter(cpu,&frame))stop();
    Gamepad **slot=token?gamepad_slot(token):NULL;
    SDL_CloseGamepad(slot?(*slot)->native:NULL);
    if(slot&&!--(*slot)->refs)forget_gamepad(slot);
    leave(&frame);tribes_set_last_error(error);
}
uint32_t tribes_sdl_get_primary_display(_cpu *cpu) {
    event_flags(SDL_INIT_VIDEO);uint32_t error=tribes_get_last_error();NativeFrame frame;
    uint32_t result=enter(cpu,&frame)?SDL_GetPrimaryDisplay():0;
    leave(&frame);tribes_set_last_error(error);return result;
}
static uint32_t display_mode(_cpu *cpu,uint32_t display,int current) {
    event_flags(SDL_INIT_VIDEO);uint32_t error=tribes_get_last_error();NativeFrame frame;uint32_t result=0;
    if(enter(cpu,&frame)) {
        const SDL_DisplayMode *native=current?SDL_GetCurrentDisplayMode(display):SDL_GetDesktopDisplayMode(display);
        DisplayMode **slot=&display_modes;
        while(*slot&&((*slot)->display!=display||(*slot)->current!=current))slot=&(*slot)->next;
        if(!native){
            /* A removed display no longer owns a borrowed result. */
            if(*slot){DisplayMode *old=*slot;*slot=old->next;public_free(active_frame,old->public);free(old);}
        } else {
            if(!*slot){
                DisplayMode *m=calloc(1,sizeof *m);
                if(m){PublicBuffer *b=public_new(&frame,sizeof *m->public);if(b)m->public=b->host;}
                if(!m||!m->public){free(m);SDL_OutOfMemory();}
                else {if(!next_mode_token)stop();m->display=display;m->current=current;m->token=next_mode_token++;*slot=m;}
            }
            if(*slot){
                DisplayMode *m=*slot;m->native=*native;
                *m->public=(TribesSDLDisplayMode){native->displayID,native->format,native->w,native->h,
                    native->pixel_density,native->refresh_rate,native->refresh_rate_numerator,
                    native->refresh_rate_denominator,native->internal?m->token:0};
                PublicBuffer *b=public_find(m->public);public_publish(&frame,b);result=b->address;
            }
        }
    }
    leave(&frame);tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_get_desktop_display_mode(_cpu *cpu,uint32_t display){return display_mode(cpu,display,0);}
uint32_t tribes_sdl_get_current_display_mode(_cpu *cpu,uint32_t display){return display_mode(cpu,display,1);}
/* SDL returns one caller-owned allocation containing the pointer array and
   mode copies. Repack that same ownership into 32-bit pointers/36-byte modes.
   Internal fields are opaque enumeration identities, never native pointers;
   passing these records into a future mode-setting API still needs an adapter. */
uint32_t tribes_sdl_get_fullscreen_display_modes(_cpu *cpu,uint32_t display,uint32_t count_address){
    event_flags(SDL_INIT_VIDEO);uint32_t error=tribes_get_last_error();NativeFrame frame;uint32_t result=0;int count=0;
    if(enter(cpu,&frame)){
        SDL_DisplayMode **native=SDL_GetFullscreenDisplayModes(display,&count);
        if(native){
            if(count<0||(uint32_t)count>(UINT32_MAX-4)/40)stop();
            uint32_t table=((uint32_t)count+1)*4,bytes=table+(uint32_t)count*36,args[]={1,bytes};
            result=allocate_guest(&frame,1,2,args);
            if(result){
                (void)guest(cpu,result,bytes);
                for(int n=0;n<count;n++){
                    const SDL_DisplayMode *m=native[n];if(!m||!next_mode_token)stop();
                    uint32_t at=result+table+(uint32_t)n*36;
                    TribesSDLDisplayMode copy={m->displayID,m->format,m->w,m->h,m->pixel_density,m->refresh_rate,m->refresh_rate_numerator,m->refresh_rate_denominator,m->internal?next_mode_token++:0};
                    checked_copy(guest(cpu,result+(uint32_t)n*4,4),&at,4);checked_copy(guest(cpu,at,36),&copy,36);
                }
                if(native[count])stop();uint32_t zero=0;checked_copy(guest(cpu,result+(uint32_t)count*4,4),&zero,4);
            }else {SDL_OutOfMemory();count=0;}
            SDL_free(native);
        }
    }
    if(count_address)checked_copy(guest(cpu,count_address,4),&count,4);
    leave(&frame);tribes_set_last_error(error);return result;
}
uint64_t tribes_sdl_desktop_dimensions(_cpu *cpu,uint32_t mode) {
    event_flags(SDL_INIT_VIDEO);
    if(!cpu||!bound||application_finished||cpu->_pointer_offset!=base||!pthread_equal(owner,pthread_self()))stop();
    if(!display_modes)return 0;
    uint32_t error=tribes_get_last_error();NativeFrame frame;uint64_t result=0;
    if(!enter(cpu,&frame))stop();
    for(DisplayMode *m=display_modes;m;m=m->next){
        if(!m->current&&public_find(m->public)->address==mode){
            if(m->public->width<=0||m->public->height<=0)stop();
            result=((uint64_t)(uint32_t)m->public->height<<32)|(uint32_t)m->public->width;break;
        }
    }
    leave(&frame);tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_init(_cpu *cpu,uint32_t flags) {
    event_flags(flags);uint32_t error=tribes_get_last_error();NativeFrame frame;
    uint32_t result=enter(cpu,&frame)?SDL_Init(flags):0;
    leave(&frame);tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_was_init(_cpu *cpu,uint32_t flags) {
    uint32_t error=tribes_get_last_error();NativeFrame frame;
    if(!enter(cpu,&frame))stop();
    uint32_t result=SDL_WasInit(flags);leave(&frame);tribes_set_last_error(error);return result;
}
void tribes_sdl_quit_subsystem(_cpu *cpu,uint32_t flags) {
    event_flags(flags);uint32_t error=tribes_get_last_error();NativeFrame frame;
    if(!enter(cpu,&frame))stop();
    if((flags&SDL_INIT_VIDEO)&&(windows||contexts||cursors)){fputs("TribesPad: SDL video subsystem quit with live graphics objects is not implemented\n",stderr);abort();}
    SDL_QuitSubSystem(flags);
    /* Native SDL destroys streams/devices only when the final audio ref ends. */
    if((flags&SDL_INIT_AUDIO)&&!SDL_WasInit(SDL_INIT_AUDIO))clear_audio_records();
    if(!SDL_WasInit(SDL_INIT_GAMEPAD))clear_gamepads();
    if((flags&SDL_INIT_VIDEO)&&!SDL_WasInit(SDL_INIT_VIDEO))clear_display_modes();
    leave(&frame);tribes_set_last_error(error);
}
uint32_t tribes_sdl_set_hint_priority(_cpu *cpu,uint32_t name,uint32_t value,uint32_t priority) {
    if(priority>SDL_HINT_OVERRIDE)stop();
    uint32_t error=tribes_get_last_error();
    char *n=string(cpu,name),*v=string(cpu,value);
    if((name&&!n)||(value&&!v)){free(n);free(v);tribes_sdl_memory_out_of_memory();return 0;}
    NativeFrame frame;
    /* The pinned Windows client hardcodes DirectSound as its SDL backend.
       Translate that platform choice, preserving hint priority and actual
       native success/failure. Unknown drivers still fail in native SDL. */
    const char *native_value=v;
#ifdef __APPLE__
    if(n&&v&&!strcmp(n,SDL_HINT_AUDIO_DRIVER)&&!strcmp(v,"directsound"))native_value="coreaudio";
#endif
    uint32_t result=enter(cpu,&frame)?SDL_SetHintWithPriority(n,native_value,(SDL_HintPriority)priority):0;
    leave(&frame);
    free(n);free(v);tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_set_hint(_cpu *cpu,uint32_t name,uint32_t value) {
    return tribes_sdl_set_hint_priority(cpu,name,value,SDL_HINT_NORMAL);
}
uint32_t tribes_sdl_set_app_metadata(_cpu *cpu,uint32_t name,uint32_t version,uint32_t identifier) {
    uint32_t error=tribes_get_last_error();
    char *n=string(cpu,name),*v=string(cpu,version),*i=string(cpu,identifier);
    if((name&&!n)||(version&&!v)||(identifier&&!i)){
        free(n);free(v);free(i);tribes_sdl_memory_out_of_memory();tribes_set_last_error(error);return 0;
    }
    NativeFrame frame;
    uint32_t result=enter(cpu,&frame)?SDL_SetAppMetadata(n,v,i):0;
    OBSERVE_METADATA();
    leave(&frame);free(n);free(v);free(i);
    tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_set_app_metadata_property(_cpu *cpu,uint32_t name,uint32_t value) {
    uint32_t error=tribes_get_last_error();char *n=string(cpu,name),*v=string(cpu,value);
    if((name&&!n)||(value&&!v)){
        free(n);free(v);tribes_sdl_memory_out_of_memory();tribes_set_last_error(error);return 0;
    }
    NativeFrame frame;
    uint32_t result=enter(cpu,&frame)?SDL_SetAppMetadataProperty(n,v):0;
    OBSERVE_METADATA();
    leave(&frame);free(n);free(v);
    tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_reset_hint(_cpu *cpu,uint32_t name) {
    uint32_t error=tribes_get_last_error();char *n=string(cpu,name);
    if(name&&!n){tribes_sdl_memory_out_of_memory();return 0;}
    NativeFrame frame;uint32_t result=enter(cpu,&frame)?SDL_ResetHint(n):0;
    leave(&frame);free(n);tribes_set_last_error(error);return result;
}
void tribes_sdl_backend_quit(_cpu *cpu) {
    uint32_t error=tribes_get_last_error();NativeFrame frame;
    (void)enter(cpu,&frame);if(graphics_active)stop();graphics_active=1;
    clear_graphics();clear_display_modes();tribes_sdl_tls_cleanup_begin(cpu);SDL_Quit();tribes_sdl_tls_cleanup_end();clear_audio_records();clear_gamepads();keyboard_state=NULL;
    while(public_buffers)public_free(&frame,public_buffers->host);
    leave(&frame);ready=0;graphics_active=0;event_cycle_finished=0;
    /* Native SDL may retain native library-lifetime storage. Guest copies are
       released by their registered callbacks before the CPU can go away. */
    tribes_set_last_error(error);
}
uint32_t tribes_sdl_get_hint(_cpu *cpu,uint32_t name) {
    uint32_t error=tribes_get_last_error();char *n=string(cpu,name);
    if(name&&!n){tribes_sdl_memory_out_of_memory();return 0;}
    NativeFrame frame;uint32_t result=0;
    if(enter(cpu,&frame)) {
        const char *value=SDL_GetHint(n);
        if(value) {
            size_t bytes=strlen(value)+1;PublicBuffer *found=NULL;
            for(PublicBuffer *v=public_buffers;v;v=v->next)
                if(v->hint&&v->size==bytes&&!memcmp(v->host,value,bytes)){found=v;break;}
            if(!found){found=public_new(&frame,(uint32_t)bytes);if(found){found->hint=1;memcpy(found->host,value,bytes);public_publish(&frame,found);}else SDL_OutOfMemory();}
            if(found)result=found->address;
        }
    }
    leave(&frame);free(n);tribes_set_last_error(error);return result;
}

/* Original audio initialization (0x100569f0) uses private native streams.
   SDL device IDs are scalar; stream pointers are represented by owned tokens.
   Native worker threads never see guest sample/specification pointers. */
#define AUDIO_ENTER() event_flags(SDL_INIT_AUDIO);uint32_t error=tribes_get_last_error();NativeFrame frame;if(!enter(cpu,&frame))stop()
#define AUDIO_RETURN(value) do{uint32_t audio_result=(uint32_t)(value);leave(&frame);tribes_set_last_error(error);return audio_result;}while(0)
#define AUDIO_DONE() do{leave(&frame);tribes_set_last_error(error);return;}while(0)
static AudioStream **audio_stream_slot(uint32_t token){
    AudioStream **slot=&audio_streams;while(*slot&&(*slot)->token!=token)slot=&(*slot)->next;
    if(!*slot)stop();return slot;
}
static SDL_AudioStream *native_audio_stream(uint32_t token){return token?(*audio_stream_slot(token))->native:NULL;}
static AudioDevice **audio_device_slot(uint32_t id){
    AudioDevice **slot=&audio_devices;while(*slot&&(*slot)->native!=id)slot=&(*slot)->next;
    if(!*slot)stop();return slot;
}
static void clear_audio_records(void){
    /* SDL_QuitAudio has already destroyed every stream and logical device. */
    while(audio_streams){AudioStream *v=audio_streams;audio_streams=v->next;free(v);}
    while(audio_devices){AudioDevice *v=audio_devices;audio_devices=v->next;free(v);}
}
uint32_t tribes_sdl_open_audio_device(_cpu *cpu,uint32_t id,uint32_t specification){
    AUDIO_ENTER();SDL_AudioSpec spec;SDL_AudioSpec *requested=NULL;
    if(specification){checked_copy(&spec,guest(cpu,specification,sizeof spec),sizeof spec);requested=&spec;}
    AudioDevice *record=calloc(1,sizeof *record);if(!record){SDL_OutOfMemory();AUDIO_RETURN(0);}
    SDL_AudioDeviceID result=SDL_OpenAudioDevice(id,requested);
    if(result){record->native=result;record->next=audio_devices;audio_devices=record;}else free(record);
    AUDIO_RETURN(result);
}
uint32_t tribes_sdl_get_audio_device_format(_cpu *cpu,uint32_t id,uint32_t specification,uint32_t sample_frames){
    AUDIO_ENTER();SDL_AudioSpec spec;int frames;
    bool result=SDL_GetAudioDeviceFormat(id,specification?&spec:NULL,sample_frames?&frames:NULL);
    if(result){checked_copy(guest(cpu,specification,sizeof spec),&spec,sizeof spec);if(sample_frames)checked_copy(guest(cpu,sample_frames,sizeof frames),&frames,sizeof frames);}
    AUDIO_RETURN(result);
}
uint32_t tribes_sdl_pause_audio_device(_cpu *cpu,uint32_t id){
    AUDIO_ENTER();if(id)(void)audio_device_slot(id);bool result=SDL_PauseAudioDevice(id);AUDIO_RETURN(result);
}
void tribes_sdl_close_audio_device(_cpu *cpu,uint32_t id){
    AUDIO_ENTER();if(id){AudioDevice **slot=audio_device_slot(id);AudioDevice *v=*slot;SDL_CloseAudioDevice(id);*slot=v->next;free(v);}else SDL_CloseAudioDevice(0);AUDIO_DONE();
}
uint32_t tribes_sdl_create_audio_stream(_cpu *cpu,uint32_t source,uint32_t destination){
    AUDIO_ENTER();SDL_AudioSpec src,dst;
    if(source)checked_copy(&src,guest(cpu,source,sizeof src),sizeof src);
    if(destination)checked_copy(&dst,guest(cpu,destination,sizeof dst),sizeof dst);
    AudioStream *record=calloc(1,sizeof *record);if(!record){SDL_OutOfMemory();AUDIO_RETURN(0);}
    record->native=SDL_CreateAudioStream(source?&src:NULL,destination?&dst:NULL);
    uint32_t result=0;
    if(record->native){if(next_audio_stream_token>=0xd8000000)stop();result=record->token=next_audio_stream_token++;record->next=audio_streams;audio_streams=record;
#ifdef TRIBESPAD_SDL_TEST_OBSERVER
        extern void sdl_test_audio_stream_observe(SDL_AudioStream *);sdl_test_audio_stream_observe(record->native);
#endif
    }else free(record);
    AUDIO_RETURN(result);
}
uint32_t tribes_sdl_bind_audio_stream(_cpu *cpu,uint32_t id,uint32_t token){
    AUDIO_ENTER();if(id)(void)audio_device_slot(id);bool result=SDL_BindAudioStream(id,native_audio_stream(token));AUDIO_RETURN(result);
}
void tribes_sdl_destroy_audio_stream(_cpu *cpu,uint32_t token){
    AUDIO_ENTER();if(token){AudioStream **slot=audio_stream_slot(token);AudioStream *v=*slot;SDL_DestroyAudioStream(v->native);*slot=v->next;free(v);}else SDL_DestroyAudioStream(NULL);AUDIO_DONE();
}
uint32_t tribes_sdl_resume_audio_stream_device(_cpu *cpu,uint32_t token){
    AUDIO_ENTER();bool result=SDL_ResumeAudioStreamDevice(native_audio_stream(token));AUDIO_RETURN(result);
}
uint32_t tribes_sdl_pause_audio_stream_device(_cpu *cpu,uint32_t token){
    AUDIO_ENTER();bool result=SDL_PauseAudioStreamDevice(native_audio_stream(token));AUDIO_RETURN(result);
}
uint32_t tribes_sdl_clear_audio_stream(_cpu *cpu,uint32_t token){
    AUDIO_ENTER();bool result=SDL_ClearAudioStream(native_audio_stream(token));AUDIO_RETURN(result);
}
uint32_t tribes_sdl_get_audio_stream_available(_cpu *cpu,uint32_t token){
    AUDIO_ENTER();int result=SDL_GetAudioStreamAvailable(native_audio_stream(token));AUDIO_RETURN(result);
}
uint32_t tribes_sdl_get_audio_stream_data(_cpu *cpu,uint32_t token,uint32_t output,uint32_t length){
    AUDIO_ENTER();SDL_AudioStream *stream=native_audio_stream(token);int size=(int32_t)length;
    /* Preserve native NULL/negative/zero-length behavior without accessing any
       guest bytes. Otherwise publish only the actual converted byte count. */
    if(!output||size<=0){unsigned char unused;int result=SDL_GetAudioStreamData(stream,output?&unused:NULL,size);AUDIO_RETURN(result);}
    void *data=malloc((size_t)size);if(!data){SDL_OutOfMemory();AUDIO_RETURN(-1);}
    int result=SDL_GetAudioStreamData(stream,data,size);if(result>size)stop();
    if(result>0)checked_copy(guest(cpu,output,(size_t)result),data,(size_t)result);
    free(data);AUDIO_RETURN(result);
}
#undef AUDIO_ENTER
#undef AUDIO_RETURN
#undef AUDIO_DONE

/* Guest window/context tokens own native objects; addresses never cross the
   i686 ABI. Calls stay on the established SDL application/guest owner thread. */
static Window **window_slot(uint32_t token){Window **p=&windows;while(*p&&(*p)->token!=token)p=&(*p)->next;if(!*p)stop();return p;}
static GLContext **context_slot(uint32_t token){GLContext **p=&contexts;while(*p&&(*p)->token!=token)p=&(*p)->next;if(!*p)stop();return p;}
static SDL_Window *native_window(uint32_t token){return token?(*window_slot(token))->native:NULL;}
static SDL_GLContext native_context(uint32_t token){return token?(*context_slot(token))->native:NULL;}
static GLContext *current_context(void){
    SDL_GLContext native=SDL_GL_GetCurrentContext();
    for(GLContext *c=contexts;c;c=c->next)if(c->native==native)return c;return NULL;
}
static void free_context(GLContext *c){
    if(c->textures&&!--c->textures->refs){
        while(c->textures->automatic){AutoMipmap *m=c->textures->automatic;c->textures->automatic=m->next;free(m);}
        while(c->textures->syncs){SyncObject *s=c->textures->syncs;c->textures->syncs=s->next;free(s);}
        free(c->textures);
    }free(c);
}
static AutoMipmap **automatic_slot(GLContext *c,GLuint name){
    AutoMipmap **p=&c->textures->automatic;while(*p&&(*p)->name!=name)p=&(*p)->next;return p;
}
static void clear_graphics(void){
    clear_surfaces_and_cursors();
    while(properties){Properties *p=properties;SDL_DestroyProperties(p->native);properties=p->next;free(p);}
    while(contexts){GLContext *c=contexts;if(!SDL_GL_DestroyContext(c->native))stop();contexts=c->next;free_context(c);}
    while(windows){Window *w=windows;tribes_window_remove(base,w->token+0x03000000u);SDL_DestroyWindow(w->native);windows=w->next;free(w);}
}
#define WINDOW_ENTER() event_flags(SDL_INIT_VIDEO);if(graphics_active)stop();graphics_active=1;uint32_t error=tribes_get_last_error();NativeFrame frame;if(!enter(cpu,&frame))stop()
#define WINDOW_RETURN(value) do{uint32_t result_=(value);graphics_active=0;leave(&frame);tribes_set_last_error(error);return result_;}while(0)
/* Only packed 32-bit cursor formats are currently reached. Host SDL never
   retains a guest pointer: surfaces own a private pixel copy, while public
   i686 headers and owned pixel buffers use the registered guest allocator. */
_Static_assert(sizeof(TribesSDLSurface)==32&&offsetof(TribesSDLSurface,pixels)==20,"SDL i686 surface ABI");
static void surface_format(uint32_t format){
    if(format!=SDL_PIXELFORMAT_ARGB8888&&format!=SDL_PIXELFORMAT_ABGR8888&&
       format!=SDL_PIXELFORMAT_RGBA8888&&format!=SDL_PIXELFORMAT_BGRA8888)stop();
}
static Surface **surface_slot(uint32_t address){
    Surface **p=&surfaces;while(*p&&(*p)->header->address!=address)p=&(*p)->next;if(!*p)stop();return p;
}
static Cursor **cursor_slot(uint32_t token){
    Cursor **p=&cursors;while(*p&&(*p)->token!=token)p=&(*p)->next;if(!*p)stop();return p;
}
static SDL_Surface *sync_surface(_cpu *cpu,uint32_t address){
    if(!address)return NULL;Surface *s=*surface_slot(address);
    checked_copy(s->native->pixels,guest(cpu,s->pixels,s->bytes),s->bytes);return s->native;
}
static void free_surface(Surface *s){
    SDL_DestroySurface(s->native);free(s->shadow);
    if(s->owned_pixels)public_free(active_frame,s->owned_pixels->host);
    public_free(active_frame,s->header->host);free(s);
}
static void clear_surfaces_and_cursors(void){
    while(cursors){Cursor *c=cursors;cursors=c->next;SDL_DestroyCursor(c->native);free(c);}
    while(surfaces){Surface *s=surfaces;surfaces=s->next;free_surface(s);}
}
static uint32_t publish_surface(SDL_Surface *native,uint32_t pixels,void *shadow){
    if(!native){free(shadow);return 0;}
    Surface *s=calloc(1,sizeof *s);if(!s){SDL_DestroySurface(native);free(shadow);SDL_OutOfMemory();return 0;}
    s->native=native;s->shadow=shadow;
    uint64_t bytes=(uint64_t)native->pitch*native->h;if(!bytes||bytes>16777216)stop();s->bytes=(uint32_t)bytes;
    if(!pixels){
        s->owned_pixels=public_new(active_frame,s->bytes);
        if(!s->owned_pixels)goto oom;
        s->owned_pixels->mutable_pixels=1;pixels=s->owned_pixels->address;
        checked_copy(guest(active_frame->cpu,pixels,s->bytes),native->pixels,s->bytes);
    }
    s->pixels=pixels;s->header=public_new(active_frame,sizeof(TribesSDLSurface));if(!s->header)goto oom;
    s->header->surface_header=1;
    *(TribesSDLSurface *)s->header->host=(TribesSDLSurface){native->flags&~SDL_SURFACE_SIMD_ALIGNED,native->format,native->w,native->h,native->pitch,pixels,1,0};
    public_publish(active_frame,s->header);s->next=surfaces;surfaces=s;return s->header->address;
oom:
    if(s->owned_pixels)public_free(active_frame,s->owned_pixels->host);
    SDL_DestroySurface(native);free(shadow);free(s);SDL_OutOfMemory();return 0;
}
uint32_t tribes_sdl_create_surface_from(_cpu *cpu,uint32_t width,uint32_t height,uint32_t format,uint32_t pixels,uint32_t pitch){
    WINDOW_ENTER();surface_format(format);
    if((int32_t)width<=0||(int32_t)height<=0||(int32_t)pitch<=0||(uint64_t)width*4>pitch||!pixels){SDL_SetError("Invalid cursor surface dimensions or pixels");WINDOW_RETURN(0);}
    uint64_t bytes=(uint64_t)pitch*height;if(bytes>16777216)stop();
    void *shadow=malloc((size_t)bytes);if(!shadow){SDL_OutOfMemory();WINDOW_RETURN(0);}
    checked_copy(shadow,guest(cpu,pixels,(size_t)bytes),(size_t)bytes);
    SDL_Surface *native=SDL_CreateSurfaceFrom((int)width,(int)height,format,shadow,(int)pitch);
    WINDOW_RETURN(publish_surface(native,pixels,shadow));
}
uint32_t tribes_sdl_convert_surface(_cpu *cpu,uint32_t address,uint32_t format){
    WINDOW_ENTER();surface_format(format);SDL_Surface *native=SDL_ConvertSurface(sync_surface(cpu,address),format);
    WINDOW_RETURN(publish_surface(native,0,NULL));
}
uint32_t tribes_sdl_premultiply_surface_alpha(_cpu *cpu,uint32_t address,uint32_t linear){
    WINDOW_ENTER();uint32_t result=SDL_PremultiplySurfaceAlpha(sync_surface(cpu,address),(linear&0xffu)!=0);
    if(result){Surface *s=*surface_slot(address);checked_copy(guest(cpu,s->pixels,s->bytes),s->native->pixels,s->bytes);}
    WINDOW_RETURN(result);
}
void tribes_sdl_remove_surface_alternate_images(_cpu *cpu,uint32_t address){
    WINDOW_ENTER();
    /* Alternate images belong to the private native surface, not guest pixels. */
    SDL_RemoveSurfaceAlternateImages(address?(*surface_slot(address))->native:NULL);
    graphics_active=0;leave(&frame);tribes_set_last_error(error);
}
void tribes_sdl_destroy_surface(_cpu *cpu,uint32_t address){
    WINDOW_ENTER();if(address){Surface **slot=surface_slot(address);Surface *s=*slot;
        TribesSDLSurface *header=s->header->host;
        if(--header->refcount)public_publish(&frame,s->header);else {*slot=s->next;free_surface(s);}
    }graphics_active=0;leave(&frame);tribes_set_last_error(error);
}
uint32_t tribes_sdl_create_color_cursor(_cpu *cpu,uint32_t address,uint32_t x,uint32_t y){
    WINDOW_ENTER();Cursor *c=calloc(1,sizeof *c);if(!c){SDL_OutOfMemory();WINDOW_RETURN(0);}
    c->native=SDL_CreateColorCursor(sync_surface(cpu,address),(int32_t)x,(int32_t)y);
    if(!c->native){free(c);WINDOW_RETURN(0);}
    if(next_cursor_token>=0xdc100000)stop();c->token=next_cursor_token++;c->next=cursors;cursors=c;WINDOW_RETURN(c->token);
}
uint32_t tribes_sdl_set_cursor(_cpu *cpu,uint32_t token){
    WINDOW_ENTER();WINDOW_RETURN(SDL_SetCursor(token?(*cursor_slot(token))->native:NULL));
}
void tribes_sdl_destroy_cursor(_cpu *cpu,uint32_t token){
    WINDOW_ENTER();if(token){Cursor **slot=cursor_slot(token);Cursor *c=*slot;*slot=c->next;SDL_DestroyCursor(c->native);free(c);}
    graphics_active=0;leave(&frame);tribes_set_last_error(error);
}
static Properties **properties_slot(uint32_t id){Properties **p=&properties;while(*p&&(*p)->native!=id)p=&(*p)->next;if(!*p)stop();return p;}
uint32_t tribes_sdl_create_properties(_cpu *cpu){
    WINDOW_ENTER();Properties *p=calloc(1,sizeof *p);if(!p){SDL_OutOfMemory();WINDOW_RETURN(0);}
    p->native=SDL_CreateProperties();if(!p->native){free(p);WINDOW_RETURN(0);}
    p->next=properties;properties=p;WINDOW_RETURN(p->native);
}
void tribes_sdl_destroy_properties(_cpu *cpu,uint32_t id){
    WINDOW_ENTER();Properties **slot=id?properties_slot(id):NULL;SDL_DestroyProperties(id);
    if(slot){Properties *p=*slot;*slot=p->next;free(p);}graphics_active=0;leave(&frame);tribes_set_last_error(error);
}
static int creation_property(const char *name,unsigned kind){
    static const char *const booleans[]={SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN,SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN,SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN,SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN,SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN};
    static const char *const numbers[]={SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,SDL_PROP_WINDOW_CREATE_X_NUMBER,SDL_PROP_WINDOW_CREATE_Y_NUMBER};
    if(!name)return 0;if(kind==0)return !strcmp(name,SDL_PROP_WINDOW_CREATE_TITLE_STRING);
    if(kind==1){for(unsigned i=0;i<sizeof booleans/sizeof *booleans;i++)if(!strcmp(name,booleans[i]))return 1;}
    if(kind==2){for(unsigned i=0;i<sizeof numbers/sizeof *numbers;i++)if(!strcmp(name,numbers[i]))return 1;}
    return 0;
}
uint32_t tribes_sdl_set_string_property(_cpu *cpu,uint32_t id,uint32_t name,uint32_t value){
    WINDOW_ENTER();(void)properties_slot(id);char *n=string(cpu,name),*v=string(cpu,value);
    if(!creation_property(n,0)){free(n);free(v);stop();}
    if(value&&!v){free(n);SDL_OutOfMemory();WINDOW_RETURN(0);}
    uint32_t result=SDL_SetStringProperty(id,n,v);free(n);free(v);WINDOW_RETURN(result);
}
uint32_t tribes_sdl_set_boolean_property(_cpu *cpu,uint32_t id,uint32_t name,uint32_t value){
    WINDOW_ENTER();(void)properties_slot(id);char *n=string(cpu,name);if(!creation_property(n,1)){free(n);stop();}
    uint32_t result=SDL_SetBooleanProperty(id,n,(value&0xffu)!=0);free(n);WINDOW_RETURN(result);
}
uint32_t tribes_sdl_set_number_property(_cpu *cpu,uint32_t id,uint32_t name,uint32_t low,uint32_t high){
    WINDOW_ENTER();(void)properties_slot(id);char *n=string(cpu,name);if(!creation_property(n,2)){free(n);stop();}
    uint64_t bits=((uint64_t)high<<32)|low;Sint64 value;memcpy(&value,&bits,8);
    uint32_t result=SDL_SetNumberProperty(id,n,value);free(n);WINDOW_RETURN(result);
}
uint32_t tribes_sdl_create_window_properties(_cpu *cpu,uint32_t id){
    WINDOW_ENTER();(void)properties_slot(id);Window *w=calloc(1,sizeof *w);if(!w){SDL_OutOfMemory();WINDOW_RETURN(0);}
    w->native=SDL_CreateWindowWithProperties(id);if(!w->native){free(w);WINDOW_RETURN(0);}
    if(next_window_token>=0xda100000)stop();w->token=next_window_token++;if(!tribes_window_add(base,w->token+0x03000000u,0,window_style(w->native)))stop();w->next=windows;windows=w;WINDOW_RETURN(w->token);
}
uint32_t tribes_sdl_create_window(_cpu *cpu,uint32_t title,uint32_t width,uint32_t height,uint32_t low,uint32_t high){
    WINDOW_ENTER();char *name=string(cpu,title);if(title&&!name){SDL_OutOfMemory();WINDOW_RETURN(0);}
    Window *w=calloc(1,sizeof *w);if(!w){free(name);SDL_OutOfMemory();WINDOW_RETURN(0);}
    w->native=SDL_CreateWindow(name,(int32_t)width,(int32_t)height,((uint64_t)high<<32)|low);free(name);
    if(!w->native){free(w);WINDOW_RETURN(0);}
    if(next_window_token>=0xda100000)stop();w->token=next_window_token++;if(!tribes_window_add(base,w->token+0x03000000u,0,window_style(w->native)))stop();w->next=windows;windows=w;WINDOW_RETURN(w->token);
}
void tribes_sdl_destroy_window(_cpu *cpu,uint32_t token){
    WINDOW_ENTER();Window **p=token?window_slot(token):NULL;SDL_DestroyWindow(p?(*p)->native:NULL);
    if(p){Window *w=*p;tribes_window_remove(base,w->token+0x03000000u);*p=w->next;free(w);}graphics_active=0;leave(&frame);tribes_set_last_error(error);
}
uint32_t tribes_sdl_show_window(_cpu *cpu,uint32_t token){WINDOW_ENTER();WINDOW_RETURN(SDL_ShowWindow(native_window(token)));}
/* These i686 float returns belong in ST0; the cdecl wrapper preserves EAX. */
extern void x87_fld_float(_cpu *,uint32_t);
static void return_float(_cpu *cpu,float value){uint32_t bits;memcpy(&bits,&value,4);x87_fld_float(cpu,bits);}
void tribes_sdl_get_window_display_scale(_cpu *cpu,uint32_t token){
    WINDOW_ENTER();float value=SDL_GetWindowDisplayScale(native_window(token));
    graphics_active=0;leave(&frame);tribes_set_last_error(error);return_float(cpu,value);
}
void tribes_sdl_ceilf(_cpu *cpu,uint32_t bits){
    uint32_t error=tribes_get_last_error();float value;memcpy(&value,&bits,4);
    value=SDL_ceilf(value);tribes_set_last_error(error);return_float(cpu,value);
}
uint32_t tribes_sdl_get_display_for_window(_cpu *cpu,uint32_t token){WINDOW_ENTER();WINDOW_RETURN(SDL_GetDisplayForWindow(native_window(token)));}
uint32_t tribes_sdl_get_closest_fullscreen_mode(_cpu *cpu,uint32_t display,uint32_t width,uint32_t height,uint32_t rate_bits,uint32_t high_density,uint32_t output){
    WINDOW_ENTER();float rate;memcpy(&rate,&rate_bits,4);SDL_DisplayMode native={0};TribesSDLDisplayMode copy={0};
    uint32_t result=SDL_GetClosestFullscreenDisplayMode(display,(int32_t)width,(int32_t)height,rate,(high_density&0xffu)!=0,output?&native:NULL);
    if(result){
        ClosestMode *m=closest_modes;while(m&&memcmp(&m->native,&native,sizeof native))m=m->next;
        if(!m){m=calloc(1,sizeof *m);if(!m){SDL_OutOfMemory();result=0;}
            else {if(!next_mode_token)stop();m->native=native;m->public=(TribesSDLDisplayMode){native.displayID,native.format,native.w,native.h,native.pixel_density,native.refresh_rate,native.refresh_rate_numerator,native.refresh_rate_denominator,native.internal?next_mode_token++:0};m->next=closest_modes;closest_modes=m;}}
        if(m)copy=m->public;
    }
    if(output)checked_copy(guest(cpu,output,sizeof copy),&copy,sizeof copy);WINDOW_RETURN(result);
}
uint32_t tribes_sdl_sync_window(_cpu *cpu,uint32_t token){WINDOW_ENTER();WINDOW_RETURN(SDL_SyncWindow(native_window(token)));}
uint32_t tribes_sdl_set_window_fullscreen(_cpu *cpu,uint32_t token,uint32_t value){WINDOW_ENTER();WINDOW_RETURN(SDL_SetWindowFullscreen(native_window(token),(value&0xffu)!=0));}
uint32_t tribes_sdl_set_window_size(_cpu *cpu,uint32_t token,uint32_t width,uint32_t height){WINDOW_ENTER();WINDOW_RETURN(SDL_SetWindowSize(native_window(token),(int32_t)width,(int32_t)height));}
uint32_t tribes_sdl_set_window_bordered(_cpu *cpu,uint32_t token,uint32_t value){WINDOW_ENTER();WINDOW_RETURN(SDL_SetWindowBordered(native_window(token),(value&0xffu)!=0));}
uint32_t tribes_sdl_set_window_relative_mouse_mode(_cpu *cpu,uint32_t token,uint32_t value){WINDOW_ENTER();WINDOW_RETURN(SDL_SetWindowRelativeMouseMode(native_window(token),(value&0xffu)!=0));}
uint32_t tribes_sdl_set_window_position(_cpu *cpu,uint32_t token,uint32_t x,uint32_t y){WINDOW_ENTER();WINDOW_RETURN(SDL_SetWindowPosition(native_window(token),(int32_t)x,(int32_t)y));}
uint32_t tribes_sdl_set_window_fullscreen_mode(_cpu *cpu,uint32_t token,uint32_t mode){
    WINDOW_ENTER();const SDL_DisplayMode *native=NULL;
    /* enter validated every borrowed guest copy against its private mirror.
       Closest-mode results are caller-owned and may be copied, but their
       complete scalar record must match a private native snapshot. */
    if(mode){
        for(DisplayMode *m=display_modes;m;m=m->next)if(public_find(m->public)->address==mode){native=&m->native;break;}
        if(!native){TribesSDLDisplayMode copy;checked_copy(&copy,guest(cpu,mode,sizeof copy),sizeof copy);
            for(ClosestMode *m=closest_modes;m;m=m->next)if(!memcmp(&copy,&m->public,sizeof copy)){native=&m->native;break;}}
        if(!native)stop();
    }
    WINDOW_RETURN(SDL_SetWindowFullscreenMode(native_window(token),native));
}
/* WGL interval belongs to the drawable, while Cocoa SDL stores it on the
   native context. Restore the drawable setting whenever a context is bound.
   The original Windows context default is one frame, not Cocoa's zero. */
static Window *current_window(void){
    SDL_Window *native=SDL_GL_GetCurrentWindow();if(!native)return NULL;
    for(Window *w=windows;w;w=w->next)if(w->native==native)return w;
    stop();
}
static int set_swap_interval(int interval){
    if(!SDL_GL_SetSwapInterval(interval))return 0;
    Window *w=current_window();if(!w||!SDL_GL_GetCurrentContext())stop();
    int actual=0;if(!SDL_GL_GetSwapInterval(&actual))return 0;
    w->swap_interval=actual;w->swap_set=1;return 1;
}
static int restore_swap_interval(void){
    Window *w=current_window();if(!w)return 0;
    return set_swap_interval(w->swap_set?w->swap_interval:1);
}
uint32_t tribes_sdl_gl_set_swap_interval(_cpu *cpu,uint32_t interval){WINDOW_ENTER();WINDOW_RETURN(set_swap_interval((int32_t)interval));}
uint32_t tribes_wgl_swap_interval(_cpu *cpu,uint32_t interval){
    WINDOW_ENTER();
    if((int32_t)interval<0){error=13;WINDOW_RETURN(0);}
    if(!SDL_GL_GetCurrentContext()||!current_window()){error=1425;WINDOW_RETURN(0);}
    uint32_t result=set_swap_interval((int32_t)interval);if(!result)error=50;
    WINDOW_RETURN(result);
}
uint32_t tribes_wgl_get_swap_interval(_cpu *cpu){
    WINDOW_ENTER();int interval=0;
    if(!SDL_GL_GetCurrentContext()||!current_window()){error=1425;WINDOW_RETURN(0);}
    if(!SDL_GL_GetSwapInterval(&interval)){error=50;WINDOW_RETURN(0);}
    WINDOW_RETURN((uint32_t)interval);
}
uint32_t tribes_sdl_get_window_id(_cpu *cpu,uint32_t token){WINDOW_ENTER();WINDOW_RETURN(SDL_GetWindowID(native_window(token)));}
uint32_t tribes_sdl_get_window_properties(_cpu *cpu,uint32_t token){WINDOW_ENTER();WINDOW_RETURN(SDL_GetWindowProperties(native_window(token)));}
static uint32_t window_size(_cpu *cpu,uint32_t token,uint32_t width,uint32_t height,int pixels){
    WINDOW_ENTER();int w=0,h=0;SDL_Window *window=native_window(token);
    uint32_t result=pixels?SDL_GetWindowSizeInPixels(window,width?&w:NULL,height?&h:NULL):SDL_GetWindowSize(window,width?&w:NULL,height?&h:NULL);
    if(width)memcpy(guest(cpu,width,4),&w,4);if(height)memcpy(guest(cpu,height,4),&h,4);WINDOW_RETURN(result);
}
uint32_t tribes_sdl_get_window_size(_cpu *cpu,uint32_t token,uint32_t width,uint32_t height){return window_size(cpu,token,width,height,0);}
uint32_t tribes_sdl_get_window_size_pixels(_cpu *cpu,uint32_t token,uint32_t width,uint32_t height){return window_size(cpu,token,width,height,1);}
uint32_t tribes_sdl_gl_create_context(_cpu *cpu,uint32_t token){
    WINDOW_ENTER();GLContext *c=calloc(1,sizeof *c);if(!c){SDL_OutOfMemory();WINDOW_RETURN(0);}
    GLContext *shared=current_context();int share=0;
    if(!SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,&share)){free(c);WINDOW_RETURN(0);}
    if(share&&!shared)stop();
    SDL_Window *window=native_window(token);c->native=SDL_GL_CreateContext(window);
#if !TARGET_OS_IPHONE
    /* The original UI uses fixed-function matrices. Cocoa cannot create the
       requested 3.2 compatibility profile; use its real 2.1 compatibility
       context, with the reached copy-buffer targets adapted below. */
    if(!c->native&&!strcmp(SDL_GetError(),"Failed creating OpenGL context at version requested")){
        int major=0,minor=0,profile=0,flags=0;
        if(SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,&major)&&SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,&minor)&&
           SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,&profile)&&SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS,&flags)&&
           major==3&&minor==2&&profile==SDL_GL_CONTEXT_PROFILE_COMPATIBILITY){
            if(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2)&&SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1)&&
               SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,0)&&SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,0))c->native=SDL_GL_CreateContext(window);
            if(!c->native){
                (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,major);(void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,minor);
                (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,profile);(void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,flags);
            }
#ifdef TB_CAPTURE_PATCHES
            fprintf(stderr,"Observed Cocoa GL compatibility retry requested=3.2 negotiated=2.1 result=%u\n",c->native!=NULL);
#endif
        }
    }
#endif
    if(!c->native){free(c);WINDOW_RETURN(0);}
    if(!restore_swap_interval()){SDL_GL_DestroyContext(c->native);free(c);WINDOW_RETURN(0);}
    c->mipmap_hint=GL_DONT_CARE;
    if(share){c->textures=shared->textures;c->textures->refs++;}
    else {c->textures=calloc(1,sizeof *c->textures);if(!c->textures)stop();c->textures->refs=1;}
#if !TARGET_OS_IPHONE
    int profile=0;if(SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,&profile))c->core=profile==SDL_GL_CONTEXT_PROFILE_CORE;
#endif
    if(next_context_token>=0xdb100000)stop();c->token=next_context_token++;c->next=contexts;contexts=c;WINDOW_RETURN(c->token);
}
uint32_t tribes_sdl_gl_destroy_context(_cpu *cpu,uint32_t token){
    WINDOW_ENTER();GLContext **p=token?context_slot(token):NULL;uint32_t result=SDL_GL_DestroyContext(p?(*p)->native:NULL);
    if(p&&result){GLContext *c=*p;*p=c->next;free_context(c);}WINDOW_RETURN(result);
}
uint32_t tribes_sdl_gl_make_current(_cpu *cpu,uint32_t window,uint32_t context){
    WINDOW_ENTER();uint32_t result=SDL_GL_MakeCurrent(native_window(window),native_context(context));
    if(result&&context)result=restore_swap_interval();WINDOW_RETURN(result);
}
uint32_t tribes_sdl_gl_get_current_window(_cpu *cpu){
    WINDOW_ENTER();SDL_Window *native=SDL_GL_GetCurrentWindow();if(!native)WINDOW_RETURN(0);
    for(Window *w=windows;w;w=w->next)if(w->native==native)WINDOW_RETURN(w->token);stop();
}
uint32_t tribes_sdl_gl_get_current_context(_cpu *cpu){
    WINDOW_ENTER();SDL_GLContext native=SDL_GL_GetCurrentContext();if(!native)WINDOW_RETURN(0);
    for(GLContext *c=contexts;c;c=c->next)if(c->native==native)WINDOW_RETURN(c->token);stop();
}
uint32_t tribes_sdl_gl_swap_window(_cpu *cpu,uint32_t token){WINDOW_ENTER();WINDOW_RETURN(SDL_GL_SwapWindow(native_window(token)));}
uint32_t tribes_sdl_gl_set_attribute(_cpu *cpu,uint32_t attribute,uint32_t value){WINDOW_ENTER();WINDOW_RETURN(SDL_GL_SetAttribute((SDL_GLAttr)attribute,(int32_t)value));}
uint32_t tribes_sdl_gl_get_attribute(_cpu *cpu,uint32_t attribute,uint32_t output){
    WINDOW_ENTER();int value=0;uint32_t result=SDL_GL_GetAttribute((SDL_GLAttr)attribute,output?&value:NULL);
    if(output)memcpy(guest(cpu,output,4),&value,4);WINDOW_RETURN(result);
}
void tribes_sdl_gl_reset_attributes(_cpu *cpu){WINDOW_ENTER();SDL_GL_ResetAttributes();graphics_active=0;leave(&frame);tribes_set_last_error(error);}
/* Graphics strings are borrowed immutable guest copies until SDL_Quit. They
   share the existing public-buffer validation and guest allocation lifecycle. */
static uint32_t graphics_string(NativeFrame *frame,const char *text){
    if(!text)return 0;size_t n=strnlen(text,1048576);if(n==1048576)stop();n++;
    for(PublicBuffer *b=public_buffers;b;b=b->next)if(b->hint&&b->size==n&&!memcmp(b->host,text,n))return b->address;
    PublicBuffer *b=public_new(frame,(uint32_t)n);if(!b){SDL_OutOfMemory();return 0;}
    b->hint=1;memcpy(b->host,text,n);public_publish(frame,b);return b->address;
}
/* Reset can call these while no context is current. Delegate to the native
   driver rather than inventing a context or
   changing that lifecycle. Calls with a live context perform real drawing. */
#ifdef TB_CAPTURE_PATCHES
#define DRAW_OBSERVE() do{static unsigned observations;if(observations++<4)fprintf(stderr,"Observed GL draw %s context=%u\n",__func__,SDL_GL_GetCurrentContext()!=NULL);}while(0)
#else
#define DRAW_OBSERVE() ((void)0)
#endif
#define DRAW_RETURN() do{DRAW_OBSERVE();graphics_active=0;leave(&frame);tribes_set_last_error(error);return;}while(0)
void tribes_gl_clear_color(_cpu *cpu,uint32_t r,uint32_t g,uint32_t b,uint32_t a){
    WINDOW_ENTER();GLfloat v[4];uint32_t bits[]={r,g,b,a};memcpy(v,bits,sizeof v);
    glClearColor(v[0],v[1],v[2],v[3]);DRAW_RETURN();
}
void tribes_gl_bind_framebuffer(_cpu *cpu,uint32_t target,uint32_t framebuffer){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    /* UIKit's real window drawable is a native FBO, not framebuffer zero.
       Bind guest default zero to SDL's published drawable. User-created FBO
       name conversion is a separate, currently unsupported object API. */
    if(framebuffer)stop();SDL_Window *w=SDL_GL_GetCurrentWindow();
    if(w&&SDL_GL_GetCurrentContext()){
        Sint64 drawable=SDL_GetNumberProperty(SDL_GetWindowProperties(w),SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER,0);
        if(drawable<=0||drawable>UINT32_MAX)stop();framebuffer=(uint32_t)drawable;
    }
#endif
    glBindFramebuffer(target,framebuffer);DRAW_RETURN();
}
void tribes_gl_clear(_cpu *cpu,uint32_t mask){WINDOW_ENTER();glClear(mask);DRAW_RETURN();}
void tribes_gl_draw_buffer(_cpu *cpu,uint32_t buffer){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)buffer;(void)error;stop(); /* Deliberately absent from the mobile export catalog. */
#else
    glDrawBuffer(buffer);DRAW_RETURN();
#endif
}
void tribes_gl_pixel_store(_cpu *cpu,uint32_t parameter,uint32_t value){WINDOW_ENTER();glPixelStorei(parameter,(int32_t)value);DRAW_RETURN();}
/* Sync objects share the native context group. Only typed, monotonically
   allocated tokens cross the guest ABI; never truncate or forward guest pointers. */
#if !TARGET_OS_IPHONE
static SyncObject **sync_slot(GLContext *c,uint32_t token){
    SyncObject **p=&c->textures->syncs;while(*p&&(*p)->token!=token)p=&(*p)->next;return p;
}
#endif
uint32_t tribes_gl_fence_sync(_cpu *cpu,uint32_t condition,uint32_t flags){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)condition;(void)flags;(void)error;stop();
#else
    GLContext *c=current_context();if(!c)stop();GLsync native=glFenceSync(condition,flags);
    if(!native)WINDOW_RETURN(0);
    SyncObject *s=calloc(1,sizeof *s);if(!s||next_sync_token>=0xdd100000)stop();
    s->native=native;s->token=next_sync_token++;s->next=c->textures->syncs;c->textures->syncs=s;
    DRAW_OBSERVE();WINDOW_RETURN(s->token);
#endif
}
uint32_t tribes_gl_is_sync(_cpu *cpu,uint32_t token){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)token;(void)error;stop();
#else
    GLContext *c=current_context();if(!c)stop();SyncObject *s=*sync_slot(c,token);
    /* Unknown/stale/foreign names are false, never native pointer casts. */
    WINDOW_RETURN(glIsSync(s?s->native:NULL)==GL_TRUE);
#endif
}
uint32_t tribes_gl_client_wait_sync(_cpu *cpu,uint32_t token,uint32_t flags,uint32_t low,uint32_t high){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)token;(void)flags;(void)low;(void)high;(void)error;stop();
#else
    GLContext *c=current_context();if(!c)stop();SyncObject *s=*sync_slot(c,token);
    if(!s){c->errors|=1u<<(GL_INVALID_VALUE-0x500);WINDOW_RETURN(GL_WAIT_FAILED);}
    GLenum result=glClientWaitSync(s->native,flags,((uint64_t)high<<32)|low);
    DRAW_OBSERVE();WINDOW_RETURN(result);
#endif
}
void tribes_gl_get_sync_iv(_cpu *cpu,uint32_t token,uint32_t name,uint32_t capacity,uint32_t length,uint32_t output){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)token;(void)name;(void)capacity;(void)length;(void)output;(void)error;stop();
#else
    GLContext *c=current_context();if(!c)stop();SyncObject *s=*sync_slot(c,token);
    if(!s){c->errors|=1u<<(GL_INVALID_VALUE-0x500);DRAW_RETURN();}
    /* All defined sync properties are scalar. A caller's larger capacity
       neither writes nor requires access to neighboring guest words. */
    GLint value=0;GLsizei count=0;void *dst=NULL,*len=NULL;uint32_t dt=0,lt=0;
    if((int32_t)capacity>0){dst=guest(cpu,output,4);dt=tribes_memory_access_begin(dst,4,3);checked_copy(&value,dst,4);}
    if(length){len=guest(cpu,length,4);lt=tribes_memory_access_begin(len,4,3);checked_copy(&count,len,4);}
    glGetSynciv(s->native,name,(int32_t)capacity,length?&count:NULL,dst?&value:NULL);
    if(len)checked_copy(len,&count,4);if(dst)checked_copy(dst,&value,4);
    if(len)tribes_memory_access_end(lt);if(dst)tribes_memory_access_end(dt);DRAW_RETURN();
#endif
}
void tribes_gl_delete_sync(_cpu *cpu,uint32_t token){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)token;(void)error;stop();
#else
    GLContext *c=current_context();if(!c)stop();
    if(!token){glDeleteSync(NULL);DRAW_RETURN();}
    SyncObject **p=sync_slot(c,token);if(!*p){c->errors|=1u<<(GL_INVALID_VALUE-0x500);DRAW_RETURN();}
    SyncObject *s=*p;glDeleteSync(s->native);*p=s->next;free(s);DRAW_RETURN();
#endif
}
void tribes_gl_flush(_cpu *cpu){WINDOW_ENTER();glFlush();DRAW_RETURN();}
void tribes_gl_finish(_cpu *cpu){WINDOW_ENTER();glFinish();DRAW_RETURN();}

void tribes_gl_active_texture(_cpu *cpu,uint32_t texture){WINDOW_ENTER();glActiveTexture(texture);DRAW_RETURN();}
void tribes_gl_client_active_texture(_cpu *cpu,uint32_t texture){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)texture;(void)error;stop();
#else
    glClientActiveTexture(texture);DRAW_RETURN();
#endif
}
void tribes_gl_multitex_coord2f(_cpu *cpu,uint32_t texture,uint32_t s,uint32_t t){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)texture;(void)s;(void)t;(void)error;stop();
#else
    uint32_t bits[]={s,t};GLfloat v[2];memcpy(v,bits,sizeof v);
    glMultiTexCoord2f(texture,v[0],v[1]);DRAW_RETURN();
#endif
}
void tribes_gl_multitex_coord2fv(_cpu *cpu,uint32_t texture,uint32_t address){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)texture;(void)address;(void)error;stop();
#else
    GLfloat v[2];checked_copy(v,guest(cpu,address,sizeof v),sizeof v);
    glMultiTexCoord2f(texture,v[0],v[1]);DRAW_RETURN();
#endif
}
void tribes_gl_multitex_coord3fv(_cpu *cpu,uint32_t texture,uint32_t address){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)texture;(void)address;(void)error;stop();
#else
    GLfloat v[3];checked_copy(v,guest(cpu,address,sizeof v),sizeof v);
    glMultiTexCoord3f(texture,v[0],v[1],v[2]);DRAW_RETURN();
#endif
}
void tribes_gl_hint(_cpu *cpu,uint32_t target,uint32_t mode){
    WINDOW_ENTER();GLContext *c=current_context();
    /* The removed core hint is advisory. All legal modes use the native
       generator's implementation-defined filter; retain queryable hint state.
       Invalid modes still reach the driver and produce GL_INVALID_ENUM. */
    if(c&&c->core&&target==0x8192&&(mode==GL_DONT_CARE||mode==GL_FASTEST||mode==GL_NICEST))c->mipmap_hint=mode;
    else glHint(target,mode);DRAW_RETURN();
}
void tribes_gl_gen_queries(_cpu *cpu,uint32_t count,uint32_t output){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)count;(void)output;(void)error;stop(); /* Desktop query API is not a GLES2 export. */
#else
    if(!SDL_GL_GetCurrentContext())stop();
    if((int32_t)count<=0){glGenQueriesARB((int32_t)count,NULL);DRAW_RETURN();}
    if(count>UINT32_MAX/sizeof(GLuint))stop();
    size_t bytes=(size_t)count*sizeof(GLuint);void *destination=guest(cpu,output,bytes);
    /* Hold permission validation through native allocation and publication.
       Seed the private output so native failure preserves the caller's bytes. */
    uint32_t token=tribes_memory_access_begin(destination,bytes,3);
    GLuint *names=malloc(bytes);if(!names)stop();checked_copy(names,destination,bytes);
    glGenQueriesARB((int32_t)count,names);checked_copy(destination,names,bytes);
    free(names);tribes_memory_access_end(token);DRAW_RETURN();
#endif
}
/* Query names belong to the native context. Preserve actual GPU completion,
   timing and GL errors; no CPU-clock substitute or fabricated result. */
void tribes_gl_begin_query(_cpu *cpu,uint32_t target,uint32_t name){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)target;(void)name;(void)error;stop();
#else
    glBeginQueryARB(target,name);DRAW_RETURN();
#endif
}
void tribes_gl_end_query(_cpu *cpu,uint32_t target){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)target;(void)error;stop();
#else
    glEndQueryARB(target);DRAW_RETURN();
#endif
}
void tribes_gl_get_query_object_ui64(_cpu *cpu,uint32_t name,uint32_t parameter,uint32_t output){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)name;(void)parameter;(void)output;(void)error;stop();
#else
    if(!SDL_GL_GetCurrentContext())stop();
    GLuint64 value;void *destination=guest(cpu,output,sizeof value);
    uint32_t token=tribes_memory_access_begin(destination,sizeof value,3);
    checked_copy(&value,destination,sizeof value);
    glGetQueryObjectui64v(name,parameter,&value);
    checked_copy(destination,&value,sizeof value);
    tribes_memory_access_end(token);DRAW_RETURN();
#endif
}
/* Buffer storage remains owned by the current native context. Uploads copy
   guest input synchronously; GL never retains a pointer into guest memory. */
void tribes_gl_gen_buffers(_cpu *cpu,uint32_t count,uint32_t output){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();
    if((int32_t)count<=0){glGenBuffers((int32_t)count,NULL);DRAW_RETURN();}
    if(count>UINT32_MAX/sizeof(GLuint))stop();
    size_t bytes=(size_t)count*sizeof(GLuint);void *destination=guest(cpu,output,bytes);
    uint32_t token=tribes_memory_access_begin(destination,bytes,3);
    GLuint *names=malloc(bytes);if(!names)stop();checked_copy(names,destination,bytes);
    glGenBuffers((int32_t)count,names);checked_copy(destination,names,bytes);
    free(names);tribes_memory_access_end(token);DRAW_RETURN();
}
/* COPY_* binding points are independent of drawing state. On the legacy Mac
   context use a temporary ARRAY binding only during the storage operation.
   Native buffers retain all data; the adapter stores only binding names. */
static GLuint *copy_buffer_slot(GLContext *c,uint32_t target){
#if TARGET_OS_IPHONE
    (void)c;(void)target;return NULL;
#else
    return c&&!c->core&&(target==0x8f36||target==0x8f37)?&c->copy_bindings[target-0x8f36]:NULL;
#endif
}
void tribes_gl_delete_buffers(_cpu *cpu,uint32_t count,uint32_t input){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();
    if((int32_t)count<=0){glDeleteBuffers((int32_t)count,NULL);DRAW_RETURN();}
    if(count>UINT32_MAX/sizeof(GLuint))stop();
    size_t bytes=(size_t)count*sizeof(GLuint);GLuint *names=malloc(bytes);if(!names)stop();
    checked_copy(names,guest(cpu,input,bytes),bytes);GLContext *c=current_context();
#if !TARGET_OS_IPHONE
    /* While a legacy context shares objects, native ARRAY bindings as well
       as copy bindings can retain a buffer deleted in another context. Our
       temporary rebind cannot restore such an anonymous object by its old
       name. Reject that unobserved lifecycle before mutation. */
    if(c&&c->textures->refs>1){
        int legacy=!c->core;
        for(GLContext *other=contexts;other;other=other->next)if(other->textures==c->textures&&!other->core)legacy=1;
        if(legacy)for(uint32_t i=0;i<count;i++)if(names[i])stop();
    }
#endif
    glDeleteBuffers((int32_t)count,names);
    if(c)for(uint32_t i=0;i<count;i++)for(unsigned j=0;j<2;j++)if(c->copy_bindings[j]==names[i])c->copy_bindings[j]=0;
    free(names);DRAW_RETURN();
}
void tribes_gl_bind_buffer(_cpu *cpu,uint32_t target,uint32_t name){
    WINDOW_ENTER();GLuint *slot=copy_buffer_slot(current_context(),target);
    if(slot){
        GLint previous=0,actual=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&previous);
        glBindBuffer(GL_ARRAY_BUFFER,name);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&actual);
        if((GLuint)actual==name)*slot=name;
        glBindBuffer(GL_ARRAY_BUFFER,(GLuint)previous);
    }else glBindBuffer(target,name);DRAW_RETURN();
}
void tribes_gl_buffer_data(_cpu *cpu,uint32_t target,uint32_t size,uint32_t input,uint32_t usage){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();
    /* GLsizeiptr is signed and 32 bits in the original ABI. Negative/zero sizes
       must reach native validation without reading the ignored guest input. */
    void *data=NULL;
    if(input&&(int32_t)size>0){
        const void *source=guest(cpu,input,size);data=malloc(size);if(!data)stop();
        checked_copy(data,source,size);
    }
    GLuint *slot=copy_buffer_slot(current_context(),target);GLint previous=0;
    if(slot){glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&previous);glBindBuffer(GL_ARRAY_BUFFER,*slot);target=GL_ARRAY_BUFFER;}
    glBufferData(target,(GLsizeiptr)(int32_t)size,data,usage);
    if(slot)glBindBuffer(GL_ARRAY_BUFFER,(GLuint)previous);
    free(data);DRAW_RETURN();
}
void tribes_gl_matrix_mode(_cpu *cpu,uint32_t mode){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)mode;(void)error;stop();
#else
    glMatrixMode(mode);DRAW_RETURN();
#endif
}
void tribes_gl_load_identity(_cpu *cpu){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)error;stop();
#else
    glLoadIdentity();DRAW_RETURN();
#endif
}
void tribes_gl_load_matrix_d(_cpu *cpu,uint32_t input){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)input;(void)error;stop();
#else
    GLdouble matrix[16];checked_copy(matrix,guest(cpu,input,sizeof matrix),sizeof matrix);
    glLoadMatrixd(matrix);DRAW_RETURN();
#endif
}
void tribes_gl_mult_matrix_f(_cpu *cpu,uint32_t input){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)input;(void)error;stop();
#else
    GLfloat matrix[16];checked_copy(matrix,guest(cpu,input,sizeof matrix),sizeof matrix);
    glMultMatrixf(matrix);DRAW_RETURN();
#endif
}
void tribes_gl_mult_matrix_d(_cpu *cpu,uint32_t input){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)input;(void)error;stop();
#else
    GLdouble matrix[16];checked_copy(matrix,guest(cpu,input,sizeof matrix),sizeof matrix);
    glMultMatrixd(matrix);DRAW_RETURN();
#endif
}
void tribes_gl_viewport(_cpu *cpu,uint32_t x,uint32_t y,uint32_t width,uint32_t height){
    WINDOW_ENTER();glViewport((int32_t)x,(int32_t)y,(int32_t)width,(int32_t)height);DRAW_RETURN();
}
void tribes_gl_disable(_cpu *cpu,uint32_t cap){WINDOW_ENTER();glDisable(cap);DRAW_RETURN();}
void tribes_gl_enable(_cpu *cpu,uint32_t cap){WINDOW_ENTER();glEnable(cap);DRAW_RETURN();}
/* Client pointers remain guest descriptors. Only draw-time host copies are
   submitted to GL, and the native pointers are detached before those copies
   are released. The reached path uses client memory, not VBO offsets. */
#if !TARGET_OS_IPHONE
static GLContext *legacy_context(void){GLContext *c=current_context();if(!c||c->core)stop();return c;}
static unsigned client_texture_unit(void){GLint unit=0;glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&unit);if(unit<GL_TEXTURE0||unit>=GL_TEXTURE0+32)stop();return (unsigned)(unit-GL_TEXTURE0);}
static void client_state(uint32_t cap,int enabled){
    GLContext *c=legacy_context();ClientArray *a;
    /* Original reset routine 0x447239 disables this optional array. Disabling
       consumes no pointer; enabling still needs a checked draw descriptor. */
    if(cap==GL_FOG_COORD_ARRAY&&!enabled){glDisableClientState(cap);return;}
    if(cap==GL_VERTEX_ARRAY)a=&c->vertex;
    else if(cap==GL_COLOR_ARRAY)a=&c->color;
    else if(cap==GL_NORMAL_ARRAY)a=&c->normal;
    else if(cap==GL_TEXTURE_COORD_ARRAY)a=&c->texcoord[client_texture_unit()];
    else stop();
    if(enabled)glEnableClientState(cap);else glDisableClientState(cap);a->enabled=enabled;
}
static unsigned array_scalar_bytes(uint32_t type){
    switch(type){case GL_BYTE:case GL_UNSIGNED_BYTE:return 1;case GL_SHORT:case GL_UNSIGNED_SHORT:return 2;case GL_INT:case GL_UNSIGNED_INT:case GL_FLOAT:return 4;case GL_DOUBLE:return 8;default:stop();}
}
static void client_pointer(uint32_t size,uint32_t type,uint32_t stride,uint32_t address,int texture){
    GLContext *c=legacy_context();
    if(type!=GL_SHORT&&type!=GL_INT&&type!=GL_FLOAT&&type!=GL_DOUBLE)stop();
    if(size<(texture?1u:2u)||size>4||(int32_t)stride<0){
        if(texture)glTexCoordPointer((int32_t)size,type,(int32_t)stride,NULL);
        else glVertexPointer((int32_t)size,type,(int32_t)stride,NULL);
        return;
    }
    GLint buffer=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);if(buffer)stop();
    ClientArray *a=texture?&c->texcoord[client_texture_unit()]:&c->vertex;
    a->address=address;a->size=size;a->type=type;a->stride=stride;a->defined=1;
}
static uint32_t draw_index(const unsigned char *indices,unsigned width,uint32_t i){
    uint32_t index=0;memcpy(&index,indices+(size_t)i*width,width);return index;
}
static void *stage_array(_cpu *cpu,const ClientArray *a,const unsigned char *indices,unsigned width,uint32_t count,uint32_t maximum){
    if(!a->defined)stop();unsigned element=a->size*array_scalar_bytes(a->type);uint32_t stride=a->stride?a->stride:element;
    uint64_t extent=((uint64_t)maximum+1)*element;if(extent>UINT32_MAX)stop();
    /* Validate the actual referenced elements before allocating. Padding and
       unreferenced vertices are not guest reads. Native index values remain
       unchanged, including their shader-visible vertex identity. */
    for(uint32_t i=0;i<count;i++){
        uint64_t address=(uint64_t)a->address+(uint64_t)draw_index(indices,width,i)*stride;
        if(address+element>UINT64_C(0x100000000))stop();
        void *source=guest(cpu,(uint32_t)address,element);uint32_t token=tribes_memory_access_begin(source,element,1);tribes_memory_access_end(token);
    }
    unsigned char *copy=calloc(1,(size_t)extent);if(!copy)stop();
    for(uint32_t i=0;i<count;i++){
        uint32_t index=draw_index(indices,width,i);uint64_t address=(uint64_t)a->address+(uint64_t)index*stride;
        checked_copy(copy+(size_t)index*element,guest(cpu,(uint32_t)address,element),element);
    }return copy;
}
#endif
void tribes_gl_push_matrix(_cpu *cpu){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)error;stop();
#else
    glPushMatrix();DRAW_RETURN();
#endif
}
void tribes_gl_pop_matrix(_cpu *cpu){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)error;stop();
#else
    glPopMatrix();DRAW_RETURN();
#endif
}
void tribes_gl_scale_f(_cpu *cpu,uint32_t x,uint32_t y,uint32_t z){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)x;(void)y;(void)z;(void)error;stop();
#else
    uint32_t bits[]={x,y,z};GLfloat v[3];memcpy(v,bits,sizeof v);glScalef(v[0],v[1],v[2]);DRAW_RETURN();
#endif
}
void tribes_gl_front_face(_cpu *cpu,uint32_t mode){WINDOW_ENTER();glFrontFace(mode);DRAW_RETURN();}
void tribes_gl_depth_mask(_cpu *cpu,uint32_t value){WINDOW_ENTER();glDepthMask((GLboolean)value);DRAW_RETURN();}
void tribes_gl_polygon_offset(_cpu *cpu,uint32_t factor,uint32_t units){
    WINDOW_ENTER();uint32_t bits[]={factor,units};GLfloat value[2];memcpy(value,bits,sizeof value);
    glPolygonOffset(value[0],value[1]);DRAW_RETURN();
}
uint32_t tribes_gl_is_enabled(_cpu *cpu,uint32_t cap){WINDOW_ENTER();uint32_t result=glIsEnabled(cap);WINDOW_RETURN(result);}
void tribes_gl_color4fv(_cpu *cpu,uint32_t input){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)input;(void)error;stop();
#else
    GLfloat value[4];checked_copy(value,guest(cpu,input,sizeof value),sizeof value);glColor4fv(value);DRAW_RETURN();
#endif
}
void tribes_gl_material_fv(_cpu *cpu,uint32_t face,uint32_t name,uint32_t input){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)face;(void)name;(void)input;(void)error;stop();
#else
    unsigned count=0;
    switch(name){
    case GL_AMBIENT:case GL_DIFFUSE:case GL_SPECULAR:case GL_EMISSION:case GL_AMBIENT_AND_DIFFUSE:count=4;break;
    case GL_SHININESS:count=1;break;case GL_COLOR_INDEXES:count=3;break;
    default:stop();
    }
    GLfloat value[4]={0};
    if(face==GL_FRONT||face==GL_BACK||face==GL_FRONT_AND_BACK)checked_copy(value,guest(cpu,input,count*sizeof *value),count*sizeof *value);
    glMaterialfv(face,name,value);DRAW_RETURN();
#endif
}
#if !TARGET_OS_IPHONE
static unsigned tex_env_count(uint32_t target,uint32_t name){
    if(target==GL_TEXTURE_FILTER_CONTROL)return name==GL_TEXTURE_LOD_BIAS?1:0;
    if(target==GL_POINT_SPRITE)return name==GL_COORD_REPLACE?1:0;
    if(target!=GL_TEXTURE_ENV)return 0;
    switch(name){
    case GL_TEXTURE_ENV_COLOR:return 4;
    case GL_TEXTURE_ENV_MODE:case GL_COMBINE_RGB:case GL_COMBINE_ALPHA:case GL_RGB_SCALE:case GL_ALPHA_SCALE:
    case GL_SOURCE0_RGB:case GL_SOURCE1_RGB:case GL_SOURCE2_RGB:case GL_SOURCE0_ALPHA:case GL_SOURCE1_ALPHA:case GL_SOURCE2_ALPHA:
    case GL_OPERAND0_RGB:case GL_OPERAND1_RGB:case GL_OPERAND2_RGB:case GL_OPERAND0_ALPHA:case GL_OPERAND1_ALPHA:case GL_OPERAND2_ALPHA:return 1;
    default:stop();
    }
}
#endif
void tribes_gl_tex_env_fv(_cpu *cpu,uint32_t target,uint32_t name,uint32_t input){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)target;(void)name;(void)input;(void)error;stop();
#else
    unsigned count=tex_env_count(target,name);GLfloat value[4]={0};
    if(count)checked_copy(value,guest(cpu,input,count*sizeof *value),count*sizeof *value);
    glTexEnvfv(target,name,value);DRAW_RETURN();
#endif
}
void tribes_gl_tex_env_f(_cpu *cpu,uint32_t target,uint32_t name,uint32_t bits){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)target;(void)name;(void)bits;(void)error;stop();
#else
    GLfloat value;memcpy(&value,&bits,sizeof value);glTexEnvf(target,name,value);DRAW_RETURN();
#endif
}
void tribes_gl_normal_pointer(_cpu *cpu,uint32_t type,uint32_t stride,uint32_t address){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)type;(void)stride;(void)address;(void)error;stop();
#else
    GLContext *c=legacy_context();
    if((type!=GL_BYTE&&type!=GL_SHORT&&type!=GL_INT&&type!=GL_FLOAT&&type!=GL_DOUBLE)||(int32_t)stride<0){glNormalPointer(type,(int32_t)stride,NULL);DRAW_RETURN();}
    GLint buffer=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);if(buffer)stop();
    ClientArray *a=&c->normal;a->address=address;a->size=3;a->type=type;a->stride=stride;a->defined=1;DRAW_RETURN();
#endif
}
void tribes_gl_tex_env_i(_cpu *cpu,uint32_t target,uint32_t name,uint32_t value){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)target;(void)name;(void)value;(void)error;stop();
#else
    glTexEnvi(target,name,(int32_t)value);DRAW_RETURN();
#endif
}
void tribes_gl_color4ub(_cpu *cpu,uint32_t r,uint32_t g,uint32_t b,uint32_t a){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)r;(void)g;(void)b;(void)a;(void)error;stop();
#else
    glColor4ub((GLubyte)r,(GLubyte)g,(GLubyte)b,(GLubyte)a);DRAW_RETURN();
#endif
}
void tribes_gl_blend_func(_cpu *cpu,uint32_t source,uint32_t destination){WINDOW_ENTER();glBlendFunc(source,destination);DRAW_RETURN();}
void tribes_gl_begin(_cpu *cpu,uint32_t mode){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)mode;(void)error;stop();
#else
    (void)legacy_context();glBegin(mode);DRAW_RETURN();
#endif
}
void tribes_gl_vertex2f(_cpu *cpu,uint32_t x,uint32_t y){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)x;(void)y;(void)error;stop();
#else
    (void)legacy_context();uint32_t bits[]={x,y};GLfloat v[2];memcpy(v,bits,sizeof v);
    glVertex2f(v[0],v[1]);DRAW_RETURN();
#endif
}
void tribes_gl_end(_cpu *cpu){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)error;stop();
#else
    (void)legacy_context();glEnd();DRAW_RETURN();
#endif
}
void tribes_gl_blend_func_separate(_cpu *cpu,uint32_t sr,uint32_t dr,uint32_t sa,uint32_t da){WINDOW_ENTER();glBlendFuncSeparate(sr,dr,sa,da);DRAW_RETURN();}
void tribes_gl_use_program(_cpu *cpu,uint32_t name){WINDOW_ENTER();glUseProgram(name);DRAW_RETURN();}
void tribes_gl_uniform2i(_cpu *cpu,uint32_t location,uint32_t x,uint32_t y){WINDOW_ENTER();glUniform2i((int32_t)location,(int32_t)x,(int32_t)y);DRAW_RETURN();}
void tribes_gl_uniform4i(_cpu *cpu,uint32_t location,uint32_t x,uint32_t y,uint32_t z,uint32_t w){WINDOW_ENTER();glUniform4i((int32_t)location,(int32_t)x,(int32_t)y,(int32_t)z,(int32_t)w);DRAW_RETURN();}
void tribes_gl_enable_client_state(_cpu *cpu,uint32_t cap){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)cap;(void)error;stop();
#else
    client_state(cap,1);DRAW_RETURN();
#endif
}
void tribes_gl_disable_client_state(_cpu *cpu,uint32_t cap){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)cap;(void)error;stop();
#else
    client_state(cap,0);DRAW_RETURN();
#endif
}
void tribes_gl_vertex_pointer(_cpu *cpu,uint32_t size,uint32_t type,uint32_t stride,uint32_t address){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)size;(void)type;(void)stride;(void)address;(void)error;stop();
#else
    client_pointer(size,type,stride,address,0);DRAW_RETURN();
#endif
}
void tribes_gl_color_pointer(_cpu *cpu,uint32_t size,uint32_t type,uint32_t stride,uint32_t address){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)size;(void)type;(void)stride;(void)address;(void)error;stop();
#else
    GLContext *c=legacy_context();
    int scalar=type==GL_BYTE||type==GL_UNSIGNED_BYTE||type==GL_SHORT||type==GL_UNSIGNED_SHORT||type==GL_INT||type==GL_UNSIGNED_INT||type==GL_FLOAT||type==GL_DOUBLE;
    if(!scalar||(size!=3&&size!=4)||(int32_t)stride<0){glColorPointer((int32_t)size,type,(int32_t)stride,NULL);DRAW_RETURN();}
    GLint buffer=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);if(buffer)stop();
    ClientArray *a=&c->color;a->address=address;a->size=size;a->type=type;a->stride=stride;a->defined=1;DRAW_RETURN();
#endif
}
void tribes_gl_texcoord_pointer(_cpu *cpu,uint32_t size,uint32_t type,uint32_t stride,uint32_t address){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)size;(void)type;(void)stride;(void)address;(void)error;stop();
#else
    client_pointer(size,type,stride,address,1);DRAW_RETURN();
#endif
}
void tribes_gl_draw_elements(_cpu *cpu,uint32_t mode,uint32_t count,uint32_t type,uint32_t input){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)mode;(void)count;(void)type;(void)input;(void)error;stop();
#else
    GLContext *c=legacy_context();if(mode>GL_POLYGON)stop();
    if((int32_t)count<=0||!(type==GL_UNSIGNED_BYTE||type==GL_UNSIGNED_SHORT||type==GL_UNSIGNED_INT)){
        glDrawElements(mode,(int32_t)count<=0?(int32_t)count:0,type,NULL);DRAW_RETURN();
    }
    GLint element_buffer=0,previous=0;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&element_buffer);if(element_buffer)stop();
    unsigned width=type==GL_UNSIGNED_BYTE?1:type==GL_UNSIGNED_SHORT?2:4;
    if(count>UINT32_MAX/width)stop();size_t bytes=(size_t)count*width;
    unsigned char *indices=malloc(bytes);if(!indices)stop();checked_copy(indices,guest(cpu,input,bytes),bytes);
    uint32_t maximum=0;for(uint32_t i=0;i<count;i++){uint32_t index=draw_index(indices,width,i);if(index>maximum)maximum=index;}
    void *vertex=NULL,*color=NULL,*normal=NULL,*texcoords[32]={0};
    if(c->vertex.enabled)vertex=stage_array(cpu,&c->vertex,indices,width,count,maximum);
    if(c->color.enabled)color=stage_array(cpu,&c->color,indices,width,count,maximum);
    if(c->normal.enabled)normal=stage_array(cpu,&c->normal,indices,width,count,maximum);
    for(unsigned i=0;i<32;i++)if(c->texcoord[i].enabled)texcoords[i]=stage_array(cpu,&c->texcoord[i],indices,width,count,maximum);
    unsigned unit=client_texture_unit();glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&previous);glBindBuffer(GL_ARRAY_BUFFER,0);
    if(vertex)glVertexPointer((GLint)c->vertex.size,c->vertex.type,0,vertex);
    if(color)glColorPointer((GLint)c->color.size,c->color.type,0,color);
    if(normal)glNormalPointer(c->normal.type,0,normal);
    for(unsigned i=0;i<32;i++)if(texcoords[i]){glClientActiveTexture(GL_TEXTURE0+i);glTexCoordPointer((GLint)c->texcoord[i].size,c->texcoord[i].type,0,texcoords[i]);}
    glClientActiveTexture(GL_TEXTURE0+unit);glDrawElements(mode,(GLsizei)count,type,indices);
    if(vertex)glVertexPointer((GLint)c->vertex.size,c->vertex.type,0,NULL);
    if(color)glColorPointer((GLint)c->color.size,c->color.type,0,NULL);
    if(normal)glNormalPointer(c->normal.type,0,NULL);
    for(unsigned i=0;i<32;i++)if(texcoords[i]){glClientActiveTexture(GL_TEXTURE0+i);glTexCoordPointer((GLint)c->texcoord[i].size,c->texcoord[i].type,0,NULL);free(texcoords[i]);}
    glClientActiveTexture(GL_TEXTURE0+unit);glBindBuffer(GL_ARRAY_BUFFER,(GLuint)previous);free(normal);free(color);free(vertex);free(indices);DRAW_RETURN();
#endif
}
/* Texture names are the native context's GLuint values. No host pointers or
   opaque object structures cross the guest ABI. */
void tribes_gl_gen_textures(_cpu *cpu,uint32_t count,uint32_t output){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();
    if((int32_t)count<=0){glGenTextures((int32_t)count,NULL);DRAW_RETURN();}
    if(count>UINT32_MAX/sizeof(GLuint))stop();
    size_t bytes=(size_t)count*sizeof(GLuint);void *destination=guest(cpu,output,bytes);
    uint32_t token=tribes_memory_access_begin(destination,bytes,3);
    GLuint *names=malloc(bytes);if(!names)stop();checked_copy(names,destination,bytes);
    glGenTextures((int32_t)count,names);checked_copy(destination,names,bytes);
    free(names);tribes_memory_access_end(token);DRAW_RETURN();
}
void tribes_gl_delete_textures(_cpu *cpu,uint32_t count,uint32_t input){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();
    if((int32_t)count<=0){glDeleteTextures((int32_t)count,NULL);DRAW_RETURN();}
    if(count>UINT32_MAX/sizeof(GLuint))stop();
    size_t bytes=(size_t)count*sizeof(GLuint);GLuint *names=malloc(bytes);if(!names)stop();
    checked_copy(names,guest(cpu,input,bytes),bytes);glDeleteTextures((int32_t)count,names);
    GLContext *c=current_context();if(c)for(uint32_t i=0;i<count;i++){
        AutoMipmap **p=automatic_slot(c,names[i]);if(*p){AutoMipmap *m=*p;*p=m->next;free(m);}
    }
    free(names);DRAW_RETURN();
}
void tribes_gl_bind_texture(_cpu *cpu,uint32_t target,uint32_t name){WINDOW_ENTER();glBindTexture(target,name);DRAW_RETURN();}
static int set_automatic_mipmap(GLenum target,GLenum parameter,int enabled){
    GLContext *c=current_context();if(!c||!c->core||parameter!=0x8191)return 0;
    /* Only the reached 2D object contract. Default objects and other texture
       targets need their own audited upload paths before accepting this flag. */
    if(target!=GL_TEXTURE_2D)stop();GLint name=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&name);if(!name)stop();
    AutoMipmap **p=automatic_slot(c,(GLuint)name);
    if(enabled&&!*p){*p=calloc(1,sizeof **p);if(!*p)stop();(*p)->name=(GLuint)name;}
    else if(!enabled&&*p){AutoMipmap *m=*p;*p=m->next;free(m);}
    return 1;
}
/* Testing upload success must not discard earlier or new driver errors.
   Retain actual native error flags per context for the guest GetError API.
   OpenGL permits arbitrary ordering of distinct outstanding error flags. */
static unsigned collect_gl_errors(GLContext *c){
    unsigned flags=0;for(GLenum e;(e=glGetError())!=GL_NO_ERROR;){
        if(e<0x500||e>0x507)stop();flags|=1u<<(e-0x500);
    }c->errors|=flags;return flags;
}
void tribes_gl_tex_parameter_i(_cpu *cpu,uint32_t target,uint32_t parameter,uint32_t value){
    WINDOW_ENTER();if(!set_automatic_mipmap(target,parameter,value!=0))glTexParameteri(target,parameter,(int32_t)value);DRAW_RETURN();
}
void tribes_gl_tex_parameter_f(_cpu *cpu,uint32_t target,uint32_t parameter,uint32_t bits){
    WINDOW_ENTER();GLfloat value;memcpy(&value,&bits,4);
    if(!set_automatic_mipmap(target,parameter,value!=0.0f))glTexParameterf(target,parameter,value);DRAW_RETURN();
}
void tribes_gl_generate_mipmap(_cpu *cpu,uint32_t target){
    WINDOW_ENTER();glGenerateMipmap(target);DRAW_RETURN();
}
void tribes_gl_tex_image_2d(_cpu *cpu,uint32_t target,uint32_t level,uint32_t internal,uint32_t width,uint32_t height,uint32_t border,uint32_t format,uint32_t type,uint32_t input){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();
    GLint alignment=4,row_length=0,skip_rows=0,skip_pixels=0;
#if !TARGET_OS_IPHONE
    GLint pbo=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pbo);if(pbo)stop();
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row_length);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skip_rows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip_pixels);
#else
    /* ES2 exposes row/skip state only with EXT_unpack_subimage. */
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    const char *version=(const char *)glGetString(GL_VERSION);
    int es3=version&&!strncmp(version,"OpenGL ES 3.",12),unpack_subimage=0;
    if(extensions)for(const char *p=extensions;*p;p++){
        if((p==extensions||p[-1]==' ')&&!strncmp(p,"GL_EXT_unpack_subimage",21)&&(p[21]==0||p[21]==' ')){unpack_subimage=1;break;}
    }
    if(es3){GLint pbo=0;glGetIntegerv(0x88ef,&pbo);if(pbo)stop();}
    if(es3||unpack_subimage){
        glGetIntegerv(0x0cf2,&row_length);glGetIntegerv(0x0cf3,&skip_rows);glGetIntegerv(0x0cf4,&skip_pixels);
    }
#endif
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
    void *pixels=NULL;
    if(input&&(int32_t)width>0&&(int32_t)height>0){
        /* Reached original bitmap formats: byte channels and packed RGBA5551.
           Other pixel layouts remain explicit stops until their bounds are audited. */
        uint32_t bpp=0;
        if(type==GL_UNSIGNED_BYTE){
            switch(format){
                case GL_RGB:bpp=3;break;case GL_RGBA:bpp=4;break;
                case GL_ALPHA:case GL_LUMINANCE:bpp=1;break;case GL_LUMINANCE_ALPHA:bpp=2;break;
#if !TARGET_OS_IPHONE
                case GL_COLOR_INDEX:bpp=1;break;case GL_BGR:bpp=3;break;case GL_BGRA:bpp=4;break;
#endif
            }
        }else if(type==GL_UNSIGNED_SHORT_5_5_5_1&&format==GL_RGBA)bpp=2;
        if(!bpp||row_length<0||skip_rows<0||skip_pixels<0||(alignment!=1&&alignment!=2&&alignment!=4&&alignment!=8))stop();
        uint64_t row=(uint64_t)(row_length?row_length:width)*bpp;
        uint64_t stride=(row+(uint32_t)alignment-1)&~((uint64_t)alignment-1);
        if(stride>UINT32_MAX)stop();
        uint64_t offset=(uint64_t)skip_rows*stride+(uint64_t)skip_pixels*bpp;
        if(offset>UINT32_MAX)stop();
        uint64_t used=(uint64_t)width*bpp;
        uint64_t bytes=offset+((uint64_t)height-1)*stride+used;
        if(bytes>UINT32_MAX)stop();
        guest(cpu,input,(size_t)bytes);pixels=calloc(1,(size_t)bytes);if(!pixels)stop();
        /* Copy only rows the driver reads; padding/skipped bytes need not be readable. */
        for(uint32_t y=0;y<height;y++){
            uint64_t at=offset+(uint64_t)y*stride;
            checked_copy((unsigned char *)pixels+at,guest(cpu,input+(uint32_t)at,(size_t)used),(size_t)used);
        }
    }
#ifdef TB_CAPTURE_PATCHES
    fprintf(stderr,"Observed texture upload target=0x%x level=%d internal=0x%x size=%ux%u format=0x%x type=0x%x input=0x%08x\n",target,(int32_t)level,internal,width,height,format,type,input);
#endif
    GLContext *c=current_context();int generate=0;
#if !TARGET_OS_IPHONE
    if(c&&c->core&&target==GL_TEXTURE_2D){
        GLint name=0,base_level=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&name);
        if(name&&*automatic_slot(c,(GLuint)name)){
            glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&base_level);
            /* The reached renderer uses base zero. Apple's current driver
               returns stale generated pixels after nonzero-base reuploads;
               the independent native probe reproduces this. Reject that
               unsupported path rather than claiming correct mipmap content. */
            if(base_level!=0)stop();generate=(int32_t)level==0;
        }
    }
#endif
    if(generate)collect_gl_errors(c);
    glTexImage2D(target,(int32_t)level,(int32_t)internal,(int32_t)width,(int32_t)height,(int32_t)border,format,type,pixels);
    if(generate&&!collect_gl_errors(c)&&(int32_t)width>0&&(int32_t)height>0){
        glGenerateMipmap(target);
#ifdef TB_CAPTURE_PATCHES
        fprintf(stderr,"Observed native automatic mipmap generation target=0x%x base=%d\n",target,(int32_t)level);
#endif
    }
    free(pixels);DRAW_RETURN();
}
/* Array textures use the same checked row staging as 2D uploads, extended
   across image strides. Native unpack state is retained; skipped bytes are
   zero-filled host padding, never reads of unrelated guest memory. */
#if !TARGET_OS_IPHONE
static void *texture_3d_pixels(_cpu *cpu,uint32_t width,uint32_t height,uint32_t depth,uint32_t format,uint32_t type,uint32_t input,int allocation){
    GLint pbo=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pbo);if(pbo)stop();
    if((int32_t)width<=0||(int32_t)height<=0||(int32_t)depth<=0)return NULL;
    if(!input){if(!allocation)stop();return NULL;}
    uint32_t channels=0;
    switch(format){case GL_RED:case GL_ALPHA:case GL_LUMINANCE:channels=1;break;case GL_LUMINANCE_ALPHA:channels=2;break;case GL_RGB:case GL_BGR:channels=3;break;case GL_RGBA:case GL_BGRA:channels=4;break;}
    uint32_t component=type==GL_UNSIGNED_BYTE?1:type==GL_FLOAT?4:0;if(!channels||!component)stop();
    uint32_t bpp=channels*component;GLint alignment=4,row_length=0,image_height=0,skip_pixels=0,skip_rows=0,skip_images=0;
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row_length);glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT,&image_height);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip_pixels);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skip_rows);glGetIntegerv(GL_UNPACK_SKIP_IMAGES,&skip_images);
    if(row_length<0||image_height<0||skip_pixels<0||skip_rows<0||skip_images<0||(alignment!=1&&alignment!=2&&alignment!=4&&alignment!=8))stop();
    uint64_t row=(uint64_t)(row_length?row_length:width)*bpp;
    uint64_t stride=(row+(uint32_t)alignment-1)&~((uint64_t)alignment-1);if(stride>UINT32_MAX)stop();
    uint64_t slice=stride*(uint32_t)(image_height?image_height:height);if(slice>UINT32_MAX)stop();
    uint64_t offset=(uint64_t)skip_images*slice;if(offset>UINT32_MAX)stop();
    offset+=(uint64_t)skip_rows*stride+(uint64_t)skip_pixels*bpp;if(offset>UINT32_MAX)stop();
    uint64_t used=(uint64_t)width*bpp,tail=offset+((uint64_t)height-1)*stride+used;if(tail>UINT32_MAX)stop();
    uint64_t bytes=((uint64_t)depth-1)*slice+tail;if(bytes>UINT32_MAX)stop();
    guest(cpu,input,(size_t)bytes);unsigned char *pixels=calloc(1,(size_t)bytes);if(!pixels)stop();
    for(uint32_t z=0;z<depth;z++)for(uint32_t y=0;y<height;y++){
        uint64_t at=offset+(uint64_t)z*slice+(uint64_t)y*stride;
        checked_copy(pixels+at,guest(cpu,input+(uint32_t)at,(size_t)used),(size_t)used);
    }
    return pixels;
}
#endif
void tribes_gl_tex_image_3d(_cpu *cpu,uint32_t target,uint32_t level,uint32_t internal,uint32_t width,uint32_t height,uint32_t depth,uint32_t border,uint32_t format,uint32_t type,uint32_t input){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)target;(void)level;(void)internal;(void)width;(void)height;(void)depth;(void)border;(void)format;(void)type;(void)input;stop();
#else
    if(!SDL_GL_GetCurrentContext())stop();void *pixels=texture_3d_pixels(cpu,width,height,depth,format,type,input,1);
    glTexImage3D(target,(int32_t)level,(int32_t)internal,(int32_t)width,(int32_t)height,(int32_t)depth,(int32_t)border,format,type,pixels);free(pixels);
#endif
    DRAW_RETURN();
}
void tribes_gl_tex_sub_image_3d(_cpu *cpu,uint32_t target,uint32_t level,uint32_t x,uint32_t y,uint32_t z,uint32_t width,uint32_t height,uint32_t depth,uint32_t format,uint32_t type,uint32_t input){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)target;(void)level;(void)x;(void)y;(void)z;(void)width;(void)height;(void)depth;(void)format;(void)type;(void)input;stop();
#else
    if(!SDL_GL_GetCurrentContext())stop();void *pixels=texture_3d_pixels(cpu,width,height,depth,format,type,input,0);
    glTexSubImage3D(target,(int32_t)level,(int32_t)x,(int32_t)y,(int32_t)z,(int32_t)width,(int32_t)height,(int32_t)depth,format,type,pixels);free(pixels);
#endif
    DRAW_RETURN();
}
/* Opaque shader/program names are native GL names in the existing share group.
   Never retain a guest string/vector or expose a host pointer to the engine. */
static double gl_double_words(uint32_t low,uint32_t high){uint64_t bits=((uint64_t)high<<32)|low;double value;memcpy(&value,&bits,8);return value;}
void tribes_gl_frustum(_cpu *cpu,uint32_t ll,uint32_t lh,uint32_t rl,uint32_t rh,uint32_t bl,uint32_t bh,uint32_t tl,uint32_t th,uint32_t nl,uint32_t nh,uint32_t fl,uint32_t fh){
    WINDOW_ENTER();
#if TARGET_OS_IPHONE
    (void)ll;(void)lh;(void)rl;(void)rh;(void)bl;(void)bh;(void)tl;(void)th;(void)nl;(void)nh;(void)fl;(void)fh;(void)error;stop();
#else
    glFrustum(gl_double_words(ll,lh),gl_double_words(rl,rh),gl_double_words(bl,bh),gl_double_words(tl,th),gl_double_words(nl,nh),gl_double_words(fl,fh));DRAW_RETURN();
#endif
}
void tribes_gl_depth_range(_cpu *cpu,uint32_t lo,uint32_t hi,uint32_t flo,uint32_t fhi){
    WINDOW_ENTER();double near=gl_double_words(lo,hi),far=gl_double_words(flo,fhi);
#if TARGET_OS_IPHONE
    glDepthRangef((float)near,(float)far);
#else
    glDepthRange(near,far);
#endif
    DRAW_RETURN();
}
void tribes_gl_clear_depth(_cpu *cpu,uint32_t lo,uint32_t hi){
    WINDOW_ENTER();double value=gl_double_words(lo,hi);
#if TARGET_OS_IPHONE
    glClearDepthf((float)value);
#else
    glClearDepth(value);
#endif
    DRAW_RETURN();
}
void tribes_gl_depth_func(_cpu *cpu,uint32_t func){WINDOW_ENTER();glDepthFunc(func);DRAW_RETURN();}
uint32_t tribes_gl_create_shader(_cpu *cpu,uint32_t type){WINDOW_ENTER();uint32_t result=glCreateShader(type);WINDOW_RETURN(result);}
void tribes_gl_compile_shader(_cpu *cpu,uint32_t shader){WINDOW_ENTER();glCompileShader(shader);DRAW_RETURN();}
void tribes_gl_delete_shader(_cpu *cpu,uint32_t shader){WINDOW_ENTER();glDeleteShader(shader);DRAW_RETURN();}
uint32_t tribes_gl_create_program(_cpu *cpu){WINDOW_ENTER();uint32_t result=glCreateProgram();WINDOW_RETURN(result);}
void tribes_gl_attach_shader(_cpu *cpu,uint32_t program,uint32_t shader){WINDOW_ENTER();glAttachShader(program,shader);DRAW_RETURN();}
void tribes_gl_link_program(_cpu *cpu,uint32_t program){WINDOW_ENTER();glLinkProgram(program);DRAW_RETURN();}
void tribes_gl_delete_program(_cpu *cpu,uint32_t program){WINDOW_ENTER();glDeleteProgram(program);DRAW_RETURN();}
void tribes_gl_uniform1i(_cpu *cpu,uint32_t location,uint32_t value){WINDOW_ENTER();glUniform1i((int32_t)location,(int32_t)value);DRAW_RETURN();}
void tribes_gl_uniform1f(_cpu *cpu,uint32_t location,uint32_t bits){WINDOW_ENTER();float value;memcpy(&value,&bits,4);glUniform1f((int32_t)location,value);DRAW_RETURN();}
void tribes_gl_uniform4fv(_cpu *cpu,uint32_t location,uint32_t count,uint32_t values){
    WINDOW_ENTER();GLfloat *copy=NULL;
    if((int32_t)count>0&&(int32_t)location!=-1){
        if(count>UINT32_MAX/16)stop();size_t bytes=(size_t)count*16;
        copy=malloc(bytes);if(!copy)stop();checked_copy(copy,guest(cpu,values,bytes),bytes);
    }
    glUniform4fv((int32_t)location,(int32_t)count,copy);free(copy);DRAW_RETURN();
}
#if !TARGET_OS_IPHONE
typedef struct LegacyShaderTemplate {const char *name;int injected;const char *prefix,*suffix,*replacement_prefix,*replacement_suffix;} LegacyShaderTemplate;
#include "legacy_shader_sources.inc"
static int legacy_shader_extension(const char *extensions,const char *name){
    size_t n=strlen(name);if(!extensions)return 0;
    for(const char *p=extensions;*p;p++)if((p==extensions||p[-1]==' ')&&!strncmp(p,name,n)&&(p[n]==' '||p[n]==0))return 1;
    return 0;
}
static char *legacy_shader_source(const char *source,size_t bytes){
    GLContext *context=current_context();if(!context||context->core||strlen(source)!=bytes)return NULL;
    for(size_t i=0;i<sizeof legacy_shader_templates/sizeof *legacy_shader_templates;i++){
        const LegacyShaderTemplate *t=&legacy_shader_templates[i];size_t pre=strlen(t->prefix),suf=strlen(t->suffix);
        if(bytes<pre+suf||(!t->injected&&bytes!=pre)||memcmp(source,t->prefix,pre)||memcmp(source+bytes-suf,t->suffix,suf))continue;
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
        if(!legacy_shader_extension(extensions,"GL_EXT_gpu_shader4")||!legacy_shader_extension(extensions,"GL_EXT_texture_array")||!legacy_shader_extension(extensions,"GL_ARB_shader_texture_lod"))return NULL;
        size_t newpre=strlen(t->replacement_prefix),newsuf=strlen(t->replacement_suffix),injected=bytes-pre-suf;
        if(newpre+newsuf+injected>INT32_MAX)stop();
        char *result=malloc(newpre+injected+newsuf+1);if(!result)stop();
        memcpy(result,t->replacement_prefix,newpre);memcpy(result+newpre,source+pre,injected);memcpy(result+newpre+injected,t->replacement_suffix,newsuf);result[newpre+injected+newsuf]=0;
        return result;
    }
    return NULL;
}
#endif
void tribes_gl_shader_source(_cpu *cpu,uint32_t shader,uint32_t count,uint32_t pointers,uint32_t lengths){
    WINDOW_ENTER();
    if((int32_t)count<=0){glShaderSource(shader,(int32_t)count,NULL,NULL);DRAW_RETURN();}
    if(count>UINT32_MAX/4)stop();
    uint32_t *addresses=malloc((size_t)count*4);GLint *sizes=malloc((size_t)count*4);
    char **sources=calloc(count,sizeof *sources);if(!addresses||!sizes||!sources)stop();
    checked_copy(addresses,guest(cpu,pointers,(size_t)count*4),(size_t)count*4);
    if(lengths)checked_copy(sizes,guest(cpu,lengths,(size_t)count*4),(size_t)count*4);
    else for(uint32_t i=0;i<count;i++)sizes[i]=-1;
    for(uint32_t i=0;i<count;i++){
        if(sizes[i]<0){sources[i]=string(cpu,addresses[i]);if(!sources[i])stop();}
        else {size_t bytes=(uint32_t)sizes[i];sources[i]=malloc(bytes+1);if(!sources[i])stop();
            if(bytes)checked_copy(sources[i],guest(cpu,addresses[i],bytes),bytes);sources[i][bytes]=0;}
    }
#if !TARGET_OS_IPHONE
    /* Exact shipped source templates only. Preserve the original injected
       defines/line directives and leave other programs to the native compiler. */
    if(count==1){size_t bytes=sizes[0]<0?strlen(sources[0]):(size_t)sizes[0];char *adapted=legacy_shader_source(sources[0],bytes);
        if(adapted){free(sources[0]);sources[0]=adapted;sizes[0]=(GLint)strlen(adapted);}}
#endif
    glShaderSource(shader,(int32_t)count,(const GLchar *const *)sources,sizes);
    for(uint32_t i=0;i<count;i++)free(sources[i]);free(sources);free(sizes);free(addresses);DRAW_RETURN();
}
static void shader_parameter(_cpu *cpu,uint32_t name,uint32_t parameter,uint32_t output,int program){
    WINDOW_ENTER();GLint value;checked_copy(&value,guest(cpu,output,4),4);
    /* All supported GL2/GLES2 parameters are scalar. Compute work-group size
       is a three-value desktop query and is deliberately outside this ABI. */
    if(program&&parameter==0x8267)stop();
    if(program)glGetProgramiv(name,parameter,&value);else glGetShaderiv(name,parameter,&value);
    checked_copy(guest(cpu,output,4),&value,4);DRAW_RETURN();
}
void tribes_gl_get_shader_iv(_cpu *cpu,uint32_t name,uint32_t parameter,uint32_t output){shader_parameter(cpu,name,parameter,output,0);}
void tribes_gl_get_program_iv(_cpu *cpu,uint32_t name,uint32_t parameter,uint32_t output){shader_parameter(cpu,name,parameter,output,1);}
/* Seed scratch output with caller bytes so driver errors and unused capacity
   preserve them. Copy back in C parameter order; optional length may be NULL. */
static void shader_log(_cpu *cpu,uint32_t name,uint32_t capacity,uint32_t length,uint32_t output,int program){
    WINDOW_ENTER();GLsizei written=0;char *buffer=NULL;
    if((int32_t)capacity<0){if(program)glGetProgramInfoLog(name,(int32_t)capacity,NULL,NULL);else glGetShaderInfoLog(name,(int32_t)capacity,NULL,NULL);DRAW_RETURN();}
    if(length)checked_copy(&written,guest(cpu,length,4),4);
    if(capacity){buffer=malloc(capacity);if(!buffer)stop();checked_copy(buffer,guest(cpu,output,capacity),capacity);}
    if(program)glGetProgramInfoLog(name,(int32_t)capacity,length?&written:NULL,buffer);else glGetShaderInfoLog(name,(int32_t)capacity,length?&written:NULL,buffer);
    if(length)checked_copy(guest(cpu,length,4),&written,4);
    if(capacity)checked_copy(guest(cpu,output,capacity),buffer,capacity);free(buffer);DRAW_RETURN();
}
void tribes_gl_get_shader_log(_cpu *cpu,uint32_t name,uint32_t capacity,uint32_t length,uint32_t output){shader_log(cpu,name,capacity,length,output,0);}
void tribes_gl_get_program_log(_cpu *cpu,uint32_t name,uint32_t capacity,uint32_t length,uint32_t output){shader_log(cpu,name,capacity,length,output,1);}
static void shader_active(_cpu *cpu,uint32_t program,uint32_t index,uint32_t capacity,uint32_t length,uint32_t size,uint32_t type,uint32_t output,int attribute){
    WINDOW_ENTER();GLsizei written=0;GLint elements=0;GLenum kind=0;char *buffer=NULL;
    if((int32_t)capacity<0){if(attribute)glGetActiveAttrib(program,index,(int32_t)capacity,NULL,&elements,&kind,NULL);else glGetActiveUniform(program,index,(int32_t)capacity,NULL,&elements,&kind,NULL);DRAW_RETURN();}
    if(length)checked_copy(&written,guest(cpu,length,4),4);
    checked_copy(&elements,guest(cpu,size,4),4);checked_copy(&kind,guest(cpu,type,4),4);
    if(capacity){buffer=malloc(capacity);if(!buffer)stop();checked_copy(buffer,guest(cpu,output,capacity),capacity);}
    if(attribute)glGetActiveAttrib(program,index,(int32_t)capacity,length?&written:NULL,&elements,&kind,buffer);else glGetActiveUniform(program,index,(int32_t)capacity,length?&written:NULL,&elements,&kind,buffer);
    if(length)checked_copy(guest(cpu,length,4),&written,4);
    checked_copy(guest(cpu,size,4),&elements,4);checked_copy(guest(cpu,type,4),&kind,4);
    if(capacity)checked_copy(guest(cpu,output,capacity),buffer,capacity);free(buffer);DRAW_RETURN();
}
void tribes_gl_get_active_uniform(_cpu *cpu,uint32_t program,uint32_t index,uint32_t capacity,uint32_t length,uint32_t size,uint32_t type,uint32_t output){shader_active(cpu,program,index,capacity,length,size,type,output,0);}
void tribes_gl_get_active_attrib(_cpu *cpu,uint32_t program,uint32_t index,uint32_t capacity,uint32_t length,uint32_t size,uint32_t type,uint32_t output){shader_active(cpu,program,index,capacity,length,size,type,output,1);}
uint32_t tribes_gl_get_uniform_location(_cpu *cpu,uint32_t program,uint32_t input){WINDOW_ENTER();char *name=string(cpu,input);if(!name)stop();GLint result=glGetUniformLocation(program,name);free(name);WINDOW_RETURN((uint32_t)result);}
uint32_t tribes_gl_get_attrib_location(_cpu *cpu,uint32_t program,uint32_t input){WINDOW_ENTER();char *name=string(cpu,input);if(!name)stop();GLint result=glGetAttribLocation(program,name);free(name);WINDOW_RETURN((uint32_t)result);}


#undef DRAW_RETURN
#undef DRAW_OBSERVE
uint32_t tribes_gl_get_string(_cpu *cpu,uint32_t name){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();
#if !TARGET_OS_IPHONE
    /* Core drivers expose the same capability names as indexed strings.
       Adapt only their representation for the original extension parser;
       never add compatibility extensions or consume the native error state. */
    GLContext *current=contexts;while(current&&current->native!=SDL_GL_GetCurrentContext())current=current->next;
    if(name==GL_EXTENSIONS&&current&&current->core){
        GLint count=0;glGetIntegerv(0x821d,&count);if(count<0||count>65536)stop();
        size_t total=1;for(GLint i=0;i<count;i++){
            const char *s=(const char *)glGetStringi(GL_EXTENSIONS,(GLuint)i);if(!s)stop();
            size_t n=strnlen(s,1048576);if(n==1048576||total+n+1>1048576)stop();total+=n+1;
        }
        char *text=malloc(total);if(!text){SDL_OutOfMemory();WINDOW_RETURN(0);}
        size_t used=0;for(GLint i=0;i<count;i++){
            const char *s=(const char *)glGetStringi(GL_EXTENSIONS,(GLuint)i);size_t n=strlen(s);
            if(i)text[used++]=' ';memcpy(text+used,s,n);used+=n;
        }
        text[used]=0;uint32_t result=graphics_string(&frame,text);free(text);WINDOW_RETURN(result);
    }
#endif
    WINDOW_RETURN(graphics_string(&frame,(const char *)glGetString(name)));
}
uint32_t tribes_gl_get_error(_cpu *cpu){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();GLContext *c=current_context();
    if(c&&c->errors){collect_gl_errors(c);for(unsigned i=0;i<8;i++)if(c->errors&(1u<<i)){c->errors&=~(1u<<i);WINDOW_RETURN(0x500+i);}}
    WINDOW_RETURN(glGetError());
}
void tribes_gl_get_integer(_cpu *cpu,uint32_t name,uint32_t output){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();
    /* Reached scalar queries only. Other enums can have multiple outputs. */
    if(name!=0x8d57&&name!=GL_MAX_TEXTURE_SIZE&&name!=0x84e2&&name!=0x8192&&name!=0x0d31&&name!=0x8f36&&name!=0x8f37&&name!=GL_ARRAY_BUFFER_BINDING)stop();
    GLint value;checked_copy(&value,guest(cpu,output,4),4);GLContext *c=current_context();
    GLuint *copy=copy_buffer_slot(c,name);
    if(copy)value=(GLint)*copy;else if(c&&c->core&&name==0x8192)value=(GLint)c->mipmap_hint;else glGetIntegerv(name,&value);
    checked_copy(guest(cpu,output,4),&value,4);graphics_active=0;leave(&frame);tribes_set_last_error(error);
}
/* These reached limits are single floats. Other enums may be vectors and
   remain guarded until their exact output extent is supported. */
void tribes_gl_get_float(_cpu *cpu,uint32_t name,uint32_t output){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();
    if(name!=0x84ff&&name!=GL_MAX_TEXTURE_SIZE&&name!=0x8192)stop();
    GLfloat value;checked_copy(&value,guest(cpu,output,4),4);GLContext *c=current_context();
    if(c&&c->core&&name==0x8192)value=(GLfloat)c->mipmap_hint;else glGetFloatv(name,&value);
    checked_copy(guest(cpu,output,4),&value,4);graphics_active=0;leave(&frame);tribes_set_last_error(error);
}
uint32_t tribes_glu_error_string(_cpu *cpu,uint32_t code){
    WINDOW_ENTER();const char *text=NULL;
#if !TARGET_OS_IPHONE
    text=(const char *)gluErrorString(code);
#else
    /* GLU's portable error-name mapping requires no graphics context. */
    switch(code){case 0:text="no error";break;case 0x500:text="invalid enumerant";break;
    case 0x501:text="invalid value";break;case 0x502:text="invalid operation";break;
    case 0x503:text="stack overflow";break;case 0x504:text="stack underflow";break;
    case 0x505:text="out of memory";break;case 0x506:text="invalid framebuffer operation";break;
    case 100900:text="invalid enumerant";break;case 100901:text="invalid value";break;
    case 100902:text="out of memory";break;case 100903:text="incompatible gl version";break;
    case 100904:text="invalid operation";break;default:if(code>=100000)stop();}
#endif
    WINDOW_RETURN(graphics_string(&frame,text));
}
uint32_t tribes_sdl_get_pointer_property(_cpu *cpu,uint32_t properties,uint32_t name,uint32_t fallback){
    WINDOW_ENTER();char *key=string(cpu,name);if(!key)stop();
    uint32_t kind=!strcmp(key,"SDL.window.win32.hdc")?0x02000000u:!strcmp(key,"SDL.window.win32.hwnd")?0x03000000u:0;
    free(key);if(!kind)stop();
    /* HDC/HWND adapter identities refer to an actual SDL window. No native
       pointer escapes. Further Win32 window operations need explicit routing. */
    for(Window *w=windows;w;w=w->next)if(SDL_GetWindowProperties(w->native)==properties)
        WINDOW_RETURN(w->token+kind);
    WINDOW_RETURN(fallback);
}
uint32_t tribes_wgl_extensions_arb(_cpu *cpu,uint32_t dc){
    WINDOW_ENTER();Window *w=*window_slot(dc-0x02000000u);
    if(!SDL_GL_GetCurrentContext()||SDL_GL_GetCurrentWindow()!=w->native)stop();
    WINDOW_RETURN(graphics_string(&frame,"WGL_ARB_extensions_string WGL_EXT_extensions_string WGL_EXT_swap_control"));
}
uint32_t tribes_wgl_extensions_ext(_cpu *cpu){
    WINDOW_ENTER();if(!SDL_GL_GetCurrentContext())stop();WINDOW_RETURN(graphics_string(&frame,"WGL_ARB_extensions_string WGL_EXT_extensions_string WGL_EXT_swap_control"));
}
#undef WINDOW_ENTER
#undef WINDOW_RETURN

/* DirectSound cooperative ownership uses the existing HWND adapter token.
   Window lifetime is owner-thread only, as for all other window adapters. */
int tribes_sdl_has_window_handle(uint64_t guest_base,uint32_t hwnd){
    if(!bound||guest_base!=base||!pthread_equal(owner,pthread_self()))return 0;
    for(Window *w=windows;w;w=w->next)if(w->token+0x03000000u==hwnd)return 1;
    return 0;
}

static uint32_t window_style(SDL_Window *w){
    SDL_WindowFlags f=SDL_GetWindowFlags(w);
    uint32_t style=(f&(SDL_WINDOW_BORDERLESS|SDL_WINDOW_FULLSCREEN))?0x80000000u:0x00cf0000u;
    if(!(f&SDL_WINDOW_RESIZABLE))style&=~0x00050000u;
    if(!(f&SDL_WINDOW_HIDDEN))style|=0x10000000u;
    if(f&SDL_WINDOW_MINIMIZED)style|=0x20000000u;
    if(f&SDL_WINDOW_MAXIMIZED)style|=0x01000000u;
    return style;
}
void tribes_sdl_refresh_window_catalog(uint64_t guest_base){
    if(!bound||guest_base!=base||!windows)return;
    if(!pthread_equal(owner,pthread_self()))stop();
    /* One native game window has an unambiguous position among our virtual
       notification windows. Multiple native windows need native Z-order. */
    if(windows->next){fputs("TribesPad: unsupported multi-window native Z-order\n",stderr);abort();}
    Window *w=windows;SDL_Window *focus=SDL_GetKeyboardFocus();
    tribes_window_update(base,w->token+0x03000000u,window_style(w->native),focus==w->native);
}
