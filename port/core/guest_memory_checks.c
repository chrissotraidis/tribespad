/* Trusted barrier implementation: compile separately WITHOUT the memory pass.
   This is additional guest subpage enforcement, not Windows SEH delivery. */
#include "guest_memory_checks.h"
#include "guest_system.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static uintptr_t guest_base;
static uint64_t guest_size;
static unsigned char *permissions;
typedef struct { uint64_t begin,end; } PublishedInterval;
static PublishedInterval *published;
static uint32_t published_count;
static int pending_bank_published,renderer_bank_published,audio_bank_published;
static TribesCodeWrite *revision;
static uint32_t revision_count,revision_index;
static int revision_finished;
static int revision_font = -1;
static unsigned font_size_published;
static unsigned completed_revisions;
static pthread_t revision_owner;
/* Readers announce on separate cache lines. Publication closes admission and
   waits for every announced reader before touching permissions or guest pages.
   Sequential consistency on both sides prevents the store/load race in which
   a reader misses the closed gate and a writer misses that reader. */
typedef struct __attribute__((aligned(128))) MemoryReader {
 _Atomic uint32_t active;
 struct MemoryReader *next;
} MemoryReader;
static MemoryReader *readers;
static _Atomic uint32_t writer_pending;
static pthread_mutex_t guard_writer=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t guard_wait=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t guard_changed=PTHREAD_COND_INITIALIZER;
static pthread_key_t reader_key;
static pthread_once_t reader_key_once=PTHREAD_ONCE_INIT;
static _Thread_local struct {MemoryReader *node;uint32_t depth;} reader_local;
#define read_depth reader_local.depth
static void reader_destroy(void *pointer){
 MemoryReader *node=pointer;
 if(atomic_load_explicit(&node->active,memory_order_seq_cst))abort();
 if(pthread_mutex_lock(&guard_wait))abort();
 MemoryReader **link=&readers;
 while(*link&&*link!=node)link=&(*link)->next;
 if(!*link)abort();*link=node->next;
 if(pthread_mutex_unlock(&guard_wait))abort();
 /* Another pthread-key destructor can still perform checked work. Let it
    register again instead of retaining a pointer to this retired node. */
 reader_local.node=NULL;free(node);
}
static void reader_key_create(void){if(pthread_key_create(&reader_key,reader_destroy))abort();}
static __attribute__((cold,noinline)) MemoryReader *reader_register(void){
 if(pthread_once(&reader_key_once,reader_key_create))abort();
 MemoryReader *node=NULL;
 if(posix_memalign((void **)&node,128,sizeof *node))abort();
 atomic_init(&node->active,0);
 if(pthread_mutex_lock(&guard_wait))abort();node->next=readers;readers=node;
 if(pthread_mutex_unlock(&guard_wait))abort();
 if(pthread_setspecific(reader_key,node))abort();reader_local.node=node;return node;
}
static __attribute__((cold,noinline)) void reader_wait(void){
 if(pthread_mutex_lock(&guard_wait))abort();
 while(atomic_load_explicit(&writer_pending,memory_order_seq_cst))
  if(pthread_cond_wait(&guard_changed,&guard_wait))abort();
 if(pthread_mutex_unlock(&guard_wait))abort();
}
static __attribute__((cold,noinline)) void reader_notify_writer(void){
 if(pthread_mutex_lock(&guard_wait))abort();
 if(pthread_cond_broadcast(&guard_changed))abort();
 if(pthread_mutex_unlock(&guard_wait))abort();
}
static void reader_leave(void){
 atomic_store_explicit(&reader_local.node->active,0,memory_order_seq_cst);
 /* Pair with the writer's gate store and reader scan: either it sees us idle
    or we see its gate and wake it. This also covers rejected announcements. */
 if(atomic_load_explicit(&writer_pending,memory_order_seq_cst))reader_notify_writer();
}
static void reader_enter(void){
 MemoryReader *node=reader_local.node;if(!node)node=reader_register();
 for(;;){
  atomic_store_explicit(&node->active,1,memory_order_seq_cst);
  if(!atomic_load_explicit(&writer_pending,memory_order_seq_cst))return;
  reader_leave();reader_wait();
 }
}
static void write_unlock(void){
 atomic_store_explicit(&writer_pending,0,memory_order_seq_cst);
 if(pthread_cond_broadcast(&guard_changed))abort();
 if(pthread_mutex_unlock(&guard_wait))abort();
 if(pthread_mutex_unlock(&guard_writer))abort();
}
/* Explicit buffer brackets can contain compiler-emitted access brackets.
   Keep the outer announcement until the last nested access has been checked. */
