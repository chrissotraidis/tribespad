#include "../port/core/guest_cursors.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static _Atomic uint32_t shared;
static void *worker(void *argument) {
    (void)argument;tribes_set_last_error(123);
    for(unsigned i=0;i<10000;i++){uint32_t handle=tribes_load_cursor(1,0,32512),expected=0;assert(handle);atomic_compare_exchange_strong(&shared,&expected,handle);assert(handle==shared&&tribes_get_last_error()==123);
        TribesCursorView view;assert(tribes_cursor_view(1,handle,&view)&&view.width==32&&view.height==32&&!view.hotspot_x&&!view.hotspot_y&&view.system_id==32512);
        assert(view.rgba[3]==255&&view.rgba[0]==0&&view.and_mask[0]==0);unsigned inside=(4*32+1)*4;assert(view.rgba[inside]==255&&view.rgba[inside+3]==255);assert(view.rgba[31*4+3]==0&&view.and_mask[31]==1);
        if(!i)for(unsigned p=0;p<1024;p++){assert(view.rgba[p*4]==view.rgba[p*4+1]&&view.rgba[p*4+1]==view.rgba[p*4+2]);assert(view.rgba[p*4+3]==(view.and_mask[p]?0:255));if(view.and_mask[p])assert(!view.rgba[p*4]);}
    }return NULL;
}
int main(void) {
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    uint32_t other=tribes_load_cursor(2,0,32512);assert(other&&other!=shared);TribesCursorView view,before;memset(&view,0xa5,sizeof view);before=view;
    assert(!tribes_cursor_view(2,shared,&view)&&!memcmp(&view,&before,sizeof view));assert(!tribes_cursor_view(1,0,&view)&&!memcmp(&view,&before,sizeof view));assert(!tribes_cursor_view(1,shared,NULL));
    assert(!tribes_load_cursor(1,0,0)&&tribes_get_last_error()==1814);assert(!tribes_load_cursor(1,0,42)&&tribes_get_last_error()==1814);assert(tribes_load_cursor(2,0,32512)==other);
    puts("PASS: 80000 concurrent initial/cache cursor loads, shared handle identity and owning-process separation, immutable full arrow pixels/AND mask, visible fill/outline/hotspot geometry, transparent XOR zeros, missing-ID errors and guarded lookup failures");
}
