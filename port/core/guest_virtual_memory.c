/* Nonexecutable current-process Win32 VM reservations in a private guest arena.
   https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2fromapp
   https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree
   Checked mode enforces 4 KiB commitment on larger native pages; the native-only
   mode retains its explicit geometry gate. */
#include "guest_virtual_memory.h"
#include "guest_system.h"
#include "guest_heap.h"
#include "guest_memory_layout.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
uint32_t tribes_virtual_memory_low=0x60030000;
#define LOW tribes_virtual_memory_low
#define END UINT32_C(0x7fff0000)
typedef struct Region { uint64_t base;uint32_t address,size,backing,page,allocation_protect;unsigned char *state;struct Region *next; } Region;
static Region *regions;
typedef struct Checked {uint64_t base;TribesVirtualMemoryBackend backend;struct Checked *next;} Checked;
static Checked *checked;
static Checked *find_checked(uint64_t base){for(Checked *c=checked;c;c=c->next)if(c->base==base)return c;return NULL;}
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
typedef struct Capture {uint64_t base;TribesPatchCaptureAllocator allocator;struct Capture *next;} Capture;
static Capture *captures;
uint32_t tribes_virtual_memory_enable_patch_capture(uint64_t base,TribesPatchCaptureAllocator allocator) {
 if(!allocator)return 0;
 pthread_mutex_lock(&lock);
 for(Capture *c=captures;c;c=c->next)if(c->base==base){pthread_mutex_unlock(&lock);return 0;}
 Capture *c=calloc(1,sizeof *c);if(!c){pthread_mutex_unlock(&lock);return 0;}
 c->base=base;c->allocator=allocator;c->next=captures;captures=c;pthread_mutex_unlock(&lock);return 1;
}
static _Noreturn void unsupported(const char *reason) { fprintf(stderr,"TribesPad: virtual memory contract unsupported: %s\n",reason);abort(); }
static uint32_t failure(uint32_t error) { tribes_set_last_error(error);return 0; }
static uint64_t up(uint64_t n,uint64_t alignment) { return (n+alignment-1)&~(alignment-1); }
static int prot(uint32_t p) { if(p==1)return PROT_NONE;if(p==2)return PROT_READ;if(p==4)return PROT_READ|PROT_WRITE;return -1; }
static int guest_range(uint64_t base,uint32_t address,uint64_t size) { return size&&size<=UINT64_C(0x100000000)-address&&base<=UINTPTR_MAX-address-(size-1); }
static Region *containing(uint64_t base,uint32_t address,uint64_t size) {
    for(Region *r=regions;r;r=r->next)if(r->base==base&&address>=r->address&&(uint64_t)address+size<=(uint64_t)r->address+r->size)return r;
    return NULL;
}
uint32_t tribes_virtual_memory_enable_checked(uint64_t base,const TribesVirtualMemoryBackend *backend){
    if(!backend||!backend->initialize||!backend->change)return failure(87);
    pthread_mutex_lock(&lock);
    if(find_checked(base)){pthread_mutex_unlock(&lock);return failure(87);}
    for(Region *r=regions;r;r=r->next)if(r->base==base){pthread_mutex_unlock(&lock);return failure(87);}
    Checked *c=calloc(1,sizeof *c);if(!c){pthread_mutex_unlock(&lock);return failure(8);}
    c->base=base;c->backend=*backend;
    if(!c->backend.initialize(base,LOW,END-LOW)){free(c);pthread_mutex_unlock(&lock);return failure(50);}
    c->next=checked;checked=c;pthread_mutex_unlock(&lock);return 1;
}
/* Caller holds VM ownership lock. Publish complete native groups so changing
   one logical page retains every neighbor's permission and contents. */
