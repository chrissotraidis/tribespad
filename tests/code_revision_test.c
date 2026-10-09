#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern void emitted_store(uint32_t *,uint32_t);
extern void emitted_copy(void *,const void *,uint64_t);
static unsigned char *memory;
static uint64_t base;
static unsigned faults;
#ifdef TB_MOBILE_LEGACY_FONT_BANK
#define RENDERER_COUNT 18
#define AUDIO_BANK_START 0x3c13b0
#else
#define RENDERER_COUNT 26
#define AUDIO_BANK_START 0x3c15f0
#endif
static TribesPublishedCodeRange bank_ranges[34],renderer_ranges[RENDERER_COUNT];
static void *wrong_renderer_thread(void *unused){(void)unused;assert(!tribes_memory_publish_renderer_bank(base,renderer_ranges,RENDERER_COUNT));return NULL;}
static void *wrong_bank_thread(void *unused){(void)unused;assert(!tribes_memory_publish_pending_bank(base,bank_ranges,34));return NULL;}
static void ordinary_write_fault(void){
 unsigned char before[128];memcpy(before,memory+4080,sizeof before);fflush(stdout);
 pid_t child=fork();assert(child>=0);if(!child){emitted_store((uint32_t *)(memory+4094),0);_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 assert(!memcmp(before,memory+4080,sizeof before));faults++;
}
static void published_boundary_fault(uint32_t address,uint32_t size){
 unsigned char before[0x5000],source[0x2000]={0};assert(size<=sizeof source&&address+size<=sizeof before);
 memcpy(before,memory,sizeof before);fflush(stdout);
 pid_t child=fork();assert(child>=0);if(!child){emitted_copy(memory+address,source,size);_exit(90);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 assert(!memcmp(before,memory,sizeof before));faults++;
}
static void published_boundaries(void){
 /* Exact outer edges and an internal gap remain writable. */
 const uint32_t allowed[]={764,800,4168,8200,0x4000};
 for(unsigned i=0;i<sizeof allowed/sizeof *allowed;i++){
  uint32_t value;emitted_store((uint32_t *)(memory+allowed[i]),0x12345678);
  memcpy(&value,memory+allowed[i],4);assert(value==0x12345678);
  emitted_store((uint32_t *)(memory+allowed[i]),0x11111111);
 }
 published_boundary_fault(767,4);  /* Cross the first begin. */
 published_boundary_fault(799,4);  /* Cross the first end. */
 published_boundary_fault(8199,4); /* Cross the last end. */
 published_boundary_fault(764,7437); /* Span the entire envelope from outside. */
 assert(tribes_memory_permissions_set(0x4000,4096,1));
 published_boundary_fault(0x4000,4); /* Outside code still obeys page protection. */
 assert(tribes_memory_permissions_set(0x4000,4096,3));
}
static TribesCodeWrite next_writes[]={{780,4120,4,0x11111111,0x90909090},{784,4124,2,0x1111,0x9090}};
static void *wrong_next_thread(void *unused){
 (void)unused;assert(!tribes_memory_prepare_next_code_revision(base,next_writes,2));assert(!tribes_memory_apply_code_write(base,780,memory+4120,4,0x90909090));assert(!tribes_memory_finish_code_revision(base));return NULL;
}
static void *wrong_thread(void *unused){
 (void)unused;assert(!tribes_memory_apply_code_write(base,772,memory+4104,2,0xaabb));assert(!tribes_memory_finish_code_revision(base));return NULL;
}
int main(void){
 memory=mmap(NULL,0x400000,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;memset(memory,0x11,0x400000);
 assert(tribes_memory_checks_configure(base,0x400000));
 assert(!tribes_memory_prepare_next_code_revision(base,next_writes,2));
 TribesCodeWrite writes[]={{768,4094,4,0x11111111,0xdeadbeef},{772,4104,2,0x1111,0xaabb},{776,4112,1,0x11,0xcc}};
 assert(!tribes_memory_prepare_code_revision(base,writes,0));
 assert(!tribes_memory_prepare_code_revision(base,(void *)memory,3));
 TribesCodeWrite overlap[2]={writes[0],writes[0]};assert(!tribes_memory_prepare_code_revision(base,overlap,2));
 TribesCodeWrite invalid={768,4094,1,0x1111,0};assert(!tribes_memory_prepare_code_revision(base,&invalid,1));
 assert(tribes_memory_prepare_code_revision(base,writes,3));assert(!tribes_memory_prepare_code_revision(base,writes,3));
 memset(writes,0,sizeof writes); /* The runtime owns an immutable plan copy. */
 assert(!tribes_memory_apply_code_write(base,768,memory+4094,4,0xdeadbeef));assert(!tribes_memory_finish_code_revision(base));assert(!tribes_memory_code_revision(base));
 unsigned char source[32],expected[80];memcpy(source,memory+768,sizeof source);memcpy(expected,memory+4088,sizeof expected);
 uint32_t fonts[2]={544,544};memcpy(memory+8192,fonts,sizeof fonts);
 TribesPublishedCodeRange ranges[]={{768,sizeof source,source},{4088,sizeof expected,expected},{8192,sizeof fonts,(unsigned char *)fonts}};
 assert(!tribes_memory_publish_code(base,ranges+1,1)); /* Writer outside published code. */
 memory[4094]^=1;assert(!tribes_memory_publish_code(base,ranges,3));memory[4094]^=1;
 assert(tribes_memory_publish_code(base,ranges,3));assert(tribes_memory_code_revision(base)==1);ordinary_write_fault();published_boundaries();
 assert(!tribes_memory_prepare_next_code_revision(base,next_writes,2));
 unsigned char before[80];memcpy(before,memory+4088,sizeof before);
 assert(!tribes_memory_apply_code_write(base,772,memory+4104,2,0xaabb));
 assert(!tribes_memory_apply_code_write(base,769,memory+4094,4,0xdeadbeef));
 assert(!tribes_memory_apply_code_write(base,768,memory+4095,4,0xdeadbeef));
 assert(!tribes_memory_apply_code_write(base,768,memory+4094,2,0xdeadbeef));
 assert(!tribes_memory_apply_code_write(base,768,memory+4094,4,0xdeadbeee));
 assert(!tribes_memory_apply_code_write(base,768,memory+4094,0,0));
 assert(!tribes_memory_apply_code_write(base,768,memory+4094,UINT32_MAX,0));
 assert(!memcmp(before,memory+4088,sizeof before)&&tribes_memory_code_revision(base)==1);
 assert(tribes_memory_permissions_set(4096,4096,1));assert(!tribes_memory_apply_code_write(base,768,memory+4094,4,0xdeadbeef));assert(tribes_memory_permissions_set(4096,4096,3));
 memory[4094]^=1;assert(!tribes_memory_apply_code_write(base,768,memory+4094,4,0xdeadbeef));memory[4094]^=1;
 assert(tribes_memory_apply_code_write(base,768,memory+4094,4,0xdeadbeef));assert(tribes_memory_code_revision(base)==3);
 assert(!tribes_memory_prepare_next_code_revision(base,next_writes,2));
 uint32_t word;memcpy(&word,memory+4094,4);assert(word==0xdeadbeef);ordinary_write_fault();assert(!tribes_memory_finish_code_revision(base));
 pthread_t thread;assert(!pthread_create(&thread,NULL,wrong_thread,NULL));assert(!pthread_join(thread,NULL));
 assert(tribes_memory_apply_code_write(base,772,memory+4104,2,0xaabb));assert(tribes_memory_apply_code_write(base,776,memory+4112,1,0xcc));
 assert(!tribes_memory_apply_code_write(base,776,memory+4112,1,0xcc));assert(tribes_memory_code_revision(base)==3);
 memory[4104]^=1;assert(!tribes_memory_finish_code_revision(base)&&tribes_memory_code_revision(base)==3);memory[4104]^=1;
 assert(tribes_memory_permissions_set(4096,4096,0));assert(!tribes_memory_finish_code_revision(base));assert(tribes_memory_permissions_set(4096,4096,3));
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==2);
 assert(!tribes_memory_finish_code_revision(base));assert(!tribes_memory_apply_code_write(base,768,memory+4094,4,0xdeadbeef));ordinary_write_fault();
 assert(memory[4093]==0x11&&memory[4098]==0x11&&memory[4103]==0x11&&memory[4106]==0x11&&memory[4111]==0x11&&memory[4113]==0x11);
 assert(!pthread_create(&thread,NULL,wrong_next_thread,NULL));assert(!pthread_join(thread,NULL));
 TribesCodeWrite bad=next_writes[0];bad.pc=800;assert(!tribes_memory_prepare_next_code_revision(base,&bad,1));
 bad=next_writes[0];bad.address=4168;assert(!tribes_memory_prepare_next_code_revision(base,&bad,1));
 bad=next_writes[0];bad.before=0;assert(!tribes_memory_prepare_next_code_revision(base,&bad,1));
 memory[4104]^=1;assert(!tribes_memory_prepare_next_code_revision(base,next_writes,2));memory[4104]^=1;
 assert(tribes_memory_permissions_set(4096,4096,0));assert(!tribes_memory_prepare_next_code_revision(base,next_writes,2));assert(tribes_memory_permissions_set(4096,4096,3));
 assert(tribes_memory_prepare_next_code_revision(base,next_writes,2));assert(tribes_memory_code_revision(base)==2);ordinary_write_fault();
 assert(!tribes_memory_prepare_next_code_revision(base,next_writes,2));
 assert(!tribes_memory_apply_code_write(base,784,memory+4124,2,0x9090));
 assert(!pthread_create(&thread,NULL,wrong_next_thread,NULL));assert(!pthread_join(thread,NULL));
 assert(tribes_memory_apply_code_write(base,780,memory+4120,4,0x90909090));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));assert(!tribes_memory_prepare_next_code_revision(base,next_writes,2));
 assert(tribes_memory_apply_code_write(base,784,memory+4124,2,0x9090));
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==4);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));
 TribesCodeWrite third={788,4130,2,0x1111,0xbeef};assert(tribes_memory_prepare_next_code_revision(base,&third,1));assert(tribes_memory_code_revision(base)==4);ordinary_write_fault();
 assert(tribes_memory_apply_code_write(base,788,memory+4130,2,0xbeef));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==6);ordinary_write_fault();
 assert(!tribes_memory_prepare_next_code_revision(base,&third,1));assert(!tribes_memory_finish_code_revision(base));
 TribesCodeWrite fourth[]={{792,4130,2,0xbeef,0x7777},{796,4132,2,0x1111,0x8888}};
 memory[4130]^=1;assert(!tribes_memory_prepare_next_code_revision(base,fourth,2));memory[4130]^=1;
 assert(tribes_memory_prepare_next_code_revision(base,fourth,2));assert(tribes_memory_code_revision(base)==6);ordinary_write_fault();
 assert(tribes_memory_apply_code_write(base,792,memory+4130,2,0x7777));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));assert(!tribes_memory_apply_code_write(base,796,memory+4132,2,0x9999));
 assert(tribes_memory_apply_code_write(base,796,memory+4132,2,0x8888));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==8);ordinary_write_fault();
 TribesCodeWrite fifth[]={{792,4132,2,0x8888,0x9999},{796,4134,2,0x1111,0xaaaa}};
 memory[4132]^=1;assert(!tribes_memory_prepare_next_code_revision(base,fifth,2));memory[4132]^=1;
 assert(tribes_memory_prepare_next_code_revision(base,fifth,2));assert(tribes_memory_code_revision(base)==8);ordinary_write_fault();
 assert(tribes_memory_apply_code_write(base,792,memory+4132,2,0x9999));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));assert(!tribes_memory_apply_code_write(base,796,memory+4134,2,0xbbbb));
 assert(tribes_memory_apply_code_write(base,796,memory+4134,2,0xaaaa));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==10);ordinary_write_fault();
 /* The sixth phase replaces16 bytes, then a four-byte displacement
    crossing its two eight-byte stores. Every overlap must chain exactly. */
 TribesCodeWrite sixth[]={{792,4140,8,UINT64_C(0x1111111111111111),UINT64_C(0x001187e85014438b)},
 {792,4148,8,UINT64_C(0x1111111111111111),UINT64_C(0x00000098e9c68900)},
 {796,4145,4,UINT64_C(0x1187),UINT64_C(0x0fbd8c17)}};
 TribesCodeWrite inconsistent[3];memcpy(inconsistent,sixth,sizeof sixth);inconsistent[2].before^=1;
 assert(!tribes_memory_prepare_next_code_revision(base,inconsistent,3));
 memory[4149]^=1;assert(!tribes_memory_prepare_next_code_revision(base,sixth,3));memory[4149]^=1;
 assert(tribes_memory_prepare_next_code_revision(base,sixth,3));assert(tribes_memory_code_revision(base)==10);ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,796,memory+4145,4,sixth[2].value));
 assert(tribes_memory_apply_code_write(base,792,memory+4140,8,sixth[0].value));assert(tribes_memory_code_revision(base)==3);
 assert(tribes_memory_apply_code_write(base,792,memory+4148,8,sixth[1].value));ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));
 memory[4146]^=1;assert(!tribes_memory_apply_code_write(base,796,memory+4145,4,sixth[2].value));memory[4146]^=1;
 assert(tribes_memory_apply_code_write(base,796,memory+4145,4,sixth[2].value));
 for(unsigned i=4140;i<4156;i++){memory[i]^=1;assert(!tribes_memory_finish_code_revision(base));memory[i]^=1;}
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==12);ordinary_write_fault();
 TribesCodeWrite seventh[]={{796,4132,2,0x9999,0xbbbb},{796,4134,2,0xaaaa,0xcccc}};
 assert(tribes_memory_prepare_next_code_revision(base,seventh,2));assert(tribes_memory_code_revision(base)==12);ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,796,memory+4134,2,0xcccc));
 assert(tribes_memory_apply_code_write(base,796,memory+4132,2,0xbbbb));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));
 assert(tribes_memory_apply_code_write(base,796,memory+4134,2,0xcccc));assert(tribes_memory_code_revision(base)==3);
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==14);ordinary_write_fault();
 TribesCodeWrite eighth={796,4132,4,0xccccbbbb,0xeeeedddd};
 assert(tribes_memory_prepare_next_code_revision(base,&eighth,1));assert(tribes_memory_code_revision(base)==14);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));
 assert(!tribes_memory_apply_code_write(base,792,memory+4132,4,eighth.value));
 assert(!tribes_memory_apply_code_write(base,796,memory+4133,4,eighth.value));
 assert(!tribes_memory_apply_code_write(base,796,memory+4132,2,eighth.value));
 assert(!tribes_memory_apply_code_write(base,796,memory+4132,4,eighth.value^1));
 memory[4132]^=1;assert(!tribes_memory_apply_code_write(base,796,memory+4132,4,eighth.value));memory[4132]^=1;
 assert(tribes_memory_apply_code_write(base,796,memory+4132,4,eighth.value));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 memory[4133]^=1;assert(!tribes_memory_finish_code_revision(base));memory[4133]^=1;
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==16);ordinary_write_fault();
 TribesCodeWrite ninth[]={{796,4132,4,0xeeeedddd,0xffffffff},{796,4160,4,0x11111111,0x12345678}};
 assert(tribes_memory_prepare_next_code_revision(base,ninth,2));assert(tribes_memory_code_revision(base)==16);ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,796,memory+4160,4,ninth[1].value));
 assert(tribes_memory_apply_code_write(base,796,memory+4132,4,ninth[0].value));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));
 assert(!tribes_memory_apply_code_write(base,796,memory+4160,4,ninth[1].value^1));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,ninth[1].value));
 memory[4160]^=1;assert(!tribes_memory_finish_code_revision(base));memory[4160]^=1;
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==18);ordinary_write_fault();
 TribesCodeWrite tenth={796,4160,4,0x12345678,0xffffffff};
 assert(tribes_memory_prepare_next_code_revision(base,&tenth,1));assert(tribes_memory_code_revision(base)==18);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));
 assert(!tribes_memory_apply_code_write(base,795,memory+4160,4,tenth.value));
 assert(!tribes_memory_apply_code_write(base,796,memory+4160,4,tenth.value^1));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,tenth.value));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==20);ordinary_write_fault();
 TribesCodeWrite eleventh={796,4160,4,0xffffffff,0x76543210};
 assert(tribes_memory_prepare_next_code_revision(base,&eleventh,1));assert(tribes_memory_code_revision(base)==20);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));
 assert(!tribes_memory_apply_code_write(base,795,memory+4160,4,eleventh.value));
 assert(!tribes_memory_apply_code_write(base,796,memory+4160,4,eleventh.value^1));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,eleventh.value));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==22);ordinary_write_fault();
 TribesCodeWrite twelfth={796,4160,4,0x76543210,0xabcdef01};
 assert(tribes_memory_prepare_next_code_revision(base,&twelfth,1));assert(tribes_memory_code_revision(base)==22);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));
 assert(!tribes_memory_apply_code_write(base,795,memory+4160,4,twelfth.value));
 assert(!tribes_memory_apply_code_write(base,796,memory+4160,4,twelfth.value^1));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,twelfth.value));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==24);ordinary_write_fault();
 TribesCodeWrite thirteenth[]={{796,4160,4,0xabcdef01,0},{792,4164,4,0x11111111,0xabcdef01}};
 assert(tribes_memory_prepare_next_code_revision(base,thirteenth,2));assert(tribes_memory_code_revision(base)==24);ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,792,memory+4164,4,thirteenth[1].value));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,0));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));
 assert(tribes_memory_apply_code_write(base,792,memory+4164,4,thirteenth[1].value));
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==26);ordinary_write_fault();
 TribesCodeWrite fourteenth[]={{796,4160,4,0,0x1004ae80},{792,4164,1,1,0x5b}};
 assert(tribes_memory_prepare_next_code_revision(base,fourteenth,2));assert(tribes_memory_code_revision(base)==26);assert(tribes_memory_code_completed_revision(base)==26);ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,792,memory+4164,1,0x5b));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,fourteenth[0].value));assert(tribes_memory_code_revision(base)==3);assert(tribes_memory_code_completed_revision(base)==26);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));assert(!tribes_memory_apply_code_write(base,792,memory+4164,2,0x5b));
 assert(!tribes_memory_apply_code_write(base,792,memory+4164,1,0x5a));
 assert(tribes_memory_apply_code_write(base,792,memory+4164,1,0x5b));
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==28);assert(tribes_memory_code_completed_revision(base)==28);ordinary_write_fault();
 assert(memory[4165]==0xef&&memory[4166]==0xcd&&memory[4167]==0xab);
 TribesCodeWrite fifteenth[8];
 for(unsigned i=0;i<8;i++){
  unsigned size=i<6?4:i==6?2:1,address=4130+(i<6?i*4:i==6?24:26);uint64_t before=0;memcpy(&before,memory+address,size);
  fifteenth[i]=(TribesCodeWrite){784+i,address,size,before,size==4?0x90909090:size==2?0x9090:0x90};
 }
 assert(tribes_memory_prepare_next_code_revision(base,fifteenth,8));assert(tribes_memory_code_revision(base)==28);ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,fifteenth[1].pc,memory+fifteenth[1].address,4,fifteenth[1].value));
 for(unsigned i=0;i<8;i++){
  TribesCodeWrite w=fifteenth[i];assert(!tribes_memory_finish_code_revision(base));
  assert(!tribes_memory_apply_code_write(base,w.pc,memory+w.address,w.size,w.value^1));
  assert(tribes_memory_apply_code_write(base,w.pc,memory+w.address,w.size,w.value));
  assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==28);
 }
 ordinary_write_fault();memory[4156]^=1;assert(!tribes_memory_finish_code_revision(base));memory[4156]^=1;
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==30);ordinary_write_fault();
 for(unsigned i=4130;i<4157;i++)assert(memory[i]==0x90);
 TribesCodeWrite sixteenth[8];
 for(unsigned i=0;i<8;i++){
  unsigned size=i<6?4:i==6?2:1,address=4088+(i<6?i*4:i==6?24:26);uint64_t before=0;memcpy(&before,memory+address,size);
  sixteenth[i]=(TribesCodeWrite){784+i,address,size,before,size==4?0x90909090:size==2?0x9090:0x90};
 }
 assert(tribes_memory_prepare_next_code_revision(base,sixteenth,8));assert(tribes_memory_code_revision(base)==30);ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,sixteenth[1].pc,memory+sixteenth[1].address,4,sixteenth[1].value));
 for(unsigned i=0;i<8;i++){
  TribesCodeWrite w=sixteenth[i];assert(!tribes_memory_finish_code_revision(base));
  assert(!tribes_memory_apply_code_write(base,w.pc,memory+w.address,w.size,w.value^1));
  assert(tribes_memory_apply_code_write(base,w.pc,memory+w.address,w.size,w.value));
  assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==30);
 }
 ordinary_write_fault();memory[4114]^=1;assert(!tribes_memory_finish_code_revision(base));memory[4114]^=1;
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==32);ordinary_write_fault();
 for(unsigned i=4088;i<4115;i++)assert(memory[i]==0x90);
 TribesCodeWrite seventeenth[]={{796,4160,4,0x1004ae80,0x0fc58b79},{792,4164,1,0x5b,0x54},{788,4165,1,0xef,2}};
 assert(tribes_memory_prepare_next_code_revision(base,seventeenth,3));assert(tribes_memory_code_revision(base)==32);ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,792,memory+4164,1,0x54));
 for(unsigned i=0;i<3;i++){
  TribesCodeWrite w=seventeenth[i];assert(!tribes_memory_finish_code_revision(base));assert(!tribes_memory_apply_code_write(base,w.pc,memory+w.address,w.size,w.value^1));
  assert(tribes_memory_apply_code_write(base,w.pc,memory+w.address,w.size,w.value));assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==32);
 }
 ordinary_write_fault();memory[4165]^=1;assert(!tribes_memory_finish_code_revision(base));memory[4165]^=1;
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==34);ordinary_write_fault();
 assert(memory[4164]==0x54&&memory[4165]==2&&memory[4166]==0xcd&&memory[4167]==0xab);
 TribesCodeWrite eighteenth[]={{796,4093,4,0x90909090,2732},{792,4097,4,0x90909090,2048}};
 assert(tribes_memory_prepare_next_code_revision(base,eighteenth,2));assert(tribes_memory_code_revision(base)==34);ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,792,memory+4097,4,2048));
 assert(!tribes_memory_apply_code_write(base,796,memory+4093,4,1710));
 assert(tribes_memory_apply_code_write(base,796,memory+4093,4,2732));assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==34);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));assert(tribes_memory_apply_code_write(base,792,memory+4097,4,2048));
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==36);ordinary_write_fault();
 TribesCodeWrite nineteenth={796,4160,4,0x0fc58b79,1};assert(tribes_memory_prepare_next_code_revision(base,&nineteenth,1));ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,792,memory+4160,4,1));assert(!tribes_memory_finish_code_revision(base));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,1));assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==36);ordinary_write_fault();
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==38);ordinary_write_fault();
 TribesCodeWrite twentieth={796,4160,4,1,2};
 assert(tribes_memory_prepare_next_code_revision(base,&twentieth,1));assert(tribes_memory_code_revision(base)==38);ordinary_write_fault();
 assert(!tribes_memory_finish_code_revision(base));assert(!tribes_memory_apply_code_write(base,795,memory+4160,4,twentieth.value));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,twentieth.value));assert(tribes_memory_code_revision(base)==3);ordinary_write_fault();
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==40);ordinary_write_fault();
 TribesCodeWrite twentyfirst={796,4160,4,2,3};assert(!tribes_memory_prepare_next_code_revision(base,&twentyfirst,1));assert(!tribes_memory_finish_code_revision(base));
 unsigned char bank_expected[10];memset(bank_expected,0x11,sizeof bank_expected);
 for(unsigned i=0;i<34;i++)bank_ranges[i]=(TribesPublishedCodeRange){0x3c0010+i*72,10,bank_expected};
 assert(!tribes_memory_publish_pending_bank(base,bank_ranges,33));
 assert(!tribes_memory_publish_pending_bank(base+1,bank_ranges,34));
 assert(!pthread_create(&thread,NULL,wrong_bank_thread,NULL));assert(!pthread_join(thread,NULL));
 memory[0x3c0010]^=1;assert(!tribes_memory_publish_pending_bank(base,bank_ranges,34));memory[0x3c0010]^=1;
 assert(tribes_memory_permissions_set(0x3c0000,4096,0));assert(!tribes_memory_publish_pending_bank(base,bank_ranges,34));assert(tribes_memory_permissions_set(0x3c0000,4096,3));
 bank_ranges[1].address=bank_ranges[0].address;assert(!tribes_memory_publish_pending_bank(base,bank_ranges,34));bank_ranges[1].address+=72;
 assert(tribes_memory_publish_pending_bank(base,bank_ranges,34));assert(!tribes_memory_publish_pending_bank(base,bank_ranges,34));
 for(unsigned pass=0;pass<2;pass++){
  if(pass){assert(tribes_memory_prepare_next_code_revision(base,&twentyfirst,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,3));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==42);ordinary_write_fault();}
  pid_t child=fork();assert(child>=0);if(!child){emitted_store((uint32_t *)(memory+0x3c0010),0);_exit(90);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);faults++;
  emitted_store((uint32_t *)(memory+0x3c0010+12),0x12345678); /* Bank metadata stays writable. */
 }
 TribesCodeWrite twentysecond={796,4160,4,3,0x0fb408b0};
 assert(tribes_memory_prepare_next_code_revision(base,&twentysecond,1));ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,795,memory+4160,4,twentysecond.value));
 assert(!tribes_memory_apply_code_write(base,796,memory+4161,4,twentysecond.value));
 assert(!tribes_memory_apply_code_write(base,796,memory+4160,4,twentysecond.value^1));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,twentysecond.value));ordinary_write_fault();
 assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==42);
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==44);ordinary_write_fault();
 TribesCodeWrite twentythird[]={{796,4160,4,0x0fb408b0,0x90909090},{792,4160,2,0x9090,0xd989}};
 assert(tribes_memory_prepare_next_code_revision(base,twentythird,2));ordinary_write_fault();
 assert(!tribes_memory_apply_code_write(base,792,memory+4160,2,0xd989));
 assert(tribes_memory_apply_code_write(base,796,memory+4160,4,0x90909090));ordinary_write_fault();assert(!tribes_memory_finish_code_revision(base));
 assert(tribes_memory_apply_code_write(base,792,memory+4160,2,0xd989));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==46);ordinary_write_fault();
 TribesCodeWrite twentyfourth={796,4160,4,0x9090d989,1};
 assert(tribes_memory_prepare_next_code_revision(base,&twentyfourth,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,1));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==48);ordinary_write_fault();
 TribesCodeWrite twentyfifth={796,4160,4,1,2};
 assert(tribes_memory_prepare_next_code_revision(base,&twentyfifth,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,2));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==50);ordinary_write_fault();
 TribesCodeWrite twentysixth={796,4160,4,2,3};
 assert(tribes_memory_prepare_next_code_revision(base,&twentysixth,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,3));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==52);ordinary_write_fault();
 TribesCodeWrite twentyseventh={796,4160,4,3,4};
 assert(tribes_memory_prepare_next_code_revision(base,&twentyseventh,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,4));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==54);ordinary_write_fault();
 /* Publish slot1 first: either conditional store can execute independently. */
 for(unsigned n=0;n<2;n++){
  unsigned index=1-n;TribesCodeWrite font={796,8192+4*index,4,544,568};
  assert(!tribes_memory_prepare_font_size_revision(base,2,&font));
  font.before=543;assert(!tribes_memory_prepare_font_size_revision(base,index,&font));font.before=544;
  assert(!tribes_memory_font_size_published(base,index));
  assert(tribes_memory_prepare_font_size_revision(base,index,&font));ordinary_write_fault();
  assert(!tribes_memory_finish_code_revision(base));
  assert(tribes_memory_apply_code_write(base,796,memory+font.address,4,568));ordinary_write_fault();
  assert(tribes_memory_code_revision(base)==54&&!tribes_memory_font_size_published(base,index));
  assert(tribes_memory_finish_code_revision(base));ordinary_write_fault();
  assert(tribes_memory_code_revision(base)==54&&tribes_memory_font_size_published(base,index));
  assert(!tribes_memory_prepare_font_size_revision(base,index,&font));
  if(!n)assert(!tribes_memory_font_size_published(base,0));
 }
 TribesCodeWrite twentyeighth={796,4160,4,4,5};
 assert(tribes_memory_prepare_next_code_revision(base,&twentyeighth,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,5));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==56);ordinary_write_fault();
 TribesCodeWrite twentyninth={796,4160,4,5,6};
 assert(tribes_memory_prepare_next_code_revision(base,&twentyninth,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,6));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==58);ordinary_write_fault();
 TribesCodeWrite thirtieth={796,4160,4,6,7};
 assert(tribes_memory_prepare_next_code_revision(base,&thirtieth,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,7));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==60);ordinary_write_fault();
 TribesCodeWrite thirtyfirst={796,4160,4,7,8};assert(!tribes_memory_prepare_next_code_revision(base,&thirtyfirst,1));
 for(unsigned i=0;i<RENDERER_COUNT;i++)renderer_ranges[i]=(TribesPublishedCodeRange){0x3c0ea0+i*72,10,bank_expected};
 assert(!tribes_memory_publish_renderer_bank(base,renderer_ranges,RENDERER_COUNT-1));
 assert(!pthread_create(&thread,NULL,wrong_renderer_thread,NULL));assert(!pthread_join(thread,NULL));
 memory[0x3c1000]^=1; /* Outside code intervals: mutable bookkeeping. */
 memory[0x3c0ea0]^=1;assert(!tribes_memory_publish_renderer_bank(base,renderer_ranges,RENDERER_COUNT));memory[0x3c0ea0]^=1;
 assert(tribes_memory_permissions_set(0x3c1000,4096,0));assert(!tribes_memory_publish_renderer_bank(base,renderer_ranges,RENDERER_COUNT));assert(tribes_memory_permissions_set(0x3c1000,4096,3));
 renderer_ranges[1].address=renderer_ranges[0].address;assert(!tribes_memory_publish_renderer_bank(base,renderer_ranges,RENDERER_COUNT));renderer_ranges[1].address+=72;
 assert(tribes_memory_publish_renderer_bank(base,renderer_ranges,RENDERER_COUNT));assert(!tribes_memory_publish_renderer_bank(base,renderer_ranges,RENDERER_COUNT));
 pid_t child=fork();assert(child>=0);if(!child){emitted_store((uint32_t *)(memory+0x3c0ea0),0);_exit(90);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);faults++;
 assert(tribes_memory_prepare_next_code_revision(base,&thirtyfirst,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,8));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==62);ordinary_write_fault();
 TribesCodeWrite thirtysecond={796,4160,4,8,9};assert(tribes_memory_prepare_next_code_revision(base,&thirtysecond,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,9));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==64);ordinary_write_fault();
 TribesCodeWrite thirtythird={796,4160,4,9,10};assert(!tribes_memory_prepare_next_code_revision(base,&thirtythird,1));
 TribesPublishedCodeRange audio_ranges[7];unsigned char audio_expected[10]={0x90};
 for(unsigned i=0;i<7;i++){uint32_t a=AUDIO_BANK_START+i*72;memcpy(memory+a,audio_expected,10);audio_ranges[i]=(TribesPublishedCodeRange){a,10,audio_expected};}
 assert(!tribes_memory_publish_audio_bank(base,audio_ranges,6));
 memory[AUDIO_BANK_START]=0;assert(!tribes_memory_publish_audio_bank(base,audio_ranges,7));memory[AUDIO_BANK_START]=0x90;
 audio_ranges[1].address=audio_ranges[0].address;assert(!tribes_memory_publish_audio_bank(base,audio_ranges,7));audio_ranges[1].address=AUDIO_BANK_START+72;
 assert(tribes_memory_publish_audio_bank(base,audio_ranges,7));assert(!tribes_memory_publish_audio_bank(base,audio_ranges,7));
 child=fork();assert(child>=0);if(!child){emitted_store((uint32_t *)(memory+AUDIO_BANK_START),0);_exit(90);}assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);faults++;
 assert(tribes_memory_prepare_next_code_revision(base,&thirtythird,1));ordinary_write_fault();assert(tribes_memory_apply_code_write(base,796,memory+4160,4,10));ordinary_write_fault();assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==66);ordinary_write_fault();
 TribesCodeWrite thirtyfourth={796,4160,4,10,11};assert(!tribes_memory_prepare_next_code_revision(base,&thirtyfourth,1));


 assert(memory[4119]==0x11&&memory[4126]==0x11);
 printf("PASS: copied finite plan, ordered writer/address/width/value/preimage checks, cross-page permission checks, owner-thread enforcement, incomplete/final validation, retry and %u ordinary-write rejections\n",faults);return 0;
}
