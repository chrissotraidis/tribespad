#include "guest_cursors.h"
#include "guest_heap.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "cursor_theme.h"
typedef struct Cursor {uint64_t base;uint32_t handle;struct Cursor *next;} Cursor;
static Cursor *cursors;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;static uint32_t next_handle=0xd4000001;
static unsigned char rgba[32*32*4],mask[32*32];static pthread_once_t once=PTHREAD_ONCE_INIT;
static void initialize(void) {
    for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++) {
        unsigned i=y*32+x;unsigned glyph=tb_cursor_arrow[y][x];mask[i]=(glyph=='.');rgba[i*4+3]=(glyph=='.')?0:255;
        rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=(glyph=='W')?255:0;
    }
}
uint32_t tribes_load_cursor(uint64_t base,uint32_t module,uint32_t name) {
    if(module||name>0xffff){fprintf(stderr,"TribesPad: custom/named cursor resources are not implemented\n");abort();}
    if(name!=32512){if(name==32513||name==32514||name==32515||name==32516||name==32642||name==32643||name==32644||name==32645||name==32646||name==32648||name==32649||name==32650||name==32651){fprintf(stderr,"TribesPad: requested system cursor shape is not implemented\n");abort();}tribes_set_last_error(1814);return 0;}
    if(pthread_once(&once,initialize))abort();pthread_mutex_lock(&lock);
    for(Cursor *cursor=cursors;cursor;cursor=cursor->next)if(cursor->base==base){uint32_t handle=cursor->handle;pthread_mutex_unlock(&lock);return handle;}
    if(next_handle>=0xd5000000){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
    Cursor *cursor=calloc(1,sizeof *cursor);if(!cursor){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}cursor->base=base;cursor->handle=next_handle++;cursor->next=cursors;cursors=cursor;
    uint32_t handle=cursor->handle;pthread_mutex_unlock(&lock);
#ifdef TRIBESPAD_CURSOR_OBSERVER
    extern void tribes_cursor_observation(uint32_t,uint32_t,uint32_t,const TribesCursorView *);
    TribesCursorView view={32,32,0,0,32512,rgba,mask};tribes_cursor_observation(module,name,handle,&view);
#endif
    return handle;
}
int tribes_cursor_view(uint64_t base,uint32_t handle,TribesCursorView *view) {
    if(!view)return 0;pthread_mutex_lock(&lock);Cursor *cursor=cursors;while(cursor&&(cursor->base!=base||cursor->handle!=handle))cursor=cursor->next;
    if(cursor)*view=(TribesCursorView){32,32,0,0,32512,rgba,mask};pthread_mutex_unlock(&lock);return !!cursor;
}
