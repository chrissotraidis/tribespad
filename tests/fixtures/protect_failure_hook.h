#include <sys/mman.h>
int tribes_test_mprotect(void *,size_t,int);
#define mprotect tribes_test_mprotect
