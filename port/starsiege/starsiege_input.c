/* DirectInput keyboard0 and mouse0 for Starsiege: Tribes (goal loop G3).
 * Tribes 1 reads all play and menu input through DirectInput: inputActivate()
 * in console.cs acquires keyboard0 and mouse0, and its GUI moves its own
 * cursor from relative mouse motion. Tribes 2's native DirectInput root
 * (guest_dinput.c, included unchanged) creates the devices and data formats;
 * this file adds the device methods Tribes 2 never needed: cooperative level,
 * acquire, device state and buffered data, fed from SDL events.
 * SDL events come through TribesPad's SDL backend (tribes_sdl_poll_event), once
 * per frame and whenever the game reads a device.
 * Headless checks can script input: STARSIEGE_INPUT=file, one event per line,
 * "<frame> key <DIK> down|up", "<frame> move <dx> <dy>",
 * "<frame> button <0-7> down|up", "<frame> wheel <notches>". */
#include "../core/guest_dinput.c"
#include <SDL3/SDL.h>
#include <time.h>

extern uint32_t tribes_sdl_poll_event(_cpu *cpu,uint32_t address);
extern uint32_t tribes_sdl_set_window_relative_mouse_mode(_cpu *cpu,uint32_t token,uint32_t value);
extern uint32_t ss_sdl_window(void);
extern uint64_t ss_frames(void);
extern uint32_t ss_pointer_to_client(int32_t dx,int32_t dy,int32_t *cx,int32_t *cy);
extern uint32_t tribes_post_message(uint64_t base,uint32_t window,uint32_t message,uint32_t wp,uint32_t lp);

/* Host input state. Keyboard objects are identified by their DIK scan code,
   mouse objects by their catalog offset (X 0, Y 4, wheel 8, buttons 12-19). */
static unsigned char keys[256],buttons[8];
static uint32_t sequence;
static int relative_mode;
typedef struct SsDeviceState {uint32_t object,cooperative,window;int32_t dx,dy,dz;} DeviceState;
static DeviceState states[8];
/* STARSIEGE_TRACE_INPUT=1 logs device calls. */
static int tracing(void){static int v=-1;if(v<0){const char *e=getenv("STARSIEGE_TRACE_INPUT");v=e&&!strcmp(e,"1");}return v;}
#define TRACE(...) do{if(tracing())fprintf(stderr,"TribesPad input: " __VA_ARGS__);}while(0)

static uint32_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*1000u+t.tv_nsec/1000000u);}
static unsigned kind(const Input *input){uint32_t type;memcpy(&type,input->info+36,4);return type&255;} /* 2 mouse, 3 keyboard */
static DeviceState *state_of(uint32_t object,int create){
 for(unsigned i=0;i<8;i++)if(states[i].object==object)return &states[i];
 if(create)for(unsigned i=0;i<8;i++)if(!states[i].object){states[i]=(DeviceState){.object=object};return &states[i];}
 return NULL;
}
static uint32_t catalog_offset(const Object *o){uint32_t v;memcpy(&v,o->bytes+20,4);return v;}
static uint32_t object_type(const Object *o){uint32_t v;memcpy(&v,o->bytes+24,4);return v;}

/* One change on every acquired device of this kind, in each format slot that
   maps the object. Called with lock held. */
