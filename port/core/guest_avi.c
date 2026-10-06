#include "guest_avi.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
/* AVIFileInit/Exit maintain the process library reference count, independently
   of file/stream handles. No codec, file or successful decode is fabricated.
   https://learn.microsoft.com/en-us/windows/win32/api/vfw/nf-vfw-avifileinit */
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t references;
static void change(int init){
    if(pthread_mutex_lock(&lock))abort();
    if((init&&references==UINT32_MAX)||(!init&&!references)){
        fputs("TribesPad: unbalanced AVI library lifetime\n",stderr);abort();
    }
    if(init)++references;else --references;
    if(pthread_mutex_unlock(&lock))abort();
}
void tribes_avi_file_init(void){change(1);}
void tribes_avi_file_exit(void){change(0);}
uint32_t tribes_avi_reference_count(void){
    if(pthread_mutex_lock(&lock))abort();uint32_t count=references;
    if(pthread_mutex_unlock(&lock))abort();return count;
}
