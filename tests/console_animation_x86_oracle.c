/* Original animation tick conversion instructions, executed under Rosetta.
   Compare full XMM0 and MXCSR, including inexact conversion/division status. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>
int main(int argc,char **argv){
 assert(argc==2);unsigned char constants[20];FILE *f=fopen(argv[1],"rb");assert(f&&fread(constants,sizeof constants,1,f)==1&&!fclose(f));
 int translated=0;size_t size=sizeof translated;assert(!sysctlbyname("sysctl.proc_translated",&translated,&size,NULL,0));printf("translated=%d\n",translated);
 unsigned delta,mxcsr;
 while(scanf("%x %x",&delta,&mxcsr)==2){
  uint32_t output[4],status;const void *add=constants+(delta>>31)*8;
  __asm__ volatile("ldmxcsr %2\n\tmovd %3,%%xmm0\n\tcvtdq2pd %%xmm0,%%xmm0\n\taddsd (%4),%%xmm0\n\tcvtpd2ps %%xmm0,%%xmm0\n\tdivss 16(%5),%%xmm0\n\tmovdqu %%xmm0,%0\n\tstmxcsr %1"
   : "=m"(output),"=m"(status) : "m"(mxcsr),"r"(delta),"r"(add),"r"(constants) : "xmm0","memory");
  printf("%08x %08x %08x %08x %08x\n",output[0],output[1],output[2],output[3],status);
 }
 return 0;
}
