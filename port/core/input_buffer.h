#ifndef TRIBESPAD_INPUT_BUFFER_H
#define TRIBESPAD_INPUT_BUFFER_H
#include <stdint.h>
/* Owner must serialize calls and prevent destruction while in use. DirectInput
   buffer size includes one reserved ring slot, so N stores at most N-1 events. */
typedef struct {uint32_t offset,data,timestamp,sequence;} TribesInputEvent;
typedef struct {TribesInputEvent *events;uint32_t size,head,count;int overflow;} TribesInputBuffer;
#define TB_INPUT_BUFFER_MAX 4096u
int tribes_input_buffer_resize(TribesInputBuffer *buffer,uint32_t requested);
void tribes_input_buffer_clear(TribesInputBuffer *buffer);
void tribes_input_buffer_destroy(TribesInputBuffer *buffer);
int tribes_input_buffer_push(TribesInputBuffer *buffer,const TribesInputEvent *event);
uint32_t tribes_input_buffer_read(TribesInputBuffer *buffer,TribesInputEvent *events,uint32_t requested,int peek,int *overflow);
#endif
