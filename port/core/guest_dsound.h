#ifndef TRIBESPAD_GUEST_DSOUND_H
#define TRIBESPAD_GUEST_DSOUND_H
#include "guest_callback.h"
#define TB_DSOUND_MODULE 0x20060000u
#define TB_DS_CREATE 0xffd00000u
#define TB_DS_ENUMERATE 0xffd00010u
#define TB_DSB_METHOD(i) (0xffd02000u + 16u*(i))
#define TB_DSL_METHOD(i) (0xffd03000u + 16u*(i))
#define TB_DS_METHOD(i) (0xffd01000u + 16u*(i))
int tribes_dsound_configure(uint64_t base);
void *tribes_dsound_native_lookup(uint32_t address);
uint32_t tribes_dsound_create(uint64_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_dsound_enumerate(_cpu *,uint32_t,uint32_t);
uint32_t tribes_ds_query(uint64_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_ds_addref(uint64_t,uint32_t);
uint32_t tribes_ds_release(uint64_t,uint32_t);
uint32_t tribes_ds_caps(uint64_t,uint32_t,uint32_t);
uint32_t tribes_ds_cooperative(uint64_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_ds_compact(uint64_t,uint32_t);
uint32_t tribes_ds_get_speaker(uint64_t,uint32_t,uint32_t);
uint32_t tribes_ds_set_speaker(uint64_t,uint32_t,uint32_t);
uint32_t tribes_ds_initialize(uint64_t,uint32_t,uint32_t);
/* Unimplemented buffer/speaker methods stop before returning a guest result. */
_Noreturn void tribes_ds_stop(_cpu *,uint32_t method);
#endif
