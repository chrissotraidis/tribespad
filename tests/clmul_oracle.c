#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
int main(void){uint64_t a0,a1,b0,b1;unsigned control;
while(scanf("%u %"SCNx64" %"SCNx64" %"SCNx64" %"SCNx64,&control,&a0,&a1,&b0,&b1)==5){
 __m128i a=_mm_set_epi64x((long long)a1,(long long)a0),b=_mm_set_epi64x((long long)b1,(long long)b0),r;
 switch(control){case 0:r=_mm_clmulepi64_si128(a,b,0);break;case 1:r=_mm_clmulepi64_si128(a,b,1);break;case 16:r=_mm_clmulepi64_si128(a,b,16);break;case 17:r=_mm_clmulepi64_si128(a,b,17);break;default:return 2;}
 uint64_t v[2];_mm_storeu_si128((__m128i *)v,r);printf("%016"PRIx64" %016"PRIx64"\n",v[0],v[1]);
}return ferror(stdin)?3:0;}