static void read_lock(void){
 if(read_depth==UINT32_MAX)abort();
 if(!read_depth)reader_enter();read_depth++;
}
static void read_unlock(void){
 if(!read_depth)abort();if(!--read_depth)reader_leave();
}
/* The registry is inspected only while holding guard_wait. Registration and
   thread teardown use that same mutex, so nodes cannot disappear during scans. */
static int readers_present(void){
 for(MemoryReader *node=readers;node;node=node->next)
  if(atomic_load_explicit(&node->active,memory_order_seq_cst))return 1;
 return 0;
}
static void write_lock(void){
 if(read_depth){fputs("TribesPad: permission publication inside a checked access bracket\n",stderr);abort();}
 if(pthread_mutex_lock(&guard_writer))abort();
 if(pthread_mutex_lock(&guard_wait))abort();
 atomic_store_explicit(&writer_pending,1,memory_order_seq_cst);
 while(readers_present())if(pthread_cond_wait(&guard_changed,&guard_wait))abort();
}

static __attribute__((cold,noinline)) _Noreturn void violation(const void *p,uint64_t size,uint32_t access){fprintf(stderr,"TribesPad: checked guest memory violation at 0x%llx size=%llu access=%u\n",(unsigned long long)((uintptr_t)p-guest_base),(unsigned long long)size,access);abort();}
int tribes_memory_checks_configure(uint64_t base,uint64_t size){
 if(permissions||!base||!size||(base|size)%4096||size>UINT64_C(0x100000000)||base>UINTPTR_MAX-size)return 0;
 unsigned char *p=malloc((size_t)(size/4096));if(!p)return 0;memset(p,3,(size_t)(size/4096));guest_base=(uintptr_t)base;guest_size=size;permissions=p;return 1;
}
int tribes_memory_permissions_set(uint32_t address,uint64_t size,uint32_t access){
 if(!permissions||!size||(address|size)%4096||access>3||address>=guest_size||size>guest_size-address)return 0;
 write_lock();memset(permissions+address/4096,(int)access,(size_t)(size/4096));write_unlock();return 1;
}
/* Test interval intersection without overflowing host pointer arithmetic. Host
   buffers are left alone; a range partly crossing the guest window is invalid. */
static int touches(const void *p,uint64_t size){
 uintptr_t address=(uintptr_t)p;if(!size||!permissions)return 0;
 if(size>UINTPTR_MAX-address)violation(p,size,0);
 return address<guest_base+guest_size&&address+size>guest_base;
}
static int covered(const PublishedInterval *,uint32_t,uint64_t,uint64_t);
/* Byte provenance makes overlapping finite stores explicit: each later
   preimage must follow the latest prior value, never an arbitrary rewrite. */
static int previous_writer(const TribesCodeWrite *writes,uint32_t count,uint32_t address){
 for(uint32_t i=count;i;i--)if(writes[i-1].address<=address&&(uint64_t)address<(uint64_t)writes[i-1].address+writes[i-1].size)return (int)i-1;
 return -1;
}
/* Called with the guard held; inspect only readable bytes. Initial state uses
   the first preimage; final state uses the last writer at each address. */
