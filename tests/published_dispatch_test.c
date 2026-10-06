#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
static int ready;
static void engine(_cpu *cpu){(void)cpu;}
static void bank(_cpu *cpu){(void)cpu;}
static void dll(_cpu *cpu){(void)cpu;}
void tribes_return_to_host(_cpu *cpu){(void)cpu;}
const uint32_t tribes_dispatch_count=3;
const uint32_t tribes_dispatch_vas[]={0x3c00d8,0x401000,0x10001000};
void (*const tribes_dispatch_fns[])(_cpu *)={bank,engine,dll};
int tribes_dispatch_permitted(uint32_t address){return ready||address>=0x10000000;}
extern void (*tribes_lookup(uint32_t))(_cpu *);
int main(void){
 assert(tribes_lookup(0x10001000)==dll);assert(tribes_lookup(0xfffff000)==tribes_return_to_host);
 for(unsigned i=0;i<2;i++){pid_t child=fork();assert(child>=0);if(!child){(void)tribes_lookup(tribes_dispatch_vas[i]);_exit(99);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);}
 ready=1;assert(tribes_lookup(0x401000)==engine&&tribes_lookup(0x3c00d8)==bank&&tribes_lookup(0x10001000)==dll);
 puts("PASS: linked dispatch policy rejects engine/bank before publication and admits original DLL/host-return entries; published target identities preserved");
}
