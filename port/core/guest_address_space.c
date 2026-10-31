/* Original-x86 MEMORY_BASIC_INFORMATION32 over declared loader mappings and
   actual native provider state, not Darwin mmap residency or host addresses.
   https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery */
#include "guest_address_space.h"
#include "guest_virtual_memory.h"
#include "process_heap.h"
#include "guest_heap.h"
#include "guest_system.h"
#include "guest_memory_checks.h"
#include "guest_memory_layout.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define STATIC_END UINT32_C(0x30000000)
#define VM_BEGIN tribes_virtual_memory_low
#define USER_END (TRIBES_GUEST_MAXIMUM_ADDRESS+1u)
typedef struct Page {uint32_t allocation,allocation_protect,state,protect,type;} Page;
typedef struct Space {uint64_t base;Page *pages;int sealed,checked;TribesCheckedMemoryBackend backend;unsigned char *capture_pages;uint32_t capture_allocation;struct Space *next;} Space;
static Space *spaces;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Space *find(uint64_t base){for(Space *s=spaces;s;s=s->next)if(s->base==base)return s;return NULL;}
static uint32_t fail(uint32_t error){tribes_set_last_error(error);return 0;}
static int valid_protection(uint32_t p){return p==1||p==2||p==4||p==8||p==0x10||p==0x20||p==0x40||p==0x80;}
static uint32_t data_protection(uint32_t p){return p==0x20?2:p==0x40?4:p;}
static uint32_t capture_allocate(uint64_t base,uint32_t address,uint32_t size,uint32_t type,uint32_t protection){
 pthread_mutex_lock(&lock);Space *s=find(base);
 if(!s||!s->checked||!s->capture_pages||address!=s->capture_allocation||size!=65536||type!=0x3000||protection!=0x40){pthread_mutex_unlock(&lock);return fail(50);}
 for(uint32_t p=address/4096;p<(address+size)/4096;p++)if(s->pages[p].state){pthread_mutex_unlock(&lock);return fail(487);}
 unsigned char access[16];memset(access,3,sizeof access);
 if(!s->backend.protect(base,address,size,access)){pthread_mutex_unlock(&lock);return fail(5);}
 memset((void *)(uintptr_t)(base+address),0,size);
 Page value={address,0x40,0x1000,0x40,0x20000};for(uint32_t p=address/4096;p<(address+size)/4096;p++)s->pages[p]=value;
 pthread_mutex_unlock(&lock);return address;
}
uint32_t tribes_address_space_enable_patch_capture(uint64_t base,uint32_t allocation,const uint32_t (*ranges)[2],uint32_t count){
 if(!allocation||allocation%65536||(uint64_t)allocation+65536>STATIC_END||!ranges||!count||count>32)return fail(87);
 pthread_mutex_lock(&lock);Space *s=find(base);
 if(!s||!s->sealed||!s->checked||s->capture_pages){pthread_mutex_unlock(&lock);return fail(50);}
 for(uint32_t p=allocation/4096;p<(allocation+65536)/4096;p++)if(s->pages[p].state){pthread_mutex_unlock(&lock);return fail(487);}
 unsigned char *allowed=calloc(STATIC_END/4096,1);if(!allowed){pthread_mutex_unlock(&lock);return fail(8);}
 for(uint32_t i=0;i<count;i++){
  uint32_t address=ranges[i][0],size=ranges[i][1];
  if(!size||(address|size)%4096||(uint64_t)address+size>STATIC_END){free(allowed);pthread_mutex_unlock(&lock);return fail(87);}
  for(uint32_t p=address/4096;p<(address+size)/4096;p++){
   if(s->pages[p].state!=0x1000||s->pages[p].type!=0x1000000){free(allowed);pthread_mutex_unlock(&lock);return fail(487);}
   allowed[p]=1;
  }
 }
 memset(allowed+allocation/4096,1,16);
 if(!tribes_virtual_memory_enable_patch_capture(base,capture_allocate)){free(allowed);pthread_mutex_unlock(&lock);return fail(50);}
 s->capture_pages=allowed;s->capture_allocation=allocation;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_address_space_begin(uint64_t base){
    if(base>UINTPTR_MAX-USER_END)return fail(87);
    pthread_mutex_lock(&lock);if(find(base)){pthread_mutex_unlock(&lock);return fail(87);}
    Space *s=calloc(1,sizeof *s);Page *pages=calloc(STATIC_END/4096,sizeof *pages);
    if(!s||!pages){free(s);free(pages);pthread_mutex_unlock(&lock);return fail(8);}
    s->base=base;s->pages=pages;s->next=spaces;spaces=s;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_address_space_declare(uint64_t base,uint32_t address,uint32_t size,uint32_t allocation,uint32_t allocation_protect,uint32_t state,uint32_t protection,uint32_t type){
    if(!size||(address|size|allocation)%4096||(uint64_t)address+size>STATIC_END||allocation>address||!valid_protection(allocation_protect)||(state!=0x1000&&state!=0x2000)||(state==0x1000?!valid_protection(protection):protection!=0)||(type!=0x20000&&type!=0x1000000))return fail(87);
    pthread_mutex_lock(&lock);Space *s=find(base);
    if(!s||s->sealed){pthread_mutex_unlock(&lock);return fail(87);}
    for(uint32_t i=address/4096;i<(address+size)/4096;i++)if(s->pages[i].state){pthread_mutex_unlock(&lock);return fail(487);}
    Page value={allocation,allocation_protect,state,protection,type};for(uint32_t i=address/4096;i<(address+size)/4096;i++)s->pages[i]=value;
    pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_address_space_seal(uint64_t base){
    pthread_mutex_lock(&lock);Space *s=find(base);if(!s||s->sealed){pthread_mutex_unlock(&lock);return fail(87);}s->sealed=1;pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_address_space_enable_checked(uint64_t base,const TribesCheckedMemoryBackend *backend){
 if(!backend||!backend->initialize||!backend->protect)return fail(50);
 pthread_mutex_lock(&lock);Space *s=find(base);
 if(!s||!s->sealed||s->checked){pthread_mutex_unlock(&lock);return fail(87);}
 unsigned char *access=malloc(STATIC_END/4096);if(!access){pthread_mutex_unlock(&lock);return fail(8);}
 for(uint32_t i=0;i<STATIC_END/4096;i++){
  Page p=s->pages[i];
  if(p.state==0x1000&&p.protect!=1&&p.protect!=2&&p.protect!=4){free(access);pthread_mutex_unlock(&lock);return fail(50);}
  access[i]=p.state!=0x1000||p.protect==1?0:p.protect==2?1:3;
 }
 int ok=backend->initialize(base,access,STATIC_END/4096);free(access);if(ok){s->backend=*backend;s->checked=1;}pthread_mutex_unlock(&lock);return ok?1:fail(50);
}
uint32_t tribes_address_space_protect(uint64_t base,uint32_t address,uint32_t size,uint32_t protection){
    if(!size||(address|size)%4096||(uint64_t)address+size>STATIC_END||!valid_protection(protection))return fail(87);
    pthread_mutex_lock(&lock);Space *s=find(base);if(!s){pthread_mutex_unlock(&lock);return fail(87);}
    if(s->checked){pthread_mutex_unlock(&lock);return fail(50);}
    uint32_t allocation=s->pages[address/4096].allocation;
    for(uint32_t i=address/4096;i<(address+size)/4096;i++)if(s->pages[i].state!=0x1000||s->pages[i].allocation!=allocation){pthread_mutex_unlock(&lock);return fail(487);}
    for(uint32_t i=address/4096;i<(address+size)/4096;i++)s->pages[i].protect=protection;
    pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_address_space_commit_scratch(uint64_t base,uint32_t address,uint32_t size){
 long native_page=sysconf(_SC_PAGESIZE);
 if(native_page<4096||native_page>65536||(native_page&(native_page-1))||size!=(uint32_t)native_page||
    (base|address)%size||address<0x2f000000||(uint64_t)address+size>STATIC_END||base>UINTPTR_MAX-STATIC_END)return fail(87);
 pthread_mutex_lock(&lock);Space *s=find(base);
 if(!s||!s->sealed){pthread_mutex_unlock(&lock);return fail(87);}
 Page value={0x2f000000,4,0x1000,4,0x20000};int existing=1;
 for(uint32_t p=address/4096;p<(address+size)/4096;p++){
  if(s->pages[p].state){if(memcmp(&s->pages[p],&value,sizeof value)){pthread_mutex_unlock(&lock);return fail(487);}}
  else existing=0;
 }
 /* Never regrant protection on an already committed page. New pages are
    published only after the native and checked backend commit succeeds. */
 if(existing){pthread_mutex_unlock(&lock);return 1;}
 for(uint32_t p=address/4096;p<(address+size)/4096;p++)if(s->pages[p].state){pthread_mutex_unlock(&lock);return fail(487);}
 int ok;
 if(s->checked){unsigned char access[16];memset(access,3,size/4096);ok=s->backend.protect(base,address,size,access);}
 else ok=!mprotect((void *)(uintptr_t)(base+address),size,PROT_READ|PROT_WRITE);
 if(ok)for(uint32_t p=address/4096;p<(address+size)/4096;p++)s->pages[p]=value;
 pthread_mutex_unlock(&lock);return ok?1:fail(5);
}
typedef struct HeapQuery {uint32_t address,end;int committed;} HeapQuery;
static int heap_run(uint32_t address,uint32_t size,void *context){
    HeapQuery *q=context;uint32_t end=address+size;
    if(q->address>=address&&q->address<end){q->committed=1;q->end=end;}
    else if(q->committed&&address==q->end)q->end=end;
    else if(!q->committed&&address>q->address&&address<q->end)q->end=address;
    return 1;
}
/* Ownership domains are disjoint and permanently reserved for heap providers.
   Query a dynamic domain under its provider lock; no torn cross-provider state. */
static int query(Space *s,uint32_t address,uint32_t out[7]){
    if(address>=VM_BEGIN)return tribes_virtual_memory_query(s->base,address,out);
    if(address>=tribes_process_heap_low+0x10000000u){uint32_t allocation=address&~65535u;uint32_t fields[]={address,allocation,1,allocation+65536-address,0x2000,0,0x20000};memcpy(out,fields,28);return 1;}
    if(address>=tribes_process_heap_low)return tribes_process_heap_memory_query(s->base,address,out);
    if(address>=STATIC_END){HeapQuery q={address,tribes_process_heap_low,0};if(!tribes_heap_visit(s->base,heap_run,&q))return 0;uint32_t fields[]={address,STATIC_END,4,q.end-address,q.committed?0x1000u:0x2000u,q.committed?4u:0u,0x20000};memcpy(out,fields,28);return 1;}
    uint32_t index=address/4096,end=index+1;Page p=s->pages[index];
    while(end<STATIC_END/4096&&!memcmp(&p,&s->pages[end],sizeof p))end++;
    uint32_t fields[]={address,p.allocation,p.allocation_protect,(end-index)*4096u,p.state?p.state:0x10000,p.protect,p.type};memcpy(out,fields,28);return 1;
}
/* Exact native backing only. Independent 4 KiB subpages, executable patch
   publication, modifiers and dynamic owner arenas require additional support.
   Return ERROR_NOT_SUPPORTED with no mutation for those cases; never report
   success for a metadata-only protection change. PE bytes remain nonexecutable. */
uint32_t tribes_virtual_protect(uint64_t base,uint32_t address,uint32_t size,uint32_t protection,uint32_t output){
    uint64_t end=((uint64_t)address+size+4095)&~UINT64_C(4095);
    uint32_t start=address&~4095u;
    if(!size||end>USER_END)return fail(87);
    if(protection!=1&&protection!=2&&protection!=4&&protection!=0x20&&protection!=0x40)return fail(50);
    if(end>STATIC_END)return fail(50);
    long host_page=sysconf(_SC_PAGESIZE);
    if(host_page<4096||host_page>65536||(host_page&(host_page-1))||base%(uint64_t)host_page)return fail(50);
    uint32_t lo=start&~((uint32_t)host_page-1),hi=(uint32_t)((end+host_page-1)&~((uint64_t)host_page-1));
    pthread_mutex_lock(&lock);Space *s=find(base);
    if(!s||!s->sealed){pthread_mutex_unlock(&lock);return fail(50);}
    if(protection==0x20||protection==0x40){
        if(!s->checked||!s->capture_pages){pthread_mutex_unlock(&lock);return fail(50);}
        for(uint32_t p=start/4096;p<end/4096;p++)if(!s->capture_pages[p]){pthread_mutex_unlock(&lock);return fail(50);}
    }
    uint32_t allocation=s->pages[start/4096].allocation,old=s->pages[start/4096].protect;
    for(uint32_t page=start;page<end;page+=4096)if(s->pages[page/4096].state!=0x1000||s->pages[page/4096].allocation!=allocation){pthread_mutex_unlock(&lock);return fail(487);}
    if(!output||(uint64_t)output+4>USER_END){pthread_mutex_unlock(&lock);return fail(998);}
    for(uint32_t page=output&~4095u;page<(uint64_t)output+4;page+=4096){
        uint32_t fields[7];
        if(!query(s,page,fields)||fields[4]!=0x1000||(fields[5]!=4&&fields[5]!=8&&fields[5]!=0x40&&fields[5]!=0x80)||(page>=start&&page<end&&data_protection(protection)!=4)){pthread_mutex_unlock(&lock);return fail(998);}
    }
    /* Both the preimage and final state must be exactly representable by native
       pages. This also supplies an exact rollback if the host call fails. */
    if(s->checked){
        unsigned char *access=malloc((hi-lo)/4096);if(!access){pthread_mutex_unlock(&lock);return fail(8);}
        for(uint32_t page=lo;page<hi;page+=4096){
            Page p=s->pages[page/4096];uint32_t value=data_protection((page>=start&&page<end)?protection:p.protect);
            if(p.state==0x1000&&value!=1&&value!=2&&value!=4){free(access);pthread_mutex_unlock(&lock);return fail(50);}
            access[(page-lo)/4096]=p.state!=0x1000||value==1?0:value==2?1:3;
        }
        int ok=s->backend.protect(base,lo,hi-lo,access);free(access);
        if(!ok){pthread_mutex_unlock(&lock);return fail(5);}
        for(uint32_t page=start;page<end;page+=4096)s->pages[page/4096].protect=protection;
        memcpy((void *)(uintptr_t)(base+output),&old,4);pthread_mutex_unlock(&lock);return 1;
    }
    for(uint32_t page=lo;page<hi;page+=4096){
        Page p=s->pages[page/4096];uint32_t native_first=page&~((uint32_t)host_page-1);
        uint32_t previous=s->pages[native_first/4096].protect;
        uint32_t final=(page>=start&&page<end)?protection:p.protect;
        if(p.state!=0x1000||(p.protect!=1&&p.protect!=2&&p.protect!=4)||p.protect!=previous||final!=protection){
            pthread_mutex_unlock(&lock);return fail(50);
        }
    }
    int native=protection==1?PROT_NONE:protection==2?PROT_READ:PROT_READ|PROT_WRITE;
    if(mprotect((void *)(uintptr_t)(base+lo),hi-lo,native)){
        for(uint32_t page=lo;page<hi;page+=(uint32_t)host_page){
            uint32_t previous=s->pages[page/4096].protect;int restore=previous==1?PROT_NONE:previous==2?PROT_READ:PROT_READ|PROT_WRITE;
            if(mprotect((void *)(uintptr_t)(base+page),(size_t)host_page,restore)){pthread_mutex_unlock(&lock);fputs("TribesPad: VirtualProtect rollback failed\n",stderr);abort();}
        }
        pthread_mutex_unlock(&lock);return fail(5);
    }
    for(uint32_t page=start;page<end;page+=4096)s->pages[page/4096].protect=protection;
    memcpy((void *)(uintptr_t)(base+output),&old,4);pthread_mutex_unlock(&lock);return 1;
}
uint32_t tribes_virtual_query(uint64_t base,uint32_t address,uint32_t output,uint32_t length){
    if(address>TRIBES_GUEST_MAXIMUM_ADDRESS)return fail(87);
    if(length<28)return fail(24); /* ERROR_BAD_LENGTH; 28-byte x86 ABI profile. */
    if(!output||output>UINT32_MAX-27||base>UINTPTR_MAX-output-27)return fail(998);
    pthread_mutex_lock(&lock);Space *s=find(base);
    if(!s||!s->sealed){pthread_mutex_unlock(&lock);fputs("TribesPad: VirtualQuery requires a sealed loader address-space profile\n",stderr);abort();}
    uint32_t fields[7];if(!query(s,address&~4095u,fields)){pthread_mutex_unlock(&lock);return fail(87);}
    for(uint64_t page=output&~4095u;page<=(uint64_t)(output+27u);page+=4096){
        uint32_t buffer[7];if(page>TRIBES_GUEST_MAXIMUM_ADDRESS||!query(s,(uint32_t)page,buffer)||buffer[4]!=0x1000||(buffer[5]!=4&&buffer[5]!=8&&buffer[5]!=0x40&&buffer[5]!=0x80)){pthread_mutex_unlock(&lock);return fail(998);}
    }
    memcpy((void *)(uintptr_t)(base+output),fields,28);pthread_mutex_unlock(&lock);return 28;
}