static int plan_matches(const TribesCodeWrite *writes,uint32_t count,int final){
 for(uint32_t i=0;i<count;i++)for(uint32_t b=0;b<writes[i].size;b++){
  uint32_t address=writes[i].address+b;
  if(!(permissions[address/4096]&1))return 0;
  if(previous_writer(writes,i,address)>=0)continue;
  int last=final?previous_writer(writes,count,address):(int)i;
  uint64_t value=final?writes[last].value:writes[last].before;
  unsigned char expected=(unsigned char)(value>>(8*(address-writes[last].address)));
  if(*(const unsigned char *)(guest_base+address)!=expected)return 0;
 }
 return 1;
}
static int prepare_revision(uint64_t base,const TribesCodeWrite *writes,uint32_t count,int next,int font){
 if(!permissions||base!=guest_base||!writes||!count||count>256||touches(writes,(uint64_t)count*sizeof *writes))return 0;
 TribesCodeWrite *copy=malloc((size_t)count*sizeof *copy);if(!copy)return 0;memcpy(copy,writes,(size_t)count*sizeof *copy);
 for(uint32_t i=0;i<count;i++){
  TribesCodeWrite w=copy[i];uint64_t end=(uint64_t)w.address+w.size;
  if(!w.pc||(w.size!=1&&w.size!=2&&w.size!=4&&w.size!=8)||end>guest_size||
     (w.size<8&&((w.before>>(w.size*8))||(w.value>>(w.size*8))))){free(copy);return 0;}
  for(uint32_t b=0;b<w.size;b++){
   int prior=previous_writer(copy,i,w.address+b);
   if(prior>=0&&(unsigned char)(w.before>>(8*b))!=(unsigned char)(copy[prior].value>>(8*(w.address+b-copy[prior].address)))){free(copy);return 0;}
  }
 }
 write_lock();
 int ok=next?(published&&revision_finished&&completed_revisions>=1&&completed_revisions<33&&(completed_revisions<20||pending_bank_published)&&(completed_revisions<30||renderer_bank_published)&&(completed_revisions<32||audio_bank_published)&&pthread_equal(pthread_self(),revision_owner)):(!revision&&!published);
 if(font>=0)ok=ok&&completed_revisions==27&&!(font_size_published&(1u<<font));
 if(next){
  if(ok&&!plan_matches(revision,revision_count,1))ok=0;
  for(uint32_t i=0;ok&&i<count;i++){
   TribesCodeWrite w=copy[i];
   if(!covered(published,published_count,w.pc,1)||!covered(published,published_count,w.address,w.size))ok=0;
   for(uint64_t page=w.address/4096;ok&&page<=((uint64_t)w.address+w.size-1)/4096;page++)if(!(permissions[page]&1))ok=0;

  }
 }
 if(ok&&next&&!plan_matches(copy,count,0))ok=0;
 if(ok){free(revision);revision=copy;revision_count=count;revision_index=0;revision_finished=0;revision_font=font;revision_owner=pthread_self();}else free(copy);
 write_unlock();return ok;
}
int tribes_memory_prepare_code_revision(uint64_t base,const TribesCodeWrite *writes,uint32_t count){return prepare_revision(base,writes,count,0,-1);}
/* Up to thirty-two subsequent finite plans, installed only by the original owner
   after a completed revision. No guest API or general code-write path. */
int tribes_memory_prepare_next_code_revision(uint64_t base,const TribesCodeWrite *writes,uint32_t count){return prepare_revision(base,writes,count,1,-1);}
/* Two independently conditional allocation immediates reached after renderer
   revision27. They use the same checked transaction without renumbering later
   primary plans or their bank gates. Only finish exposes the selected route. */
