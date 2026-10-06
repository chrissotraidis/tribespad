/* Independent x86-64 execution of the two original x87 timing sequences.
   Rosetta on Apple Silicon is recorded explicitly; this is not x86 hardware. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>
int main(int argc,char **argv){
 assert(argc==2);double multiplier;FILE *f=fopen(argv[1],"rb");assert(f&&fread(&multiplier,8,1,f)==1&&!fclose(f));
 int translated=0;size_t size=sizeof translated;assert(!sysctlbyname("sysctl.proc_translated",&translated,&size,NULL,0));
 printf("translated=%d\n",translated);
 for(unsigned mode=0;mode<2;mode++)for(unsigned sample=0;sample<=64;sample++){
  double previous=100.0,current=100.0+(sample+1)/4.0,scale=0.5;int32_t frequency=2;
  if(sample==64)previous=current=scale=0,frequency=0;
  unsigned top=sample%8;uint32_t env[7]={0x37f,top<<11,0xffffu&~(3u<<(2*top)),0,0,0,0};
  long double initial=(sample&16)?-1.0L:1.0L;if((sample/8)%2)initial*=2;
  uint16_t status;uint32_t result=0,prefix=0;
  __asm__ volatile("fninit\n\tfldt %0\n\tfldt %0\n\tfldt %0\n\tfldt %0\n\tfldt %0\n\tfldt %0\n\tfldt %0\n\tfldt %0\n\tfldenv %1" : : "m"(initial),"m"(env) : "memory","st","st(1)","st(2)","st(3)","st(4)","st(5)","st(6)","st(7)");
  if(mode)__asm__ volatile("fildl %1\n\tfmull %2\n\tfstps %0" : "=m"(prefix) : "m"(frequency),"m"(multiplier) : "memory","st");
  __asm__ volatile("fldl %2\n\tfsubl %3\n\tfildl %4\n\tfmull %5\n\t.byte 0xde,0xf1\n\tfstps %0\n\tfnstsw %1\n\tfninit" : "=m"(result),"=m"(status) : "m"(current),"m"(previous),"m"(frequency),"m"(scale) : "memory","st","st(1)");
  printf("%u %u %04x %08x %08x\n",mode,sample,status&~0x3800u,result,prefix);
 }
 return 0;
}
