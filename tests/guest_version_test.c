#include "../port/core/guest_version.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static void *exercise(void *unused) {
    (void)unused;
    unsigned char storage[180],before[180];uint64_t base=(uintptr_t)storage;
    for(unsigned i=0;i<16000;i++) {
        memset(storage,0xa5,sizeof storage);uint32_t size=(i&1)?148:156;memcpy(storage+1,&size,4);
        tribes_set_last_error(333);assert(tribes_get_version_ex(base,1));assert(tribes_get_last_error()==333);
        uint32_t fields[5];memcpy(fields,storage+1,sizeof fields);
        uint32_t packed=tribes_get_version();assert((packed&255)==fields[1]&&((packed>>8)&255)==fields[2]&&(packed>>16)==fields[3]&&!(packed&0x80000000));assert(tribes_get_last_error()==333);
        assert(fields[0]==size&&fields[1]==5&&fields[2]==1&&fields[3]==2600&&fields[4]==2);
        assert(!strcmp((char *)storage+21,"Service Pack 3"));
        for(unsigned j=36;j<=148;j++)assert(storage[j]==0);
        if(size==156){uint16_t sp,minor,suite;memcpy(&sp,storage+149,2);memcpy(&minor,storage+151,2);memcpy(&suite,storage+153,2);assert(sp==3&&minor==0&&suite==0x100&&storage[155]==1&&storage[156]==0);}
        assert(storage[0]==0xa5);for(unsigned j=size+1;j<sizeof storage;j++)assert(storage[j]==0xa5);
    }
    for(uint32_t size=0;size<180;size++)if(size!=148&&size!=156){memset(storage,0xa5,sizeof storage);memcpy(storage+1,&size,4);memcpy(before,storage,sizeof before);assert(!tribes_get_version_ex(base,1));assert(tribes_get_last_error()==87&&!memcmp(storage,before,sizeof before));}
    assert(!tribes_get_version_ex(base,0));assert(!tribes_get_version_ex(base,UINT32_MAX));assert(!tribes_get_version_ex(UINT64_MAX,1));
    return NULL;
}
int main(void) {
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,exercise,NULL));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: 128000 unaligned version-info layouts, guard bytes, invalid sizes and per-thread error preservation");
}