int tribes_memory_prepare_font_size_revision(uint64_t base,uint32_t index,const TribesCodeWrite *write){
 if(index>=2||!write||touches(write,sizeof *write))return 0;
 if(write->size!=4||write->before!=544||write->value!=568)return 0;
 return prepare_revision(base,write,1,1,(int)index);
}
int tribes_memory_font_size_published(uint64_t base,uint32_t index){
 if(!permissions||base!=guest_base||index>=2)return 0;
 read_lock();int ready=!!(font_size_published&(1u<<index));read_unlock();return ready;
}
static int covered(const PublishedInterval *ranges,uint32_t count,uint64_t begin,uint64_t size){
 for(uint32_t i=0;i<count;i++)if(ranges[i].begin<=begin&&begin+size<=ranges[i].end)return 1;
 return 0;
}
static __attribute__((cold,noinline)) _Noreturn void published_write_violation(const void *p,uint64_t size,uint32_t access,uint64_t offset){
 fprintf(stderr,"TribesPad: write to published AOT code at 0x%llx\n",(unsigned long long)offset);violation(p,size,access);
}
static void require(const void *p,uint64_t size,uint32_t access){
 uintptr_t address=(uintptr_t)p;if(address<guest_base||address-guest_base>=guest_size||size>guest_size-(address-guest_base))violation(p,size,access);
 uint64_t offset=address-guest_base,last=(offset+size-1)/4096;
 /* Published intervals are sorted and disjoint under this reader bracket.
    Heap/stack writes outside their envelope still need page permission checks. */
 if((access&2)&&published_count&&offset<published[published_count-1].end&&offset+size>published[0].begin){
  uint32_t lo=0,hi=published_count;
  while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(published[mid].end<=offset)lo=mid+1;else hi=mid;}
  if(lo<published_count&&published[lo].begin<offset+size)published_write_violation(p,size,access,offset);
 }
 for(uint64_t page=offset/4096;page<=last;++page)if((permissions[page]&access)!=access)violation(p,size,access);
}
int tribes_memory_publish_code(uint64_t base,const TribesPublishedCodeRange *ranges,uint32_t count){
 if(!permissions||base!=guest_base||!ranges||!count||count>256||touches(ranges,(uint64_t)count*sizeof *ranges))return 0;
 PublishedInterval *next=malloc((size_t)count*sizeof *next);if(!next)return 0;
 for(uint32_t i=0;i<count;i++){
  const TribesPublishedCodeRange *r=ranges+i;uint64_t end=(uint64_t)r->address+r->size;
  if(!r->size||end>guest_size||!r->expected||touches(r->expected,r->size)||(i&&next[i-1].end>r->address)){free(next);return 0;}
  next[i]=(PublishedInterval){r->address,end};
 }
 write_lock();int valid=!published;
 for(uint32_t i=0;valid&&i<count;i++){
  for(uint64_t page=next[i].begin/4096;page<=(next[i].end-1)/4096;page++)if(!(permissions[page]&1)){valid=0;break;}
  if(valid&&memcmp((void *)(uintptr_t)(guest_base+next[i].begin),ranges[i].expected,ranges[i].size))valid=0;
 }
 for(uint32_t i=0;valid&&i<revision_count;i++){
  TribesCodeWrite w=revision[i];
  if(!covered(next,count,w.pc,1)||!covered(next,count,w.address,w.size))valid=0;
 }
 if(valid&&!plan_matches(revision,revision_count,0))valid=0;
 if(valid){published=next;published_count=count;}else free(next);
 write_unlock();return valid;
}
/* The second captured bank is registered during original engine startup.
   Freeze only its 34 finite instruction intervals, once at revision20, before
   its entry rewrites begin. No guest API may extend the dispatch/code set. */
