#include "input_buffer.h"
#include <stdlib.h>
#include <string.h>
int tribes_input_buffer_resize(TribesInputBuffer *buffer,uint32_t requested) {
    uint32_t size=requested>TB_INPUT_BUFFER_MAX?TB_INPUT_BUFFER_MAX:requested;
    TribesInputEvent *events=size?calloc(size,sizeof *events):NULL;if(size&&!events)return 0;
    free(buffer->events);*buffer=(TribesInputBuffer){.events=events,.size=size};return 1;
}
void tribes_input_buffer_clear(TribesInputBuffer *buffer) {buffer->head=buffer->count=0;buffer->overflow=0;}
void tribes_input_buffer_destroy(TribesInputBuffer *buffer) {free(buffer->events);memset(buffer,0,sizeof *buffer);}
int tribes_input_buffer_push(TribesInputBuffer *buffer,const TribesInputEvent *event) {
    if(!buffer->size)return 0;
    /* Preserve pending event order and report the lost incoming event. */
    if(buffer->count==buffer->size-1){buffer->overflow=1;return 0;}
    buffer->events[(buffer->head+buffer->count)%buffer->size]=*event;buffer->count++;return 1;
}
uint32_t tribes_input_buffer_read(TribesInputBuffer *buffer,TribesInputEvent *events,uint32_t requested,int peek,int *overflow) {
    uint32_t count=requested<buffer->count?requested:buffer->count;if(overflow)*overflow=buffer->overflow;
    if(events)for(uint32_t i=0;i<count;i++)events[i]=buffer->events[(buffer->head+i)%buffer->size];
    if(!peek){if(buffer->size)buffer->head=(buffer->head+count)%buffer->size;buffer->count-=count;buffer->overflow=0;}
    return count;
}
