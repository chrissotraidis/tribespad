/* Required IA-32 locked memory operations. Native RMWs are sequentially
 * consistent. Unsupported forms and unaligned wider operands remain gated. */
#include "llasm_cpu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t *reg(_cpu *c,unsigned selector) {
 switch(selector){case 0:return &c->_eax;case 1:return &c->_ecx;case 2:return &c->_edx;case 3:return &c->_ebx;case 4:return &c->_esp;case 5:return &c->_ebp;case 6:return &c->_esi;case 7:return &c->_edi;default:abort();}
}
void tb_locked_xadd32(_cpu *c,uint32_t address,unsigned source) {
 uint32_t *src=reg(c,source);uint64_t host=c->_pointer_offset+address;
 if((address&3)||host<c->_pointer_offset||host>UINTPTR_MAX-3||(host&3)) {
  fputs("TribesPad: unsupported unaligned/overflowing LOCK XADD32 operand\n",stderr);abort();
 }
 uint32_t addend=*src;
 uint32_t original=__atomic_fetch_add((uint32_t *)(uintptr_t)host,addend,__ATOMIC_SEQ_CST);
 uint32_t sum=original+addend;
 uint32_t flags=(sum<original?1u:0u)|((original^addend^sum)&0x10u)|
  (sum==0?0x40u:0u)|((sum>>24)&0x80u)|((~(original^addend)&(original^sum))>>20&0x800u);
 unsigned low=sum&255;low^=low>>4;low^=low>>2;low^=low>>1;
 flags|=(~low&1u)<<2;
 *src=original;c->_eflags=(c->_eflags&~0x8d5u)|flags;
}

/* LOCK CMPXCHG32 compares EAX with the original destination. Even a failed
 * exchange requires writable memory; the checked LLVM cmpxchg enforces RW. */
void tb_locked_cmpxchg32(_cpu *c,uint32_t address,unsigned source) {
 uint32_t desired=*reg(c,source),accumulator=c->_eax;uint64_t host=c->_pointer_offset+address;
 if((address&3)||host<c->_pointer_offset||host>UINTPTR_MAX-3||(host&3)) {
  fputs("TribesPad: unsupported unaligned/overflowing LOCK CMPXCHG32 operand\n",stderr);abort();
 }
 uint32_t original=accumulator;
 (void)__atomic_compare_exchange_n((uint32_t *)(uintptr_t)host,&original,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 uint32_t difference=accumulator-original;
 uint32_t flags=(accumulator<original?1u:0u)|((accumulator^original^difference)&0x10u)|
  (difference==0?0x40u:0u)|((difference>>24)&0x80u)|(((accumulator^original)&(accumulator^difference))>>20&0x800u);
 unsigned low=difference&255;low^=low>>4;low^=low>>2;low^=low>>1;flags|=(~low&1u)<<2;
 c->_eax=original;c->_eflags=(c->_eflags&~0x8d5u)|flags;
}

/* Byte CMPXCHG uses AL, including when the source is AH/CH/DH/BH. Snapshot
 * the source before replacing AL and preserve every other register bit. A
 * failed comparison still performs a checked RW atomic memory operation. */
void tb_locked_cmpxchg8(_cpu *c,uint32_t address,unsigned source) {
 if(source>7)abort();
 uint8_t desired=(uint8_t)(*reg(c,source&3)>>(source>=4?8:0));
 uint8_t accumulator=(uint8_t)c->_eax,original=accumulator;
 uint64_t host=c->_pointer_offset+address;
 if(host<c->_pointer_offset||host>UINTPTR_MAX)abort();
 (void)__atomic_compare_exchange_n((uint8_t *)(uintptr_t)host,&original,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 uint8_t difference=(uint8_t)(accumulator-original);
 uint32_t flags=(accumulator<original?1u:0u)|((accumulator^original^difference)&0x10u)|
  (difference==0?0x40u:0u)|(difference&0x80u)|(((accumulator^original)&(accumulator^difference)&0x80u)<<4);
 unsigned low=difference;low^=low>>4;low^=low>>2;low^=low>>1;flags|=(~low&1u)<<2;
 c->_eax=(c->_eax&0xffffff00u)|original;c->_eflags=(c->_eflags&~0x8d5u)|flags;
}

/* Aligned LOCK CMPXCHG8B compares EDX:EAX and stores ECX:EBX on equality.
 * Only ZF changes. Failed comparisons still require a writable destination. */
void tb_locked_cmpxchg8b(_cpu *c,uint32_t address) {
 uint64_t expected=((uint64_t)c->_edx<<32)|c->_eax;
 uint64_t desired=((uint64_t)c->_ecx<<32)|c->_ebx;
 uint64_t host=c->_pointer_offset+address;
 if((address&7)||host<c->_pointer_offset||host>UINTPTR_MAX-7||(host&7)) {
  fputs("TribesPad: unsupported unaligned/overflowing LOCK CMPXCHG8B operand\n",stderr);abort();
 }
 int success=__atomic_compare_exchange_n((uint64_t *)(uintptr_t)host,&expected,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
 c->_eax=(uint32_t)expected;c->_edx=(uint32_t)(expected>>32);
 c->_eflags=(c->_eflags&~0x40u)|(success?0x40u:0u);
}
