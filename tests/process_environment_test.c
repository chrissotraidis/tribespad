#include "../port/core/process_environment.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../port/core/guest_filesystem.h"
static uint64_t base;static uint32_t wide_address;
static const unsigned char wide_text[]={0x22,0,0xe9,0,0x2d,0x4e,0x3d,0xd8,0x80,0xde,0x22,0,0,0};
static const unsigned char original[]="ALPHA=one\0OEM=\x82\0";
static const unsigned char replacement[]="BETA=two\0";
static unsigned char temporary_memory[8][640];
static void check_temp(uint64_t image,const char *expected) {
    unsigned char *memory=(void *)(uintptr_t)image;size_t n=strlen(expected);memset(memory,0x79,640);tribes_set_last_error(123);
    assert(tribes_get_temp_path_w(image,0,UINT32_MAX)==n+1&&tribes_get_last_error()==123);
    assert(tribes_get_temp_path_w(image,(uint32_t)n,129)==n+1&&tribes_get_last_error()==123);
    for(unsigned i=0;i<640;i++)assert(memory[i]==0x79);
    assert(tribes_get_temp_path_w(image,(uint32_t)n+1,129)==n&&tribes_get_last_error()==123);
    for(unsigned i=0;i<=n;i++){uint16_t ch;memcpy(&ch,memory+129+2*i,2);assert(ch==(unsigned char)expected[i]);}
    assert(memory[128]==0x79&&memory[129+2*(n+1)]==0x79);
}
static void *worker(void *unused) {
    unsigned index=(unsigned)(uintptr_t)unused;
    for(unsigned i=0;i<1000;i++) {
        check_temp((uintptr_t)temporary_memory[index],"C:\\TribesPad\\Temp\\");
        unsigned char *local=temporary_memory[index];uint64_t image=(uintptr_t)local;strcpy((char *)local+1,"tMp");tribes_set_last_error(123);
        const char *value="C:\\TribesPad\\Temp";uint32_t n=(uint32_t)strlen(value);
        assert(tribes_get_environment_variable_a(image,1,UINT32_MAX,0)==n+1&&tribes_get_last_error()==123);
        memset(local+256,0x79,64);assert(tribes_get_environment_variable_a(image,1,257,n)==n+1&&tribes_get_last_error()==123);
        for(unsigned k=256;k<320;k++)assert(local[k]==0x79);
        assert(tribes_get_environment_variable_a(image,1,257,n+1)==n&&!strcmp((char *)local+257,value)&&local[256]==0x79&&local[258+n]==0x79&&tribes_get_last_error()==123);
        assert(!strcmp((char *)local+1,"tMp"));strcpy((char *)local+1,"MISSING");
        assert(!tribes_get_environment_variable_a(image,1,257,64)&&tribes_get_last_error()==203);

        uint32_t w=tribes_get_command_line_w(base);assert(w==wide_address&&!memcmp((void *)(uintptr_t)(base+w),wide_text,sizeof wide_text));
        uint32_t p=tribes_get_environment_strings(base);assert(p);
        assert(!memcmp((void *)(uintptr_t)(base+p),replacement,sizeof replacement));
        assert(tribes_free_environment_strings(base,p));
    }
    return NULL;
}
int main(void) {
    void *window=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(window!=MAP_FAILED);base=(uintptr_t)window;
    assert(!tribes_environment_configure(base,"BAD",4)&&tribes_get_last_error()==87);
    assert(tribes_environment_configure(base,original,sizeof original));tribes_set_last_error(123);
    uint32_t first=tribes_get_environment_strings(base),second=tribes_get_environment_strings(base);assert(first&&second&&first!=second&&tribes_get_last_error()==123);
    assert(tribes_environment_configure(base,replacement,sizeof replacement));
    assert(!memcmp((void *)(uintptr_t)(base+first),original,sizeof original));
    uint32_t third=tribes_get_environment_strings(base);assert(!memcmp((void *)(uintptr_t)(base+third),replacement,sizeof replacement));
    assert(!tribes_free_environment_strings(base,first+1)&&tribes_get_last_error()==87);
    assert(tribes_free_environment_strings(base,first));assert(!tribes_free_environment_strings(base,first));
    assert(tribes_free_environment_strings(base,second));assert(tribes_free_environment_strings(base,third));
    const unsigned char cmd[]="\"C:\\Game With Spaces\\Tribes2.exe\" -test \xE9";
    assert(tribes_command_line_configure(base,cmd,sizeof cmd));uint32_t command=tribes_get_command_line(base);
    assert(command==tribes_get_command_line(base)&&!memcmp((void *)(uintptr_t)(base+command),cmd,sizeof cmd));
    assert(!tribes_command_line_configure(base,"new",4)&&tribes_get_last_error()==87);
    unsigned char unaligned[sizeof wide_text+1];memcpy(unaligned+1,wide_text,sizeof wide_text);
    assert(!tribes_command_line_configure_w(base,NULL,2)&&tribes_get_last_error()==87);
    assert(!tribes_command_line_configure_w(base,wide_text,3));
    const unsigned char interior[]={0,0,1,0,0,0};assert(!tribes_command_line_configure_w(base,interior,sizeof interior));
    assert(tribes_command_line_configure_w(base,unaligned+1,sizeof wide_text));memset(unaligned,0,sizeof unaligned);
    tribes_set_last_error(123);wide_address=tribes_get_command_line_w(base);assert(wide_address&&wide_address!=command&&tribes_get_last_error()==123);
    assert(!memcmp((void *)(uintptr_t)(base+wide_address),wide_text,sizeof wide_text));
    assert(!tribes_command_line_configure_w(base,wide_text,sizeof wide_text)&&tribes_get_last_error()==87);
    for(unsigned i=0;i<8;i++) {
        uint64_t image=(uintptr_t)temporary_memory[i];const char block[]="tMp=C:\\TribesPad\\Temp\0TEMP=C:\\Wrong\0USERPROFILE=C:\\User\0";
        assert(tribes_environment_configure(image,block,sizeof block));assert(tribes_temp_directory_configure(image,"C:\\Windows"));
    }
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    static const unsigned char empty[2]={0};assert(tribes_environment_configure(base,empty,2));first=tribes_get_environment_strings(base);
    assert(!memcmp((void *)(uintptr_t)(base+first),empty,2));assert(tribes_free_environment_strings(base,first));
    uint64_t image=(uintptr_t)temporary_memory[0];const char temp[]="TMP=\0TeMp=C:\\TempFolder\0USERPROFILE=C:\\User\0";
    assert(tribes_environment_configure(image,temp,sizeof temp));check_temp(image,"C:\\TempFolder\\");
    const char user[]="TEMP=\0UserProfile=C:\\User\\\0";assert(tribes_environment_configure(image,user,sizeof user));check_temp(image,"C:\\User\\");
    const char unrelated[]="SystemTemp=C:\\SystemOnly\0";assert(tribes_environment_configure(image,unrelated,sizeof unrelated));check_temp(image,"C:\\Windows\\");
    assert(!tribes_temp_directory_configure(image,"relative")&&tribes_get_last_error()==87);
    assert(tribes_environment_configure(image,empty,2));assert(tribes_temp_directory_configure(image,"D:\\Win"));check_temp(image,"D:\\Win\\");
    assert(!tribes_get_temp_path_w(image,100,0)&&tribes_get_last_error()==87);
    assert(!tribes_get_temp_path_w(image,100,UINT32_MAX)&&tribes_get_last_error()==87);
    const char variables[]="EMPTY=\0VALUE=abc\0";assert(tribes_environment_configure(image,variables,sizeof variables));
    unsigned char *local=temporary_memory[0];strcpy((char *)local+1,"EMPTY");tribes_set_last_error(123);
    assert(tribes_get_environment_variable_a(image,1,0,0)==1&&tribes_get_last_error()==123);local[257]=0x79;
    assert(!tribes_get_environment_variable_a(image,1,257,1)&&local[257]==0&&tribes_get_last_error()==123);
    strcpy((char *)local+1,"VALUE");assert(tribes_get_environment_variable_a(image,1,1,4)==3&&!strcmp((char *)local+1,"abc"));
    strcpy((char *)local+1,"VALUE");assert(!tribes_get_environment_variable_a(image,1,0,4)&&tribes_get_last_error()==87);
    assert(!tribes_get_environment_variable_a(image,0,257,64)&&tribes_get_last_error()==203);
    local[1]=0;assert(!tribes_get_environment_variable_a(image,1,257,64)&&tribes_get_last_error()==203);
    char root[]="/tmp/tribes-temp-path-XXXXXX";assert(mkdtemp(root));char game[256],native_temp[256];
    snprintf(game,sizeof game,"%s/Game",root);snprintf(native_temp,sizeof native_temp,"%s/Temp",root);assert(!mkdir(game,0700)&&!mkdir(native_temp,0700));
    const char mapped[]="TMP=C:\\TribesPad\\Temp\0";assert(tribes_environment_configure(image,mapped,sizeof mapped));
    assert(tribes_filesystem_configure(image,root,game,native_temp));check_temp(image,"C:\\TribesPad\\Temp\\");
    char selected[261],resolved[512],canonical[1024];assert(realpath(native_temp,canonical));for(unsigned i=0;;i++){uint16_t ch;memcpy(&ch,temporary_memory[0]+129+2*i,2);selected[i]=(char)ch;if(!ch)break;}
    assert(tribes_filesystem_resolve(image,selected,resolved,sizeof resolved)&&!strcmp(resolved,canonical));
    assert(!rmdir(game)&&!rmdir(native_temp)&&!rmdir(root));
    assert(!munmap(window,UINT64_C(0x100000000)));
    puts("PASS: OEM byte snapshots, double-NUL/empty validation, immutable ANSI/UTF-16 command-line lifetime, non-ASCII/surrogate retention, stale snapshot isolation, 8000 concurrent snapshot lifetimes and guarded temporary-path queries, TMP/TEMP/USERPROFILE/Windows precedence, empty/case handling, ordinary identity, short/size/exact-capacity semantics, 32000 concurrent ASCII variable lookups, empty/present/missing semantics and source/output alias handling");return 0;
}
