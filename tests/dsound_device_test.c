#include "../port/core/guest_dsound.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_modules.h"
#include "../port/core/guest_memory_checks.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t),tribes_return_to_host(void),tbimp_TestSoundCallback(void);
extern SDL_AudioDeviceID tribes_dsound_test_device(uint64_t,uint32_t);
extern void tribes_dsound_test_listener_active(uint64_t,uint32_t,uint32_t *);
static _Atomic unsigned observed_frames,observed_left,observed_right;
static void SDLCALL observe_mix(void *u,const SDL_AudioSpec *spec,float *samples,int bytes){
 (void)u;unsigned left=0,right=0;for(int i=0;i<bytes/(int)sizeof(float);i+=spec->channels){
  unsigned l=(unsigned)(fabsf(samples[i])*1000000),r=(unsigned)(fabsf(samples[i+1])*1000000);if(l>left)left=l;if(r>right)right=r;
 }
 unsigned old=atomic_load(&observed_left);while(old<left&&!atomic_compare_exchange_weak(&observed_left,&old,left)){}old=atomic_load(&observed_right);while(old<right&&!atomic_compare_exchange_weak(&observed_right,&old,right)){}
 atomic_fetch_add(&observed_frames,bytes/(sizeof(float)*spec->channels));
}
static void reset_mix(void){atomic_store(&observed_frames,0);atomic_store(&observed_left,0);atomic_store(&observed_right,0);}
static unsigned char *memory;static uint32_t callback_count;static int cancel;
int tribes_sdl_has_window_handle(uint64_t base,uint32_t hwnd){return base==(uintptr_t)memory&&hwnd==0x73000001;}
void *tribes_lookup(uint32_t address){if(address==0xfffff000)return (void *)tribes_return_to_host;if(address==1)return (void *)tbimp_TestSoundCallback;void *p=tribes_dsound_native_lookup(address);assert(p);return p;}
uint32_t test_sound_callback(_cpu *cpu,uint32_t guid,uint32_t description,uint32_t driver,uint32_t context){
 assert(context==0x12345678);assert(!strcmp((char *)memory+driver,"dsound.dll"));assert(memory[description]);
 if(!callback_count)assert(!guid);else {assert(guid);uint32_t *out=(void *)(memory+0x101000);assert(!tribes_dsound_create(cpu->_pointer_offset,guid,0x101000,0));assert(!tribes_ds_release(cpu->_pointer_offset,*out));}
 callback_count++;return !cancel;
}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned n,const uint32_t *args){
 _cpu before=*cpu;assert(cpu->_esp>4*n);cpu->_esp-=4*n;memcpy(memory+cpu->_esp,args,n*4);tribes_enter(cpu,target);uint32_t r=cpu->_eax;before._eax=r;assert(!memcmp(cpu,&before,sizeof before));return r;
}
static void test_listener(_cpu *cpu,uint32_t object,int readonly){
 uint32_t desc[9]={36,0x11};memcpy(memory+0x105000,desc,sizeof desc);
 uint32_t make[]={object,0x105000,0x105100,0};assert(!invoke(cpu,TB_DS_METHOD(3),4,make));uint32_t primary=*(uint32_t *)(memory+0x105100);
 const unsigned char iid[16]={0x84,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0xb,0xe5,0x60};memcpy(memory+0x104000,iid,16);
 uint32_t query[]={primary,0x104000,0x104020};assert(!invoke(cpu,TB_DSB_METHOD(0),3,query));uint32_t listener=*(uint32_t *)(memory+0x104020);assert(listener&&listener!=primary);
 uint32_t table=*(uint32_t *)(memory+listener);for(unsigned i=0;i<18;i++)assert(*(uint32_t *)(memory+table+4*i)==TB_DSL_METHOD(i));
 unsigned char unknown[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};memcpy(memory+0x104000,unknown,16);query[0]=listener;
 assert(!invoke(cpu,TB_DSL_METHOD(0),3,query));assert(*(uint32_t *)(memory+0x104020)==primary);assert(invoke(cpu,TB_DSB_METHOD(2),1,&primary)==2);
 assert(invoke(cpu,TB_DSL_METHOD(1),1,&listener)==3);assert(invoke(cpu,TB_DSL_METHOD(2),1,&listener)==2);
 uint32_t get[]={listener,0x108000};uint32_t *state=(void *)(memory+0x108000);*state=64;
 if(readonly)assert(tribes_memory_permissions_set(0x108000,4096,1));
 assert(!invoke(cpu,TB_DSL_METHOD(3),2,get));assert(state[0]==64&&state[9]==0x3f800000&&state[11]==0x3f800000&&state[13]==0x3f800000);
 *state=63;assert(invoke(cpu,TB_DSL_METHOD(3),2,get)==0x80070057u);*state=64;
 /* Requested deferred fields are readable but not yet applied. Immediate
    velocity must not accidentally publish the deferred listener position. */
 uint32_t setpos[]={listener,0x3f800000,0x40000000,0x40400000,1};assert(!invoke(cpu,TB_DSL_METHOD(14),5,setpos));
 assert(!invoke(cpu,TB_DSL_METHOD(7),2,get));assert(state[0]==0x3f800000&&state[1]==0x40000000&&state[2]==0x40400000);
 uint32_t applied[16];tribes_dsound_test_listener_active(cpu->_pointer_offset,listener,applied);assert(applied[1]==0&&applied[2]==0);
 setpos[4]=0;assert(!invoke(cpu,TB_DSL_METHOD(16),5,setpos));tribes_dsound_test_listener_active(cpu->_pointer_offset,listener,applied);assert(!applied[1]&&applied[4]==0x3f800000);
 assert(!invoke(cpu,TB_DSL_METHOD(17),1,&listener));tribes_dsound_test_listener_active(cpu->_pointer_offset,listener,applied);assert(applied[1]==0x3f800000&&applied[2]==0x40000000);
 uint32_t scalar[]={listener,0x40000000,0};
 unsigned setters[]={11,12,15},getters[]={4,5,8};for(unsigned i=0;i<3;i++){assert(!invoke(cpu,TB_DSL_METHOD(setters[i]),3,scalar));assert(!invoke(cpu,TB_DSL_METHOD(getters[i]),2,get));assert(state[0]==scalar[1]);}
 scalar[1]=0x7fc00000;assert(invoke(cpu,TB_DSL_METHOD(11),3,scalar)==0x80070057u);scalar[1]=0;assert(invoke(cpu,TB_DSL_METHOD(11),3,scalar)==0x80070057u);scalar[1]=0x41300000;assert(invoke(cpu,TB_DSL_METHOD(12),3,scalar)==0x80070057u);
 uint32_t orientation[]={listener,0x3f800000,0,0,0,0x3f800000,0,0};assert(!invoke(cpu,TB_DSL_METHOD(13),8,orientation));
 uint32_t getorient[]={listener,0x108000,0x10800c};assert(!invoke(cpu,TB_DSL_METHOD(6),3,getorient));assert(state[0]==0x3f800000&&state[4]==0x3f800000);
 orientation[4]=0x3f800000;orientation[5]=0;assert(invoke(cpu,TB_DSL_METHOD(13),8,orientation)==0x80070057u);
 *state=64;assert(!invoke(cpu,TB_DSL_METHOD(3),2,get));uint32_t setall[]={listener,0x108000,1};state[1]=0x40800000;assert(!invoke(cpu,TB_DSL_METHOD(10),3,setall));
 assert(!invoke(cpu,TB_DSL_METHOD(17),1,&listener));tribes_dsound_test_listener_active(cpu->_pointer_offset,listener,applied);assert(applied[1]==0x40800000);
 assert(!invoke(cpu,TB_DSL_METHOD(9),2,get));assert(state[0]==0x3f800000);
 /* Releasing the buffer interface cannot free its surviving listener. */
 assert(invoke(cpu,TB_DSB_METHOD(2),1,&primary)==1);assert(!invoke(cpu,TB_DSL_METHOD(7),2,get));assert(state[0]==0x40800000);
 assert(!invoke(cpu,TB_DSL_METHOD(2),1,&listener));
}

