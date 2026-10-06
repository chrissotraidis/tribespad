#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern uint64_t checked_read(const void *);
extern void checked_write(void *,uint64_t),checked_vector(void *,const void *);
extern uint32_t checked_atomic(void *),checked_cas(void *);
extern void checked_copy(void *,const void *,uint64_t),checked_move(void *,const void *,uint64_t),checked_set(void *,uint64_t),checked_cset(void *,uint64_t);
extern void checked_copy_chk(void *,const void *,uint64_t),checked_move_chk(void *,const void *,uint64_t),checked_named_move(void *,const void *,uint64_t),checked_set_chk(void *,uint64_t);
static unsigned char *memory;
static void *worker(void *context){unsigned char *p=memory+0x4000+(uintptr_t)context*64;for(unsigned i=0;i<2000;i++){checked_write(p,i);assert(checked_read(p)==i);checked_copy(p+16,p,8);assert(checked_read(p+16)==i);assert(checked_read(memory+4096)==UINT64_C(0xa5a5a5a5a5a5a5a5));}return NULL;}
static void *publisher(void *unused){(void)unused;for(unsigned i=0;i<4000;i++)assert(tribes_memory_permissions_set(4096,4096,(i&1)?3:1));return NULL;}
static void run_fault(unsigned n){
 switch(n){
 case 0:checked_write(memory+4096,123);break;
 case 1:checked_write(memory+4092,123);break;
 case 2:(void)checked_read(memory+8192);break;
 case 3:(void)checked_read(memory+8188);break;
 case 4:checked_copy(memory+4092,memory+128,16);break;
 case 5:checked_copy(memory+128,memory+8188,16);break;
 case 6:checked_move(memory+4092,memory+128,16);break;
 case 7:checked_move(memory+128,memory+8188,16);break;
 case 8:checked_set(memory+4092,16);break;
 case 9:checked_cset(memory+4092,16);break;
 case 10:checked_vector(memory+4092,memory+128);break;
 case 11:checked_vector(memory+128,memory+8188);break;
 case 12:(void)checked_atomic(memory+4096);break;
 case 13:(void)checked_cas(memory+4096);break;
 case 14:checked_write(memory+65532,7);break;
 case 15:(void)checked_read(memory+65532);break;
 case 16:checked_copy_chk(memory+4092,memory+128,16);break;
 case 17:checked_copy_chk(memory+128,memory+8188,16);break;
 case 18:checked_move_chk(memory+4092,memory+128,16);break;
 case 19:checked_move_chk(memory+128,memory+8188,16);break;
 case 20:checked_named_move(memory+4092,memory+128,16);break;
 case 21:checked_named_move(memory+128,memory+8188,16);break;
 case 22:checked_set_chk(memory+4092,16);break;
 default:abort();
 }
}
int main(int argc,char **argv){
 (void)argv;
 memory=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_ANON|MAP_SHARED,-1,0);assert(memory!=MAP_FAILED);memset(memory,0xa5,65536);
 assert(!tribes_memory_checks_configure((uintptr_t)memory+1,65536));assert(tribes_memory_checks_configure((uintptr_t)memory,65536));assert(!tribes_memory_checks_configure((uintptr_t)memory,65536));
 assert(!tribes_memory_permissions_set(1,4096,1)&&!tribes_memory_permissions_set(0,4097,1)&&!tribes_memory_permissions_set(65536,4096,1)&&!tribes_memory_permissions_set(0,4096,4));
 assert(tribes_memory_permissions_set(4096,4096,1)&&tribes_memory_permissions_set(8192,4096,0));
 /* Native backing remains RW: only emitted software checks reject accesses. */
 assert(checked_read(memory+4096)==UINT64_C(0xa5a5a5a5a5a5a5a5));checked_write(memory,123);assert(checked_read(memory)==123);checked_vector(memory+32,memory+4096);assert(!memcmp(memory+32,memory+4096,16));
 checked_copy(memory+64,memory+4096,16);assert(!memcmp(memory+64,memory+4096,16));checked_move(memory+65,memory+64,15);checked_set(memory+96,16);for(unsigned i=96;i<112;i++)assert(memory[i]==79);checked_cset(memory+96,16);for(unsigned i=96;i<112;i++)assert(memory[i]==58);
 uint32_t value=17;memcpy(memory+128,&value,4);assert(checked_cas(memory+128)==17&&checked_atomic(memory+128)==31);memcpy(&value,memory+128,4);assert(value==32);
 checked_copy_chk(memory+256,memory+4096,16);checked_move_chk(memory+257,memory+256,15);checked_named_move(memory+258,memory+257,14);assert(!memcmp(memory+258,memory+4096,14));checked_set_chk(memory+256,16);for(unsigned i=256;i<272;i++)assert(memory[i]==43);
 uint64_t host=0;checked_write(&host,491);assert(host==491&&checked_read(&host)==491);checked_copy(memory+8192,memory+8192,0);checked_move(memory+8192,memory+8192,0);checked_set(memory+8192,0);
 unsigned char snapshot[65536];memcpy(snapshot,memory,65536);
 for(unsigned i=0;i<(argc==1?23u:0u);i++){
  int channel[2];assert(!pipe(channel));pid_t child=fork();assert(child>=0);
  if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);close(channel[0]);assert(dup2(channel[1],2)==2);close(channel[1]);run_fault(i);_exit(99);}
  close(channel[1]);char message[1024];ssize_t count=read(channel[0],message,sizeof message-1);assert(count>0);message[count]=0;close(channel[0]);int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(strstr(message,"checked guest memory violation"));assert(!memcmp(snapshot,memory,65536));
 }
 pthread_t publishing;assert(!pthread_create(&publishing,NULL,publisher,NULL));pthread_t threads[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(!pthread_join(publishing,NULL));
 assert(tribes_memory_permissions_set(4096,4096,3));checked_write(memory+4096,859);assert(checked_read(memory+4096)==859);
 printf("PASS: actual pass-emitted scalar/vector/atomic/copy/move/set checks; %u forbidden-access child faults with complete shared-memory preservation; permitted neighboring/host/zero-length accesses; 8000 concurrent emitted write/read/copy cycles and 4000 permission publications; permission restoration\n",argc==1?23u:0u);return 0;
}
