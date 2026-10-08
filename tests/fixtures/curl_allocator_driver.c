#include "../../port/core/guest_curl_allocator.h"
#include "../../port/core/guest_heap.h"
#include "../../port/core/guest_memory_checks.h"
#include "../../port/core/guest_external_checks.h"
#include "../../port/core/native_winsock.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

pthread_t owner;
size_t live,peak,counts[5];
int fail_next;
void *allocations;
typedef struct Allocation { uint32_t address,size; struct Allocation *next; } Allocation;
static unsigned char *memory;
static _cpu cpu,before;
static TribesCurlAllocator *allocator;
static TribesCurlFrame frame;
static uint32_t latest;
static int recurse;
static int fail_strdup;
static int corrupt_result;
static char *fault_pointer;
void owner_check(void) { assert(pthread_equal(owner,pthread_self())); }
extern void tbimp_CurlFixtureMalloc(void),tbimp_CurlFixtureFree(void),tbimp_CurlFixtureRealloc(void),tbimp_CurlFixtureStrdup(void),tbimp_CurlFixtureCalloc(void),tribes_return_to_host(void);
#ifdef CURL_RUNTIME_TEST
extern void tbimp_curl_easy_reset(void),tbimp_curl_global_init_mem(void),tbimp_curl_multi_init(void),tbimp_curl_multi_cleanup(void),tbimp_curl_global_cleanup(void);
extern void tbimp_curl_multi_setopt(void),tbimp_curl_easy_init(void),tbimp_curl_easy_cleanup(void),tbimp_curl_multi_add_handle(void),tbimp_curl_multi_remove_handle(void),tbimp_CurlFixtureTimer(void),tbimp_CurlFixtureTimerAlternate(void);
extern void tbimp_curl_multi_assign(void),tbimp_curl_easy_getinfo(void),tbimp_CurlFixtureSocket(void),tbimp_curl_multi_socket_action(void),tbimp_curl_easy_setopt(void);
extern void tbimp_WSAIoctl(void),tbimp_select(void),tbimp_curl_multi_info_read(void);
extern void tbimp_curl_easy_perform(void),tbimp_CurlFixtureHeader(void),tbimp_CurlFixtureProgress(void),tbimp_CurlFixtureWrite(void);
extern void tbimp_curl_url(void),tbimp_curl_url_set(void),tbimp_curl_url_cleanup(void);
extern void tbimp_curl_version(void),tbimp_curl_slist_append(void),tbimp_curl_slist_free_all(void);
#endif
void *tribes_lookup(uint32_t pc) {
    switch(pc) {
        case 1:return (void *)tbimp_CurlFixtureMalloc;
        case 2:return (void *)tbimp_CurlFixtureFree;
        case 3:return (void *)tbimp_CurlFixtureRealloc;
        case 4:return (void *)tbimp_CurlFixtureStrdup;
        case 5:return (void *)tbimp_CurlFixtureCalloc;
#ifdef CURL_RUNTIME_TEST
        case 6:return (void *)tbimp_curl_global_init_mem;
        case 7:return (void *)tbimp_curl_multi_init;
        case 8:return (void *)tbimp_curl_multi_cleanup;
        case 9:return (void *)tbimp_curl_global_cleanup;
        case 10:return (void *)tbimp_curl_multi_setopt;
        case 11:return (void *)tbimp_curl_easy_init;
        case 12:return (void *)tbimp_curl_easy_cleanup;
        case 13:return (void *)tbimp_curl_multi_add_handle;
        case 14:return (void *)tbimp_curl_multi_remove_handle;
        case 15:return (void *)tbimp_CurlFixtureTimer;
        case 16:return (void *)tbimp_CurlFixtureTimerAlternate;
        case 17:return (void *)tbimp_curl_multi_assign;
        case 18:return (void *)tbimp_curl_easy_getinfo;
        case 19:return (void *)tbimp_CurlFixtureSocket;
        case 20:return (void *)tbimp_curl_multi_socket_action;
        case 21:return (void *)tbimp_curl_easy_setopt;
        case 22:return (void *)tbimp_curl_version;
        case 23:return (void *)tbimp_curl_slist_append;
        case 24:return (void *)tbimp_curl_slist_free_all;
        case 25:return (void *)tbimp_curl_url;
        case 26:return (void *)tbimp_curl_url_set;
        case 27:return (void *)tbimp_curl_url_cleanup;
        case 28:return (void *)tbimp_curl_easy_perform;
        case 29:return (void *)tbimp_CurlFixtureHeader;
        case 30:return (void *)tbimp_CurlFixtureProgress;
        case 31:return (void *)tbimp_CurlFixtureWrite;
        case 32:return (void *)tbimp_WSAIoctl;
        case 33:return (void *)tbimp_select;
        case 34:return (void *)tbimp_curl_multi_info_read;
        case 35:return (void *)tbimp_curl_easy_reset;
#endif
        case 0xfffff000:return (void *)tribes_return_to_host;
        default:abort();
    }
}
_Noreturn void tribes_unimplemented_dinput(_cpu *c,uint32_t index) { (void)c;(void)index;abort(); }
static Allocation **find(uint32_t address) {
    Allocation **p=(Allocation **)&allocations;
    while(*p && (*p)->address!=address) p=&(*p)->next;
    assert(*p); return p;
}
static uint32_t allocate(_cpu *c,uint32_t size,int zero) {
    owner_check(); assert(c->_pointer_offset==(uintptr_t)memory);
    if(recurse) (void)tribes_curl_host_malloc(1);
    if(fail_next) { fail_next=0; return 0; }
    uint32_t p=tribes_global_alloc(c->_pointer_offset,0,size); assert(p);
    Allocation *entry=malloc(sizeof *entry); assert(entry);
    *entry=(Allocation){p,size,allocations}; allocations=entry;
    memset(memory+p,zero?0:0x5a,size); latest=p;
    live++; if(live>peak) peak=live;
    /* Scratch general registers must not escape the cdecl callback. */
    c->_ecx=0xabcdef01; c->_edx=0x98123456; c->_eflags=0x246;
    c->_xmm[1][7]^=0x5a; before._xmm[1][7]^=0x5a;
    c->_ymm_hi[3][11]^=0xc6; before._ymm_hi[3][11]^=0xc6;
    c->_mxcsr^=0x2000; before._mxcsr^=0x2000;
    return p;
}
uint32_t curl_fixture_malloc(_cpu *c,uint32_t size) { counts[0]++; return allocate(c,size,0); }
void curl_fixture_free(_cpu *c,uint32_t p) {
    owner_check(); counts[1]++;
    if(!p) return;
    Allocation **slot=find(p); Allocation *entry=*slot; *slot=entry->next;
    assert(!tribes_global_free(c->_pointer_offset,p)); free(entry); live--;
}
uint32_t curl_fixture_realloc(_cpu *c,uint32_t p,uint32_t size) {
    counts[2]++;
    if(p && size && !fail_next) {
        Allocation *entry=*find(p);
        if(size<=entry->size && size>=entry->size/2) { entry->size=size; return p; }
    }
    uint32_t q=allocate(c,size,0); if(!q) return 0;
    if(p) {
        uint32_t old=(*find(p))->size;
        memcpy(memory+q,memory+p,size<old?size:old);
        curl_fixture_free(c,p);
    }
    if(corrupt_result==2 && size) memory[q]^=3;
    return q;
}
uint32_t curl_fixture_strdup(_cpu *c,uint32_t source) {
    counts[3]++; assert((source>=0x100000 && source<0x200000) || (source>=0x30000000 && source<0x50000000));
    if(fail_strdup) { fail_strdup=0; return 0; }
    size_t length=strlen((char *)memory+source)+1; assert(length<UINT32_MAX);
    uint32_t p=allocate(c,(uint32_t)length,0);
    if(p) { memcpy(memory+p,memory+source,length); if(corrupt_result==3) memory[p]^=3; }
    return p;
}
uint32_t curl_fixture_calloc(_cpu *c,uint32_t count,uint32_t size) {
    counts[4]++; assert(!size || count<=UINT32_MAX/size);
    uint32_t p=allocate(c,count*size,1);
    if(p && count && size && corrupt_result==1) memory[p]=1;
    return p;
}
static void exercises(void) {
    unsigned char *p=tribes_curl_host_malloc(37); assert(p && p!=memory+latest);
    for(unsigned i=0;i<37;i++) assert(p[i]==0x5a);
    memset(p,0xa7,37);
    uint32_t address=latest;
    tribes_curl_allocator_guest_begin(&frame);
    unsigned char staged[37]; memcpy(staged,memory+address,sizeof staged);
    assert(!memcmp(staged,p,37));
    TribesCurlFrame child; tribes_curl_allocator_enter(allocator,&cpu,&child);
    assert(p[0]==0xa7); p[1]=0x77;
    tribes_curl_allocator_leave(&child);
    assert(memory[address+1]==0x77);
    tribes_curl_allocator_guest_end(&frame);
    fail_next=1; assert(!tribes_curl_host_realloc(p,0));
    assert(p[0]==0xa7 && p[1]==0x77);
    unsigned char *q=tribes_curl_host_realloc(p,91); assert(q && q[0]==0xa7 && q[1]==0x77);
    for(unsigned i=2;i<37;i++) assert(q[i]==0xa7);
    uint32_t unchanged=latest;
    q=tribes_curl_host_realloc(q,60); assert(q && latest==unchanged && q[0]==0xa7);
    q=tribes_curl_host_realloc(q,0); assert(q); tribes_curl_host_free(q);
    char *s=tribes_curl_host_strdup("allocator ownership test"); assert(s && !strcmp(s,"allocator ownership test"));
    uint32_t transferred=tribes_curl_allocator_export(s);
    assert(!strcmp((char *)memory+transferred,"allocator ownership test"));
    uint32_t arg=transferred; (void)tribes_call_guest_cdecl(&cpu,2,1,&arg,cpu._esp-128);
    size_t large_size=1048613;
    char *large=malloc(large_size+1); assert(large); memset(large,'Q',large_size); large[large_size]=0;
    char *duplicate=tribes_curl_host_strdup(large); assert(duplicate && !memcmp(large,duplicate,large_size+1));
    tribes_curl_host_free(duplicate);
    fail_strdup=1; assert(!tribes_curl_host_strdup(large));
    fail_strdup=1; assert(!tribes_curl_host_strdup("short"));
    assert(!live); free(large);
    size_t calls=counts[0]+counts[4];
    assert(!tribes_curl_host_malloc((size_t)UINT32_MAX+1));
    assert(!tribes_curl_host_calloc(UINT32_MAX,2));
    assert(counts[0]+counts[4]==calls);
    assert(!live && !tribes_curl_allocator_live(allocator));
    assert(!memcmp(&cpu,&before,sizeof cpu));
}
void curl_allocator_test_setup(void) {
    memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0); assert(memory!=MAP_FAILED);
    assert(!mprotect(memory+0x100000,0x100000,PROT_READ|PROT_WRITE));
    assert(tribes_memory_checks_configure((uintptr_t)memory,UINT64_C(0x100000000)));