static int publish_later_bank(uint64_t base,const TribesPublishedCodeRange *ranges,uint32_t count,unsigned group){
#ifdef TB_MOBILE_LEGACY_FONT_BANK
 /* Finite original layout when the eight SDF-font registrations are absent.
    Every interval, byte and revision is still validated below. */
 static const uint32_t counts[]={34,18,7},starts[]={0x3c0000,0x3c0ea0,0x3c13b0},ends[]={0x3c1000,0x3c1372,0x3c156a},stages[]={20,30,32};
#else
 static const uint32_t counts[]={34,26,7},starts[]={0x3c0000,0x3c0ea0,0x3c15f0},ends[]={0x3c1000,0x3c15b2,0x3c17aa},stages[]={20,30,32};
#endif
 if(group>2)return 0;
 if(!permissions||base!=guest_base||!ranges||count!=counts[group]||touches(ranges,(uint64_t)count*sizeof *ranges))return 0;
 PublishedInterval extra[34];
 for(uint32_t i=0;i<count;i++){
  const TribesPublishedCodeRange *r=ranges+i;uint64_t end=(uint64_t)r->address+r->size;
  if(end>guest_size||r->address<starts[group]||end>ends[group]||r->size<10||r->size>27||!r->expected||touches(r->expected,r->size)||(i&&extra[i-1].end>r->address))return 0;
  extra[i]=(PublishedInterval){r->address,end};
 }
 write_lock();int ok=published&&!(group==2?audio_bank_published:group==1?renderer_bank_published:pending_bank_published)&&(group<1||pending_bank_published)&&(group<2||renderer_bank_published)&&revision_finished&&completed_revisions==stages[group]&&pthread_equal(pthread_self(),revision_owner)&&published_count<=256-count;
 if(ok&&!plan_matches(revision,revision_count,1))ok=0;
 for(uint32_t i=0;ok&&i<count;i++){
  for(uint32_t j=0;j<published_count;j++)if(extra[i].begin<published[j].end&&published[j].begin<extra[i].end){ok=0;break;}
  for(uint64_t page=extra[i].begin/4096;ok&&page<=(extra[i].end-1)/4096;page++)if(!(permissions[page]&1))ok=0;
  if(ok&&memcmp((void *)(uintptr_t)(base+extra[i].begin),ranges[i].expected,ranges[i].size))ok=0;
 }
 PublishedInterval *merged=ok?malloc((size_t)(published_count+count)*sizeof *merged):NULL;
 if(ok&&!merged)ok=0;
 if(ok){
  uint32_t a=0,b=0,n=0;
  while(a<published_count||b<count){
   if(b==count||(a<published_count&&published[a].begin<extra[b].begin))merged[n++]=published[a++];
   else merged[n++]=extra[b++];
  }
  free(published);published=merged;published_count+=count;if(group==2)audio_bank_published=1;else if(group==1)renderer_bank_published=1;else pending_bank_published=1;
 }
 write_unlock();return ok;
}
int tribes_memory_publish_pending_bank(uint64_t base,const TribesPublishedCodeRange *ranges,uint32_t count){return publish_later_bank(base,ranges,count,0);}
/* The renderer group is fixed to its captured 26 entries and revision30. */
int tribes_memory_publish_renderer_bank(uint64_t base,const TribesPublishedCodeRange *ranges,uint32_t count){return publish_later_bank(base,ranges,count,1);}
/* Seven original audio trampolines, frozen only after operand revision32. */
int tribes_memory_publish_audio_bank(uint64_t base,const TribesPublishedCodeRange *ranges,uint32_t count){return publish_later_bank(base,ranges,count,2);}
int tribes_memory_code_published(uint64_t base){
 if(!permissions||base!=guest_base)return 0;read_lock();int ready=published!=NULL;read_unlock();return ready;
}
uint32_t tribes_memory_code_revision(uint64_t base){
 if(!permissions||base!=guest_base)return 0;read_lock();
 uint32_t state=!published?0:(revision_finished||revision_font>=0)?completed_revisions*2:revision_index?3:completed_revisions?completed_revisions*2:1;
 read_unlock();return state;
}
uint32_t tribes_memory_code_completed_revision(uint64_t base){
 if(!permissions||base!=guest_base)return 0;read_lock();
 uint32_t phase=!published?0:completed_revisions?completed_revisions*2:1;
 read_unlock();return phase;
}
int tribes_memory_apply_code_write(uint64_t base,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 uintptr_t p=(uintptr_t)pointer;if(!permissions||base!=guest_base||p<guest_base||p-guest_base>=guest_size)return 0;
 write_lock();int ok=published&&revision&&revision_index<revision_count&&!revision_finished&&pthread_equal(pthread_self(),revision_owner);
 if(ok){
  TribesCodeWrite w=revision[revision_index];uint64_t offset=p-guest_base;
  ok=w.pc==pc&&w.address==offset&&w.size==size&&w.value==value;
  for(uint64_t page=offset/4096;ok&&page<=(offset+size-1)/4096;page++)if((permissions[page]&3)!=3)ok=0;
  if(ok&&memcmp(pointer,&w.before,size))ok=0;
  if(ok){memcpy((void *)pointer,&w.value,size);revision_index++;}
 }
 write_unlock();return ok;
}
int tribes_memory_finish_code_revision(uint64_t base){
 if(!permissions||base!=guest_base)return 0;write_lock();
 int ok=published&&revision&&revision_index==revision_count&&!revision_finished&&pthread_equal(pthread_self(),revision_owner);
 if(ok&&!plan_matches(revision,revision_count,1))ok=0;
 if(ok){revision_finished=1;if(revision_font>=0)font_size_published|=1u<<revision_font;else completed_revisions++;}
 write_unlock();return ok;
}
int tribes_memory_write_mission_literal(uint64_t base,uint32_t pc,uint32_t address,uint32_t value){
 static const uint32_t writers[]={0x10010f23,0x10010f50,0x10010f7d};
 static const uint32_t fields[]={0x5edc8d,0x5edc94,0x5edc9b};
 static const unsigned char displacements[]={0xcc,0xc4,0xc8};
 if(!permissions||base!=guest_base)return 0;
 unsigned index=0;while(index<3&&(writers[index]!=pc||fields[index]!=address))index++;
 if(index==3||guest_size<UINT64_C(0x10010f85))return 0;
 write_lock();
 int ok=published&&revision_finished&&completed_revisions==33&&
        pthread_equal(pthread_self(),revision_owner)&&
        covered(published,published_count,pc,8)&&
        covered(published,published_count,address-3,7)&&
        (permissions[pc/4096]&1)&&(permissions[address/4096]&3)==3;
 if(ok){
  unsigned char instruction[]={0xf3,0x0f,0x11,0x05,0,0,0,0};
  memcpy(instruction+4,&address,4);
  const unsigned char *reader=(const unsigned char *)(guest_base+address-3);
  ok=!memcmp((const void *)(guest_base+pc),instruction,8)&&
     reader[0]==0xc7&&reader[1]==0x45&&reader[2]==displacements[index];
  /* Preserve each conditional original store separately, including repeated
     and zero/NaN bit patterns. No instruction opcode or native code changes. */
  if(ok)memcpy((void *)(guest_base+address),&value,4);
 }
 write_unlock();return ok;
}
uint32_t tribes_memory_access_begin(const void *p,uint64_t size,uint32_t access){
 if(access<1||access>3)abort();if(!touches(p,size))return 0;read_lock();require(p,size,access);return 1;
}
void tribes_memory_access_host_span(const void *p,uint64_t size){
 /* Fixed translated prefix or the full native SIMD CPU object. */
 if(!p||(size!=120&&size!=440)||size>UINTPTR_MAX-(uintptr_t)p||touches(p,size))violation(p,size,0);
}
uint32_t tribes_memory_transfer_begin(void *destination,const void *source,uint64_t size){
 int write=touches(destination,size),read=touches(source,size);if(!write&&!read)return 0;
 read_lock();if(read)require(source,size,1);if(write)require(destination,size,2);return 1;
}
void tribes_memory_access_end(uint32_t token){if(token>1)abort();if(token)read_unlock();}
void tribes_memory_access_end_many(uint32_t count){
 if(count>16)abort();if(!count)return;
 if(count>read_depth)abort();read_depth-=count;
 if(!read_depth)reader_leave();
}
int tribes_memory_initialize_static(uint64_t base,const unsigned char *access,uint64_t pages){
 if(!permissions||base!=guest_base||!pages||pages>guest_size/4096||!access)return 0;
 for(uint64_t i=0;i<pages;i++)if(access[i]!=0&&access[i]!=1&&access[i]!=3)return 0;
 write_lock();memcpy(permissions,access,(size_t)pages);
 /* Addresses above the declared user limit are tokens or unowned reservation,
    never data. Dynamic user arenas retain their native mapping enforcement. */
 uint64_t user_end=(uint64_t)TRIBES_GUEST_MAXIMUM_ADDRESS+1;
 if(guest_size>user_end)memset(permissions+user_end/4096,0,(size_t)((guest_size-user_end)/4096));
 write_unlock();return 1;
}
static int native_access(const unsigned char *access,uint32_t count){unsigned bits=0;for(uint32_t i=0;i<count;i++)bits|=access[i];return ((bits&1)?PROT_READ:0)|((bits&2)?PROT_WRITE:0);}
int tribes_memory_protect_pages(uint64_t base,uint32_t address,uint32_t size,const unsigned char *access){
 long native_page=sysconf(_SC_PAGESIZE);
 if(!permissions||base!=guest_base||!access||!size||native_page<4096||native_page>65536||(native_page&(native_page-1))||((uint64_t)address|size|base)%(uint64_t)native_page||address>=guest_size||size>guest_size-address)return 0;
 uint32_t pages=size/4096,group=(uint32_t)native_page/4096;
 for(uint32_t i=0;i<pages;i++)if(access[i]!=0&&access[i]!=1&&access[i]!=3)return 0;
 write_lock();unsigned char *previous=permissions+address/4096;
 for(uint32_t i=0;i<pages;i+=group){
  if(mprotect((void *)(uintptr_t)(base+address+(uint64_t)i*4096),(size_t)native_page,native_access(access+i,group))){
   /* Include the failing native group in rollback; do not assume an OS failure
      left its backing unchanged. The old software map remains unpublished. */
   for(uint32_t j=0;j<=i;j+=group)if(mprotect((void *)(uintptr_t)(base+address+(uint64_t)j*4096),(size_t)native_page,native_access(previous+j,group))){fputs("TribesPad: checked protection rollback failed\n",stderr);abort();}
   write_unlock();return 0;
  }
 }
 memcpy(previous,access,pages);write_unlock();return 1;
}

