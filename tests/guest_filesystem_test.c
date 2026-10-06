#include "../port/core/guest_filesystem.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
static unsigned char memory[8192],worker_memory[8][32];static uint64_t base;
static char volume[PATH_MAX],game[PATH_MAX],temp[PATH_MAX],file[PATH_MAX];
static void *worker(void *arg){uint64_t worker_base=(uintptr_t)worker_memory[(uintptr_t)arg];char result[PATH_MAX];for(unsigned i=0;i<1000;i++){assert(tribes_filesystem_resolve(base,".\\BASE\\aSSET.cs",result,sizeof result));assert(!strcmp(result,file));memset((void *)(uintptr_t)worker_base,0xa5,32);tribes_set_last_error(123);assert(tribes_get_logical_drive_strings(worker_base,5,7)==4);assert(!memcmp((void *)(uintptr_t)(worker_base+7),"C:\\\0\0",5));assert(tribes_get_drive_type(worker_base,7)==3&&tribes_get_drive_type(worker_base,0)==3);assert(worker_memory[(uintptr_t)arg][6]==0xa5&&worker_memory[(uintptr_t)arg][12]==0xa5&&tribes_get_last_error()==123);}return NULL;}
int main(void){
    char root[]="/tmp/tribes-fs-XXXXXX";assert(mkdtemp(root));assert(realpath(root,volume));
    snprintf(game,sizeof game,"%s/Game",volume);snprintf(temp,sizeof temp,"%s/Temp",volume);assert(!mkdir(game,0700)&&!mkdir(temp,0700));
    char dir[PATH_MAX],outside[PATH_MAX],link[PATH_MAX];snprintf(dir,sizeof dir,"%s/Base",game);assert(!mkdir(dir,0700));snprintf(file,sizeof file,"%s/Asset.cs",dir);FILE *f=fopen(file,"wb");assert(f);fputs("real fixture",f);assert(!fclose(f));
    snprintf(outside,sizeof outside,"%s/outside",volume);f=fopen(outside,"wb");assert(f);assert(!fclose(f));snprintf(link,sizeof link,"%s/escape",game);assert(!symlink(outside,link));
    base=(uintptr_t)memory;assert(tribes_filesystem_configure(base,volume,game,temp));assert(!tribes_filesystem_configure(base,volume,game,temp)&&errno==EALREADY);
    char host[PATH_MAX];assert(tribes_filesystem_resolve(base,"c:/tribespad/game/BASE/ASSET.CS",host,sizeof host)&&!strcmp(host,file));
    assert(tribes_filesystem_resolve(base,"C:Base/Asset.cs",host,sizeof host)&&!strcmp(host,file));
    assert(tribes_filesystem_resolve(base,"Base/../Base/./Asset.cs",host,sizeof host)&&!strcmp(host,file));
    assert(tribes_filesystem_resolve(base,"C:\\../../",host,sizeof host)&&!strcmp(host,volume));
    assert(tribes_filesystem_resolve(base,"..\\Temp",host,sizeof host)&&!strcmp(host,temp));
    assert(!tribes_filesystem_resolve(base,"escape",host,sizeof host)&&errno==EACCES);
    assert(!tribes_filesystem_resolve(base,"D:\\",host,sizeof host)&&errno==ENOENT);
    assert(!tribes_filesystem_resolve(base,"\\\\server\\share\\",host,sizeof host)&&errno==ENOTSUP);
    assert(!tribes_filesystem_resolve(base,"caf\xe9.cs",host,sizeof host)&&errno==ENOTSUP);
    assert(!tribes_filesystem_resolve(base,"missing",host,sizeof host)&&errno==ENOENT);
    assert(!tribes_filesystem_resolve(base,"Base/Asset.cs",host,2)&&errno==ENAMETOOLONG);
    assert(!tribes_filesystem_resolve(base+1,"C:\\",host,sizeof host)&&errno==ENODEV);
    const char expected[]="C:\\TribesPad\\Game";
    for(uint32_t n=0;n<sizeof expected+2;n++){
        memset(memory+499,0xa5,128);tribes_set_last_error(123);uint32_t result=tribes_get_current_directory(base,n,500);
        if(n<sizeof expected){assert(result==sizeof expected);for(unsigned j=499;j<627;j++)assert(memory[j]==0xa5);}
        else assert(result==sizeof expected-1&&!strcmp((char *)memory+500,expected)&&memory[499]==0xa5&&memory[500+sizeof expected]==0xa5);
        assert(tribes_get_last_error()==123);
    }
    assert(tribes_get_current_directory(base,0,0)==sizeof expected);
    assert(!tribes_get_current_directory(base,64,0)&&tribes_get_last_error()==87);
    for(unsigned n=0;n<8;n++){memset(memory+600,0xa5,32);tribes_set_last_error(123);uint32_t result=tribes_get_logical_drive_strings(base,n,607);assert(result==(n<5?5:4));if(n<5){for(unsigned i=600;i<632;i++)assert(memory[i]==0xa5);}else{assert(!memcmp(memory+607,"C:\\\0\0",5)&&memory[606]==0xa5&&memory[612]==0xa5);}assert(tribes_get_last_error()==123);}
    assert(tribes_get_logical_drive_strings(base,0,UINT32_MAX)==5);assert(!tribes_get_logical_drive_strings(base,5,0)&&tribes_get_last_error()==87);assert(!tribes_get_logical_drive_strings(base,5,UINT32_MAX-3)&&tribes_get_last_error()==87);assert(!tribes_get_logical_drive_strings(base+1,5,607)&&tribes_get_last_error()==3);assert(tribes_get_logical_drive_strings(base,UINT32_MAX,607)==4);
    const char *roots[]={"C:\\","c:\\","D:\\","C:","C:/","", "C:\\x"};
    for(unsigned i=0;i<sizeof roots/sizeof roots[0];i++){memset(memory+699,0xa5,32);strcpy((char *)memory+700,roots[i]);unsigned char before_root[32];memcpy(before_root,memory+699,32);tribes_set_last_error(123);assert(tribes_get_drive_type(base,700)==(i<2?3:1));assert(!memcmp(before_root,memory+699,32)&&tribes_get_last_error()==123);}
    assert(tribes_get_drive_type(base,0)==3&&tribes_get_drive_type(base+1,0)==1);assert(tribes_get_drive_type(base,UINT32_MAX-2)==1&&tribes_get_last_error()==87);
    pthread_t threads[8];for(unsigned i=0;i<8;i++){assert(tribes_filesystem_configure((uintptr_t)worker_memory[i],volume,game,temp));assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));}for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    memset(memory,0xa5,sizeof memory);strcpy((char *)memory+128,"C:\\");tribes_set_last_error(123);
    struct statvfs before;assert(!statvfs(volume,&before));
    assert(tribes_disk_free_space(base,128,17,25,33,41));uint32_t values[4];for(unsigned i=0;i<4;i++)memcpy(values+i,memory+17+8*i,4);
    struct statvfs s;assert(!statvfs(volume,&s));assert(values[0]*values[1]==(s.f_frsize?s.f_frsize:s.f_bsize));assert(values[1]==512&&values[3]==s.f_blocks);assert(values[2]<=values[3]);
    assert(values[2]>=(before.f_bavail<s.f_bavail?before.f_bavail:s.f_bavail)&&values[2]<=(before.f_bavail>s.f_bavail?before.f_bavail:s.f_bavail));assert(tribes_get_last_error()==123);
    for(unsigned i=0;i<4;i++){assert(memory[16+8*i]==0xa5&&memory[21+8*i]==0xa5);}
    assert(tribes_disk_free_space(base,0,17,25,33,41));
    assert(!tribes_disk_free_space(base,0,0,25,33,41)&&tribes_get_last_error()==87);
    assert(!tribes_disk_free_space(base,0,UINT32_MAX,25,33,41)&&tribes_get_last_error()==87);
    strcpy((char *)memory+128,"D:\\");assert(!tribes_disk_free_space(base,128,17,25,33,41)&&tribes_get_last_error()==3);
    assert(!unlink(link)&&!unlink(file)&&!unlink(outside)&&!rmdir(dir)&&!rmdir(game)&&!rmdir(temp)&&!rmdir(volume));
    puts("PASS: real mounted files, case and relative paths, unsupported-path errors, 8000 concurrent lookups and process-owned guarded MULTI_SZ drive enumerations with size-query/short/exact/oversized buffers, native disk geometry and unaligned output guards");return 0;
}
