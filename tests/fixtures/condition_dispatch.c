#include <stdint.h>
#include <stdlib.h>
extern void tbimp_InitializeConditionVariable(void),tbimp_WakeConditionVariable(void),tbimp_WakeAllConditionVariable(void),tbimp_SleepConditionVariableSRW(void),tbimp_SleepConditionVariableCS(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){
 switch(pc){case 1:return (void *)tbimp_InitializeConditionVariable;case 2:return (void *)tbimp_WakeConditionVariable;case 3:return (void *)tbimp_WakeAllConditionVariable;case 4:return (void *)tbimp_SleepConditionVariableSRW;case 5:return (void *)tbimp_SleepConditionVariableCS;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}
}