/* Only the VM owner may opt into these trusted operations. Initializing an
   already-owned arena is forbidden by that owner before reaching this layer. */
static int vm_initialized;
int tribes_memory_vm_initialize(uint64_t base,uint32_t address,uint32_t size){
 if(!permissions||base!=guest_base||address!=0x60030000||size!=0x1ffc0000||
    (uint64_t)address+size>guest_size)return 0;
 write_lock();
 if(vm_initialized){write_unlock();return 0;}
 /* No existing VM allocation is allowed here. An OS failure cannot leave a
    partially enabled arena available for execution. */
 if(mprotect((void *)(uintptr_t)(base+address),size,PROT_NONE)){fputs("TribesPad: checked VM initialization failed\n",stderr);abort();}
 memset(permissions+address/4096,0,size/4096);vm_initialized=1;
 write_unlock();return 1;
}
int tribes_memory_vm_change(uint64_t base,uint32_t address,uint32_t size,const unsigned char *access,const unsigned char *zero){
 long native_page=sysconf(_SC_PAGESIZE);
 if(!permissions||base!=guest_base||!access||!zero||!size||native_page<4096||native_page>65536||
    (native_page&(native_page-1))||((uint64_t)address|size|base)%(uint64_t)native_page||
    address<0x60030000||(uint64_t)address+size>0x7fff0000||touches(access,size/4096)||touches(zero,size/4096))return 0;
 uint32_t pages=size/4096,group=(uint32_t)native_page/4096;
 for(uint32_t i=0;i<pages;i++)if((access[i]!=0&&access[i]!=1&&access[i]!=3)||zero[i]>1)return 0;
 write_lock();
 if(!vm_initialized){write_unlock();return 0;}
 unsigned char *previous=permissions+address/4096;
 for(uint32_t i=0;i<pages;i++)if(zero[i]&&previous[i]){write_unlock();return 0;}
 for(uint32_t i=0;i<pages;i+=group){
  void *host=(void *)(uintptr_t)(base+address+(uint64_t)i*4096);
  int final=native_access(access+i,group),writable=final;
  for(uint32_t j=0;j<group;j++)if(zero[i+j])writable=PROT_READ|PROT_WRITE;
  int failed=mprotect(host,(size_t)native_page,writable);
  if(!failed){
   for(uint32_t j=0;j<group;j++)if(zero[i+j])memset((char *)host+j*4096,0,4096);
   if(writable!=final)failed=mprotect(host,(size_t)native_page,final);
  }
  if(failed){
   for(uint32_t j=0;j<=i;j+=group)if(mprotect((void *)(uintptr_t)(base+address+(uint64_t)j*4096),(size_t)native_page,native_access(previous+j,group))){fputs("TribesPad: checked VM rollback failed\n",stderr);abort();}
   write_unlock();return 0;
  }
 }
 memcpy(previous,access,pages);write_unlock();return 1;
}
