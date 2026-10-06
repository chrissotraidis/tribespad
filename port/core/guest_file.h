#ifndef TRIBESPAD_GUEST_FILE_H
#define TRIBESPAD_GUEST_FILE_H
#include <stdint.h>
uint32_t tribes_create_directory(uint64_t base,uint32_t name,uint32_t security);
uint32_t tribes_create_directory_w(uint64_t base,uint32_t name,uint32_t security);
uint32_t tribes_create_file(uint64_t base,uint32_t name,uint32_t access,uint32_t share,uint32_t security,uint32_t creation,uint32_t flags,uint32_t template_handle);
uint32_t tribes_create_file_w(uint64_t base,uint32_t name,uint32_t access,uint32_t share,uint32_t security,uint32_t creation,uint32_t flags,uint32_t template_handle);
uint32_t tribes_get_file_attributes(uint64_t base,uint32_t name);
uint32_t tribes_get_file_attributes_w(uint64_t base,uint32_t name);
uint32_t tribes_nt_query_volume_information_file(uint64_t base,uint32_t handle,uint32_t iosb,uint32_t output,uint32_t capacity,uint32_t information_class);
uint32_t tribes_nt_query_information_file(uint64_t base,uint32_t handle,uint32_t iosb,uint32_t output,uint32_t capacity,uint32_t information_class);
uint32_t tribes_get_file_size(uint64_t base,uint32_t handle,uint32_t high);
uint32_t tribes_set_file_pointer(uint64_t base,uint32_t handle,uint32_t low,uint32_t high,uint32_t method);
uint32_t tribes_read_file(uint64_t base,uint32_t handle,uint32_t buffer,uint32_t count,uint32_t output,uint32_t overlapped);
uint32_t tribes_write_file(uint64_t base,uint32_t handle,uint32_t buffer,uint32_t count,uint32_t output,uint32_t overlapped);
#endif
