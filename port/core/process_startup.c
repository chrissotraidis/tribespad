/* Original 32-bit STARTUPINFOA/W layout, filled from explicit host launch metadata.
   https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfoa
   A/W share this layout and the configured string pointers are all NULL.
   https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow
   Initial descriptor handles are captured at launch, not re-read after SetStdHandle. */
#include "process_startup.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Launch { uint64_t base;unsigned char info[68];struct Launch *next; } Launch;
static Launch *launches;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
int tribes_startup_configure(uint64_t base,uint16_t show,const uint32_t handles[3]) {
    if(!handles||show>11||show==10){errno=EINVAL;return 0;}
    Launch *entry=calloc(1,sizeof *entry);if(!entry)return 0;entry->base=base;
    uint32_t size=68,flags=0x101;memcpy(entry->info,&size,4);memcpy(entry->info+44,&flags,4);memcpy(entry->info+48,&show,2);memcpy(entry->info+56,handles,12);
    pthread_mutex_lock(&lock);for(Launch *p=launches;p;p=p->next)if(p->base==base){pthread_mutex_unlock(&lock);free(entry);errno=EALREADY;return 0;}
    entry->next=launches;launches=entry;pthread_mutex_unlock(&lock);return 1;
}
void tribes_get_startup_info(uint64_t base,uint32_t output) {
    if(!output||output>UINT32_MAX-67||base>UINTPTR_MAX-output-67)abort();
    pthread_mutex_lock(&lock);Launch *entry=launches;while(entry&&entry->base!=base)entry=entry->next;
    if(!entry){fprintf(stderr,"TribesPad: launch metadata was not configured\n");abort();}
    memcpy((void *)(uintptr_t)(base+output),entry->info,sizeof entry->info);pthread_mutex_unlock(&lock);
}

/* NT-family Win32 retains the DOS API as an identity operation. It neither
   changes the native handle registry nor opens descriptors.
   https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-sethandlecount
   https://www.winehq.org/pipermail/wine-cvs/2010-September/070039.html */
uint32_t tribes_set_handle_count(uint32_t requested) { return requested; }
