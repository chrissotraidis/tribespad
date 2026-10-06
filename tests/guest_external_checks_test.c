#include "../port/core/guest_external_checks.h"
#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern uint64_t external_length(const void *),external_read(const void *),external_indirect(const void *,const void *);
extern uint64_t external_bounded(const void *,uint64_t);
extern int external_unknown(void),external_compare(const void *,const void *),external_ncompare(const void *,const void *,uint64_t);
extern void external_copy(void *,const void *,uint64_t),external_aggregate(void *);
extern void *external_allocate(uint64_t);
extern int external_once(void *,const void *),external_keycreate(void *,const void *);
extern void external_initializer(void);
extern int external_initializations;
extern int external_fdflags(int),external_fddup(int),external_fdunknown(int,int,void *);
extern uint64_t external_f64_add(uint64_t,uint64_t),external_f64_sub(uint64_t,uint64_t);
extern uint32_t external_f64_bad(void);
extern uint64_t external_f32_bad(void),external_f64_mul(uint64_t,uint64_t),external_f64_div(uint64_t,uint64_t);
extern uint32_t external_f32_add(uint64_t,uint64_t),external_f32_sub(uint64_t,uint64_t),external_f32_mul(uint64_t,uint64_t),external_f32_div(uint64_t,uint64_t);
extern void tb_sse_fence(void *,unsigned);
extern uint32_t external_ext_round32(uint64_t),external_ext_bad_argument(void);
extern uint64_t external_ext_round64(uint64_t),external_ext_i64(int64_t),external_ext_ui64(uint64_t),external_ext_bad_result(void);
static unsigned char *memory;
uint64_t unregistered(const void *p){(void)p;memory[0]=0x55;return 99;}
typedef struct {void *p;uint64_t n;} aggregate;
void unsafe_aggregate(aggregate value){(void)value;memory[0]=0x36;}
int main(void){memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);memset(memory,0xa5,65536);memcpy(memory+4096,"abc",4);assert(tribes_memory_checks_configure((uintptr_t)memory,65536));assert(tribes_memory_permissions_set(4096,4096,1));assert(tribes_memory_permissions_set(8192,4096,0));tribes_memory_external_configure((uintptr_t)memory,65536);tribes_memory_external_enable();
 assert(external_length("abc")==3);assert(external_indirect((const void *)external_read,memory)==UINT64_C(0xa5a5a5a5a5a5a5a5));external_copy(memory,memory+4096,4);assert(!memcmp(memory,"abc",4));void *p=external_allocate(32);assert(p);free(p);
 assert(external_bounded(memory+4096,8192)==3);assert(external_bounded(memory+8192,0)==0);assert(external_bounded(memory+8188,4)==4);memory[8191]=0;assert(external_bounded(memory+8191,8)==0);memory[8191]=0xa5;
 assert(external_compare(memory+4096,"abc")==0);assert(external_ncompare(memory+8192,memory+8192,0)==0);assert(external_compare(memory+8188,"\xa4")>0);assert(external_ncompare(memory+4096,"abcd",3)==0);memory[4200]=0xfe;memory[4201]=0;assert(external_compare("\x7f",memory+4200)<0);
 pthread_once_t once=PTHREAD_ONCE_INIT;assert(!external_once(&once,(void *)external_initializer)&&!external_once(&once,(void *)external_initializer)&&external_initializations==1);pthread_key_t key;assert(!external_keycreate(&key,NULL)&&!pthread_key_delete(key));
 int fd=open("/dev/null",O_RDONLY);assert(fd>=0);assert(external_fdflags(fd)==fcntl(fd,F_GETFD));int copy=external_fddup(fd);assert(copy>=3&&copy!=fd&&(fcntl(copy,F_GETFD)&FD_CLOEXEC));assert(!close(copy)&&!close(fd));
 unsigned char scalar_snapshot[65536];memcpy(scalar_snapshot,memory,65536);
 assert(external_f64_add(UINT64_C(0x3ff8000000000000),UINT64_C(0x3fe0000000000000))==UINT64_C(0x4000000000000000));assert(external_f64_sub(UINT64_C(0x3ff8000000000000),UINT64_C(0x3fe0000000000000))==UINT64_C(0x3ff0000000000000));assert(external_f64_sub((uintptr_t)memory,(uintptr_t)memory)==0);
 assert(external_f32_add(0x3fc00000,0x3f000000)==0x40000000);
 assert(external_f32_sub(0x3fc00000,0x3f000000)==0x3f800000);
 assert(external_f32_mul(0x3fc00000,0x3f000000)==0x3f400000);
 assert(external_f32_div(0x3fc00000,0x3f000000)==0x40400000);
 assert(external_f32_mul(1,0x3f800000)==1); /* smallest subnormal */
 assert(external_f32_div(0x3f800000,0)==0x7f800000);
 assert((external_f32_mul(0x7f800000,0)&0x7fc00000)==0x7fc00000);
 uint32_t address_bits=(uint32_t)(uintptr_t)memory;
 if((address_bits&0x7f800000)!=0x7f800000)assert(external_f32_sub(address_bits,address_bits)==0);
 assert(external_f64_mul(UINT64_C(0x3ff8000000000000),UINT64_C(0x3fe0000000000000))==UINT64_C(0x3fe8000000000000));
 assert(external_f64_div(UINT64_C(0x3ff8000000000000),UINT64_C(0x3fe0000000000000))==UINT64_C(0x4008000000000000));
 assert(external_f64_mul(1,UINT64_C(0x3ff0000000000000))==1);
 assert(external_f64_div(UINT64_C(0x3ff0000000000000),0)==UINT64_C(0x7ff0000000000000));
 const uint32_t binary32[]={0,0x80000000,1,0x800001,0x3fc00000,0x7f7fffff,0x7f800000,0xff800000,0x7fc12345,0x7f812345};
 for(unsigned i=0;i<sizeof binary32/sizeof binary32[0];i++){uint32_t v=binary32[i],expected=v;if((v&0x7f800000)==0x7f800000&&(v&0x7fffff))expected|=0x400000;assert(external_ext_round32(v)==expected);}
 const uint64_t binary64[]={0,UINT64_C(0x8000000000000000),1,UINT64_C(0x0010000000000000),UINT64_C(0x3ff8000000000000),UINT64_C(0x7fefffffffffffff),UINT64_C(0x7ff0000000000000),UINT64_C(0xfff0000000000000),UINT64_C(0x7ff8123456789abc),UINT64_C(0x7ff0123456789abc)};
 for(unsigned i=0;i<sizeof binary64/sizeof binary64[0];i++){uint64_t v=binary64[i],expected=v;if((v&UINT64_C(0x7ff0000000000000))==UINT64_C(0x7ff0000000000000)&&(v&UINT64_C(0xfffffffffffff)))expected|=UINT64_C(0x8000000000000);assert(external_ext_round64(v)==expected);}
 assert(external_ext_i64(-1)==UINT64_C(0xbff0000000000000));assert(external_ext_i64(INT64_MIN)==UINT64_C(0xc3e0000000000000));assert(external_ext_ui64(UINT64_C(1)<<63)==UINT64_C(0x43e0000000000000));assert(external_ext_ui64(0)==0);
 assert(!memcmp(memory,scalar_snapshot,65536));
 tb_sse_fence(&key,0);
 const char *reasons[]={"unverified call: getpid","guest pointer passed to host-only call: strlen","unregistered indirect target","unverified call: unsafe_aggregate","checked guest memory violation","checked guest memory violation","unregistered indirect target","unverified call: fcntl","unregistered indirect target","unverified call: <inline-asm>","unregistered indirect target","unregistered indirect target","unregistered indirect target"};unsigned char snapshot[65536];memcpy(snapshot,memory,65536);
 for(unsigned i=0;i<13;i++){int fd[2];assert(!pipe(fd));pid_t child=fork();assert(child>=0);if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);close(fd[0]);assert(dup2(fd[1],2)==2);close(fd[1]);if(i==0)(void)external_unknown();if(i==1)(void)external_length(memory+4096);if(i==2)(void)external_indirect((const void *)unregistered,memory);if(i==3)external_aggregate(memory);if(i==4)(void)external_bounded(memory+8188,8);if(i==5)(void)external_compare(memory+8188,"\xa5\xa5\xa5\xa5");if(i==6){pthread_once_t other=PTHREAD_ONCE_INIT;(void)external_once(&other,(const void *)unregistered);}if(i==7)(void)external_fdunknown(0,F_GETPATH,memory);if(i==8)(void)external_f64_bad();if(i==9)tb_sse_fence(&key,99);if(i==10)(void)external_f32_bad();if(i==11)(void)external_ext_bad_result();if(i==12)(void)external_ext_bad_argument();_exit(99);}close(fd[1]);char message[1024];ssize_t bytes=read(fd[0],message,sizeof message-1);assert(bytes>0);message[bytes]=0;close(fd[0]);int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(strstr(message,reasons[i]));assert(!memcmp(memory,snapshot,65536));}
 puts("PASS: emitted external-call policy; registered AOT indirect target; bounded guest memcpy; host-only strlen/malloc; thirteen boundary stops including nested pointer aggregate, unregistered code and guest-pointer escape; checked strings/comparisons with terminator/page/zero-limit/unsigned-byte coverage, registered once callbacks, nullable TLS cleanup and exact scalar fcntl shapes and by-value SoftFloat binary32/64 add/sub/mul/div and six exact by-value ext80 conversions with mismatched ABI rejection, exact ARM64 barrier admission and unknown assembly rejection, with shared guest memory unchanged");return 0;}
