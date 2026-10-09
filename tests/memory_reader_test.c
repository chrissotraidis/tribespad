/* Exercise the real admission barrier, including a rejected reservation that
   outlives writer unlock. Protected data is intentionally non-atomic so TSan
   also verifies the reader/writer synchronization. */
#include "../port/core/guest_memory_checks.c"
#include <assert.h>
static uint64_t value,complement=UINT64_MAX;
static atomic_uint started;
static void *reader(void *unused){
 (void)unused;atomic_fetch_add(&started,1);
 for(unsigned i=0;i<100000;i++){
  read_lock();uint64_t first=value;
  read_lock();assert(complement==~first);read_unlock();
  assert(value==first);read_unlock();
 }
 return NULL;
}
static void *writer(void *unused){
 (void)unused;while(atomic_load(&started)!=8)sched_yield();
 for(unsigned i=0;i<20000;i++){
  write_lock();value++;if(!(i%100))sched_yield();complement=~value;write_unlock();
 }
 return NULL;
}
int main(void){
 /* The writer may open admission while a rejected reader is paused before
    withdrawal. That outstanding reservation must survive writer unlock. */
 write_lock();uint32_t rejected=atomic_fetch_add_explicit(&guard_state,1,memory_order_acquire);
 assert(rejected&WRITER_PENDING);write_unlock();
 read_lock();assert(atomic_load(&guard_state)==2);reader_leave();
 assert(atomic_load(&guard_state)==1);read_unlock();assert(!atomic_load(&guard_state));
 /* Withdrawal while another writer is pending must release that writer. */
 pthread_t threads[9];for(unsigned i=0;i<8;i++)assert(!pthread_create(threads+i,NULL,reader,NULL));
 assert(!pthread_create(threads+8,NULL,writer,NULL));
 for(unsigned i=0;i<9;i++)assert(!pthread_join(threads[i],NULL));
 assert(value==20000&&complement==~value&&!atomic_load(&guard_state)&&!read_depth);
 puts("PASS: delayed rejected reservation, eight nested readers, 20000 publications and 800000 protected snapshots");
}
