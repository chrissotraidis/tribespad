/* Original guest WNDCLASS and finite native window-procedure dispatch.
   https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassa
   https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unregisterclassa
   Window creation/message delivery and non-ASCII/global class namespaces remain gates. */
#include "window_classes.h"
#include "guest_heap.h"
#include "../llasm-support/llasm_cpu.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
extern void (*tribes_lookup(uint32_t))(_cpu *);
typedef struct Class { uint64_t base;uint32_t atom,windows,fields[10];char name[256];unsigned char *extra;struct Class *next; } Class;
static Class *classes;
static uint32_t next_atom=0xc000;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int name_copy(uint64_t base,uint32_t pointer,char out[256]) {
    if(pointer<=UINT16_MAX||pointer>UINT32_MAX-255||base>UINTPTR_MAX-pointer-255){tribes_set_last_error(87);return 0;}
    const char *name=(const char *)(uintptr_t)(base+pointer);size_t n=strnlen(name,256);if(!n||n>=256){tribes_set_last_error(87);return 0;}
    for(size_t i=0;i<n;i++)if((unsigned char)name[i]>=128){tribes_set_last_error(50);return 0;}
    memcpy(out,name,n+1);return 1;
}
static Class *find(uint64_t base,uint32_t name,uint32_t instance,const char *text) {
    for(Class *p=classes;p;p=p->next)if(p->base==base&&p->fields[4]==instance&&(name<=UINT16_MAX?p->atom==name:!strcasecmp(p->name,text)))return p;
    return NULL;
}
uint32_t tribes_register_class(uint64_t base,uint32_t descriptor) {
    if(!descriptor||descriptor>UINT32_MAX-39||base>UINTPTR_MAX-descriptor-39){tribes_set_last_error(87);return 0;}
    uint32_t fields[10];memcpy(fields,(void *)(uintptr_t)(base+descriptor),40);char name[256];if(!name_copy(base,fields[9],name))return 0;
    if(!fields[1]||fields[2]>INT32_MAX||fields[3]>INT32_MAX){tribes_set_last_error(87);return 0;}
    if(fields[0]&0x4000||fields[8]){tribes_set_last_error(50);return 0;}
    (void)tribes_lookup(fields[1]); /* Unknown original PCs fail closed; no JIT callbacks. */
    if(fields[2]>1048576||fields[3]>1048576){tribes_set_last_error(8);return 0;}
    Class *entry=calloc(1,sizeof *entry);if(!entry){tribes_set_last_error(8);return 0;}entry->base=base;memcpy(entry->fields,fields,40);strcpy(entry->name,name);
    if(fields[2]){entry->extra=calloc(1,fields[2]);if(!entry->extra){free(entry);tribes_set_last_error(8);return 0;}}
    pthread_mutex_lock(&lock);
    if(find(base,fields[9],fields[4],name)){pthread_mutex_unlock(&lock);free(entry->extra);free(entry);tribes_set_last_error(1410);return 0;}
    if(next_atom>UINT16_MAX){pthread_mutex_unlock(&lock);free(entry->extra);free(entry);tribes_set_last_error(8);return 0;}
    entry->atom=next_atom++;entry->next=classes;classes=entry;uint32_t atom=entry->atom;pthread_mutex_unlock(&lock);return atom;
}
uint32_t tribes_unregister_class(uint64_t base,uint32_t name,uint32_t instance) {
    char text[256];if(name>UINT16_MAX&&!name_copy(base,name,text))return 0;
    pthread_mutex_lock(&lock);Class *p=find(base,name,instance,text);
    if(!p){pthread_mutex_unlock(&lock);tribes_set_last_error(1411);return 0;}
    if(p->windows){pthread_mutex_unlock(&lock);tribes_set_last_error(1412);return 0;}
    Class **link=&classes;while(*link!=p)link=&(*link)->next;*link=p->next;pthread_mutex_unlock(&lock);free(p->extra);free(p);return 1;
}
uint32_t tribes_class_acquire(uint64_t base,uint32_t name,uint32_t instance,uint32_t fields[10]) {
    if(!fields)return 0;char text[256];if(name>UINT16_MAX&&!name_copy(base,name,text))return 0;
    pthread_mutex_lock(&lock);Class *p=find(base,name,instance,text);if(!p||p->windows==UINT32_MAX){pthread_mutex_unlock(&lock);return 0;}
    p->windows++;memcpy(fields,p->fields,40);fields[9]=p->atom;uint32_t atom=p->atom;pthread_mutex_unlock(&lock);return atom;
}
void tribes_class_release(uint64_t base,uint32_t atom) {
    pthread_mutex_lock(&lock);for(Class *p=classes;p;p=p->next)if(p->base==base&&p->atom==atom){if(!p->windows)abort();p->windows--;pthread_mutex_unlock(&lock);return;}abort();
}
