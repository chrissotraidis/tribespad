#include "../port/core/guest_file.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/guest_heap.h"
#include "../port/core/native_handles.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;
static uint64_t base;
static struct stat fixture;
#include "fixtures/file_metadata_cases.h"
int main(void){
    memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
    base=(uintptr_t)memory;assert(!mprotect(memory+0x10000,0x20000,PROT_READ|PROT_WRITE));
    assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
    tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
    char root[]="/tmp/tribes-metadata-XXXXXX",path[4096];assert(mkdtemp(root));
    snprintf(path,sizeof path,"%s/public.store",root);int fd=open(path,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600);
    assert(fd>=0&&write(fd,"TRIBES",6)==6&&!fstat(fd,&fixture)&&!close(fd));
    assert(tribes_filesystem_configure(base,root,root,root));metadata_cases(root);
    assert(!unlink(path)&&!rmdir(root));
    puts("PASS: checked emitted A/W metadata-only rights, delete sharing and duplicates, missing paths, directory backup semantics, followed and final symlink objects, dangling links and intermediate escape rejection");
    return 0;
}
