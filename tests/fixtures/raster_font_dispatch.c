#include <stdint.h>
#include <stdlib.h>
extern void tbimp_CreateFontA(void),tbimp_SetBkColor(void),tbimp_SetTextColor(void),tbimp_GetTextMetricsA(void),tbimp_GetGlyphOutlineA(void),tbimp_GetTextExtentPoint32A(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t address){
 switch(address){case 1:return (void *)tbimp_CreateFontA;case 2:return (void *)tbimp_SetBkColor;case 3:return (void *)tbimp_SetTextColor;case 4:return (void *)tbimp_GetTextMetricsA;case 5:return (void *)tbimp_GetGlyphOutlineA;case 6:return (void *)tbimp_GetTextExtentPoint32A;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}
}
