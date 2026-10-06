#include "../port/core/input_buffer.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void *worker(void *argument) {
    uint32_t id=(uint32_t)(uintptr_t)argument;
    for(uint32_t pass=0;pass<2000;pass++) {
        TribesInputBuffer buffer={0};uint32_t size=2+pass%15;assert(tribes_input_buffer_resize(&buffer,size));
        for(uint32_t round=0;round<4;round++) {
            for(uint32_t i=0;i<size-1;i++){TribesInputEvent event={i+round*100,id,0xfffffffeu+i,i+round*1000};assert(tribes_input_buffer_push(&buffer,&event));}
            TribesInputEvent lost={999,999,999,999};assert(!tribes_input_buffer_push(&buffer,&lost));int overflow=0;
            struct {uint32_t before;TribesInputEvent events[16];uint32_t after;} output;memset(&output,0xa5,sizeof output);
            assert(tribes_input_buffer_read(&buffer,NULL,UINT32_MAX,1,&overflow)==size-1&&overflow);assert(buffer.count==size-1);
            assert(tribes_input_buffer_read(&buffer,output.events,1,1,&overflow)==1&&overflow);assert(output.events[0].offset==round*100&&output.events[0].data==id);
            assert(tribes_input_buffer_read(&buffer,output.events,size-1,0,&overflow)==size-1&&overflow);assert(!buffer.count&&!buffer.overflow);
            for(uint32_t i=0;i<size-1;i++){TribesInputEvent expected={i+round*100,id,0xfffffffeu+i,i+round*1000};assert(!memcmp(&expected,&output.events[i],sizeof expected));}
            assert(output.before==0xa5a5a5a5&&output.after==0xa5a5a5a5);
        }
        TribesInputEvent event={1,2,3,4};assert(tribes_input_buffer_push(&buffer,&event));assert(tribes_input_buffer_read(&buffer,NULL,UINT32_MAX,0,NULL)==1&&!buffer.count);assert(tribes_input_buffer_push(&buffer,&event));tribes_input_buffer_clear(&buffer);assert(!buffer.count&&!buffer.head&&!buffer.overflow);
        assert(tribes_input_buffer_resize(&buffer,0)&&!buffer.events&&!buffer.size);assert(!tribes_input_buffer_push(&buffer,&event)&&!buffer.overflow);
        assert(tribes_input_buffer_resize(&buffer,1));assert(!tribes_input_buffer_push(&buffer,&event)&&buffer.overflow);int overflow;assert(!tribes_input_buffer_read(&buffer,NULL,UINT32_MAX,0,&overflow)&&overflow&&!buffer.overflow);
        tribes_input_buffer_destroy(&buffer);assert(!buffer.events&&!buffer.size);
    }
    return NULL;
}
int main(void) {
    TribesInputBuffer buffer={0};assert(tribes_input_buffer_resize(&buffer,UINT32_MAX)&&buffer.size==TB_INPUT_BUFFER_MAX);tribes_input_buffer_destroy(&buffer);
    assert(tribes_input_buffer_resize(&buffer,128));TribesInputEvent event={0};for(unsigned i=0;i<127;i++)assert(tribes_input_buffer_push(&buffer,&event));assert(!tribes_input_buffer_push(&buffer,&event));tribes_input_buffer_destroy(&buffer);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: 16000 independently owned buffer lifetimes; 128-slot/127-event capacity, 64000 wrap/overflow cycles, four-DWORD event preservation, sticky peek overflow, consume/discard/clear, zero/one sizes and 4096-slot clamp; owner synchronization required");
}
