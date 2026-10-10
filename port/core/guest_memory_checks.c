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
#include <errno.h>
#include <time.h>
static uintptr_t guest_base;
static uint64_t guest_size;
static unsigned char *permissions;
/* Trusted runtime data path. In MMU-enforced builds the guest view may be
   narrowed (inaccessible mixed pages, read-only published code), so trusted
   reads/writes made under the writer lock use a private RW alias of the same
   memory. Without that mode the alias is absent and this is the guest view. */
static uintptr_t alias_base;
#define GUEST_DATA(offset) ((unsigned char *)(uintptr_t)((alias_base?alias_base:guest_base)+(uint64_t)(offset)))
#ifdef TRIBES_HARDWARE_PERMISSIONS
static _Atomic uint32_t emulators;
static int hardware_configure(void);
static void apply_native(uint64_t first_page,uint64_t end_page);
static void apply_published_native(void);
#endif
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
static _Atomic int reader_key_ready;
static void reader_key_create(void){if(pthread_key_create(&reader_key,reader_destroy))abort();atomic_store(&reader_key_ready,1);}
/* Signal-context lookup of this thread's reader: TSD read only, no allocation. */
static __attribute__((unused)) MemoryReader *reader_local_peek(void){return atomic_load(&reader_key_ready)?pthread_getspecific(reader_key):NULL;}
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
#ifdef TRIBES_HARDWARE_PERMISSIONS
 /* Fault emulations are counted, not registered readers; they never block on
    this mutex, so poll them with a bounded wait. Publications are rare. */
 for(;;){
  if(!readers_present()&&!atomic_load_explicit(&emulators,memory_order_seq_cst))break;
  struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=200000;if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}
  int r=pthread_cond_timedwait(&guard_changed,&guard_wait,&t);if(r&&r!=ETIMEDOUT)abort();
 }
#else
 while(readers_present())if(pthread_cond_wait(&guard_changed,&guard_wait))abort();
#endif
}

