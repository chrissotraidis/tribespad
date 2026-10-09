/* Exercise actual reader admission, nested access, registration and teardown.
   Protected data is non-atomic so TSan verifies publication synchronization. */
#include "../port/core/guest_memory_checks.c"
#include <assert.h>
static uint64_t value,complement=UINT64_MAX;
static atomic_uint started,snapshots,churn_done,churn_publications;
static pthread_key_t late_key;
static void *reader(void *unused){
 (void)unused;atomic_fetch_add(&started,1);
 for(unsigned i=0;i<100000;i++){
  read_lock();uint64_t first=value;
  read_lock();assert(complement==~first);read_unlock();
  assert(value==first);read_unlock();atomic_fetch_add(&snapshots,1);
 }
 return NULL;
}
static void *short_reader(void *unused){
 (void)unused;read_lock();assert(complement==~value);read_unlock();
 assert(!pthread_setspecific(late_key,(void *)1));return NULL;
}
static void late_destroy(void *unused){(void)unused;read_lock();assert(complement==~value);read_unlock();}
static void *churn_writer(void *unused){
 (void)unused;
 while(!atomic_load(&churn_done)){
  write_lock();value++;complement=~value;write_unlock();atomic_fetch_add(&churn_publications,1);
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
 read_lock();read_lock();assert(read_depth==2&&atomic_load(&reader_local.node->active));
 read_unlock();assert(atomic_load(&reader_local.node->active));read_unlock();
 assert(!atomic_load(&reader_local.node->active));
 // Newly created readers must wait if registration meets an active publisher.
 write_lock();
 pthread_t threads[9];for(unsigned i=0;i<8;i++)assert(!pthread_create(threads+i,NULL,reader,NULL));
 while(atomic_load(&started)!=8)sched_yield();
 assert(!atomic_load(&snapshots));
 write_unlock();assert(!pthread_create(threads+8,NULL,writer,NULL));
 for(unsigned i=0;i<9;i++)assert(!pthread_join(threads[i],NULL));
 assert(value==20000&&complement==~value&&!atomic_load(&writer_pending)&&!read_depth);
 // Every exited worker must have removed its node; the main thread remains.
 assert(readers==reader_local.node&&!readers->next&&!atomic_load(&readers->active));
 assert(atomic_load(&snapshots)==800000);
 assert(!pthread_key_create(&late_key,late_destroy));
 // Register and destroy nodes while a live publisher repeatedly scans them.
 pthread_t publisher;assert(!pthread_create(&publisher,NULL,churn_writer,NULL));
 while(!atomic_load(&churn_publications))sched_yield();
 for(unsigned i=0;i<200;i++){
  pthread_t child;assert(!pthread_create(&child,NULL,short_reader,NULL));assert(!pthread_join(child,NULL));
 }
 atomic_store(&churn_done,1);assert(!pthread_join(publisher,NULL));
 assert(value==20000+atomic_load(&churn_publications)&&complement==~value);
 assert(readers==reader_local.node&&!readers->next&&!atomic_load(&readers->active));
 assert(!pthread_key_delete(late_key));
 puts("PASS: registration under publication, nested readers, 200 thread lifetimes, 20000 publications and 800000 snapshots");
}