int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory+0x100000,0x100000,PROT_READ|PROT_WRITE));assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));
 _cpu cpu={0};cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0x1f0000;cpu.stack_bottom=memory+0x180000;cpu.stack_top=memory+0x200000;cpu._eax=123;cpu._eflags=0x202;
 assert(tribes_dsound_configure(cpu._pointer_offset));strcpy((char *)memory+0x100000,"dsound.dll");assert(tribes_load_library(cpu._pointer_offset,0x100000)==TB_DSOUND_MODULE);
 strcpy((char *)memory+0x100100,"DirectSoundCreate");assert(tribes_get_proc_address(cpu._pointer_offset,TB_DSOUND_MODULE,0x100100)==TB_DS_CREATE);
 uint32_t create[]={0,0x101000,0};assert(invoke(&cpu,TB_DS_CREATE,3,create)==0x88780078u);
 assert(SDL_Init(SDL_INIT_AUDIO));assert(!strcmp(SDL_GetCurrentAudioDriver(),"coreaudio"));
 if(argc>1&&!strcmp(argv[1],"readonly"))assert(tribes_memory_permissions_set(0x101000,4096,1));
 if(argc>1&&!strcmp(argv[1],"unreadable-guid")){create[0]=0x102000;assert(tribes_memory_permissions_set(0x102000,4096,0));}
 assert(!invoke(&cpu,TB_DS_CREATE,3,create));uint32_t object=*(uint32_t *)(memory+0x101000);assert(object>=0x30000000);
 uint32_t table;memcpy(&table,memory+object,4);assert(table==object+4);for(unsigned i=0;i<11;i++){uint32_t token;memcpy(&token,memory+table+4*i,4);assert(token==TB_DS_METHOD(i)&&tribes_dsound_native_lookup(token));}
 uint32_t caps[]={object,0x103000};*(uint32_t *)(memory+0x103000)=96;memset(memory+0x103060,0xac,4);assert(!invoke(&cpu,TB_DS_METHOD(4),2,caps));uint32_t *c=(void *)(memory+0x103000);assert(!(c[1]&0x20));assert(c[2]==100&&c[3]==200000&&c[4]==1);for(unsigned i=5;i<24;i++)assert(!c[i]);assert(memory[0x103060]==0xac);
 *(uint32_t *)(memory+0x103000)=95;assert(invoke(&cpu,TB_DS_METHOD(4),2,caps)==0x80070057u);
 uint32_t coop[]={object,0,2};assert(invoke(&cpu,TB_DS_METHOD(6),3,coop)==0x80070057u);coop[1]=0x73000001;coop[2]=4;assert(invoke(&cpu,TB_DS_METHOD(6),3,coop)==0x80004001u);coop[2]=2;assert(!invoke(&cpu,TB_DS_METHOD(6),3,coop));assert(!invoke(&cpu,TB_DS_METHOD(7),1,&object));
 uint32_t init[]={object,0};assert(invoke(&cpu,TB_DS_METHOD(10),2,init)==0x88780082u);
 const unsigned char iid[16]={0x83,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0xb,0xe5,0x60};memcpy(memory+0x104000,iid,16);uint32_t query[]={object,0x104000,0x104020};assert(!invoke(&cpu,TB_DS_METHOD(0),3,query));assert(*(uint32_t *)(memory+0x104020)==object);assert(invoke(&cpu,TB_DS_METHOD(2),1,&object)==1);
 memory[0x104000]++;assert(invoke(&cpu,TB_DS_METHOD(0),3,query)==0x80004002u);assert(!*(uint32_t *)(memory+0x104020));
 /* Original primary descriptor and COM argument shapes. PCM input is exactly
    16 bytes at a permission boundary; SetFormat must not read cbSize. */
 uint32_t descriptor[9]={36,1};memcpy(memory+0x105000,descriptor,sizeof descriptor);
 uint32_t make[]={object,0x105000,0x105100,0};assert(!invoke(&cpu,TB_DS_METHOD(3),4,make));uint32_t buffer=*(uint32_t *)(memory+0x105100);
 /* A 3D primary probe fails without returning/referencing the existing
    PCM primary. Ordinary primary creation below still shares its lifetime. */
 descriptor[1]=0x11;memcpy(memory+0x105000,descriptor,sizeof descriptor);
 assert(invoke(&cpu,TB_DS_METHOD(3),4,make)==0x8878001eu);assert(!*(uint32_t *)(memory+0x105100));
 descriptor[2]=4;memcpy(memory+0x105000,descriptor,sizeof descriptor);assert(invoke(&cpu,TB_DS_METHOD(3),4,make)==0x80070057u);
 descriptor[1]=1;descriptor[2]=0;memcpy(memory+0x105000,descriptor,sizeof descriptor);
 uint32_t bt=*(uint32_t *)(memory+buffer);for(unsigned i=0;i<21;i++)assert(*(uint32_t *)(memory+bt+4*i)==TB_DSB_METHOD(i));
 assert(!invoke(&cpu,TB_DS_METHOD(3),4,make));assert(*(uint32_t *)(memory+0x105100)==buffer);assert(invoke(&cpu,TB_DSB_METHOD(2),1,&buffer)==1);
 uint32_t get[]={buffer,0,0,0x105200};assert(!invoke(&cpu,TB_DSB_METHOD(5),4,get));assert(*(uint32_t *)(memory+0x105200)==18);
 get[1]=0x105300;get[2]=17;assert(invoke(&cpu,TB_DSB_METHOD(5),4,get)==0x80070057u);get[2]=18;
 uint16_t wave[8]={1,1,22050,0,22050,0,1,8};memcpy(memory+0x106ff0,wave,16);assert(tribes_memory_permissions_set(0x107000,4096,0));uint32_t set[]={buffer,0x106ff0};
 coop[2]=1;assert(!invoke(&cpu,TB_DS_METHOD(6),3,coop));assert(invoke(&cpu,TB_DSB_METHOD(14),2,set)==0x88780046u);coop[2]=2;assert(!invoke(&cpu,TB_DS_METHOD(6),3,coop));
 assert(!invoke(&cpu,TB_DSB_METHOD(14),2,set));memset(memory+0x105300,0xac,20);
 if(argc>1&&!strcmp(argv[1],"readonly-format"))assert(tribes_memory_permissions_set(0x105000,4096,1));
 assert(!invoke(&cpu,TB_DSB_METHOD(5),4,get));assert(!memcmp(memory+0x105300,wave,16));assert(memory[0x105310]==0&&memory[0x105311]==0&&memory[0x105312]==0xac);
 uint32_t status[]={buffer,0x105400},play[]={buffer,0,0,0};assert(invoke(&cpu,TB_DSB_METHOD(12),4,play)==0x80070057u);play[3]=1;
 assert(!invoke(&cpu,TB_DSB_METHOD(12),4,play));assert(!invoke(&cpu,TB_DSB_METHOD(9),2,status));assert(*(uint32_t *)(memory+0x105400)==5);
 assert(!invoke(&cpu,TB_DSB_METHOD(18),1,&buffer));assert(!invoke(&cpu,TB_DSB_METHOD(9),2,status));assert(!*(uint32_t *)(memory+0x105400));
 assert(!invoke(&cpu,TB_DSB_METHOD(2),1,&buffer));
 test_listener(&cpu,object,argc>1&&!strcmp(argv[1],"readonly-listener"));
 /* Real requested secondary ring, original-width Lock/Unlock, and real
    CoreAudio postmix observation. Low-amplitude PCM keeps the probe quiet. */
 uint16_t stereo[8]={1,2,22050,0,22664,1,4,16}; /* 88200 bytes/sec */
 memcpy(memory+0x106ff0,stereo,16);descriptor[1]=0x180e0;descriptor[2]=136704;descriptor[4]=0x106ff0;memcpy(memory+0x105000,descriptor,sizeof descriptor);
 assert(!invoke(&cpu,TB_DS_METHOD(3),4,make));buffer=*(uint32_t *)(memory+0x105100);
 uint32_t bcaps[]={buffer,0x105500};*(uint32_t *)(memory+0x105500)=20;assert(!invoke(&cpu,TB_DSB_METHOD(3),2,bcaps));assert(*(uint32_t *)(memory+0x105508)==136704);
 uint32_t acquire[]={buffer,136700,8,0x105600,0x105604,0x105608,0x10560c,0};
 if(argc>1&&!strcmp(argv[1],"readonly-lock"))assert(tribes_memory_permissions_set(0x105000,4096,1));
 assert(!invoke(&cpu,TB_DSB_METHOD(11),8,acquire));uint32_t *spans=(void *)(memory+0x105600);assert(spans[1]==4&&spans[3]==4&&spans[0]==spans[2]+136700);uint32_t data=spans[2];
 uint32_t unlock[]={buffer,spans[0],spans[1],spans[2],spans[3]};assert(invoke(&cpu,TB_DSB_METHOD(11),8,acquire)==0x80070057u);unlock[1]++;assert(invoke(&cpu,TB_DSB_METHOD(19),5,unlock)==0x80070057u);unlock[1]--;
 if(argc>1&&!strcmp(argv[1],"unreadable-pcm"))assert(tribes_memory_permissions_set(data,4096,0));
 assert(!invoke(&cpu,TB_DSB_METHOD(19),5,unlock));acquire[1]=0;acquire[2]=136704;acquire[7]=2;assert(!invoke(&cpu,TB_DSB_METHOD(11),8,acquire));assert(spans[0]==data&&spans[1]==136704&&!spans[2]&&!spans[3]);
 for(unsigned i=0;i<136704/2;i++)((int16_t *)(memory+data))[i]=512;
 uint32_t whole[]={buffer,data,136704,0,0};assert(!invoke(&cpu,TB_DSB_METHOD(19),5,whole));
 SDL_AudioDeviceID device=tribes_dsound_test_device(cpu._pointer_offset,object);assert(SDL_SetAudioPostmixCallback(device,observe_mix,NULL));reset_mix();
 play[0]=buffer;play[3]=1;assert(!invoke(&cpu,TB_DSB_METHOD(12),4,play));assert(!invoke(&cpu,TB_DSB_METHOD(12),4,play));
 /* Exercise original producer writes against the active native consumer. */
 for(unsigned tick=0;tick<24;tick++){
  acquire[1]=(tick*256)%136704;acquire[2]=256;acquire[7]=0;assert(!invoke(&cpu,TB_DSB_METHOD(11),8,acquire));
  for(unsigned i=0;i<spans[1]/2;i++)((int16_t *)(memory+spans[0]))[i]=512;
  uint32_t commit[]={buffer,spans[0],spans[1],spans[2],spans[3]};assert(!invoke(&cpu,TB_DSB_METHOD(19),5,commit));SDL_Delay(3);
 }
 SDL_Delay(120);
 uint32_t cursors[]={buffer,0x105700,0x105704};assert(!invoke(&cpu,TB_DSB_METHOD(4),3,cursors));uint32_t at=*(uint32_t *)(memory+0x105700);assert(at>0&&at<136704&&!(at%4));assert(atomic_load(&observed_frames)>0&&atomic_load(&observed_left)>12000&&atomic_load(&observed_right)>12000);
 assert(!invoke(&cpu,TB_DSB_METHOD(18),1,&buffer));assert(!invoke(&cpu,TB_DSB_METHOD(4),3,cursors));at=*(uint32_t *)(memory+0x105700);SDL_Delay(60);assert(!invoke(&cpu,TB_DSB_METHOD(4),3,cursors));assert(at==*(uint32_t *)(memory+0x105700));
 uint32_t ctl[]={buffer,10000};assert(!invoke(&cpu,TB_DSB_METHOD(16),2,ctl));ctl[1]=(uint32_t)-600;assert(!invoke(&cpu,TB_DSB_METHOD(15),2,ctl));ctl[1]=0;assert(!invoke(&cpu,TB_DSB_METHOD(13),2,ctl));
 reset_mix();assert(!invoke(&cpu,TB_DSB_METHOD(12),4,play));SDL_Delay(120);assert(atomic_load(&observed_left)==0&&atomic_load(&observed_right)>6500&&atomic_load(&observed_right)<10000);
 ctl[1]=44100;assert(!invoke(&cpu,TB_DSB_METHOD(17),2,ctl));ctl[1]=0x105800;assert(!invoke(&cpu,TB_DSB_METHOD(8),2,ctl));assert(*(uint32_t *)(memory+0x105800)==44100);
 /* Seek near the end and prove real looping wrap, then a non-looping end. */
 assert(!invoke(&cpu,TB_DSB_METHOD(18),1,&buffer));ctl[1]=136704-400;assert(!invoke(&cpu,TB_DSB_METHOD(13),2,ctl));assert(!invoke(&cpu,TB_DSB_METHOD(12),4,play));SDL_Delay(80);assert(!invoke(&cpu,TB_DSB_METHOD(4),3,cursors));assert(*(uint32_t *)(memory+0x105700)<136704-400);
 assert(!invoke(&cpu,TB_DSB_METHOD(18),1,&buffer));assert(!invoke(&cpu,TB_DSB_METHOD(13),2,ctl));play[3]=0;assert(!invoke(&cpu,TB_DSB_METHOD(12),4,play));SDL_Delay(100);status[0]=buffer;assert(!invoke(&cpu,TB_DSB_METHOD(9),2,status));assert(!*(uint32_t *)(memory+0x105400));
 assert(SDL_SetAudioPostmixCallback(device,NULL,NULL));play[3]=1;assert(!invoke(&cpu,TB_DSB_METHOD(12),4,play));SDL_Delay(25);assert(!invoke(&cpu,TB_DSB_METHOD(2),1,&buffer));
 uint32_t enumeration[]={1,0x12345678};cancel=1;assert(!invoke(&cpu,TB_DS_ENUMERATE,2,enumeration));assert(callback_count==1);cancel=0;callback_count=0;assert(!invoke(&cpu,TB_DS_ENUMERATE,2,enumeration));assert(callback_count>=2);
 assert(invoke(&cpu,TB_DS_METHOD(1),1,&object)==2);assert(invoke(&cpu,TB_DS_METHOD(2),1,&object)==1);assert(!invoke(&cpu,TB_DS_METHOD(2),1,&object));
 create[2]=1;assert(invoke(&cpu,TB_DS_CREATE,3,create)==0x80040110u);assert(!*(uint32_t *)(memory+0x101000));create[2]=0;create[0]=0x104000;assert(invoke(&cpu,TB_DS_CREATE,3,create)==0x88780078u);
 SDL_Quit();puts("PASS: real CoreAudio device lifetime, enumeration/reentrant creation, COM/ABI preservation, primary stream format/start-stop, caps and cooperation errors");return 0;
}
