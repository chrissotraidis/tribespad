#include "../port/core/native_handles.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
static void *churn(void *unused) {
    (void)unused;
    for(unsigned i=0;i<1000;i++) {
        int fd=open("/dev/null",O_RDWR);assert(fd>=0);uint32_t h=tribes_fd_adopt(fd);assert(h);
        int copy=tribes_fd_borrow(h);assert(copy>=3&&fcntl(copy,F_GETFD)&FD_CLOEXEC);
        assert(tribes_close_handle(h));assert(tribes_get_file_type(h)==0&&tribes_get_last_error()==6);
        assert(fcntl(copy,F_GETFD)>=0);close(copy);
    }
    return NULL;
}
int main(void) {
    /* No existing std handle table: copying stdin must not occupy missing stdout/stderr. */
    pid_t child=fork();assert(child>=0);
    if(!child) { close(1);close(2);uint32_t input=tribes_get_std_handle(0xfffffff6);_exit(!input||tribes_get_std_handle(0xfffffff5)!=0||tribes_get_std_handle(0xfffffff4)!=0); }
    int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    tribes_set_last_error(123);uint32_t output=tribes_get_std_handle(0xfffffff5);assert(output&&output!=UINT32_MAX&&tribes_get_last_error()==123);
    assert(tribes_get_std_handle(0xfffffff5)==output);assert(tribes_get_std_handle(99)==UINT32_MAX&&tribes_get_last_error()==6);
    assert(tribes_set_std_handle(0xfffffff5,0x12345678));assert(tribes_get_std_handle(0xfffffff5)==0x12345678);
    assert(tribes_get_file_type(0x12345678)==0);assert(tribes_set_std_handle(0xfffffff5,output));
    int fd=open("/dev/null",O_RDWR);assert(fd>=0);uint32_t character=tribes_fd_adopt(fd);assert(tribes_get_file_type(character)==2);assert(tribes_close_handle(character));
    int pipefd[2];assert(!pipe(pipefd));uint32_t readh=tribes_fd_adopt(pipefd[0]),writeh=tribes_fd_adopt(pipefd[1]);assert(tribes_get_file_type(readh)==3&&tribes_get_file_type(writeh)==3);
    int writer=tribes_fd_borrow(writeh),reader=tribes_fd_borrow(readh);assert(tribes_close_handle(writeh)&&tribes_close_handle(readh));
    assert(write(writer,"T2",2)==2);char data[2];assert(read(reader,data,2)==2&&data[0]=='T'&&data[1]=='2');close(writer);close(reader);
    int sockets[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));uint32_t socket=tribes_fd_adopt(sockets[0]);assert(tribes_get_file_type(socket)==3);assert(tribes_close_handle(socket));close(sockets[1]);
    char name[]="/tmp/tribespad-handles-XXXXXX";fd=mkstemp(name);assert(fd>=0);unlink(name);uint32_t disk=tribes_fd_adopt(fd);assert(tribes_get_file_type(disk)==1);
    unsigned char out[8]={0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa};uint64_t base=(uintptr_t)out-17;
    assert(write(fd,"ABC",3)==3&&lseek(fd,0,SEEK_SET)==0);
    assert(tribes_duplicate_handle(base,UINT32_MAX,disk,UINT32_MAX,17,0,0,2));uint32_t duplicate;memcpy(&duplicate,out,4);
    for(unsigned i=4;i<8;i++)assert(out[i]==0xaa);
    int original=tribes_fd_borrow(disk),copy=tribes_fd_borrow(duplicate);char value;
    assert(read(original,&value,1)==1&&value=='A');assert(read(copy,&value,1)==1&&value=='B');close(original);close(copy);
    assert(tribes_close_handle(duplicate));
    assert(tribes_duplicate_handle(base,UINT32_MAX,disk,UINT32_MAX,17,0,1,3));memcpy(&duplicate,out,4);
    assert(tribes_get_file_type(disk)==0&&tribes_get_file_type(duplicate)==1);assert(tribes_close_handle(duplicate));
    assert(!tribes_duplicate_handle(base,UINT32_MAX,disk,UINT32_MAX,17,0,0,2));assert(tribes_get_last_error()==6);
    assert(!tribes_close_handle(disk)&&tribes_get_last_error()==6);assert(tribes_get_current_process()==UINT32_MAX);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,churn,NULL));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(fcntl(STDOUT_FILENO,F_GETFD)>=0);
    puts("PASS: standard/absent/redirected handles, disk/pipe/socket/character types, borrow-close and duplicate/shared-position/source-close lifetimes, 8000 concurrent lifetimes");return 0;
}
