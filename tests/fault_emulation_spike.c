/* Feasibility spike, not linked into TribesPad: can MMU-enforced guest pages
   plus a fault handler preserve exact 4 KiB permissions on 16 KiB hosts?
   Uniform host pages get native protections (no software check at all).
   A host page whose 4 KiB subpages differ is PROT_NONE in the guest view;
   its faults are decoded, checked against the 4 KiB table and either
   aborted (violation) or emulated through a private RW alias and resumed. */
#include <assert.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define HOST 16384u
#define SPAN (4u*HOST)
static unsigned char *guest,*alias;
static unsigned char perm[SPAN/4096]; /* bit0 read, bit1 write */
static _Atomic unsigned long emulated;
static void violation(uintptr_t offset,int is_write){
 char m[96];int n=snprintf(m,sizeof m,"checked guest memory violation at 0x%lx %s\n",(unsigned long)offset,is_write?"write":"read");
 write(2,m,(size_t)n);abort();
}
/* Decode the AArch64 single-register integer load/store forms emitted for
   ordinary scalar accesses: unsigned immediate, unscaled immediate and
   register offset. Anything else is a hard stop in this spike. */
static int decode(uint32_t insn,unsigned *size,int *load,int *sign,unsigned *rt,int *pair){
 *pair=0;
 if((insn&0x3b000000u)==0x39000000u||(insn&0x3b200c00u)==0x38000000u||(insn&0x3b200c00u)==0x38200800u){
  unsigned opc=(insn>>22)&3,sz=insn>>30;if(insn&(1u<<26))return 0; /* SIMD */
  *size=1u<<sz;*rt=insn&31;*load=opc!=0;*sign=opc>=2;return !(sz==3&&opc>=2);
 }
 if((insn&0x3e000000u)==0x28000000u&&!(insn&(1u<<26))&&((insn>>23)&7)==2){ /* LDP/STP signed offset */
  unsigned opc=insn>>30;if(opc==1||opc==3)return 0;*size=opc==2?8:4;*rt=insn&31;*load=(insn>>22)&1;*sign=0;*pair=1;return 1;
 }
 return 0;
}
static void handler(int sig,siginfo_t *info,void *context){
 ucontext_t *uc=context;uintptr_t address=(uintptr_t)info->si_addr,base=(uintptr_t)guest;
 if(address<base||address-base>=SPAN){signal(sig,SIG_DFL);return;}
 uint64_t pc=__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
 uint32_t insn=*(const uint32_t *)(uintptr_t)pc;unsigned size,rt;int load,sign,pair;
 if(!decode(insn,&size,&load,&sign,&rt,&pair)){write(2,"unsupported faulting instruction\n",33);abort();}
 /* si_addr is the faulting byte; the base register gives the access start. */
 unsigned rn=(insn>>5)&31;uint64_t start=rn==31?__darwin_arm_thread_state64_get_sp(uc->uc_mcontext->__ss):uc->uc_mcontext->__ss.__x[rn];
 if((insn&0x3b000000u)==0x39000000u)start+=(uint64_t)((insn>>10)&0xfff)*size;
 else if((insn&0x3b200c00u)==0x38000000u){int64_t imm=(int64_t)(int32_t)(((insn>>12)&0x1ff)<<23)>>23;start+=(uint64_t)imm;}
 else if(pair){int64_t imm=(int64_t)(int32_t)(((insn>>15)&0x7f)<<25)>>25;start+=(uint64_t)(imm*(int64_t)size);}
 else {unsigned rm=(insn>>16)&31;int s=(insn>>12)&1;uint64_t index=rm==31?0:uc->uc_mcontext->__ss.__x[rm];start+=s?index*size:index;}
 unsigned total=pair?2*size:size;
 if(start<base||start-base>SPAN-total){write(2,"fault outside window\n",21);abort();}
 uintptr_t offset=start-base;
 for(uintptr_t p=offset/4096;p<=(offset+total-1)/4096;p++)if((perm[p]&(load?1:2))!=(load?1:2))violation(offset,!load);
 for(unsigned k=0;k<(pair?2u:1u);k++){
  unsigned r=k?((insn>>10)&31):rt;unsigned char *a=alias+offset+k*size;
  if(load){uint64_t v=0;memcpy(&v,a,size);if(sign&&size<8){unsigned bits=8*size;v=(uint64_t)((int64_t)(v<<(64-bits))>>(64-bits));if(((insn>>22)&3)==3)v&=0xffffffffu;}
   if(r!=31)uc->uc_mcontext->__ss.__x[r]=v;}
  else {uint64_t v=r==31?0:uc->uc_mcontext->__ss.__x[r];memcpy(a,&v,size);}
 }
 __darwin_arm_thread_state64_set_pc_fptr(uc->uc_mcontext->__ss,(void *)(uintptr_t)(pc+4));
 atomic_fetch_add_explicit(&emulated,1,memory_order_relaxed);
}
static void map_window(void){
 vm_address_t g=0,a=0;vm_prot_t cur,max;
 assert(vm_allocate(mach_task_self(),&g,SPAN,VM_FLAGS_ANYWHERE)==KERN_SUCCESS);
 assert(vm_remap(mach_task_self(),&a,SPAN,0,VM_FLAGS_ANYWHERE,mach_task_self(),g,FALSE,&cur,&max,VM_INHERIT_NONE)==KERN_SUCCESS);
 guest=(unsigned char *)(uintptr_t)g;alias=(unsigned char *)(uintptr_t)a;
 /* host page 0: uniform RW; host page 1: mixed RW,R,none,RW; host page 2: uniform R; host page 3: uniform none */
 unsigned char layout[16]={3,3,3,3, 3,1,0,3, 1,1,1,1, 0,0,0,0};memcpy(perm,layout,sizeof perm);
 for(unsigned h=0;h<4;h++){unsigned char all=perm[h*4]&perm[h*4+1]&perm[h*4+2]&perm[h*4+3],any=perm[h*4]|perm[h*4+1]|perm[h*4+2]|perm[h*4+3];
  int prot=all!=any?PROT_NONE:((all&1)?PROT_READ:0)|((all&2)?PROT_WRITE:0);assert(!mprotect(guest+h*HOST,HOST,prot));}
}
static int expect_abort(void (*body)(void)){
 pid_t c=fork();assert(c>=0);if(!c){alarm(5);body();_exit(0);}
 int s;assert(waitpid(c,&s,0)==c);return WIFSIGNALED(s)&&WTERMSIG(s)==SIGABRT;
}
static void deny_read(void){(void)*(volatile uint32_t *)(guest+HOST+2*4096+8);}
static void deny_write(void){*(volatile uint32_t *)(guest+HOST+1*4096)=7;}
static void deny_uniform_write(void){*(volatile uint32_t *)(guest+2*HOST)=7;}
static void deny_straddle(void){*(volatile uint64_t *)(guest+HOST+1*4096-4)=7;}
static void *worker(void *arg){
 uintptr_t id=(uintptr_t)arg;volatile uint64_t *rw=(volatile uint64_t *)(guest+HOST+3*4096)+id*8;
 volatile uint32_t *ro=(volatile uint32_t *)(guest+HOST+1*4096);uint64_t sum=0;
 for(unsigned i=0;i<20000;i++){*rw=*rw+1;sum+=*ro;}
 assert(*rw==20000&&sum==20000ull*0x11223344u);return NULL;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(void){
 map_window();struct sigaction sa={0};sa.sa_sigaction=handler;sa.sa_flags=SA_SIGINFO;sigemptyset(&sa.sa_mask);
 assert(!sigaction(SIGSEGV,&sa,NULL)&&!sigaction(SIGBUS,&sa,NULL));
 memset(alias,0,SPAN);*(uint32_t *)(alias+HOST+1*4096)=0x11223344u;*(uint32_t *)(alias+2*HOST)=0x55667788u;
 /* Uniform pages: native access, no handler. */
 *(volatile uint32_t *)guest=42;assert(*(volatile uint32_t *)guest==42&&*(volatile uint32_t *)(guest+2*HOST)==0x55667788u&&emulated==0);
 /* Mixed page: allowed reads/writes are emulated and resumed with exact values. */
 volatile uint64_t *mixed_rw=(volatile uint64_t *)guest+HOST/8;*mixed_rw=0x0102030405060708ull;
 assert(*mixed_rw==0x0102030405060708ull&&*(uint64_t *)(alias+HOST)==0x0102030405060708ull);
 volatile int8_t *signed_byte=(volatile int8_t *)(alias+HOST+3*4096);*(int8_t *)signed_byte=-5;assert(*(volatile int8_t *)(guest+HOST+3*4096)==-5);
 assert(*(volatile uint32_t *)(guest+HOST+1*4096)==0x11223344u);
 unsigned long faults=emulated;assert(faults>=4);
 int aborts=expect_abort(deny_read)+expect_abort(deny_write)+expect_abort(deny_uniform_write)+expect_abort(deny_straddle);
 assert(aborts==4&&*(uint32_t *)(alias+HOST+1*4096)==0x11223344u);
 memset(alias+HOST+3*4096,0,4*64);pthread_t t[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(t+i,NULL,worker,(void *)i));for(int i=0;i<4;i++)pthread_join(t[i],NULL);
 double a=now();for(unsigned i=0;i<200000;i++)*(volatile uint32_t *)guest+=1;double native=now()-a;
 unsigned long before=emulated;a=now();for(unsigned i=0;i<200000;i++)*(volatile uint32_t *)(guest+HOST+3*4096+512)+=1;double slow=now()-a;
 printf("PASS: uniform pages native; mixed-page emulation exact; %d denied accesses aborted; 4 threads; native %.2f ns/rmw; emulated %.0f ns/access (%lu faults)\n",aborts,native*1e9/200000,slow*1e9/(emulated-before),emulated-before);
 return 0;
}