static __attribute__((cold,noinline)) _Noreturn void violation(const void *p,uint64_t size,uint32_t access){fprintf(stderr,"TribesPad: checked guest memory violation at 0x%llx size=%llu access=%u\n",(unsigned long long)((uintptr_t)p-guest_base),(unsigned long long)size,access);abort();}
int tribes_memory_checks_configure(uint64_t base,uint64_t size){
 if(permissions||!base||!size||(base|size)%4096||size>UINT64_C(0x100000000)||base>UINTPTR_MAX-size)return 0;
 unsigned char *p=malloc((size_t)(size/4096));if(!p)return 0;memset(p,3,(size_t)(size/4096));guest_base=(uintptr_t)base;guest_size=size;permissions=p;
#ifdef TRIBES_HARDWARE_PERMISSIONS
 if(!hardware_configure()){fputs("TribesPad: MMU-enforced checked memory setup failed\n",stderr);abort();}
#endif
 return 1;
}
int tribes_memory_permissions_set(uint32_t address,uint64_t size,uint32_t access){
 if(!permissions||!size||(address|size)%4096||access>3||address>=guest_size||size>guest_size-address)return 0;
 write_lock();memset(permissions+address/4096,(int)access,(size_t)(size/4096));
#ifdef TRIBES_HARDWARE_PERMISSIONS
 apply_native(address/4096,(address+size)/4096);
#endif
 write_unlock();return 1;
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
  if(*GUEST_DATA(address)!=expected)return 0;
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
  if(valid&&memcmp(GUEST_DATA(next[i].begin),ranges[i].expected,ranges[i].size))valid=0;
 }
 for(uint32_t i=0;valid&&i<revision_count;i++){
  TribesCodeWrite w=revision[i];
  if(!covered(next,count,w.pc,1)||!covered(next,count,w.address,w.size))valid=0;
 }
 if(valid&&!plan_matches(revision,revision_count,0))valid=0;
 if(valid){published=next;published_count=count;
#ifdef TRIBES_HARDWARE_PERMISSIONS
  apply_published_native();
#endif
 }else free(next);
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
  if(ok&&memcmp(GUEST_DATA(extra[i].begin),ranges[i].expected,ranges[i].size))ok=0;
 }
 PublishedInterval *merged=ok?malloc((size_t)(published_count+count)*sizeof *merged):NULL;
 if(ok&&!merged)ok=0;
 if(ok){
  uint32_t a=0,b=0,n=0;
  while(a<published_count||b<count){
   if(b==count||(a<published_count&&published[a].begin<extra[b].begin))merged[n++]=published[a++];
   else merged[n++]=extra[b++];
  }
  free(published);published=merged;published_count+=count;
#ifdef TRIBES_HARDWARE_PERMISSIONS
  apply_published_native();
#endif
if(group==2)audio_bank_published=1;else if(group==1)renderer_bank_published=1;else pending_bank_published=1;
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
  if(ok&&memcmp(GUEST_DATA(offset),&w.before,size))ok=0;
  if(ok){memcpy(GUEST_DATA(offset),&w.value,size);revision_index++;}
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
  const unsigned char *reader=GUEST_DATA(address-3);
  ok=!memcmp(GUEST_DATA(pc),instruction,8)&&
     reader[0]==0xc7&&reader[1]==0x45&&reader[2]==displacements[index];
  /* Preserve each conditional original store separately, including repeated
     and zero/NaN bit patterns. No instruction opcode or native code changes. */
  if(ok)memcpy(GUEST_DATA(address),&value,4);
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
#ifdef TRIBES_HARDWARE_PERMISSIONS
 apply_native(0,pages);if(guest_size>user_end)apply_native(user_end/4096,guest_size/4096);
#endif
 write_unlock();return 1;
}
static int native_access(const unsigned char *access,uint32_t count){unsigned bits=0;for(uint32_t i=0;i<count;i++)bits|=access[i];return ((bits&1)?PROT_READ:0)|((bits&2)?PROT_WRITE:0);}
int tribes_memory_protect_pages(uint64_t base,uint32_t address,uint32_t size,const unsigned char *access){
 long native_page=sysconf(_SC_PAGESIZE);
 if(!permissions||base!=guest_base||!access||!size||native_page<4096||native_page>65536||(native_page&(native_page-1))||((uint64_t)address|size|base)%(uint64_t)native_page||address>=guest_size||size>guest_size-address)return 0;
 uint32_t pages=size/4096,group=(uint32_t)native_page/4096;
 for(uint32_t i=0;i<pages;i++)if(access[i]!=0&&access[i]!=1&&access[i]!=3)return 0;
 write_lock();unsigned char *previous=permissions+address/4096;
#ifdef TRIBES_HARDWARE_PERMISSIONS
 (void)group;memcpy(previous,access,pages);apply_native(address/4096,(uint64_t)address/4096+pages);write_unlock();return 1;
#endif
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
 memset(permissions+address/4096,0,size/4096);
#ifdef TRIBES_HARDWARE_PERMISSIONS
 apply_native(address/4096,((uint64_t)address+size)/4096);
#else
 if(mprotect((void *)(uintptr_t)(base+address),size,PROT_NONE)){fputs("TribesPad: checked VM initialization failed\n",stderr);abort();}
#endif
 vm_initialized=1;
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
#ifdef TRIBES_HARDWARE_PERMISSIONS
 /* Zero-filled 4 KiB pages were inaccessible (previous==0), so every native
    access to them faults until the new protections are published below. */
 (void)group;
 for(uint32_t i=0;i<pages;i++)if(zero[i])memset(GUEST_DATA((uint64_t)address+(uint64_t)i*4096),0,4096);
 memcpy(previous,access,pages);apply_native(address/4096,(uint64_t)address/4096+pages);write_unlock();return 1;
#endif
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

#ifdef TRIBES_HARDWARE_PERMISSIONS
/* ---- MMU-enforced checked memory -------------------------------------------
   Every 16 KiB host page whose four 4 KiB guest permissions agree receives the
   exact native protection, so ordinary guest accesses need no software check.
   A host page with mixed permissions is inaccessible, and a writable page that
   holds published code is read-only ("narrowed"). Faults on narrowed pages are
   decoded here, checked against the exact table and published intervals, and
   either aborted or performed through the private alias and resumed. Faults
   on pages this layer did not narrow (owner-managed residency, guard space,
   uniform denials) abort: the original build also stopped on them. */
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <sched.h>
#include <signal.h>
#include <sys/ucontext.h>
static uint64_t host_page,host_group;
static unsigned char *narrowed;           /* per host page */
static struct sigaction previous_segv,previous_bus;
static _Atomic uint64_t emulated_accesses;
static int native_protection(uint64_t page,int *narrow){
 const unsigned char *q=permissions+page*host_group;unsigned all=3,any=0;
 for(uint64_t j=0;j<host_group;j++){all&=q[j];any|=q[j];}
 int prot;*narrow=0;
 if(all!=any){prot=PROT_NONE;*narrow=1;}
 else prot=((all&1)?PROT_READ:0)|((all&2)?PROT_WRITE:0);
 if(prot&PROT_WRITE){
  uint64_t lo=page*host_page,hi=lo+host_page;
  for(uint32_t i=0;i<published_count;i++)if(published[i].begin<hi&&published[i].end>lo){prot=PROT_READ;*narrow=1;break;}
 }
 return prot;
}
/* Called with the writer lock held: no emulation or bracketed access runs. */
static void apply_native(uint64_t first_page,uint64_t end_page){
 uint64_t lo=first_page/host_group,hi=(end_page+host_group-1)/host_group;
 if(hi>guest_size/host_page)hi=guest_size/host_page;
 uint64_t run=lo;int run_prot=-1;
 for(uint64_t h=lo;h<=hi;h++){
  int narrow=0,prot=h<hi?native_protection(h,&narrow):-2;
  if(h<hi)narrowed[h]=(unsigned char)narrow;
  if(prot!=run_prot){
   if(run_prot>=0&&mprotect((void *)(uintptr_t)(guest_base+run*host_page),(size_t)((h-run)*host_page),run_prot)){
    fputs("TribesPad: native checked protection failed\n",stderr);abort();
   }
   run=h;run_prot=prot;
  }
 }
}
static void apply_published_native(void){
 for(uint32_t i=0;i<published_count;i++)apply_native(published[i].begin/4096,(published[i].end+4095)/4096);
}
static void fault_handler(int,siginfo_t *,void *);
static int hardware_configure(void){
 long page=sysconf(_SC_PAGESIZE);
 if(page<4096||page>65536||(page&(page-1))||guest_base%(uint64_t)page||guest_size%(uint64_t)page)return 0;
 host_page=(uint64_t)page;host_group=host_page/4096;
 narrowed=calloc((size_t)(guest_size/host_page),1);if(!narrowed)return 0;
 /* Accesses near the top of the window must fault rather than reach an
    adjacent host mapping: reserve an inaccessible guard after it. */
 vm_address_t guard=(vm_address_t)(guest_base+guest_size);
 if(vm_allocate(mach_task_self(),&guard,65536,VM_FLAGS_FIXED)!=KERN_SUCCESS||mprotect((void *)guard,65536,PROT_NONE))return 0;
 vm_address_t alias=0;vm_prot_t current,maximum;
 if(vm_remap(mach_task_self(),&alias,(vm_size_t)guest_size,0,VM_FLAGS_ANYWHERE,mach_task_self(),(vm_address_t)guest_base,FALSE,&current,&maximum,VM_INHERIT_NONE)!=KERN_SUCCESS)return 0;
 if(mprotect((void *)alias,(size_t)guest_size,PROT_READ|PROT_WRITE))return 0;
 alias_base=(uintptr_t)alias;
 struct sigaction action;memset(&action,0,sizeof action);action.sa_sigaction=fault_handler;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
 if(sigaction(SIGSEGV,&action,&previous_segv)||sigaction(SIGBUS,&action,&previous_bus))return 0;
 return 1;
}
/* ---- fault-path helpers (async-signal context: no allocation) ---- */
static __attribute__((noreturn)) void fault_stop(const char *what,uint64_t pc,uint32_t insn,uintptr_t address){
 char m[192];int n=snprintf(m,sizeof m,"TribesPad: %s pc=0x%llx insn=0x%08x address=0x%llx guest=0x%llx\n",what,(unsigned long long)pc,insn,(unsigned long long)address,(unsigned long long)(address-guest_base));
 if(n>0)write(2,m,(size_t)(n<(int)sizeof m?n:(int)sizeof m-1));abort();
}
typedef struct { struct __darwin_mcontext64 *mc; } Regs;
static uint64_t xget(Regs r,unsigned n,int sp){
 if(n==31)return sp?__darwin_arm_thread_state64_get_sp(r.mc->__ss):0;
 if(n<29)return r.mc->__ss.__x[n];
 if(n==29)return (uint64_t)__darwin_arm_thread_state64_get_fp(r.mc->__ss);
 return (uint64_t)__darwin_arm_thread_state64_get_lr(r.mc->__ss);
}
static void xset(Regs r,unsigned n,uint64_t v,int sp){
 if(n==31){if(sp)__darwin_arm_thread_state64_set_sp(r.mc->__ss,v);return;}
 if(n<29)r.mc->__ss.__x[n]=v;
 else if(n==29)__darwin_arm_thread_state64_set_fp(r.mc->__ss,(void *)(uintptr_t)v);
 else __darwin_arm_thread_state64_set_lr_fptr(r.mc->__ss,(void *)(uintptr_t)v);
}
static int64_t sext(uint64_t v,unsigned bits){return (int64_t)(v<<(64-bits))>>(64-bits);}
/* Exact checks for one decoded access. access: 1 read, 2 write, 3 both. */
static void check_access(uint64_t address,uint64_t size,unsigned access,uint64_t pc,uint32_t insn){
 if(address<guest_base||address-guest_base>guest_size||size>guest_size-(address-guest_base))fault_stop("checked guest memory violation (window)",pc,insn,address);
 uint64_t offset=address-guest_base;
 for(uint64_t p=offset/4096;p<=(offset+size-1)/4096;p++)if((permissions[p]&access)!=access)fault_stop("checked guest memory violation",pc,insn,address);
 if(access&2)for(uint32_t i=0;i<published_count;i++)if(published[i].begin<offset+size&&published[i].end>offset)fault_stop("write to published AOT code",pc,insn,address);
}
static unsigned char *alias_of(uint64_t address){return (unsigned char *)(uintptr_t)(alias_base+(address-guest_base));}
static uint64_t load_bytes(uint64_t address,unsigned size){
 unsigned char *a=alias_of(address);uint64_t v=0;
 if(!((uintptr_t)a&(size-1))){
  if(size==1)return __atomic_load_n(a,__ATOMIC_SEQ_CST);
  if(size==2)return __atomic_load_n((uint16_t *)a,__ATOMIC_SEQ_CST);
  if(size==4)return __atomic_load_n((uint32_t *)a,__ATOMIC_SEQ_CST);
  if(size==8)return __atomic_load_n((uint64_t *)a,__ATOMIC_SEQ_CST);
 }
 memcpy(&v,a,size);return v;
}
static void store_bytes(uint64_t address,unsigned size,uint64_t v){
 unsigned char *a=alias_of(address);
 if(!((uintptr_t)a&(size-1))){
  if(size==1){__atomic_store_n(a,(uint8_t)v,__ATOMIC_SEQ_CST);return;}
  if(size==2){__atomic_store_n((uint16_t *)a,(uint16_t)v,__ATOMIC_SEQ_CST);return;}
  if(size==4){__atomic_store_n((uint32_t *)a,(uint32_t)v,__ATOMIC_SEQ_CST);return;}
  if(size==8){__atomic_store_n((uint64_t *)a,v,__ATOMIC_SEQ_CST);return;}
 }
 memcpy(a,&v,size);
}
/* Exclusive monitors for LDXR/STXR emulated on narrowed pages. */
typedef struct { _Atomic(pthread_t) owner; uint64_t address; unsigned size,pair; uint64_t value[2]; } Monitor;
static Monitor monitors[64];
static Monitor *monitor(void){
 pthread_t self=pthread_self();
 for(unsigned i=0;i<64;i++)if(pthread_equal(atomic_load_explicit(&monitors[i].owner,memory_order_acquire),self))return monitors+i;
 for(unsigned i=0;i<64;i++){pthread_t none=0;if(atomic_compare_exchange_strong(&monitors[i].owner,&none,self))return monitors+i;}
 return NULL;
}
static int cas_bytes(uint64_t address,unsigned size,uint64_t expected,uint64_t desired,uint64_t *observed){
 unsigned char *a=alias_of(address);int ok=0;
 switch(size){
  case 1:{uint8_t e=(uint8_t)expected;ok=__atomic_compare_exchange_n(a,&e,(uint8_t)desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);*observed=e;break;}
  case 2:{uint16_t e=(uint16_t)expected;ok=__atomic_compare_exchange_n((uint16_t *)a,&e,(uint16_t)desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);*observed=e;break;}
  case 4:{uint32_t e=(uint32_t)expected;ok=__atomic_compare_exchange_n((uint32_t *)a,&e,(uint32_t)desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);*observed=e;break;}
  default:{uint64_t e=expected;ok=__atomic_compare_exchange_n((uint64_t *)a,&e,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);*observed=e;break;}
 }
 return ok;
}
static uint64_t mask_bits(unsigned size){return size==8?~UINT64_C(0):(UINT64_C(1)<<(8*size))-1;}
static int fault_native_allows(uintptr_t address,int write){
 vm_address_t region=(vm_address_t)address;vm_size_t length=0;vm_region_basic_info_data_64_t info;mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
 if(vm_region_64(mach_task_self(),&region,&length,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object)!=KERN_SUCCESS||region>address)return 0;
 return (info.protection&VM_PROT_READ)&&(!write||(info.protection&VM_PROT_WRITE));
}
/* Decode and emulate one AArch64 data access. Returns 1 when emulated, 0 for
   "not a narrowed guest page" (caller retries or stops), and never returns on
   a violation. Only forms observed in practice need to be complete; anything
   else in a narrowed page stops with the instruction word. */
static int emulate(Regs r,uint64_t pc,uint32_t insn){
 unsigned rt=insn&31,rn=(insn>>5)&31;uint64_t base=xget(r,rn,1);
 /* Load/store register: unsigned immediate, unscaled/pre/post, register offset. */
 int ui=(insn&0x3b000000u)==0x39000000u,imm9=(insn&0x3b200000u)==0x38000000u,reg=(insn&0x3b200c00u)==0x38200800u;
 if(ui||imm9||reg){
  unsigned size=insn>>30,v=(insn>>26)&1,opc=(insn>>22)&3,bytes;int load,sign=0,sign32=0;
  if(v){if(size==0&&opc>=2){bytes=16;load=opc==3;}else{if(opc>1)fault_stop("unsupported SIMD access",pc,insn,base);bytes=1u<<size;load=opc==1;}}
  else{bytes=1u<<size;load=opc!=0;if(opc==2){if(size==3)return 2;sign=1;}if(opc==3){if(size>=2)fault_stop("unsupported load",pc,insn,base);sign=1;sign32=1;}}
  uint64_t address=base;int writeback=0;uint64_t final=base;
  if(ui)address=base+(uint64_t)((insn>>10)&0xfff)*bytes;
  else if(imm9){int64_t imm=sext((insn>>12)&0x1ff,9);unsigned mode=(insn>>10)&3;
   if(mode==1){final=base+(uint64_t)imm;writeback=1;}else if(mode==3){address=base+(uint64_t)imm;final=address;writeback=1;}else address=base+(uint64_t)imm;}
  else{unsigned option=(insn>>13)&7,rm=(insn>>16)&31,shift=((insn>>12)&1)?(bytes==16?4:size):0;uint64_t index=xget(r,rm,0);
   if(option==2)index=(uint32_t)index;else if(option==6)index=(uint64_t)(int64_t)(int32_t)index;else if(option!=3&&option!=7)fault_stop("unsupported index",pc,insn,base);
   address=base+(index<<shift);}
  check_access(address,bytes,load?1:2,pc,insn);
  if(v){
   __uint128_t value=0;
   if(load){memcpy(&value,alias_of(address),bytes);r.mc->__ns.__v[rt]=value;}
   else{value=r.mc->__ns.__v[rt];memcpy(alias_of(address),&value,bytes);}
  }else if(load){uint64_t value=load_bytes(address,bytes);if(sign){value=(uint64_t)sext(value,8*bytes);if(sign32)value&=0xffffffffu;}xset(r,rt,value,0);}
  else store_bytes(address,bytes,xget(r,rt,0));
  if(writeback)xset(r,rn,final,1);
  return 1;
 }
 /* Load/store pair: no-allocate, post-index, offset, pre-index. */
 if((insn&0x3a000000u)==0x28000000u){
  unsigned opc=insn>>30,v=(insn>>26)&1,mode=(insn>>23)&3,load=(insn>>22)&1,rt2=(insn>>10)&31,bytes;int sign=0;
  if(v){if(opc==3)fault_stop("unsupported SIMD pair",pc,insn,base);bytes=4u<<opc;}
  else{if(opc==3||(opc==1&&!load))fault_stop("unsupported pair",pc,insn,base);bytes=opc==2?8:4;sign=opc==1;}
  int64_t imm=sext((insn>>15)&0x7f,7)*(int64_t)bytes;uint64_t address=mode==1?base:base+(uint64_t)imm;
  check_access(address,2*bytes,load?1:2,pc,insn);
  for(unsigned k=0;k<2;k++){unsigned t=k?rt2:rt;uint64_t a=address+k*bytes;
   if(v){__uint128_t value=0;if(load){memcpy(&value,alias_of(a),bytes);r.mc->__ns.__v[t]=value;}else{value=r.mc->__ns.__v[t];memcpy(alias_of(a),&value,bytes);}}
   else if(load){uint64_t value=load_bytes(a,bytes);if(sign)value=(uint64_t)sext(value,32);xset(r,t,value,0);}
   else store_bytes(a,bytes,xget(r,t,0));}
  if(mode==1||mode==3)xset(r,rn,base+(uint64_t)imm,1);
  return 1;
 }
 /* Exclusive, load-acquire/store-release and compare-and-swap. */
 if((insn&0x3f000000u)==0x08000000u){
  unsigned size=insn>>30,o2=(insn>>23)&1,load=(insn>>22)&1,o1=(insn>>21)&1,rs=(insn>>16)&31,rt2=(insn>>10)&31,bytes=1u<<size;
  if(o2&&!o1){check_access(base,bytes,load?1:2,pc,insn);if(load)xset(r,rt,load_bytes(base,bytes),0);else store_bytes(base,bytes,xget(r,rt,0));return 1;}
  if(o2&&o1){/* CAS{A}{L}{B,H,,} Rs,Rt,[Rn] */
   check_access(base,bytes,3,pc,insn);uint64_t observed;cas_bytes(base,bytes,xget(r,rs,0)&mask_bits(bytes),xget(r,rt,0)&mask_bits(bytes),&observed);xset(r,rs,observed,0);return 1;}
  if(!o2&&!o1){Monitor *m=monitor();if(!m)fault_stop("exclusive monitor table full",pc,insn,base);
   if(load){check_access(base,bytes,1,pc,insn);uint64_t value=load_bytes(base,bytes);m->address=base;m->size=bytes;m->pair=0;m->value[0]=value;xset(r,rt,value,0);return 1;}
   check_access(base,bytes,2,pc,insn);uint64_t observed;int ok=m->address==base&&m->size==bytes&&!m->pair&&cas_bytes(base,bytes,m->value[0],xget(r,rt,0)&mask_bits(bytes),&observed);
   m->size=0;xset(r,rs,ok?0:1,0);return 1;}
  (void)rt2;fault_stop("unsupported exclusive pair/CASP",pc,insn,base);
 }
 /* LSE atomic memory operations and LDAPR. */
 if((insn&0x3b200c00u)==0x38200000u&&!((insn>>26)&1)){
  unsigned size=insn>>30,rs=(insn>>16)&31,o3=(insn>>15)&1,opc=(insn>>12)&7,bytes=1u<<size;uint64_t m=mask_bits(bytes);
  if(o3&&opc==4){check_access(base,bytes,1,pc,insn);xset(r,rt,load_bytes(base,bytes),0);return 1;}
  if(o3&&opc!=0)fault_stop("unsupported atomic",pc,insn,base);
  check_access(base,bytes,3,pc,insn);uint64_t operand=xget(r,rs,0)&m,old=load_bytes(base,bytes),observed;
  for(;;){uint64_t next;unsigned bits=8*bytes;
   if(o3)next=operand;
   else switch(opc){case 0:next=old+operand;break;case 1:next=old&~operand;break;case 2:next=old^operand;break;case 3:next=old|operand;break;
    case 4:next=sext(old,bits)>sext(operand,bits)?old:operand;break;case 5:next=sext(old,bits)<sext(operand,bits)?old:operand;break;
    case 6:next=old>operand?old:operand;break;default:next=old<operand?old:operand;break;}
   if(cas_bytes(base,bytes,old,next&m,&observed))break;old=observed;}
  xset(r,rt,old,0);return 1;
 }
 /* LDAPUR/STLUR family. */
 if((insn&0x3f200c00u)==0x19000000u){
  unsigned size=insn>>30,opc=(insn>>22)&3,bytes=1u<<size;uint64_t address=base+(uint64_t)sext((insn>>12)&0x1ff,9);
  check_access(address,bytes,opc?1:2,pc,insn);
  if(!opc)store_bytes(address,bytes,xget(r,rt,0));
  else{uint64_t value=load_bytes(address,bytes);if(opc>=2){value=(uint64_t)sext(value,8*bytes);if(opc==3)value&=0xffffffffu;}xset(r,rt,value,0);}
  return 1;
 }
 /* SIMD LD1/ST1 multiple structures, 1-4 consecutive registers, optional post-index. */
 if((insn&0xbfbf0000u)==0x0c000000u||(insn&0xbfa00000u)==0x0c800000u){
  unsigned q=(insn>>30)&1,load=(insn>>22)&1,opcode=(insn>>12)&15,count=opcode==7?1:opcode==10?2:opcode==6?3:opcode==2?4:0,width=q?16:8;
  if(!count)fault_stop("unsupported SIMD structure access",pc,insn,base);
  check_access(base,(uint64_t)count*width,load?1:2,pc,insn);
  for(unsigned k=0;k<count;k++){unsigned t=(rt+k)&31;__uint128_t value=0;
   if(load){memcpy(&value,alias_of(base+k*width),width);r.mc->__ns.__v[t]=value;}else{value=r.mc->__ns.__v[t];memcpy(alias_of(base+k*width),&value,width);}}
  if(insn&(1u<<23)){unsigned rm=(insn>>16)&31;xset(r,rn,base+(rm==31?(uint64_t)count*width:xget(r,rm,0)),1);}
  return 1;
 }
 /* DC ZVA: zero one naturally aligned block. */
 if((insn&0xffffffe0u)==0xd50b7420u){
  uint64_t dczid;__asm__ volatile("mrs %0, dczid_el0":"=r"(dczid));uint64_t block=4u<<(dczid&15);
  uint64_t address=xget(r,rt,0)&~(block-1);check_access(address,block,2,pc,insn);memset(alias_of(address),0,block);return 1;
 }
 return 0;
}
static void chain(int signal_number,siginfo_t *info,void *context){
 struct sigaction *previous=signal_number==SIGBUS?&previous_bus:&previous_segv;
 if((previous->sa_flags&SA_SIGINFO)&&previous->sa_sigaction){previous->sa_sigaction(signal_number,info,context);return;}
 if(previous->sa_handler!=SIG_DFL&&previous->sa_handler!=SIG_IGN&&previous->sa_handler){previous->sa_handler(signal_number);return;}
 sigaction(signal_number,previous,NULL); /* re-fault with the default action */
}
static void fault_handler(int signal_number,siginfo_t *info,void *context){
 uintptr_t address=(uintptr_t)info->si_addr;
 if(address<guest_base||address>=guest_base+guest_size+65536){chain(signal_number,info,context);return;}
 int saved_errno=errno;
 Regs r={((ucontext_t *)context)->uc_mcontext};uint64_t pc=__darwin_arm_thread_state64_get_pc(r.mc->__ss);uint32_t insn=*(const uint32_t *)(uintptr_t)pc;
 if(address>=guest_base+guest_size)fault_stop("checked guest memory violation (window end)",pc,insn,address);
 /* A thread inside a bracket already excludes writers; others enter as a
    counted emulator and step aside while a publication is pending. */
 MemoryReader *self=reader_local_peek();int held=self&&atomic_load_explicit(&self->active,memory_order_seq_cst);
 if(!held)for(;;){
  atomic_fetch_add_explicit(&emulators,1,memory_order_seq_cst);
  if(!atomic_load_explicit(&writer_pending,memory_order_seq_cst))break;
  atomic_fetch_sub_explicit(&emulators,1,memory_order_seq_cst);sched_yield();
 }
 uint64_t page=(address-guest_base)/host_page;int result=0;
 if(narrowed[page])result=emulate(r,pc,insn);
 else{
  /* Not narrowed by this layer: either a protection change raced this fault
     (retry) or the owner's native mapping denies it, as before (stop). */
  int write=(int)((r.mc->__es.__esr>>6)&1); /* ESR WnR for data aborts */
  if(fault_native_allows(address,write))result=-1;
  else{
   uint64_t offset=address-guest_base;
   if((permissions[offset/4096]&(write?2:1))!=(write?2u:1u))fault_stop("checked guest memory violation",pc,insn,address);
   fault_stop("guest access to memory without native backing",pc,insn,address);
  }
 }
 if(result==0)fault_stop("unsupported faulting instruction in narrowed page",pc,insn,address);
 if(result>0){__darwin_arm_thread_state64_set_pc_fptr(r.mc->__ss,(void *)(uintptr_t)(pc+4));atomic_fetch_add_explicit(&emulated_accesses,1,memory_order_relaxed);}
 if(!held)atomic_fetch_sub_explicit(&emulators,1,memory_order_seq_cst);
 errno=saved_errno;
}
uint64_t tribes_memory_emulated_accesses(void){return atomic_load_explicit(&emulated_accesses,memory_order_relaxed);}
#endif

