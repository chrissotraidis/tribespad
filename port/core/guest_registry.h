#ifndef TRIBESPAD_GUEST_REGISTRY_H
#define TRIBESPAD_GUEST_REGISTRY_H
#include <stdint.h>
uint32_t tribes_reg_open_key_ex_w(uint64_t base,uint32_t key,uint32_t subkey,uint32_t options,uint32_t access,uint32_t output);
uint32_t tribes_reg_query_value_ex_w(uint64_t base,uint32_t key,uint32_t name,uint32_t reserved,uint32_t type,uint32_t data,uint32_t size);
uint32_t tribes_reg_close_key(uint64_t base,uint32_t key);
uint32_t tribes_reg_open_key_ex_a(uint64_t base,uint32_t key,uint32_t subkey,uint32_t options,uint32_t access,uint32_t output);
uint32_t tribes_reg_get_value_a(uint64_t base,uint32_t key,uint32_t subkey,uint32_t name,uint32_t flags,uint32_t type,uint32_t data,uint32_t size);
uint32_t tribes_reg_query_value_ex_a(uint64_t base,uint32_t key,uint32_t name,uint32_t reserved,uint32_t type,uint32_t data,uint32_t size);
uint32_t tribes_reg_set_value_ex_a(uint64_t base,uint32_t key,uint32_t name,uint32_t reserved,uint32_t type,uint32_t data,uint32_t size);
uint32_t tribes_reg_delete_value_a(uint64_t base,uint32_t key,uint32_t name);
#endif
