#ifndef TRIBESPAD_GUEST_NTSTATUS_H
#define TRIBESPAD_GUEST_NTSTATUS_H
#include <stdint.h>
uint32_t tribes_ntstatus_to_dos_error(uint32_t status);
#endif
