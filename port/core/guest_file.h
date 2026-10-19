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
uint32_t tribes_nt_set_information_file(uint64_t base,uint32_t handle,uint32_t iosb,uint32_t input,uint32_t length,uint32_t information_class);
uint32_t tribes_get_file_size(uint64_t base,uint32_t handle,uint32_t high);
uint32_t tribes_get_file_information_by_handle(uint64_t base,uint32_t handle,uint32_t output);
uint32_t tribes_move_file_ex_w(uint64_t base,uint32_t from,uint32_t to,uint32_t flags);
uint32_t tribes_set_file_time(uint64_t base,uint32_t handle,uint32_t creation,uint32_t access,uint32_t write);
uint32_t tribes_set_file_pointer(uint64_t base,uint32_t handle,uint32_t low,uint32_t high,uint32_t method);
uint32_t tribes_read_file(uint64_t base,uint32_t handle,uint32_t buffer,uint32_t count,uint32_t output,uint32_t overlapped);
uint32_t tribes_write_file(uint64_t base,uint32_t handle,uint32_t buffer,uint32_t count,uint32_t output,uint32_t overlapped);
uint32_t tribes_set_file_pointer_ex(uint64_t base,uint32_t handle,uint32_t low,uint32_t high,uint32_t output,uint32_t method);
uint32_t tribes_get_file_size_ex(uint64_t base,uint32_t handle,uint32_t output);
uint32_t tribes_flush_file_buffers(uint64_t base,uint32_t handle);
uint32_t tribes_set_end_of_file(uint64_t base,uint32_t handle);
uint32_t tribes_rtl_gen_random(uint64_t base,uint32_t buffer,uint32_t length);
#endif
