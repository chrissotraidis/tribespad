#ifndef TRIBESPAD_PROCESS_ENVIRONMENT_H
#define TRIBESPAD_PROCESS_ENVIRONMENT_H
#include <stdint.h>
/* Host configuration takes a complete, double-NUL-terminated OEM byte block.
   The calling runtime owns encoding/path mappings; existing snapshots stay valid. */
uint32_t tribes_environment_configure(uint64_t base,const void *block,uint32_t length);
/* Ordinary non-SYSTEM process profile; host supplies its Windows directory. */
uint32_t tribes_temp_directory_configure(uint64_t base,const char *windows_directory);
uint32_t tribes_get_windows_directory_a(uint64_t base,uint32_t output,uint32_t capacity);
uint32_t tribes_get_system_directory_a(uint64_t base,uint32_t output,uint32_t capacity);
uint32_t tribes_get_temp_path_w(uint64_t base,uint32_t capacity,uint32_t output);
uint32_t tribes_get_environment_variable_a(uint64_t base,uint32_t name,uint32_t output,uint32_t capacity);
uint32_t tribes_get_environment_strings(uint64_t base);
uint32_t tribes_free_environment_strings(uint64_t base,uint32_t pointer);
uint32_t tribes_command_line_configure(uint64_t base,const void *text,uint32_t length);
uint32_t tribes_get_command_line(uint64_t base);
/* Explicit UTF-16LE launch text, including its two-byte terminator. Host
   configuration supplies consistent ANSI/code-page and wide representations. */
uint32_t tribes_command_line_configure_w(uint64_t base,const void *text,uint32_t bytes);
uint32_t tribes_get_command_line_w(uint64_t base);
uint32_t tribes_get_environment_strings_w(uint64_t base);
uint32_t tribes_free_environment_strings_w(uint64_t base,uint32_t pointer);
/* Native diagnostic metadata, never a guest API or pointer to mutable records. */
uint32_t tribes_environment_snapshot_size(uint64_t base,uint32_t pointer,int wide);
#endif
