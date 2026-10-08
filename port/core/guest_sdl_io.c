#include "../llasm-support/llasm_cpu.h"
#include "guest_filesystem.h"
#include "guest_heap.h"
#include "guest_memory_checks.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original music decoder file streams. Native SDL owns stream storage; only
   process-owned scalar tokens and copied bytes cross the i686 boundary. */
typedef struct IO {uint64_t base;uint32_t token;SDL_IOStream *stream;struct IO *next;} IO;
static IO *streams;
static uint32_t next_token=0xd6000001;
static pthread_mutex_t io_lock=PTHREAD_MUTEX_INITIALIZER;
static _Noreturn void stop(void){fputs("TribesPad: unsupported SDL IO ownership or mode\n",stderr);abort();}
static void *guest(_cpu *cpu,uint32_t address,size_t bytes){
    if(!address||(uint64_t)address+bytes>UINT64_C(0x100000000))stop();
    return (void *)(uintptr_t)(cpu->_pointer_offset+address);
}
static void copy(void *to,const void *from,size_t n){
    uint32_t guard=tribes_memory_transfer_begin(to,from,n);memcpy(to,from,n);tribes_memory_access_end(guard);
}
static int string(_cpu *cpu,uint32_t address,char *out,size_t capacity){
    if(!address)return 0;
    for(size_t i=0;i<capacity;i++){
        if((uint64_t)address+i>UINT32_MAX)stop();
        copy(out+i,guest(cpu,address+(uint32_t)i,1),1);if(!out[i])return 1;
    }
    SDL_SetError("%s","SDL IO path is too long");return 0;
}
static IO **slot(_cpu *cpu,uint32_t token){
    IO **p=&streams;while(*p&&((*p)->base!=cpu->_pointer_offset||(*p)->token!=token))p=&(*p)->next;
    if(!*p)stop();return p;
}
uint32_t tribes_sdl_io_from_file(_cpu *cpu,uint32_t path,uint32_t mode){
    uint32_t error=tribes_get_last_error(),result=0;char name[PATH_MAX],m[16],host[PATH_MAX];
    if(!path||!mode){SDL_SetError("%s","Invalid SDL IO file or mode");goto done;}
    if(!string(cpu,path,name,sizeof name)||!string(cpu,mode,m,sizeof m))goto done;
    /* Only the reached read-only binary mode is admitted. Writes need the
       existing create/open confinement contract, not a raw host fopen. */
    if(strcmp(m,"rb"))stop();
    if(!tribes_filesystem_resolve(cpu->_pointer_offset,name,host,sizeof host)){
        SDL_SetError("%s","Cannot resolve SDL IO file in guest filesystem");goto done;
    }
    IO *record=calloc(1,sizeof *record);if(!record){SDL_SetError("%s","Out of memory");goto done;}
    record->stream=SDL_IOFromFile(host,m);
    if(!record->stream){free(record);goto done;}
    pthread_mutex_lock(&io_lock);
    if(next_token>=0xd7000000)stop();
    result=record->token=next_token++;record->base=cpu->_pointer_offset;record->next=streams;streams=record;
    pthread_mutex_unlock(&io_lock);
done:tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_close_io(_cpu *cpu,uint32_t token){
    uint32_t error=tribes_get_last_error();pthread_mutex_lock(&io_lock);bool result;
    if(token){IO **p=slot(cpu,token);IO *v=*p;result=SDL_CloseIO(v->stream);*p=v->next;free(v);}
    else result=SDL_CloseIO(NULL);
    pthread_mutex_unlock(&io_lock);tribes_set_last_error(error);return result;
}
static uint32_t position(_cpu *cpu,uint32_t token,uint64_t offset,int whence,int operation){
    uint32_t error=tribes_get_last_error();pthread_mutex_lock(&io_lock);
    SDL_IOStream *stream=token?(*slot(cpu,token))->stream:NULL;int64_t result;
    if(operation==0)result=SDL_SeekIO(stream,(int64_t)offset,(SDL_IOWhence)whence);
    else if(operation==1)result=SDL_TellIO(stream);
    else result=SDL_GetIOSize(stream);
    pthread_mutex_unlock(&io_lock);tribes_set_last_error(error);
    cpu->_edx=(uint32_t)((uint64_t)result>>32);return (uint32_t)result;
}
uint32_t tribes_sdl_seek_io(_cpu *cpu,uint32_t token,uint32_t low,uint32_t high,uint32_t whence){return position(cpu,token,((uint64_t)high<<32)|low,(int32_t)whence,0);}
uint32_t tribes_sdl_tell_io(_cpu *cpu,uint32_t token){return position(cpu,token,0,0,1);}
uint32_t tribes_sdl_get_io_size(_cpu *cpu,uint32_t token){return position(cpu,token,0,0,2);}
uint32_t tribes_sdl_read_io(_cpu *cpu,uint32_t token,uint32_t output,uint32_t size){
    uint32_t error=tribes_get_last_error();pthread_mutex_lock(&io_lock);
    SDL_IOStream *stream=token?(*slot(cpu,token))->stream:NULL;size_t result=0;
    if(!output||!size){unsigned char unused;result=SDL_ReadIO(stream,output?&unused:NULL,size);}
    else {
        void *data=malloc(size);
        if(!data)SDL_SetError("%s","Out of memory");
        else {result=SDL_ReadIO(stream,data,size);if(result>size)stop();if(result)copy(guest(cpu,output,result),data,result);free(data);}
    }
    pthread_mutex_unlock(&io_lock);tribes_set_last_error(error);return (uint32_t)result;
}
uint32_t tribes_sdl_read_u32le(_cpu *cpu,uint32_t token,uint32_t output){
    uint32_t error=tribes_get_last_error(),value=0;pthread_mutex_lock(&io_lock);
    SDL_IOStream *stream=token?(*slot(cpu,token))->stream:NULL;
    bool result=SDL_ReadU32LE(stream,output?&value:NULL);
    /* SDL publishes its zero-initialized partial value even at EOF. */
    if(output)copy(guest(cpu,output,4),&value,4);
    pthread_mutex_unlock(&io_lock);tribes_set_last_error(error);return result;
}
uint32_t tribes_sdl_get_simd_alignment(_cpu *cpu){
    (void)cpu;uint32_t error=tribes_get_last_error();size_t result=SDL_GetSIMDAlignment();
    if(result>UINT32_MAX)stop();tribes_set_last_error(error);return (uint32_t)result;
}
/* Adjacent original decoder extension selection and error text. SDL's
   comparison folds UTF-8, so host strcasecmp is not interchangeable. */
static char *decoder_string(_cpu *cpu,uint32_t address,uint32_t *length){
    uint32_t n=0;unsigned char value;
    for(;;n++){
        if(n==1048576||(uint64_t)address+n>UINT32_MAX)stop();
        copy(&value,guest(cpu,address+n,1),1);if(!value)break;
    }
    char *text=malloc((size_t)n+1);if(!text)stop();
    copy(text,guest(cpu,address,(size_t)n+1),(size_t)n+1);*length=n;return text;
}
uint32_t tribes_sdl_strcasecmp(_cpu *cpu,uint32_t left,uint32_t right){
    uint32_t error=tribes_get_last_error(),n;
    char *a=decoder_string(cpu,left,&n),*b=decoder_string(cpu,right,&n);
    int result=SDL_strcasecmp(a,b);free(a);free(b);
    tribes_set_last_error(error);return (uint32_t)result;
}
uint32_t tribes_sdl_strlcpy(_cpu *cpu,uint32_t destination,uint32_t source,uint32_t capacity){
    uint32_t error=tribes_get_last_error(),length;
    char *text=decoder_string(cpu,source,&length);
    if(capacity){
        uint32_t n=length<capacity?length:capacity-1;text[n]=0;
        copy(guest(cpu,destination,(size_t)n+1),text,(size_t)n+1);
    }
    free(text);tribes_set_last_error(error);return length;
}