static void queue(unsigned device_kind,uint32_t offset,uint32_t data){
 uint32_t stamp=now_ms(),seq=++sequence;
 for(Input *p=inputs;p;p=p->next){
  if(!p->is_device||!p->acquired||kind(p)!=device_kind||!p->format_set)continue;
  for(uint32_t i=0;i<p->format_count;i++){
   uint32_t index=p->format[i].index;if(index==UINT32_MAX||catalog_offset(&p->objects[index])!=offset)continue;
   TribesInputEvent e={p->format[i].offset,data,stamp,seq};(void)tribes_input_buffer_push(&p->buffer,&e);
  }
 }
}
static void key_change(unsigned dik,int down){
 if(!dik||dik>255)return;pthread_mutex_lock(&lock);
 if(!!keys[dik]!=!!down){keys[dik]=down?0x80:0;queue(3,dik,keys[dik]);}
 pthread_mutex_unlock(&lock);
}
/* The game's canvas draws its own cursor only after a WM_MOUSEMOVE tells it
   the pointer is over the window; it then acquires mouse0 and follows
   DirectInput motion. When that motion crosses the window edge it releases
   mouse0 and waits for the Windows pointer to come back. Windows sends
   WM_MOUSEMOVE for both; so does the host: once when the window first shows,
   and for motion while mouse0 is released (kept inside the window). */
static uint64_t pointer_base;
static void windows_pointer(int32_t dx,int32_t dy){
 int32_t x,y;uint32_t window=ss_pointer_to_client(dx,dy,&x,&y);if(!window||!pointer_base)return;
 uint32_t held=(buttons[0]?1u:0u)|(buttons[1]?2u:0u)|(buttons[2]?0x10u:0u);
 uint32_t posted=tribes_post_message(pointer_base,window,0x0200,held,((uint32_t)(uint16_t)y<<16)|(uint16_t)x);TRACE("WM_MOUSEMOVE %d,%d to 0x%x posted %u error %u\n",x,y,window,posted,tribes_get_last_error());
}
static int mouse_acquired_locked(void){for(Input *p=inputs;p;p=p->next)if(p->is_device&&p->acquired&&kind(p)==2)return 1;return 0;}
static void mouse_move(int32_t dx,int32_t dy,int32_t dz){
 pthread_mutex_lock(&lock);
 if(!mouse_acquired_locked()){pthread_mutex_unlock(&lock);if(dx||dy)windows_pointer(dx,dy);return;}
 for(Input *p=inputs;p;p=p->next)if(p->is_device&&p->acquired&&kind(p)==2){DeviceState *s=state_of(p->object,1);if(s){s->dx+=dx;s->dy+=dy;s->dz+=dz;}}
 if(dx)queue(2,0,(uint32_t)dx);if(dy)queue(2,4,(uint32_t)dy);if(dz)queue(2,8,(uint32_t)dz);
 pthread_mutex_unlock(&lock);
}
static void mouse_button(unsigned button,int down){
 if(button>7)return;pthread_mutex_lock(&lock);
 if(!!buttons[button]!=!!down){buttons[button]=down?0x80:0;queue(2,12+button,buttons[button]);}
 pthread_mutex_unlock(&lock);
}

