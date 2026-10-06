#include <stdio.h>
int main(void){unsigned saved;__asm__ volatile("stmxcsr %0":"=m"(saved));for(unsigned x=0;x<=0x1f80;x+=0x1f80){unsigned y=~0u;__asm__ volatile("ldmxcsr %1; stmxcsr %0":"=m"(y):"m"(x):"memory");printf("%x -> %x\n",x,y);}__asm__ volatile("ldmxcsr %0"::"m"(saved):"memory");return 0;}
