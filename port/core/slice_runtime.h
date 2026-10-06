#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint32_t base,size;uint8_t *host;} TBRegion;
typedef struct {uint32_t r[8],pc,cf,zf,sf,of,pf;TBRegion regions[3];} TBState;
static inline uint8_t *tb_address(TBState*s,uint32_t a,uint32_t n){
 for(int i=0;i<3;i++){TBRegion*r=&s->regions[i];if(a>=r->base && n<=r->size && a-r->base<=r->size-n)return r->host+a-r->base;}
 abort();
}
static inline uint32_t tb_read(TBState*s,uint32_t a,uint32_t n){uint32_t v=0;memcpy(&v,tb_address(s,a,n),n);return v;}
static inline void tb_write(TBState*s,uint32_t a,uint32_t n,uint32_t v){memcpy(tb_address(s,a,n),&v,n);}
static inline void tb_result(TBState*s,uint32_t v,uint32_t bits){s->zf=v==0;s->sf=(v>>(bits-1))&1;s->pf=!__builtin_parity(v&255);}
static inline void tb_logic(TBState*s,uint32_t v,uint32_t bits){s->cf=0;s->of=0;tb_result(s,v,bits);}
static inline void tb_arith(TBState*s,uint32_t a,uint32_t b,uint32_t v,uint32_t bits,int sub,int keepCF){
 if(!keepCF)s->cf=sub?a<b:((uint64_t)a+b)>>bits;
 s->of=((sub?(a^b):~(a^b))&(a^v))>>(bits-1)&1;tb_result(s,v,bits);
}
static inline void tb_shr(TBState*s,uint32_t a,uint32_t b,uint32_t v,uint32_t bits){
 b&=31;if(!b)return;s->cf=(a>>(b-1))&1;if(b==1)s->of=a>>(bits-1);tb_result(s,v,bits);
}
void tb_crc32(TBState*);
void tb_game_version(TBState*);
