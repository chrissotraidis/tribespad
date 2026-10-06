#include "../port/core/keyboard_state.h"
#include "../port/core/keyboard_translation.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[600];static uint64_t base;
static void *worker(void *arg) {
    (void)arg;assert(tribes_get_key_state(base,20)==1);assert(!tribes_get_key_state(base,'A'));
    for(unsigned i=0;i<10000;i++) {
        assert(tribes_keyboard_apply(base,'A',1));assert(tribes_get_key_state(base,'A')==0xffff8000);
        assert(tribes_keyboard_apply(base,'A',0));assert(!tribes_get_key_state(base,'A'));
        assert(tribes_keyboard_apply(base,20,1));assert(tribes_get_key_state(base,20)==0xffff8000);
        assert(tribes_keyboard_apply(base,20,1));assert(tribes_get_key_state(base,20)==0xffff8000);
        assert(tribes_keyboard_apply(base,20,0));assert(!tribes_get_key_state(base,20));
        assert(tribes_keyboard_apply(base,20,1));assert(tribes_get_key_state(base,20)==0xffff8001);
        assert(tribes_keyboard_apply(base,20,0));assert(tribes_get_key_state(base,20)==1);
    }return NULL;
}
int main(void) {
    unsigned char seed[256]={0},state[256],before[256];base=(uintptr_t)memory;seed[20]=3;seed['B']=0x7f;
    assert(tribes_keyboard_configure(base,seed));memset(seed,0,sizeof seed);
    assert(!tribes_keyboard_configure(base,seed)&&errno==EEXIST);assert(!tribes_keyboard_configure(base+1,NULL)&&errno==EINVAL);
    assert(tribes_get_key_state(base,20)==1&&!tribes_get_key_state(base,'B')&&!tribes_get_key_state(base,256));
    for(unsigned i=0;i<3;i++) {unsigned left=0xa0+2*i,right=left+1;
        assert(tribes_keyboard_apply(base,left,1));assert(tribes_keyboard_apply(base,right,1));assert(tribes_get_key_state(base,16+i)==0xffff8000);
        assert(tribes_keyboard_apply(base,left,0));assert(tribes_get_key_state(base,16+i)==0xffff8000);
        assert(tribes_keyboard_apply(base,right,0));assert(!tribes_get_key_state(base,16+i));
    }
    tribes_keyboard_snapshot(base,before);assert(!tribes_keyboard_apply(base,16,1)&&errno==EINVAL);assert(!tribes_keyboard_apply(base,999,1));tribes_keyboard_snapshot(base,state);assert(!memcmp(before,state,256));
    for(unsigned vk=144;vk<=145;vk++){assert(tribes_keyboard_apply(base,vk,1));assert(tribes_get_key_state(base,vk)==0xffff8001);assert(tribes_keyboard_apply(base,vk,0));assert(tribes_get_key_state(base,vk)==1);}
    memset(memory,0xa5,sizeof memory);tribes_set_last_error(123);assert(tribes_get_keyboard_state(base,17));assert(tribes_get_last_error()==123&&memory[16]==0xa5&&memory[273]==0xa5);tribes_keyboard_snapshot(base,state);assert(!memcmp(memory+17,state,256));
    assert(!tribes_get_keyboard_state(base,0)&&tribes_get_last_error()==87);assert(!tribes_get_keyboard_state(base,UINT32_MAX));
    assert(tribes_to_ascii(base,'A',0,0,301,0)==1&&memory[301]=='A'&&memory[302]==0&&memory[300]==0xa5&&memory[303]==0xa5);
    assert(tribes_keyboard_apply(base,0xa1,1));assert(tribes_to_ascii(base,'A',0,0,301,0)==1&&memory[301]=='a');assert(tribes_keyboard_apply(base,0xa1,0));
    assert(tribes_keyboard_apply(base,0xa2,1));assert(tribes_to_ascii(base,'A',0,0,301,0)==1&&memory[301]==1);assert(tribes_keyboard_apply(base,0xa2,0));
    assert(tribes_keyboard_apply(base,'A',1));
    for(unsigned round=0;round<8;round++){pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));}
    assert(tribes_get_key_state(base,'A')==0xffff8000&&tribes_get_key_state(base,20)==1);
    assert(tribes_keyboard_configure(base+1,seed));assert(!tribes_get_key_state(base+1,20)&&!tribes_get_key_state(base+1,'A'));
    puts("PASS: 640000 thread-isolated press/release and autorepeat cycles, immutable seeds and fresh-thread state, left/right modifier aggregation, Caps/Num/Scroll toggles, sign-extended SHORT state, guarded snapshots and null-state ToAscii");
}