/* SDL scancodes (USB usage IDs) to DirectInput scan codes. */
static unsigned dik_of(unsigned sc){
 static const unsigned char letters[26]={30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
 if(sc>=SDL_SCANCODE_A&&sc<=SDL_SCANCODE_Z)return letters[sc-SDL_SCANCODE_A];
 if(sc>=SDL_SCANCODE_1&&sc<=SDL_SCANCODE_0)return 2+(sc-SDL_SCANCODE_1);
 if(sc>=SDL_SCANCODE_F1&&sc<=SDL_SCANCODE_F10)return 59+(sc-SDL_SCANCODE_F1);
 switch(sc){
 case SDL_SCANCODE_RETURN:return 0x1c;case SDL_SCANCODE_ESCAPE:return 0x01;case SDL_SCANCODE_BACKSPACE:return 0x0e;case SDL_SCANCODE_TAB:return 0x0f;
 case SDL_SCANCODE_SPACE:return 0x39;case SDL_SCANCODE_MINUS:return 0x0c;case SDL_SCANCODE_EQUALS:return 0x0d;case SDL_SCANCODE_LEFTBRACKET:return 0x1a;
 case SDL_SCANCODE_RIGHTBRACKET:return 0x1b;case SDL_SCANCODE_BACKSLASH:case SDL_SCANCODE_NONUSHASH:return 0x2b;case SDL_SCANCODE_SEMICOLON:return 0x27;
 case SDL_SCANCODE_APOSTROPHE:return 0x28;case SDL_SCANCODE_GRAVE:return 0x29;case SDL_SCANCODE_COMMA:return 0x33;case SDL_SCANCODE_PERIOD:return 0x34;
 case SDL_SCANCODE_SLASH:return 0x35;case SDL_SCANCODE_CAPSLOCK:return 0x3a;case SDL_SCANCODE_F11:return 0x57;case SDL_SCANCODE_F12:return 0x58;
 case SDL_SCANCODE_PRINTSCREEN:return 0xb7;case SDL_SCANCODE_SCROLLLOCK:return 0x46;case SDL_SCANCODE_PAUSE:return 0xc5;case SDL_SCANCODE_INSERT:return 0xd2;
 case SDL_SCANCODE_HOME:return 0xc7;case SDL_SCANCODE_PAGEUP:return 0xc9;case SDL_SCANCODE_DELETE:return 0xd3;case SDL_SCANCODE_END:return 0xcf;
 case SDL_SCANCODE_PAGEDOWN:return 0xd1;case SDL_SCANCODE_RIGHT:return 0xcd;case SDL_SCANCODE_LEFT:return 0xcb;case SDL_SCANCODE_DOWN:return 0xd0;
 case SDL_SCANCODE_UP:return 0xc8;case SDL_SCANCODE_NUMLOCKCLEAR:return 0x45;case SDL_SCANCODE_KP_DIVIDE:return 0xb5;case SDL_SCANCODE_KP_MULTIPLY:return 0x37;
 case SDL_SCANCODE_KP_MINUS:return 0x4a;case SDL_SCANCODE_KP_PLUS:return 0x4e;case SDL_SCANCODE_KP_ENTER:return 0x9c;case SDL_SCANCODE_KP_1:return 0x4f;
 case SDL_SCANCODE_KP_2:return 0x50;case SDL_SCANCODE_KP_3:return 0x51;case SDL_SCANCODE_KP_4:return 0x4b;case SDL_SCANCODE_KP_5:return 0x4c;
 case SDL_SCANCODE_KP_6:return 0x4d;case SDL_SCANCODE_KP_7:return 0x47;case SDL_SCANCODE_KP_8:return 0x48;case SDL_SCANCODE_KP_9:return 0x49;
 case SDL_SCANCODE_KP_0:return 0x52;case SDL_SCANCODE_KP_PERIOD:return 0x53;case SDL_SCANCODE_NONUSBACKSLASH:return 0x56;
 case SDL_SCANCODE_LCTRL:return 0x1d;case SDL_SCANCODE_LSHIFT:return 0x2a;case SDL_SCANCODE_LALT:return 0x38;case SDL_SCANCODE_LGUI:return 0xdb;
 case SDL_SCANCODE_RCTRL:return 0x9d;case SDL_SCANCODE_RSHIFT:return 0x36;case SDL_SCANCODE_RALT:return 0xb8;case SDL_SCANCODE_RGUI:return 0xdc;
 default:return 0;
 }
}

/* Scripted input (headless checks). */
typedef struct {uint64_t frame;char what;int32_t a,b;} Scripted;
static Scripted *script;static unsigned script_count,script_next;static int script_loaded;
static void load_script(void){
 script_loaded=1;const char *path=getenv("STARSIEGE_INPUT");if(!path)return;
 FILE *f=fopen(path,"r");if(!f){fprintf(stderr,"TribesPad: STARSIEGE_INPUT %s unreadable\n",path);abort();}
 char line[256];while(fgets(line,sizeof line,f)){
  unsigned long long frame;char word[16],extra[16]="";long a=0,b=0;
  int n=sscanf(line,"%llu %15s %li %15s",&frame,word,&a,extra);if(n<3||line[0]=='#')continue;
  Scripted s={frame,word[0],(int32_t)a,0};
  if(!strcmp(word,"move"))b=strtol(extra,NULL,0);else b=!strcmp(extra,"down");s.b=(int32_t)b;
  if(!strchr("kmbw",s.what)){fprintf(stderr,"TribesPad: STARSIEGE_INPUT bad line: %s",line);abort();}
  Scripted *grown=realloc(script,(script_count+1)*sizeof *grown);if(!grown)abort();script=grown;script[script_count++]=s;
 }
 fclose(f);fprintf(stderr,"TribesPad: %u scripted input events\n",script_count);
}
static void run_script(void){
 if(!script_loaded)load_script();uint64_t frame=ss_frames();
 while(script_next<script_count&&script[script_next].frame<=frame){
  Scripted s=script[script_next++];
  if(s.what=='k')key_change((unsigned)s.a,s.b);else if(s.what=='m')mouse_move(s.a,s.b,0);
  else if(s.what=='b')mouse_button((unsigned)s.a,s.b);else mouse_move(0,0,s.a*120);
 }
}

/* Drain SDL's queue. The event buffer is guest stack scratch below ESP. */
static float carry_x,carry_y;
void ss_input_pump(_cpu *cpu){
 static int pumping,introduced;if(pumping)return;pumping=1;pointer_base=cpu->_pointer_offset;
 if(!introduced&&ss_sdl_window()){introduced=1;windows_pointer(0,0);} /* the pointer starts over the new window */
 run_script();
 uint32_t saved=cpu->_esp;cpu->_esp=(saved-512)&~15u;uint32_t address=cpu->_esp+256;
 while(tribes_sdl_poll_event(cpu,address)){
  unsigned char e[128];memcpy(e,(void *)(uintptr_t)(cpu->_pointer_offset+address),128);
  uint32_t type;memcpy(&type,e,4);
  if(type==SDL_EVENT_KEY_DOWN||type==SDL_EVENT_KEY_UP){uint32_t sc;memcpy(&sc,e+24,4);if(!(type==SDL_EVENT_KEY_DOWN&&e[37]))key_change(dik_of(sc),type==SDL_EVENT_KEY_DOWN);}
  else if(type==SDL_EVENT_MOUSE_MOTION){float rx,ry;memcpy(&rx,e+36,4);memcpy(&ry,e+40,4);carry_x+=rx;carry_y+=ry;int32_t dx=(int32_t)carry_x,dy=(int32_t)carry_y;carry_x-=dx;carry_y-=dy;if(dx||dy)mouse_move(dx,dy,0);}
  else if(type==SDL_EVENT_MOUSE_BUTTON_DOWN||type==SDL_EVENT_MOUSE_BUTTON_UP){static const unsigned map[]={0,0,2,1,3,4};unsigned b=e[24];if(b>=1&&b<=5)mouse_button(map[b],type==SDL_EVENT_MOUSE_BUTTON_DOWN);}
  else if(type==SDL_EVENT_MOUSE_WHEEL){float y;memcpy(&y,e+28,4);int32_t dz=(int32_t)(y*120.0f);if(dz)mouse_move(0,0,dz);}
  else if(type==SDL_EVENT_QUIT||type==SDL_EVENT_WINDOW_CLOSE_REQUESTED){
   /* Closing the Mac window or Quit: the game's own WM_CLOSE path quits it. */
   int32_t x,y;uint32_t window=ss_pointer_to_client(0,0,&x,&y);if(window)(void)tribes_post_message(cpu->_pointer_offset,window,0x0010,0,0);}
  else if(type==SDL_EVENT_WINDOW_FOCUS_LOST){for(unsigned k=1;k<256;k++)if(keys[k])key_change(k,0);for(unsigned b=0;b<8;b++)if(buttons[b])mouse_button(b,0);}
 }
 /* The game draws its own cursor from relative motion: capture the pointer
    while a mouse is acquired exclusively (the window is hidden when headless). */
 pthread_mutex_lock(&lock);int want=0;
 for(Input *p=inputs;p;p=p->next)if(p->is_device&&p->acquired&&kind(p)==2){DeviceState *s=state_of(p->object,0);if(s&&(s->cooperative&1))want=1;}
 pthread_mutex_unlock(&lock);
 uint32_t window=ss_sdl_window();
 if(window&&want!=relative_mode){tribes_sdl_set_window_relative_mouse_mode(cpu,window,(uint32_t)want);relative_mode=want;}
 cpu->_esp=saved;pumping=0;
}

/* HRESULT SetCooperativeLevel(this, HWND, DWORD flags). */
uint32_t ss_dinput_cooperative(uint64_t base,uint32_t object,uint32_t window,uint32_t flags){
 TRACE("cooperative 0x%x window 0x%x flags 0x%x\n",object,window,flags);
 uint32_t exclusive=flags&1,nonexclusive=flags&2,foreground=flags&4,background=flags&8;
 if(!!exclusive==!!nonexclusive||!!foreground==!!background||(flags&~0x1fu))return 0x80070057;
 pthread_mutex_lock(&lock);Input *input=find(base,object);uint32_t hr=0;
 if(!input->is_device)hr=0x80070057;else{DeviceState *s=state_of(object,1);if(!s)hr=0x8007000e;else{s->cooperative=flags;s->window=window;}}
 pthread_mutex_unlock(&lock);return hr;
}
/* HRESULT Acquire(this) / Unacquire(this): DI_OK, or S_FALSE when unchanged. */
uint32_t ss_dinput_acquire(uint64_t base,uint32_t object){
 TRACE("acquire 0x%x frame %llu\n",object,(unsigned long long)ss_frames());
 pthread_mutex_lock(&lock);Input *input=find(base,object);uint32_t hr;
 if(!input->is_device)hr=0x80070057;else if(!input->format_set)hr=0x80070057;else if(input->acquired)hr=1;
 else{input->acquired=1;tribes_input_buffer_clear(&input->buffer);DeviceState *s=state_of(object,1);if(s)s->dx=s->dy=s->dz=0;hr=0;}
 pthread_mutex_unlock(&lock);return hr;
}
uint32_t ss_dinput_unacquire(uint64_t base,uint32_t object){
 TRACE("unacquire 0x%x frame %llu\n",object,(unsigned long long)ss_frames());
 pthread_mutex_lock(&lock);Input *input=find(base,object);uint32_t hr=!input->is_device?0x80070057:input->acquired?0:1;
 if(!hr)input->acquired=0;pthread_mutex_unlock(&lock);return hr;
}
/* HRESULT GetDeviceState(this, DWORD size, LPVOID data): current buttons, and
   relative axis motion since the previous call. */
uint32_t ss_dinput_state(_cpu *cpu,uint32_t object,uint32_t size,uint32_t data){
 uint64_t base=cpu->_pointer_offset;ss_input_pump(cpu);
 unsigned char *out=pointer(base,data,size);if(!out)return 0x80004003;
 pthread_mutex_lock(&lock);Input *input=find(base,object);
 if(!input->is_device||!input->format_set||size!=input->data_size){pthread_mutex_unlock(&lock);return 0x80070057;}
 if(!input->acquired){pthread_mutex_unlock(&lock);return 0x8007000c;}
 memset(out,0,size);DeviceState *s=state_of(object,1);unsigned device_kind=kind(input);
 for(uint32_t i=0;i<input->format_count;i++){
  uint32_t index=input->format[i].index;if(index==UINT32_MAX)continue;
  const Object *o=&input->objects[index];uint32_t offset=catalog_offset(o),type=object_type(o)&255,at=input->format[i].offset;
  if(type&12){unsigned char v=device_kind==3?(offset<256?keys[offset]:0):(offset>=12&&offset<20?buttons[offset-12]:0);out[at]=v;}
  else if(type&3){int32_t v=0;if(s&&device_kind==2)v=offset==0?s->dx:offset==4?s->dy:offset==8?s->dz:0;memcpy(out+at,&v,4);}
 }
 {static uint64_t last;uint64_t f=ss_frames();if(f!=last&&f%60==0){last=f;TRACE("state 0x%x size %u frame %llu\n",object,size,(unsigned long long)f);}}
 if(s)s->dx=s->dy=s->dz=0;
 pthread_mutex_unlock(&lock);return 0;
}
/* HRESULT GetDeviceData(this, DWORD object_size, DIDEVICEOBJECTDATA *rgdod,
   DWORD *inout, DWORD flags): buffered changes; DIGDD_PEEK leaves them. */
uint32_t ss_dinput_data(_cpu *cpu,uint32_t object,uint32_t object_size,uint32_t rgdod,uint32_t inout,uint32_t flags){
 uint64_t base=cpu->_pointer_offset;ss_input_pump(cpu);
 uint32_t *count=pointer(base,inout,4);if(!count)return 0x80004003;if((object_size!=16&&object_size!=20)||(flags&~1u))return 0x80070057;
 pthread_mutex_lock(&lock);Input *input=find(base,object);
 if(!input->is_device){pthread_mutex_unlock(&lock);return 0x80070057;}
 if(!input->buffer.size){pthread_mutex_unlock(&lock);return 0x80040207;} /* DIERR_NOTBUFFERED */
 if(!input->acquired){pthread_mutex_unlock(&lock);return 0x8007000c;}
 uint32_t requested=*count;
 if(rgdod&&requested&&!pointer(base,rgdod,requested*object_size)){pthread_mutex_unlock(&lock);return 0x80004003;}
 uint32_t want=requested<input->buffer.count?requested:input->buffer.count,done;int overflow=0;
 TribesInputEvent *events=rgdod&&want?calloc(want,sizeof *events):NULL;
 if(rgdod&&want&&!events){pthread_mutex_unlock(&lock);return 0x8007000e;}
 done=tribes_input_buffer_read(&input->buffer,events,rgdod?want:requested,flags&1,&overflow);
 if(rgdod)for(uint32_t i=0;i<done;i++){unsigned char *row=(unsigned char *)pointer(base,rgdod,requested*object_size)+i*object_size;memset(row,0,object_size);memcpy(row,&events[i],16);}
 uint32_t events_first_ofs=events&&done?events[0].offset:0;int32_t events_first_data=events&&done?(int32_t)events[0].data:0;free(events);
 if(done)TRACE("data 0x%x %u events frame %llu first ofs %u data %d\n",object,done,(unsigned long long)ss_frames(),rgdod?events_first_ofs:0,rgdod?events_first_data:0);
 *count=done;pthread_mutex_unlock(&lock);return overflow?1:0;
}
/* HRESULT SetEventNotification(this, HANDLE): the game polls each frame, so
   the event is accepted and never signalled. HRESULT Poll(this): keyboard and
   mouse are not polled devices (DI_NOEFFECT). */
uint32_t ss_dinput_event_notification(uint64_t base,uint32_t object,uint32_t event){
 (void)event;pthread_mutex_lock(&lock);Input *input=find(base,object);uint32_t hr=input->is_device?(input->acquired?0x800700aa:0):0x80070057;pthread_mutex_unlock(&lock);return hr;
}
uint32_t ss_dinput_poll(_cpu *cpu,uint32_t object){
 ss_input_pump(cpu);pthread_mutex_lock(&lock);Input *input=find(cpu->_pointer_offset,object);uint32_t hr=!input->is_device?0x80070057:!input->acquired?0x8007000c:1;pthread_mutex_unlock(&lock);return hr;
}
