#include "../port/core/guest_registry.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
#ifdef CHECKED_TEST
extern void registry_guest_sysctl(void *),registry_write_sysctl(void *),registry_wrong_sysctl(void *);
#endif
static unsigned char *memory;static uint64_t base;
static uint32_t read32(uint32_t p){uint32_t n;memcpy(&n,memory+p,4);return n;}
static void put32(uint32_t p,uint32_t n){memcpy(memory+p,&n,4);}
static void wide(uint32_t p,const char *text){do{uint16_t c=(unsigned char)*text;memcpy(memory+p,&c,2);p+=2;}while(*text++);}
static uint32_t call(unsigned service,const uint32_t *args,unsigned count){
 _cpu cpu;memset(&cpu,0x7d,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0xc003;
 memcpy(memory+cpu._esp,args,count*4);_cpu expected=cpu;tribes_set_last_error(0x76543210);
 tribes_enter(&cpu,service);expected._eax=cpu._eax;expected._esp+=count*4;
 assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0xc003,args,count*4)&&tribes_get_last_error()==0x76543210);return cpu._eax;
}
static uint32_t open_key(uint32_t parent,const char *path,unsigned rights){
 wide(0x1001,path);uint32_t a[]={parent,0x1001,0,rights,0x2001};put32(0x2001,0xdeadbeef);
 assert(!call(1,a,5));return read32(0x2001);
}
static uint32_t query(uint32_t key,const char *name,uint32_t type,uint32_t data,uint32_t size){wide(0x3001,name);uint32_t a[]={key,0x3001,0,type,data,size};return call(2,a,6);}
static void close_key(uint32_t key){assert(!call(3,&key,1));}
static void *worker(void *arg){
 uint32_t at=0x40000+(uint32_t)(uintptr_t)arg*0x2000;wide(at,"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
 wide(at+0x200,"ProcessorNameString");
 for(unsigned i=0;i<32;i++){
  assert(!tribes_reg_open_key_ex_w(base,0x80000002,at,0,1,at+0x400));uint32_t h=read32(at+0x400);put32(at+0x404,512);
  assert(!tribes_reg_query_value_ex_w(base,h,at+0x200,0,at+0x408,at+0x800,at+0x404));assert(read32(at+0x408)==1);
  assert(!tribes_reg_close_key(base,h));assert(tribes_reg_close_key(base,h)==6);
 }
 return NULL;
}
#if defined(CHECKED_TEST) && !defined(SIMULATOR_TEST)
static void rejected(unsigned mode,uint32_t key){
 unsigned char saved[4096];memcpy(saved,memory+0x18000,4096);pid_t child=fork();assert(child>=0);
 if(!child){
  if(mode==0){assert(tribes_memory_permissions_set(0x18000,4096,1));tribes_reg_open_key_ex_w(base,0x80000002,0x1001,0,1,0x18000);}
  if(mode==1){put32(0x4001,512);assert(tribes_memory_permissions_set(0x18000,4096,1));query(key,"ProcessorNameString",0,0x18000,0x4001);}
  if(mode==2){put32(0x4001,512);assert(tribes_memory_permissions_set(0x19000,4096,1));query(key,"ProcessorNameString",0,0x18ffe,0x4001);}
  if(mode==3){assert(tribes_memory_permissions_set(0x18000,4096,1));query(key,"ProcessorNameString",0,0,0x18000);}
  if(mode==4){assert(tribes_memory_permissions_set(0x18000,4096,1));query(key,"ProcessorNameString",0x18000,0,0);}
  if(mode==5){wide(0x18ffe,"AB");assert(tribes_memory_permissions_set(0x19000,4096,0));tribes_reg_query_value_ex_w(base,key,0x18ffe,0,0,0,0);}
  if(mode==6)open_key(0x80000002,"SOFTWARE\\Unimplemented",1);
  if(mode==7)registry_guest_sysctl(memory+0x18000);
  if(mode==8){int n=1;registry_write_sysctl(&n);}
  if(mode==9){int n=1;registry_wrong_sysctl(&n);}
  if(mode==10){strcpy((char *)memory+0x1001,"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");assert(tribes_memory_permissions_set(0x18000,4096,1));tribes_reg_open_key_ex_a(base,0x80000002,0x1001,0,1,0x18000);}
  if(mode==11){strcpy((char *)memory+0x3001,"ProcessorNameString");put32(0x4001,512);assert(tribes_memory_permissions_set(0x18000,4096,1));tribes_reg_get_value_a(base,key,0,0x3001,2,0,0x18000,0x4001);}
  if(mode==12){strcpy((char *)memory+0x18fff,"AB");assert(tribes_memory_permissions_set(0x19000,4096,0));tribes_reg_get_value_a(base,key,0,0x18fff,0,0,0,0);}
  if(mode==13){strcpy((char *)memory+0x3001,"MissingValue");put32(0x4001,4);assert(tribes_memory_permissions_set(0x18000,4096,1));tribes_reg_get_value_a(base,key,0,0x3001,0x20000010,0,0x18000,0x4001);}
  if(mode==14||mode==15)open_key(mode==14?0x80000001:0x80000002,"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts\\Unimplemented",1);
  _exit(90);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 /* The unreadable-input test writes its input before setting page permissions. */
 if(mode!=5&&mode!=12)assert(!memcmp(saved,memory+0x18000,4096));
}
#endif
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 char brand[256];size_t bn=sizeof brand;assert(!sysctlbyname("machdep.cpu.brand_string",brand,&bn,NULL,0)&&bn&&bn<=256&&!brand[bn-1]);
 uint64_t hz=0;size_t hn=sizeof hz;int hr=sysctlbyname("hw.cpufrequency",&hz,&hn,NULL,0),he=errno;
 uint32_t root=open_key(0x80000002,"",1);assert(root==0x80000002);close_key(root);
 uint32_t parent=open_key(root,"hardware\\description\\SYSTEM\\centralprocessor\\",0x20019);
 unsigned count=(unsigned)sysconf(_SC_NPROCESSORS_ONLN);if(count>32)count=32;uint32_t last=0;
 for(unsigned i=0;i<count;i++){
  char index[16];snprintf(index,sizeof index,"%u",i);uint32_t key=open_key(parent,index,1);assert(key!=last&&key>=0xe5000001u&&key<0xe6000000u);last=key;
  assert(!query(key,"pRoCeSsOrNaMeStRiNg",0x4001,0,0x4005));assert(read32(0x4001)==1&&read32(0x4005)==2*bn);
  for(uint32_t capacity=0;capacity<=2*bn+2;capacity++){
   memset(memory+0x5000,0xa5,1024);put32(0x4005,capacity);uint32_t r=query(key,"ProcessorNameString",0x4001,0x5001,0x4005);
   assert(r==(capacity<2*bn?234u:0u)&&read32(0x4005)==2*bn&&read32(0x4001)==1);
   if(!r)for(unsigned j=0;j<bn;j++){assert(memory[0x5001+2*j]==(unsigned char)brand[j]);assert(!memory[0x5002+2*j]);}
   assert(memory[0x5000]==0xa5&&memory[0x5001+(r?0:2*bn)]==0xa5);
  }
  assert(!query(key,"ProcessorNameString",0x4001,0,0));
  put32(0x4005,4);uint32_t r=query(key,"~MHz",0x4001,0x5001,0x4005);
  if(!hr&&hn==8&&hz>=1000000){assert(!r&&read32(0x4001)==4&&read32(0x5001)==hz/1000000&&read32(0x4005)==4);}
  else {assert((hr&&he==ENOENT)||(!hr&&(!hn||!hz)));assert(!r&&read32(0x4001)==4&&read32(0x5001)==2400&&read32(0x4005)==4);}
  assert(query(key,"MissingValue",0,0,0)==2&&query(key,"",0,0,0)==2);
  assert(query(key,"ProcessorNameString",0,0x5001,0)==87);
  close_key(key);assert(call(3,&key,1)==6&&query(key,"ProcessorNameString",0,0,0)==6);
 }
 uint32_t key=open_key(parent,"0",0);assert(query(key,"ProcessorNameString",0,0,0)==5);close_key(key);
 key=open_key(parent,"0",1);assert(tribes_reg_close_key(base+0x1000,key)==6);
 wide(0x1001,"9999");uint32_t absent[]={parent,0x1001,0,1,0x2001};put32(0x2001,0xabcd);assert(call(1,absent,5)==2&&read32(0x2001)==0xabcd);
 absent[0]=0x123;assert(call(1,absent,5)==6);absent[0]=parent;absent[3]=2;assert(call(1,absent,5)==5);
 close_key(parent);assert(!query(key,"ProcessorNameString",0,0,0));
 /* ANSI path and value ABI share real hardware backing. The Apple profile
    has no Windows CursorSize registry key; preserve the caller's fallback. */
 /* The Apple profile contains game files, not the Windows installer's key. */
 for(unsigned encoding=0;encoding<2;encoding++){
  const char *name="software\\Sierra On-Line\\Tribes2\\";
  if(encoding)wide(0x1001,name);else strcpy((char *)memory+0x1001,name);
  uint32_t args[]={0x80000002,0x1001,0,1,0x2001};put32(0x2001,0xa1b2c3d4);
  assert(call(encoding?1:4,args,5)==2&&read32(0x2001)==0xa1b2c3d4);
 }
 /* Reached font-loader leaves are genuinely absent in both Apple profile
    roots. Missing-key return must preserve the caller's handle and neighbors,
    including the original read rights and ANSI/wide mixed-case lookup. */
 const char *font_paths[]={"software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes","SOFTWARE\\microsoft\\Windows NT\\CurrentVersion\\Fonts\\"};
 for(unsigned root_index=0;root_index<2;root_index++)for(unsigned encoding=0;encoding<2;encoding++)for(unsigned i=0;i<2;i++){
  if(encoding)wide(0x1001,font_paths[i]);else strcpy((char *)memory+0x1001,font_paths[i]);
  memset(memory+0x2000,0xa1,6);uint32_t args[]={0x80000001+root_index,0x1001,0,0x20019,0x2001};
  assert(call(encoding?1:4,args,5)==2&&read32(0x2001)==0xa1a1a1a1&&memory[0x2000]==0xa1&&memory[0x2005]==0xa1);
 }
 strcpy((char *)memory+0x1001,"software\\microsoft\\accessibility");uint32_t ua[]={0x80000001,0x1001,0,0x20019,0x2001};put32(0x2001,0xabcdef);assert(call(4,ua,5)==2&&read32(0x2001)==0xabcdef);
 uint32_t bad=0;assert(call(3,&bad,1)==6);
 strcpy((char *)memory+0x3001,"CursorSize");uint32_t ga[]={0x80000001,0x1001,0x3001,0x20000010,0x4001,0x5001,0x4005};put32(0x4005,4);put32(0x5001,0xa5a5a5a5);assert(call(5,ga,7)==2&&!read32(0x5001));
 strcpy((char *)memory+0x1001,"HARDWARE\\DESCRIPTION\\SYSTEM\\CentralProcessor\\0");ua[0]=0x80000002;assert(!call(4,ua,5));uint32_t ansi=read32(0x2001);
 strcpy((char *)memory+0x3001,"ProcessorNameString");ga[0]=ansi;ga[1]=0;ga[3]=2;
 for(uint32_t n=0;n<=bn+1;n++){put32(0x4005,n);memset(memory+0x5000,0xa5,512);assert(call(5,ga,7)==(n<bn?234u:0u));assert(read32(0x4005)==bn&&read32(0x4001)==1);if(n>=bn)assert(!strcmp((char *)memory+0x5001,brand));assert(memory[0x5000]==0xa5&&memory[0x5001+(n<bn?0:bn)]==0xa5);}
 ga[3]=0x20000010;put32(0x4005,8);memset(memory+0x5001,0xa5,8);assert(call(5,ga,7)==1630);for(unsigned i=0;i<8;i++)assert(!memory[0x5001+i]);
 strcpy((char *)memory+0x3001,"~MHz");ga[3]=0x10;put32(0x4005,4);assert(!call(5,ga,7)&&read32(0x4001)==4&&read32(0x4005)==4);
 ga[0]=0x80000002;ga[1]=0x1001;assert(!call(5,ga,7));ga[3]=0x30010;assert(call(5,ga,7)==87);
 close_key(ansi);ga[0]=ansi;ga[1]=0;ga[3]=0x10;assert(call(5,ga,7)==6);
 pthread_t threads[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
#if defined(CHECKED_TEST) && !defined(SIMULATOR_TEST)
 wide(0x1001,"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
 for(unsigned i=0;i<16;i++)rejected(i,key);
#endif
 close_key(key);printf("PASS: %u real CPU keys; brand=%s; frequency=%s; UTF-16 size/full/short buffers, emitted ABI, rights, stale handles, 128 concurrent lifetimes; guards when enabled\n",count,brand,hr||!hz?"2400 MHz compatibility fallback (not measured)":"host-reported");return 0;
}