static int checked_change(Checked *c,Region *r,uint32_t start,uint32_t length,uint32_t protection,int operation,int nt){
    uint32_t native_page=(uint32_t)sysconf(_SC_PAGESIZE);
    uint32_t first=(start-r->address)&~(native_page-1),end=(uint32_t)up((uint64_t)start-r->address+length,native_page);
    uint32_t pages=(end-first)/4096;unsigned char *data=calloc(pages,3);if(!data)return 0;
    unsigned char *access=data,*zero=data+pages,*state=data+2*pages;
    memcpy(state,r->state+first/4096,pages);
    for(uint32_t i=0;i<pages;i++){
        uint32_t address=r->address+first+i*4096;
        if(address>=start&&(uint64_t)address<(uint64_t)start+length){
            if(operation==0)state[i]=0; /* decommit / reserve / release */
            else if(!state[i]){state[i]=(unsigned char)protection;zero[i]=1;}
            else if(nt)state[i]=(unsigned char)protection;
        }
        access[i]=state[i]==4?3:state[i]==2?1:0;
    }
    int ok=c->backend.change(r->base,r->address+first,end-first,access,zero);
    if(ok)memcpy(r->state+first/4096,state,pages);
    free(data);return ok;
}
static uint32_t allocate_region(uint64_t base,uint32_t process,uint32_t address,uint32_t size,uint32_t type,uint32_t protection,uint32_t parameters,uint32_t count,int mode) {
    if(process&&process!=UINT32_MAX)unsupported("remote process handle");
    if(!size||size%TRIBES_GUEST_PAGE_SIZE)return failure(87);
    if(type!=0x1000&&type!=0x2000&&type!=0x3000)unsupported("allocation type / placeholder / reset / large-page mode");
    if(protection==0x10||protection==0x20||protection==0x40||protection==0x80){
        if(mode==2){
            TribesPatchCaptureAllocator allocator=NULL;pthread_mutex_lock(&lock);
            for(Capture *c=captures;c;c=c->next)if(c->base==base){allocator=c->allocator;break;}
            pthread_mutex_unlock(&lock);if(allocator)return allocator(base,address,size,type,protection);
        }
        if(mode)unsupported(mode==1?"NT executable guest allocation / patch routing":"classic executable guest allocation / patch routing");return failure(87);
    } /* FromApp forbids execution. */
    int native_prot=prot(protection);if(native_prot<0)unsupported("page protection modifiers or copy-on-write");
    uint64_t lowest=0,highest=TRIBES_GUEST_MAXIMUM_ADDRESS,alignment=TRIBES_GUEST_ALLOCATION_GRANULARITY;
    if(count>1)unsupported("multiple extended parameters");
    if(count) {
        if(!parameters||!guest_range(base,parameters,16))return failure(87);
        uint64_t fields[2];memcpy(fields,(void *)(uintptr_t)(base+parameters),16);
        if(fields[0]!=1)unsupported("extended parameter other than address requirements");
        if(!fields[1]||fields[1]>UINT32_MAX||!guest_range(base,(uint32_t)fields[1],12))return failure(87);
        uint32_t req[3];memcpy(req,(void *)(uintptr_t)(base+fields[1]),12);
        if(address&&(req[0]||req[1]||req[2]))return failure(87);
        lowest=req[0];if(req[1])highest=req[1];if(req[2])alignment=req[2];
        if(lowest%65536||(req[1]&&((uint64_t)req[1]+1)%65536)||highest>TRIBES_GUEST_MAXIMUM_ADDRESS||lowest>highest||alignment<65536||(alignment&(alignment-1)))return failure(87);
    }
    long native_page=sysconf(_SC_PAGESIZE);if(native_page<=0)unsupported("native page size unavailable");
    uint32_t page=(uint32_t)native_page;if((page&(page-1))||page>65536||65536%page)unsupported("native backing geometry");
    if(base%page||!guest_range(base,LOW,END-LOW))return failure(87);
    if(address&&(address%(mode&&type==0x1000?4096:65536)))return failure(87);
    uint32_t backing=(uint32_t)up(size,page);if(backing<size||backing>END-LOW)return failure(8);
    pthread_mutex_lock(&lock);Checked *checked_mode=find_checked(base);
    if(!checked_mode&&(type&0x1000)&&(size%page||(address&&address%page)))unsupported("commit requires independent guest subpages");
    if(type==0x1000&&address) {
        Region *r=containing(base,address,size);
        if(!r){pthread_mutex_unlock(&lock);return failure(487);}
        if(checked_mode){
            int ok=checked_change(checked_mode,r,address,size,protection,1,mode==1);
            pthread_mutex_unlock(&lock);return ok?address:failure(8);
        }
        uint32_t first=(address-r->address)/page,n=size/page;
        /* FromApp/classic recommit preserves protection/contents; NT can change protection. */
        for(uint32_t i=0;i<n;i++)if(!r->state[first+i]) {
            void *p=(void *)(uintptr_t)(base+address+(uint64_t)i*page);
            if(mprotect(p,page,PROT_READ|PROT_WRITE))unsupported("native commit failure");
            memset(p,0,page);if(mprotect(p,page,native_prot))unsupported("native commit protection failure");r->state[first+i]=(unsigned char)protection;
        } else if(mode==1&&r->state[first+i]!=protection) {
            if(mprotect((void *)(uintptr_t)(base+address+(uint64_t)i*page),page,native_prot))unsupported("native NT recommit protection failure");r->state[first+i]=(unsigned char)protection;
        }
        pthread_mutex_unlock(&lock);return address;
    }
    uint64_t candidate=address?address:up(lowest>LOW?lowest:LOW,alignment);
    for(;;) {
        if(candidate<LOW||candidate+backing>END||candidate+size-1>highest){pthread_mutex_unlock(&lock);return failure(address?487:8);}
        Region *collision=NULL;
        for(Region *r=regions;r;r=r->next)if(r->base==base&&candidate<(uint64_t)r->address+r->backing&&candidate+backing>r->address){collision=r;break;}
        if(!collision)break;
        if(address){pthread_mutex_unlock(&lock);return failure(487);}
        candidate=up((uint64_t)collision->address+collision->backing,alignment);
    }
    uint32_t logical_page=checked_mode?4096:page;
    Region *r=calloc(1,sizeof *r);unsigned char *states=calloc(backing/logical_page,1);
    if(!r||!states){free(r);free(states);pthread_mutex_unlock(&lock);return failure(8);}
    void *host=(void *)(uintptr_t)(base+candidate);
    r->base=base;r->address=(uint32_t)candidate;r->size=size;r->backing=backing;r->page=logical_page;r->allocation_protect=protection;r->state=states;
    if(checked_mode){
        if(!checked_change(checked_mode,r,(uint32_t)candidate,size,protection,(type&0x1000)!=0,0)){
            free(states);free(r);pthread_mutex_unlock(&lock);return failure(8);
        }
    } else {
    if(mprotect(host,backing,PROT_NONE)){free(r);free(states);pthread_mutex_unlock(&lock);return failure(8);}
    if(type&0x1000) {
        if(mprotect(host,size,PROT_READ|PROT_WRITE))unsupported("native initial commit failure");
        memset(host,0,size);if(mprotect(host,size,native_prot))unsupported("native initial protection failure");memset(states,protection,size/page);
    }
    }
    r->next=regions;regions=r;
    uint32_t result=r->address;pthread_mutex_unlock(&lock);return result;
}
uint32_t tribes_virtual_alloc2_from_app(uint64_t base,uint32_t process,uint32_t address,uint32_t size,uint32_t type,uint32_t protection,uint32_t parameters,uint32_t count) {
    return allocate_region(base,process,address,size,type,protection,parameters,count,0);
}
/* Classic VirtualAlloc rounds the guest byte range, retaining FromApp-style
   recommit contents/protection. Executable guest allocations need patch routing. */
