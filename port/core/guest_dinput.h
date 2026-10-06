#ifndef TRIBESPAD_GUEST_DINPUT_H
#define TRIBESPAD_GUEST_DINPUT_H
#include <stdint.h>
#define TB_DINPUT_MODULE 0x60000000u
#define TB_DI_CREATE 0xfffd0000u
#define TB_DIDEV_METHOD(i) (0xfffd2000u+16u*(i))
#define TB_DI_METHOD(i) (0xfffd1000u+16u*(i))
typedef struct { unsigned char guid[16];uint32_t offset,type,flags;const char *name;uint16_t usage_page,usage; } TribesInputObject;
typedef struct { unsigned char instance_guid[16],product_guid[16];uint32_t type;const char *instance_name,*product_name;int attached;uint32_t axes,buttons,povs;const TribesInputObject *objects;uint32_t object_count; } TribesInputDevice;
/* Host-owned device snapshots; callbacks may replace the catalog reentrantly. */
int tribes_dinput_set_devices(uint64_t base,const TribesInputDevice *devices,uint32_t count);
uint32_t tribes_dinput_enumerate(void *cpu,uint32_t object,uint32_t type,uint32_t callback,uint32_t context,uint32_t flags);
uint32_t tribes_dinput_enum_objects(void *cpu,uint32_t object,uint32_t callback,uint32_t context,uint32_t flags);
uint32_t tribes_dinput_create_device(uint64_t base,uint32_t root,uint32_t guid,uint32_t output,uint32_t outer);
uint32_t tribes_dinput_device_caps(uint64_t base,uint32_t object,uint32_t output);
uint32_t tribes_dinput_set_format(uint64_t base,uint32_t object,uint32_t format);
uint32_t tribes_dinput_set_property(void *cpu,uint32_t object,uint32_t property,uint32_t header);
uint32_t tribes_dinput_get_property(void *cpu,uint32_t object,uint32_t property,uint32_t header);
uint32_t tribes_dinput_device_info(uint64_t base,uint32_t object,uint32_t output);
int tribes_dinput_configure(uint64_t base);
uint32_t tribes_dinput_create(uint64_t base,uint32_t instance,uint32_t version,uint32_t output,uint32_t outer);
uint32_t tribes_dinput_query(uint64_t base,uint32_t object,uint32_t iid,uint32_t output);
uint32_t tribes_dinput_addref(uint64_t base,uint32_t object);
uint32_t tribes_dinput_release(uint64_t base,uint32_t object);
uint32_t tribes_dinput_initialize(uint64_t base,uint32_t object,uint32_t instance,uint32_t version);
#endif
