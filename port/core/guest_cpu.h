#ifndef TRIBESPAD_GUEST_CPU_H
#define TRIBESPAD_GUEST_CPU_H
#include "../llasm-support/llasm_cpu.h"
/* Virtual compatibility CPU; describes native lowering, not Apple hardware. */
void x86_cpuid(_cpu *cpu);
#endif
