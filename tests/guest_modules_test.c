#include "../port/core/guest_modules.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[8][131072];static uint64_t bases[8];static const char *path="C:\\TribesPad\\Game\\Tribes2.exe";
static void wide_name(uint32_t offset,const char *name){for(unsigned i=0;;i++){uint16_t unit=(unsigned char)name[i];memcpy(memory[0]+offset+2*i,&unit,2);if(!unit)break;}}
static void *provider_worker(void *unused){(void)unused;for(unsigned i=0;i<8000;i++){assert((i&1?tribes_load_library(bases[0],1024):tribes_load_library_ex_w(bases[0],2049,0,0))==0x60000000);assert(tribes_get_module_handle_w(bases[0],2049)==0x60000000);assert(tribes_get_proc_address(bases[0],0x60000000,0x10001)==0xfffd0000);assert(tribes_get_proc_address(bases[0],0x60000000,7)==0xfffd0000);assert(tribes_free_library(bases[0],0x60000000));}return NULL;}
static void *worker(void *arg){unsigned i=(unsigned)(uintptr_t)arg;for(unsigned n=0;n<2000;n++){assert(tribes_get_module_handle(bases[i],0)==0x400000);assert(tribes_get_module_handle_w(bases[i],0)==0x400000);assert(tribes_get_module_handle_w(bases[i],4097)==0x400000);assert(tribes_get_module_handle(bases[i],128)==0x400000);assert(tribes_get_module_filename(bases[i],0,513,256)==strlen(path));assert(!strcmp((char *)memory[i]+513,path));}return NULL;}
int main(void){
    for(unsigned i=0;i<8;i++){bases[i]=(uintptr_t)memory[i];assert(tribes_module_register(bases[i],0x400000,path,1));strcpy((char *)memory[i]+128,"tRIBES2.EXE");for(unsigned n=0;n<=strlen(path);n++){uint16_t ch=(unsigned char)path[n];memcpy(memory[i]+4097+2*n,&ch,2);}}
    assert(!tribes_module_register(bases[0],0x500000,path,0)&&errno==EALREADY);assert(!tribes_module_register(bases[0],0x500000,"relative.dll",0)&&errno==EINVAL);
    assert(tribes_module_register(bases[0],0x10000000,"C:\\TribesPad\\Game\\Fixture.dll",0));strcpy((char *)memory[0]+256,"fixture");assert(tribes_get_module_handle(bases[0],256)==0x10000000);strcpy((char *)memory[0]+256,"C:\\TRIBESPAD\\GAME\\FIXTURE.DLL");assert(tribes_get_module_handle(bases[0],256)==0x10000000);assert(!tribes_get_module_handle(bases[1],256));
    strcpy((char *)memory[0]+256,"missing.dll");assert(!tribes_get_module_handle(bases[0],256)&&tribes_get_last_error()==126);
    for(uint32_t size=1;size<strlen(path)+2;size++){
        memset(memory[0]+512,0xa5,256);tribes_set_last_error(123);uint32_t result=tribes_get_module_filename(bases[0],0x400000,513,size);
        if(size<=strlen(path)){assert(result==size&&memory[0][512+size]==0&&tribes_get_last_error()==122);assert(!memcmp(memory[0]+513,path,size-1));}
        else assert(result==strlen(path)&&!strcmp((char *)memory[0]+513,path)&&tribes_get_last_error()==123);
        assert(memory[0][512]==0xa5&&memory[0][513+size]==0xa5);
    }
    assert(!tribes_get_module_filename(bases[0],0,513,0)&&tribes_get_last_error()==122);assert(!tribes_get_module_filename(bases[0],0x999,513,128)&&tribes_get_last_error()==126);
    assert(!tribes_get_module_filename(bases[0],0,UINT32_MAX,128)&&tribes_get_last_error()==87);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    char export_name[]="CreateFixture";TribesNativeExport exports[]={{export_name,7,0xfffd0000},{"Second",0,0xfffd0010}};
    assert(tribes_module_define_native(bases[0],0x60000000,"C:\\Windows\\System32\\FixtureProvider.dll",exports,2));export_name[0]='x';
    assert(!tribes_module_define_native(bases[0],0x60000000,"C:\\Windows\\System32\\FixtureProvider.dll",exports,2)&&errno==EALREADY);
    exports[1].ordinal=7;assert(!tribes_module_define_native(bases[0],0x61000000,"C:\\Windows\\System32\\Bad.dll",exports,2)&&errno==EINVAL);
    strcpy((char *)memory[0]+1024,"fixtureprovider");strcpy((char *)memory[0]+0x10001,"CreateFixture");
    /* Exercise the CRT's failed modern search and unflagged wide fallback.
       Odd guest address verifies UTF-16 loads do not require host alignment. */
    wide_name(2049,"FiXtUrEpRoViDeR");tribes_set_last_error(123);
    assert(!tribes_load_library_ex_w(bases[0],2049,0,0x800)&&tribes_get_last_error()==87);
    assert(!tribes_get_module_handle(bases[0],1024));
    assert(!tribes_load_library_ex_w(bases[0],2049,7,0)&&tribes_get_last_error()==87);
    assert(!tribes_load_library_ex_w(bases[0],0,0,0)&&tribes_get_last_error()==87);
    assert(!tribes_load_library_ex_w(bases[0],UINT32_MAX,0,0)&&tribes_get_last_error()==87);
    tribes_set_last_error(123);assert(tribes_load_library_ex_w(bases[0],2049,0,0)==0x60000000&&tribes_get_last_error()==123);
    assert(tribes_get_proc_address(bases[0],0x60000000,7)==0xfffd0000);assert(tribes_free_library(bases[0],0x60000000));
    wide_name(2049,"C:\\WINDOWS\\SYSTEM32\\FIXTUREPROVIDER.DLL");assert(tribes_load_library_ex_w(bases[0],2049,0,0)==0x60000000);assert(tribes_free_library(bases[0],0x60000000));
    wide_name(2049,"");assert(!tribes_load_library_ex_w(bases[0],2049,0,0)&&tribes_get_last_error()==126);
    for(unsigned i=0;i<4096;i++){uint16_t unit='A';memcpy(memory[0]+2049+2*i,&unit,2);}assert(!tribes_load_library_ex_w(bases[0],2049,0,0)&&tribes_get_last_error()==126);
    /* Two native libraries may forward the same precompiled service while
       retaining independent load references and handles. */
    TribesNativeExport forwarded[]={{"CreateFixture",7,0xfffd0000},{"Second",0,0xfffd0010}};
    assert(tribes_module_define_native(bases[0],0x60020000,"C:\\Windows\\System32\\KernelBase.dll",forwarded,2));
    wide_name(2049,"KeRnElBaSe");assert(tribes_load_library_ex_w(bases[0],2049,0,0)==0x60020000);
    assert(tribes_get_proc_address(bases[0],0x60020000,0x10001)==0xfffd0000);
    assert(!tribes_get_module_handle(bases[0],1024));assert(tribes_free_library(bases[0],0x60020000));
    assert(!tribes_get_proc_address(bases[0],0x60020000,7)&&tribes_get_last_error()==6);
    assert(!tribes_get_module_handle(bases[0],1024));assert(tribes_load_library(bases[0],1024)==0x60000000);assert(tribes_get_module_handle(bases[0],1024)==0x60000000);
    assert(tribes_get_proc_address(bases[0],0x60000000,0x10001)==0xfffd0000);assert(tribes_get_proc_address(bases[0],0x60000000,7)==0xfffd0000);
    assert(!tribes_get_proc_address(bases[0],0x60000000,8)&&tribes_get_last_error()==127);strcpy((char *)memory[0]+0x10101,"createfixture");assert(!tribes_get_proc_address(bases[0],0x60000000,0x10101)&&tribes_get_last_error()==127);
    assert(tribes_load_library(bases[0],1024)==0x60000000);assert(tribes_free_library(bases[0],0x60000000));assert(tribes_get_module_handle(bases[0],1024)==0x60000000);assert(tribes_free_library(bases[0],0x60000000));assert(!tribes_get_module_handle(bases[0],1024));assert(!tribes_get_proc_address(bases[0],0x60000000,7)&&tribes_get_last_error()==6);assert(!tribes_free_library(bases[0],0x60000000)&&tribes_get_last_error()==6);
    wide_name(2049,"fixtureprovider");for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,provider_worker,NULL));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));assert(!tribes_get_module_handle(bases[0],1024));
    wide_name(2049,"fixtureprovider");tribes_set_last_error(123);
    assert(!tribes_get_module_handle_w(bases[0],2049)&&tribes_get_last_error()==126);
    assert(tribes_load_library_ex_w(bases[0],2049,0,0)==0x60000000);
    tribes_set_last_error(123);assert(tribes_get_module_handle_w(bases[0],2049)==0x60000000&&tribes_get_last_error()==123);
    assert(tribes_free_library(bases[0],0x60000000));assert(!tribes_get_module_handle_w(bases[0],2049)&&tribes_get_last_error()==126);
    wide_name(2049,"");assert(!tribes_get_module_handle_w(bases[0],2049)&&tribes_get_last_error()==126);
    assert(!tribes_get_module_handle_w(bases[0],UINT32_MAX)&&tribes_get_last_error()==87);
    for(unsigned i=0;i<4096;i++){uint16_t ch='A';memcpy(memory[0]+2049+2*i,&ch,2);}
    assert(!tribes_get_module_handle_w(bases[0],2049)&&tribes_get_last_error()==126);
    puts("PASS: original guest image handles, main/named metadata, process isolation, every truncation boundary 16000 concurrent image lookups and 64000 concurrent ANSI/wide provider load/ordinal/name/release cycles, legacy search failure and wide name guards, 32000 concurrent wide main/name queries and 64000 wide provider lookups with no added load references");return 0;
}
