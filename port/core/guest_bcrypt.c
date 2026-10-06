/* System-preferred BCrypt RNG, backed by Apple's cryptographic arc4random_buf.
   https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom
   Null-handle/zero-size/error behavior checked against Wine10 bcrypt tests.
   Windows8+ ignores the entropy-in-buffer flag; no guest input is consumed.
   Algorithm providers/pseudo-handles are not implemented by this adapter. */
#include "guest_bcrypt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
uint32_t tribes_bcrypt_gen_random(uint64_t base,uint32_t algorithm,uint32_t output,uint32_t length,uint32_t flags){
 if(algorithm||(flags&~3u)){fputs("TribesPad: unsupported BCryptGenRandom provider or flags\n",stderr);abort();}
 if(!(flags&2u))return UINT32_C(0xc0000008); /* STATUS_INVALID_HANDLE */
 uint64_t last=(uint64_t)output+(length?length-1:0);
 if(!base||!output||(uint64_t)output+length>UINT64_C(0x100000000)||base>UINTPTR_MAX-last)return UINT32_C(0xc000000d);
 if(!length)return 0;
 unsigned char *temporary=malloc(length);if(!temporary)return UINT32_C(0xc0000017); /* STATUS_NO_MEMORY */
 /* The external gate permits only the exact Darwin ABI and a host pointer.
    The compiler brackets the entire guest copy before any output is changed. */
 arc4random_buf(temporary,length);
 memcpy((void *)(uintptr_t)(base+output),temporary,length);free(temporary);return 0;
}
