#ifndef TRIBESPAD_GUEST_AVI_H
#define TRIBESPAD_GUEST_AVI_H
#include <stdint.h>
/* Process-wide AVI library lifetime. Opening/decoding AVI files remains an
   explicit missing-service boundary until a native media provider is added. */
void tribes_avi_file_init(void);
void tribes_avi_file_exit(void);
uint32_t tribes_avi_reference_count(void);
#endif
