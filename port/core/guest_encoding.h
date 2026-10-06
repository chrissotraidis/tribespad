#ifndef TRIBESPAD_GUEST_ENCODING_H
#define TRIBESPAD_GUEST_ENCODING_H
#include <stdint.h>
/* Fixed English legacy profile, matching the existing single-byte desktop
   contract. This does not infer a Windows code page from the Apple locale. */
/* Current process file APIs retain their default ANSI mode. OEM switching is
   not published until filename conversion supports it; filenames are ASCII. */
uint32_t tribes_are_file_apis_ansi(void);
uint32_t tribes_get_acp(void);
uint32_t tribes_get_oemcp(void);
uint32_t tribes_is_valid_code_page(uint32_t page);
uint32_t tribes_get_cp_info(uint64_t base,uint32_t page,uint32_t output);
uint32_t tribes_multibyte_to_wide(uint64_t base,uint32_t page,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity);
uint32_t tribes_wide_to_multibyte(uint64_t base,uint32_t page,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity,uint32_t default_char,uint32_t used_default);
uint32_t tribes_get_string_type_w(uint64_t base,uint32_t kind,uint32_t input,uint32_t count,uint32_t output);
uint32_t tribes_lcmap_string_ex(uint64_t base,uint32_t locale,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity,uint32_t version,uint32_t reserved,uint32_t sort_handle);
uint32_t tribes_lcmap_string_w(uint64_t base,uint32_t locale,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity);
uint32_t tribes_lcmap_string_a(uint64_t base,uint32_t locale,uint32_t flags,uint32_t input,uint32_t count,uint32_t output,uint32_t capacity);
uint32_t tribes_get_string_type_a(uint64_t base,uint32_t locale,uint32_t kind,uint32_t input,uint32_t count,uint32_t output);
#endif
