/* MMU-enforced checked memory against the real trusted runtime. */
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern uint64_t tribes_memory_emulated_accesses(void);
#define SPAN UINT64_C(0x100000000)
static unsigned char *g;
static unsigned aborts;
static void expect(int should_abort,void (*body)(void)){
 pid_t c=fork();assert(c>=0);
 if(!c){struct rlimit l={0,0};setrlimit(RLIMIT_CORE,&l);alarm(10);freopen("/dev/null","w",stderr);body();_exit(0);}
 int s;assert(waitpid(c,&s,0)==c);
 if(should_abort){assert(WIFSIGNALED(s)&&WTERMSIG(s)==SIGABRT);aborts++;}else assert(WIFEXITED(s)&&!WEXITSTATUS(s));
}
/* host page 1 (0x4000): 4 KiB pages RW, R, none, RW */
#define MIXED 0x4000u
static void denied_read(void){(void)*(volatile uint32_t *)(g+MIXED+0x2008);}
static void denied_write(void){*(volatile uint32_t *)(g+MIXED+0x1000)=1;}
static void denied_uniform(void){*(volatile uint32_t *)(g+0x8000)=1;}
static void straddle(void){*(volatile uint64_t *)(g+MIXED+0x1ffc)=1;}
static void window_end(void){*(volatile uint64_t *)(g+SPAN-4)=1;}
static void code_write(void){*(volatile uint8_t *)(g+0x10010)=0x90;}
static void code_neighbour(void){*(volatile uint32_t *)(g+0x10100)=7;assert(*(volatile uint32_t *)(g+0x10100)==7);}
static void revoked(void){assert(!mprotect(g+0x30000000,0x4000,PROT_NONE));(void)*(volatile uint32_t *)(g+0x30000000);}
static void simd_into_denied(void){char src[32]={1};memcpy(g+MIXED+0x1ff0,src,32);}
static _Atomic int stop;
/* Bounded, interleaved publications: the game republishes rarely, and an
   unthrottled writer would only measure starvation of the emulators. */
