#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned char *memory;
static uint64_t base;
static const uint32_t writers[]={0x10010f23,0x10010f50,0x10010f7d};
static const uint32_t fields[]={0x5edc8d,0x5edc94,0x5edc9b};
extern void emitted_store(uint32_t *,uint32_t);
static void bank(unsigned group){
 static const unsigned counts[]={34,26,7},starts[]={0x3c0000,0x3c0ea0,0x3c15f0};
 unsigned char bytes[34][10];TribesPublishedCodeRange ranges[34];
 for(unsigned i=0;i<counts[group];i++){
  memset(bytes[i],0x90,10);unsigned address=starts[group]+i*16;
  memcpy(memory+address,bytes[i],10);ranges[i]=(TribesPublishedCodeRange){address,10,bytes[i]};
 }
 assert(group==0?tribes_memory_publish_pending_bank(base,ranges,counts[group]):
        group==1?tribes_memory_publish_renderer_bank(base,ranges,counts[group]):
                 tribes_memory_publish_audio_bank(base,ranges,counts[group]));
}
uint64_t mission_fixture_init(void){
 memory=mmap(NULL,0x11000000,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(tribes_memory_checks_configure(base,0x11000000));
 unsigned char originals[21]={0xc7,0x45,0xcc,0x85,0xeb,0x11,0xbf,0xc7,0x45,0xc4,0x85,0xeb,0x11,0x3f,0xc7,0x45,0xc8,0x85,0xeb,0x11,0x3f};
 memcpy(memory+0x5edc8a,originals,21);
 for(unsigned i=0;i<3;i++){
  unsigned char instruction[]={0xf3,0x0f,0x11,0x05,0,0,0,0};memcpy(instruction+4,fields+i,4);memcpy(memory+writers[i],instruction,8);
 }
 assert(!tribes_memory_write_mission_literal(base,writers[0],fields[0],0));
 unsigned char author[98];memcpy(author,memory+writers[0],98);uint32_t dummy=0;
 TribesPublishedCodeRange ranges[]={{0x5edc8a,21,originals},{0x600000,4,(unsigned char *)&dummy},{writers[0],98,author}};
 TribesCodeWrite revision={writers[0],0x600000,4,0,1};
 assert(tribes_memory_prepare_code_revision(base,&revision,1)&&tribes_memory_publish_code(base,ranges,3));
 /* Fixture revisions exercise the real barrier state machine and bank gates;
    they are not a claim to execute the game's original startup patch plans. */
 for(unsigned phase=1;phase<=33;phase++){
  if(phase>1){revision.before=phase-1;revision.value=phase;assert(tribes_memory_prepare_next_code_revision(base,&revision,1));}
  assert(!tribes_memory_write_mission_literal(base,writers[0],fields[0],0));
  assert(tribes_memory_apply_code_write(base,revision.pc,memory+revision.address,4,revision.value));
  assert(tribes_memory_finish_code_revision(base));
  if(phase==20)bank(0);if(phase==30)bank(1);if(phase==32)bank(2);
 }
 return base;
}
static void *wrong_owner(void *unused){(void)unused;assert(!tribes_memory_write_mission_literal(base,writers[0],fields[0],7));return NULL;}
static void *reader(void *unused){
 (void)unused;
 for(unsigned i=0;i<12000;i++){
  uint32_t token=tribes_memory_access_begin(memory+fields[0],4,1),value;
  memcpy(&value,memory+fields[0],4);assert(value==0xaaaaaaaa||value==0x55555555);tribes_memory_access_end(token);
 }
 return NULL;
}
int main(int argc,char **argv){
 (void)argv;mission_fixture_init();unsigned char before[21];memcpy(before,memory+0x5edc8a,21);
 assert(!tribes_memory_write_mission_literal(base+4096,writers[0],fields[0],7));
 for(unsigned i=0;i<3;i++){
  assert(!tribes_memory_write_mission_literal(base,writers[i]+1,fields[i],7));
  assert(!tribes_memory_write_mission_literal(base,writers[i],fields[(i+1)%3],7));
  assert(tribes_memory_permissions_set(fields[i]&~4095u,4096,1));
  assert(!tribes_memory_write_mission_literal(base,writers[i],fields[i],7));
  assert(tribes_memory_permissions_set(fields[i]&~4095u,4096,3));
  memory[fields[i]-3]^=1;assert(!tribes_memory_write_mission_literal(base,writers[i],fields[i],7));memory[fields[i]-3]^=1;
  memory[writers[i]+4]^=1;assert(!tribes_memory_write_mission_literal(base,writers[i],fields[i],7));memory[writers[i]+4]^=1;
 }
 pthread_t thread;assert(!pthread_create(&thread,NULL,wrong_owner,NULL)&&!pthread_join(thread,NULL));
 assert(!memcmp(before,memory+0x5edc8a,21)&&tribes_memory_code_revision(base)==66);
 unsigned faults=0;
 if(argc==1)for(unsigned i=0;i<3;i++){
  pid_t pid=fork();assert(pid>=0);if(!pid){emitted_store((uint32_t *)(memory+fields[i]),0);_exit(99);}
  int status;assert(waitpid(pid,&status,0)==pid&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);faults++;
  assert(!memcmp(before,memory+0x5edc8a,21));
 }
 assert(tribes_memory_write_mission_literal(base,writers[0],fields[0],0xaaaaaaaa));
 assert(!pthread_create(&thread,NULL,reader,NULL));
 for(unsigned i=0;i<12000;i++)assert(tribes_memory_write_mission_literal(base,writers[0],fields[0],i&1?0xaaaaaaaa:0x55555555));
 assert(!pthread_join(thread,NULL));
 for(unsigned i=0;i<3;i++)assert(tribes_memory_write_mission_literal(base,writers[i],fields[i],0));
 assert(tribes_memory_code_revision(base)==66);
 printf("PASS: exact mission fields, owner/phase/permissions/opcodes; %u ordinary-write faults; 12000 concurrent protected reads and repeated writes; primary phase66 retained\n",faults);
 return 0;
}
