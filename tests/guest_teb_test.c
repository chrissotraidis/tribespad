#include "../port/core/guest_teb.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
struct Worker {uint64_t base;unsigned slot;};
static void *worker(void *context){
    struct Worker *w=context;uint32_t teb=0x3d0000+w->slot*65536;unsigned char *p=(void *)(uintptr_t)(w->base+teb);memset(p,0x71,4096);
    tribes_set_last_error(123);assert(tribes_teb_configure(w->base,teb,0x300000,0x100000,1234,2000+w->slot,0x3e0000));
    for(unsigned i=0;i<5000;i++){
        assert(tribes_get_current_process_id()==1234&&tribes_get_current_thread_id()==2000+w->slot);
        assert(x86_read_fs_dword(0x18)==teb&&x86_read_fs_dword(0x20)==1234&&x86_read_fs_dword(0x24)==2000+w->slot);
        assert(x86_read_fs_dword(4)==0x300000&&x86_read_fs_dword(8)==0x100000&&x86_read_fs_dword(0x2c)==0x3e0000);
        x86_write_fs_dword(0,0x200000+i);uint32_t direct;memcpy(&direct,p,4);assert(direct==0x200000+i&&x86_read_fs_dword(0)==direct&&tribes_get_last_error()==123);
    }
    assert(p[12]==0x71&&p[0x17]==0x71&&p[0x1c]==0x71&&p[0x28]==0x71&&p[0x30]==0&&p[0x34]==0x71&&p[4095]==0x71);
    tribes_teb_clear();assert(tribes_get_last_error()==123);return NULL;
}
int main(int argc,char **argv){
    unsigned char *m=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED);assert(!mprotect(m+0x3c0000,0x100000,PROT_READ|PROT_WRITE));uint64_t base=(uintptr_t)m;
    if(argc>1){
        if(!strcmp(argv[1],"unconfigured")){(void)x86_read_fs_dword(0x18);return 99;}
        assert(tribes_teb_configure(base,0x3d0000,0x300000,0x100000,1234,2000,0x3e0000));
        if(!strcmp(argv[1],"read"))(void)x86_read_fs_dword(0x34);
        else if(!strcmp(argv[1],"write"))x86_write_fs_dword(0x18,0);
        else if(!strcmp(argv[1],"modified")){memset(m+0x3d0018,0,4);(void)x86_read_fs_dword(0x18);}
        else if(!strcmp(argv[1],"cleared")){tribes_teb_clear();(void)x86_read_fs_dword(0);}
        else abort();return 99;
    }
    tribes_set_last_error(123);assert(tribes_teb_configure(base,0x3d0000,0x300000,0x100000,1234,2000,0x3e0000));
    unsigned char captured[4096];memcpy(captured,m+0x3d0000,4096);
    assert(!tribes_teb_configure(base,0,0x300000,0x100000,1234,2000,0x3e0000)&&tribes_get_last_error()==87);
    assert(!tribes_teb_configure(base,0x3d0001,0x300000,0x100000,1234,2000,0x3e0000));
    assert(!tribes_teb_configure(base,0xfffff000,0x300000,0x100000,1234,2000,0x3e0000)); /* Outside declared two-GiB guest profile. */
    assert(!memcmp(captured,m+0x3d0000,4096)&&x86_read_fs_dword(0x18)==0x3d0000);tribes_teb_clear();
    pthread_t threads[8];struct Worker workers[8];for(unsigned i=0;i<8;i++){workers[i]=(struct Worker){base,i};assert(!pthread_create(&threads[i],NULL,worker,&workers[i]));}for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(!munmap(m,UINT64_C(0x100000000)));puts("PASS: 40000 concurrent configured native thread FS sequences, nonzero unique TEB self identities, matching Win32 process/thread IDs, seven declared DWORD reads, direct/FS SEH-head coherence, thread/stack/TLS fields, exact field gaps and page guards, rejected config retains context/bytes, thread-local LastError; no native thread allocation/PEB/SEH delivery claim");return 0;
}