#ifdef CHECKED_TEST
    tribes_memory_external_configure((uintptr_t)memory,UINT64_C(0x100000000));
    tribes_memory_external_enable();
#endif
    memset(&cpu,0,sizeof cpu); cpu._pointer_offset=(uintptr_t)memory; cpu._esp=0x1f0000;
    cpu.stack_bottom=memory+0x100000; cpu.stack_top=memory+0x200000; cpu._eflags=0x202;
    before=cpu; uint32_t callbacks[]={1,2,3,4,5};
    allocator=tribes_curl_allocator_create((uintptr_t)memory,callbacks); assert(allocator);
    tribes_curl_allocator_enter(allocator,&cpu,&frame); exercises();
}
void curl_allocator_test_finish(void) {
    assert(!tribes_curl_allocator_live(allocator) && !memcmp(&cpu,&before,sizeof cpu));
    tribes_curl_allocator_leave(&frame); tribes_curl_allocator_destroy(allocator);
#ifndef CHECKED_TEST
    assert(!munmap(memory,UINT64_C(0x100000000)));
#endif
    puts("PASS: native curl used emitted cdecl guest allocator fixtures and private host mirrors; nested frames, moving/in-place resize, zero-size failure retention, overflow, stack/large-heap strdup and failure cleanup, exported string ownership, parent GPR/flags preservation and FP propagation checked");
}
#ifdef CURL_RUNTIME_TEST
uint32_t timer_calls,timer_multi,timer_timeout,timer_data,timer_result,timer_alternate;
uint32_t timer_reenter,timer_nested_result;
uint32_t version_expected;
uint32_t socket_multi,socket_calls,socket_active,socket_what,socket_last,socket_removes,socket_assigns,socket_result;
uint32_t curl_fixture_socket(_cpu *c,uint32_t e,uint32_t h,uint32_t what,uint32_t data,uint32_t private) {
    assert(data==0xfedcab01 && (private==0||private==0xf1234567));
    socket_calls++;socket_last=h;
    uint32_t scratch=c->_esp-2048,info[]={e,0x100015,scratch};
    assert(tribes_call_guest_cdecl(c,18,3,info,c->_esp-128)==0);
    uint32_t value;memcpy(&value,memory+scratch,4);assert(value==0xcdef5678||value==0);
    uint32_t assign[]={socket_multi,h,what==4?0:0xf1234567};
    assert(tribes_call_guest_cdecl(c,17,3,assign,c->_esp-128)==0);
    if(what==4){socket_active=0;socket_what=0;socket_removes++;}
    else {
        socket_active=h;socket_what=what;socket_assigns++;
        value=1;memcpy(memory+scratch,&value,4);
        assert(tribes_ioctlsocket(c->_pointer_offset,h,0x8004667e,scratch)==0);
        value=628;memcpy(memory+scratch,&value,4);
        assert(tribes_getsockopt(c->_pointer_offset,h,0xffff,0x2005,scratch+16,scratch)==0);
        memcpy(&value,memory+scratch+16+76,4);assert(value==2);
    }
    c->_ecx=0x76543210;c->_edx=0xfedc1234;c->_eflags=0x246;
    c->_xmm[4][0]^=0x63;before._xmm[4][0]^=0x63;
    return socket_result;
}
uint32_t header_calls,progress_calls,body_bytes,header_abort,progress_abort,callback_easy;
uint64_t progress_max[4];
uint32_t curl_fixture_header(_cpu *c,uint32_t text,uint32_t size,uint32_t count,uint32_t data){
    assert(size==1&&count&&data==0xabcd1234);header_calls++;
    assert(memory[text+count-2]=='\r'&&memory[text+count-1]=='\n');
    assert(memory[0x124001]==0); /* native transfer cleared the error buffer */
    if(callback_easy){uint32_t info[]={callback_easy,0x100015,0x122008};assert(!tribes_call_guest_cdecl(c,18,3,info,c->_esp-128));uint32_t v;memcpy(&v,memory+0x122008,4);assert(v==0x1234abcd);}
    c->_ecx=0x123;c->_edx=0x456;c->_eflags=0x246;return header_abort?0:count;
}
uint32_t curl_fixture_write(_cpu *c,uint32_t text,uint32_t size,uint32_t count,uint32_t data){
    (void)c;assert(size==1&&data==0x98765432&&count<=3);char copied[3];memcpy(copied,memory+text,count);assert(!memcmp(copied,"abc"+body_bytes,count));body_bytes+=count;return count;
}
uint32_t curl_fixture_progress(_cpu *c,uint32_t data,uint32_t a,uint32_t ah,uint32_t b,uint32_t bh,uint32_t d,uint32_t dh,uint32_t e,uint32_t eh){
    (void)c;assert(data==0xfedcba98);progress_calls++;
    uint64_t values[]={(uint64_t)ah<<32|a,(uint64_t)bh<<32|b,(uint64_t)dh<<32|d,(uint64_t)eh<<32|e};
    for(unsigned i=0;i<4;i++)if(values[i]>progress_max[i])progress_max[i]=values[i];
    return progress_abort;
}
uint32_t curl_fixture_timer(_cpu *c,uint32_t m,uint32_t timeout,uint32_t data) {
    timer_calls++; timer_multi=m; timer_timeout=timeout; timer_data=data;
    if(version_expected)assert(tribes_call_guest_cdecl(c,22,0,NULL,c->_esp-128)==version_expected);
    if(timer_reenter){
        timer_nested_result=tribes_call_guest_cdecl(c,8,1,&m,c->_esp-128);
        uint32_t args[]={m,0x125000},count=99;memcpy(memory+0x125000,&count,4);
        assert(!tribes_call_guest_cdecl(c,34,2,args,c->_esp-128));
        memcpy(&count,memory+0x125000,4);assert(!count);
    }
    c->_ecx=0x101abcde; c->_edx=0xfedcba01; c->_eflags=0x246;
    c->_xmm[0][0]^=0x5a; before._xmm[0][0]^=0x5a;
    c->_ymm_hi[2][1]^=0x91; before._ymm_hi[2][1]^=0x91;
    c->_mxcsr^=0x4000; before._mxcsr^=0x4000;
    return timer_result;
}
uint32_t curl_fixture_timer_alternate(_cpu *c,uint32_t m,uint32_t timeout,uint32_t data) {
    timer_alternate++; return curl_fixture_timer(c,m,timeout,data);
}
_cpu *curl_runtime_test_setup(void) {
    curl_allocator_test_setup();
    tribes_curl_allocator_leave(&frame); tribes_curl_allocator_destroy(allocator); allocator=NULL;
    return &cpu;
}
void curl_runtime_test_finish(void) {
    assert(!live && !allocations && !memcmp(&cpu,&before,sizeof cpu));
}
#endif
void *curl_allocator_test_thread(void *unused) {
    (void)unused; TribesCurlFrame other;
    tribes_curl_allocator_enter(allocator,&cpu,&other); return NULL;
}
uint32_t curl_allocator_test_prepare_protected(const char *name) {
    fault_pointer=tribes_curl_host_malloc(32); assert(fault_pointer); fault_pointer[0]=0x42;
    if(!strcmp(name,"protected-import")) tribes_curl_allocator_leave(&frame);
    return latest;
}
void curl_allocator_test_fault(const char *name) {
    if(!strcmp(name,"outside-frame")) {
        tribes_curl_allocator_leave(&frame); (void)tribes_curl_host_malloc(1);
    } else if(!strcmp(name,"unknown-pointer")) {
        char x; tribes_curl_host_free(&x);
    } else if(!strcmp(name,"destroy-live")) {
        (void)tribes_curl_host_malloc(3); tribes_curl_allocator_leave(&frame); tribes_curl_allocator_destroy(allocator);
    } else if(!strcmp(name,"missing-suspend")) {
        TribesCurlFrame nested; tribes_curl_allocator_enter(allocator,&cpu,&nested);
    } else if(!strcmp(name,"protected-export")) {
        (void)tribes_curl_allocator_export(fault_pointer);
    } else if(!strcmp(name,"protected-import")) {
        tribes_curl_allocator_enter(allocator,&cpu,&frame);
    } else if(!strcmp(name,"short-stack")) {
        cpu._esp=0x100040; (void)tribes_curl_host_strdup("input");
    } else if(!strcmp(name,"allocator-reentry")) {
        recurse=1; (void)tribes_curl_host_malloc(32);
    } else if(!strcmp(name,"guest-native-string")) {
        assert(tribes_curl_host_malloc(32)); (void)tribes_curl_host_strdup((char *)memory+latest);
    } else if(!strcmp(name,"opaque-corruption")) {
        assert(tribes_curl_host_malloc(32)); tribes_curl_allocator_guest_begin(&frame);
        memory[latest]^=0x41; tribes_curl_allocator_guest_end(&frame);
    } else if(!strcmp(name,"bad-calloc")) {
        corrupt_result=1; (void)tribes_curl_host_calloc(3,8);
    } else if(!strcmp(name,"bad-realloc")) {
        void *p=tribes_curl_host_malloc(32); assert(p); corrupt_result=2; (void)tribes_curl_host_realloc(p,64);
    } else if(!strcmp(name,"bad-strdup")) {
        corrupt_result=3; (void)tribes_curl_host_strdup("contract");
    }
    fputs("fault did not stop\n",stderr); exit(99);
}