uint32_t tribes_virtual_alloc(uint64_t base,uint32_t address,uint32_t size,uint32_t type,uint32_t protection) {
    if(!size)return failure(87);
    uint32_t start=address&~(type&0x2000?UINT32_C(65535):UINT32_C(4095));
    uint64_t end=up((uint64_t)address+size,4096),length=end-start;
    if(end>UINT64_C(0x100000000)||length>UINT32_MAX||(address&&start<TRIBES_GUEST_MINIMUM_ADDRESS))return failure(87);
    return allocate_region(base,UINT32_MAX,start,(uint32_t)length,type,protection,0,0,2);
}
/* Original x86 in/out pointers and SIZE_T are DWORDs. NT services retain
   Win32 LastError and return an NTSTATUS. Executable mappings remain a gate,
   rather than inheriting FromApp's valid rejection of executable protection. */
uint32_t tribes_nt_allocate_virtual_memory_ex(uint64_t base,uint32_t process,uint32_t address_pointer,uint32_t size_pointer,uint32_t type,uint32_t protection,uint32_t parameters,uint32_t count) {
    if(!process)return UINT32_C(0xc0000008); /* STATUS_INVALID_HANDLE */
    if(process!=UINT32_MAX)unsupported("NT remote process handle");
    if(!address_pointer||!size_pointer||!guest_range(base,address_pointer,4)||!guest_range(base,size_pointer,4))return UINT32_C(0xc0000005);
    if((uint64_t)address_pointer<size_pointer+UINT64_C(4)&&(uint64_t)size_pointer<address_pointer+UINT64_C(4))unsupported("overlapping NT in/out fields");
    uint32_t address,size;memcpy(&address,(void *)(uintptr_t)(base+address_pointer),4);memcpy(&size,(void *)(uintptr_t)(base+size_pointer),4);
    if(!size)return UINT32_C(0xc000000d);
    uint32_t rounded_address=address&~(type&0x2000?UINT32_C(65535):UINT32_C(4095));
    uint64_t rounded_end=up((uint64_t)address+size,4096),rounded_size=rounded_end-rounded_address;
    if(rounded_end>UINT64_C(0x100000000)||rounded_size>UINT32_MAX|| (address&&rounded_address<TRIBES_GUEST_MINIMUM_ADDRESS))return UINT32_C(0xc000000d);
    if(address&&protection!=4&&(((uint64_t)address_pointer<rounded_end&&address_pointer+UINT64_C(4)>rounded_address) || ((uint64_t)size_pointer<rounded_end&&size_pointer+UINT64_C(4)>rounded_address)))unsupported("NT output fields overlap nonwritable allocation");
    uint32_t saved_error=tribes_get_last_error();
    uint32_t result=allocate_region(base,process,rounded_address,(uint32_t)rounded_size,type,protection,parameters,count,1),error=tribes_get_last_error();
    tribes_set_last_error(saved_error);
    if(!result) { if(error==87)return UINT32_C(0xc000000d);if(error==8)return UINT32_C(0xc0000017);if(error==487)return UINT32_C(0xc0000018);unsupported("unmapped native allocation error"); }
    uint32_t actual_size=(uint32_t)rounded_size;memcpy((void *)(uintptr_t)(base+address_pointer),&result,4);memcpy((void *)(uintptr_t)(base+size_pointer),&actual_size,4);return 0;
}
uint32_t tribes_virtual_free(uint64_t base,uint32_t address,uint32_t size,uint32_t type) {
    if(type!=0x4000&&type!=0x8000)unsupported("free type / placeholder mode");
    if(!address||(type==0x8000&&size))return failure(87);
    pthread_mutex_lock(&lock);Region *r=containing(base,address,1);
    if(!r||(type==0x8000&&address!=r->address)){pthread_mutex_unlock(&lock);return failure(487);}
    Checked *checked_mode=find_checked(base);
    if(type==0x8000) {
        if(checked_mode){
            if(!checked_change(checked_mode,r,r->address,r->backing,0,0,0)){pthread_mutex_unlock(&lock);return failure(8);}
        } else if(mprotect((void *)(uintptr_t)(base+r->address),r->backing,PROT_NONE))unsupported("native release failure");
        (void)madvise((void *)(uintptr_t)(base+r->address),r->backing,MADV_DONTNEED);
        Region **link=&regions;while(*link!=r)link=&(*link)->next;*link=r->next;free(r->state);free(r);pthread_mutex_unlock(&lock);return 1;
    }
    if(!size) { if(address!=r->address){pthread_mutex_unlock(&lock);return failure(487);}size=r->size; }
    uint64_t start=address&~UINT64_C(4095),end=up((uint64_t)address+size,4096);
    if(end>(uint64_t)r->address+r->size||start<r->address){pthread_mutex_unlock(&lock);return failure(487);}
    if(checked_mode){
        int ok=checked_change(checked_mode,r,(uint32_t)start,(uint32_t)(end-start),0,0,0);
        /* Do not madvise a partial native page: it could erase committed peers.
           Each newly committed logical page is zeroed in the backend transaction. */
        pthread_mutex_unlock(&lock);return ok?1:failure(8);
    }
    if(start%r->page||end%r->page)unsupported("decommit requires independent guest subpages");
    if(mprotect((void *)(uintptr_t)(base+start),(size_t)(end-start),PROT_NONE))unsupported("native decommit failure");
    (void)madvise((void *)(uintptr_t)(base+start),(size_t)(end-start),MADV_DONTNEED);
    memset(r->state+(start-r->address)/r->page,0,(end-start)/r->page);pthread_mutex_unlock(&lock);return 1;
}
int tribes_virtual_memory_visit(uint64_t base,int (*visitor)(uint32_t,uint32_t,uint32_t,void *),void *context) {
    if(!visitor)return 0;int result=1;pthread_mutex_lock(&lock);
    for(Region *r=regions;r&&result;r=r->next)if(r->base==base)for(uint32_t offset=0;offset<r->size;) {
        uint32_t protection=r->state[offset/r->page],end=offset+r->page;
        while(end<r->size&&r->state[end/r->page]==protection)end+=r->page;
        if(end>r->size)end=r->size;if(!visitor(r->address+offset,end-offset,protection,context)){result=0;break;}offset=end;
    }
    pthread_mutex_unlock(&lock);return result;
}

/* Query one logical page and its forward matching run under provider ownership.
   Native padding is owned reserved address space, never reported as free. */
int tribes_virtual_memory_query(uint64_t base,uint32_t address,uint32_t fields[7]) {
    if(!fields||address<LOW||address>=END||address%4096)return 0;
    pthread_mutex_lock(&lock);Region *found=NULL;uint32_t next=END;
    for(Region *r=regions;r;r=r->next)if(r->base==base){
        if(address>=r->address&&(uint64_t)address<(uint64_t)r->address+r->backing)found=r;
        else if(r->address>address&&r->address<next)next=r->address;
    }
    uint32_t out[7]={address,0,0,next-address,0x10000,0,0};
    if(found){
        Region *r=found;uint32_t offset=address-r->address;
        uint32_t protection=offset<r->size?r->state[offset/r->page]:0;
        uint32_t end=offset+4096;
        while(end<r->backing&&(end<r->size?r->state[end/r->page]:0)==protection)end+=4096;
        out[1]=r->address;out[2]=r->allocation_protect;out[3]=end-offset;out[4]=protection?0x1000:0x2000;out[5]=protection;out[6]=0x20000;
    }
    memcpy(fields,out,sizeof out);pthread_mutex_unlock(&lock);return 1;
}