static _Atomic unsigned publications;
static void *publisher(void *u){(void)u;unsigned i=0;while(!atomic_load(&stop)&&i<3000){assert(tribes_memory_permissions_set(MIXED+0x1000,4096,(i++&1)?1:3));usleep(20);}atomic_store(&publications,i);return NULL;}
static void *worker(void *u){
 _Atomic uint64_t *counter=(_Atomic uint64_t *)(g+MIXED+0x3000)+(uintptr_t)u*8;
 volatile uint32_t *ro=(volatile uint32_t *)(g+MIXED+0x1000);uint64_t sum=0;
 for(unsigned i=0;i<20000;i++){atomic_fetch_add(counter,1);uint64_t e=atomic_load(counter);atomic_compare_exchange_strong(counter,&e,e);sum+=*ro;}
 assert(atomic_load(counter)==20000&&sum==20000ull*0xabcdef01u);return NULL;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(void){
 g=mmap(NULL,SPAN,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(g!=MAP_FAILED);
 assert(!mprotect(g,0x40000,PROT_READ|PROT_WRITE));memset(g,0,0x40000);
 *(uint32_t *)(g+MIXED+0x1000)=0xabcdef01u;
 assert(!mprotect(g+0x30000000,0x8000,PROT_READ|PROT_WRITE)); /* owner-managed (heap-like) region */
 assert(tribes_memory_checks_configure((uintptr_t)g,SPAN));
 unsigned pages=0x30000000/4096;unsigned char *access=calloc(pages,1);assert(access);
 memset(access,3,0x40000/4096);access[(MIXED>>12)+1]=1;access[(MIXED>>12)+2]=0;memset(access+0x8000/4096,1,4);memset(access+0xc000/4096,0,4);
 assert(tribes_memory_initialize_static((uintptr_t)g,access,pages));
 /* uniform pages: native */
 uint64_t before=tribes_memory_emulated_accesses();
 *(volatile uint32_t *)g=42;assert(*(volatile uint32_t *)g==42&&*(volatile uint32_t *)(g+0x8000)==0);
 assert(tribes_memory_emulated_accesses()==before);
 /* mixed page: exact emulation of allowed accesses */
 volatile uint64_t *rw=(volatile uint64_t *)(g+MIXED);*rw=0x0102030405060708ull;assert(*rw==0x0102030405060708ull);
 assert(*(volatile uint32_t *)(g+MIXED+0x1000)==0xabcdef01u);
 volatile int8_t *sb=(volatile int8_t *)(g+MIXED+0x3000);*sb=-5;assert(*sb==-5);
 char pattern[64];for(int i=0;i<64;i++)pattern[i]=(char)i;memcpy(g+MIXED+0x3100,pattern,64);assert(!memcmp(g+MIXED+0x3100,pattern,64));
 memset(g+MIXED+0x3000,0x5a,4096);for(int i=0;i<4096;i+=97)assert(g[MIXED+0x3000+i]==0x5a);memset(g+MIXED+0x3000,0,4096);
 memset(g+MIXED,0,4096);
 _Atomic uint32_t *a=(_Atomic uint32_t *)(g+MIXED+0x3200);atomic_store(a,5);assert(atomic_fetch_add(a,3)==5&&atomic_load(a)==8);
 uint32_t e=8;assert(atomic_compare_exchange_strong(a,&e,11)&&atomic_load(a)==11);e=1;assert(!atomic_compare_exchange_strong(a,&e,12)&&e==11);
 assert(atomic_exchange(a,99)==11&&atomic_fetch_or(a,256)==99&&atomic_load(a)==355);
 uint64_t emulated=tribes_memory_emulated_accesses()-before;assert(emulated>20);
 /* explicit software bracket still nests with an emulated access */
 uint32_t token=tribes_memory_access_begin(g+MIXED+0x1000,4,1);assert(token==1);assert(*(volatile uint32_t *)(g+MIXED+0x1000)==0xabcdef01u);tribes_memory_access_end(token);
 /* publish code bytes at 0x10010..0x10020: writes there stop, neighbours emulate */
 static unsigned char expected[16];TribesPublishedCodeRange range={0x10010,16,expected};
 expect(1,denied_read);expect(1,denied_write);expect(1,denied_uniform);expect(1,straddle);expect(1,window_end);expect(1,revoked);expect(1,simd_into_denied);
 assert(tribes_memory_publish_code((uintptr_t)g,&range,1));
 /* Forked children have no alias (VM_INHERIT_NONE): use them only for stops. */
 expect(1,code_write);code_neighbour();
 /* VM arena: initialize, commit with zero fill, mixed commit */
 assert(tribes_memory_vm_initialize((uintptr_t)g,0x60030000,0x1ffc0000));
 unsigned char vm_access[8]={3,3,3,3,3,1,0,0},vm_zero[8]={1,1,1,1,1,1,0,0};
 assert(tribes_memory_vm_change((uintptr_t)g,0x60040000,0x8000,vm_access,vm_zero));
 *(volatile uint32_t *)(g+0x60040000)=9;assert(*(volatile uint32_t *)(g+0x60040000)==9);
 assert(*(volatile uint32_t *)(g+0x60045000)==0);
 /* concurrency: four emulating workers while a publisher flips a subpage */
 pthread_t pub,w[4];assert(!pthread_create(&pub,NULL,publisher,NULL));
 for(uintptr_t i=0;i<4;i++)assert(!pthread_create(w+i,NULL,worker,(void *)i));
 for(int i=0;i<4;i++)pthread_join(w[i],NULL);atomic_store(&stop,1);pthread_join(pub,NULL);
 assert(tribes_memory_permissions_set(MIXED+0x1000,4096,1));
 double t0=now();for(unsigned i=0;i<1000000;i++)(*(volatile uint32_t *)(g+64))++;double native=now()-t0;
 uint64_t b=tribes_memory_emulated_accesses();t0=now();for(unsigned i=0;i<20000;i++)(*(volatile uint32_t *)(g+MIXED+64))++;double slow=now()-t0;
 printf("PASS: native uniform pages; %llu emulated mixed-page accesses incl. SIMD/memset/LSE; %u stops (violations, straddles, window end, revoked residency, published code); VM zero/mixed commit; 4 workers vs publisher; native %.2f ns, emulated %.0f ns\n",
  (unsigned long long)(tribes_memory_emulated_accesses()-before),aborts,native*1e9/1000000,slow*1e9/(double)(tribes_memory_emulated_accesses()-b));
 return 0;
}
