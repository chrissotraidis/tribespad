#ifndef TRIBESPAD_GUEST_EXTERNAL_CHECKS_H
#define TRIBESPAD_GUEST_EXTERNAL_CHECKS_H
#include <stdint.h>
void tribes_memory_external_configure(uint64_t base,uint64_t size);
void tribes_memory_register_targets(const void *const *targets,uint64_t count);
void tribes_memory_external_enable(void);
void tribes_memory_external_gate(const char *name,uint32_t allowed);
void tribes_memory_external_pointer(const char *name,const void *pointer);
void tribes_memory_indirect_target(const void *pointer);
void tribes_memory_callback_target(const void *pointer,uint32_t nullable);
uint64_t tribes_memory_checked_strnlen(const void *pointer,uint64_t limit);
uint64_t tribes_memory_checked_strlen(const void *pointer);
int32_t tribes_memory_checked_strcmp(const void *left,const void *right);
int32_t tribes_memory_checked_strncmp(const void *left,const void *right,uint64_t limit);
#endif
