#ifndef TRIBESPAD_GDI_MEMORY_H
#define TRIBESPAD_GDI_MEMORY_H
#include <stddef.h>
#include <stdint.h>
/* Stock brushes 0..5 own immutable logical SOLID/NULL descriptors. Drawing and
   device-specific gray-pattern realization remain separate renderer gates. */
uint32_t tribes_get_stock_object(uint64_t base,uint32_t index);
int tribes_brush_snapshot(uint64_t base,uint32_t brush,uint32_t *style,uint32_t *color);
uint32_t tribes_create_memory_dc(uint64_t base,uint32_t compatible);
uint32_t tribes_create_compatible_bitmap(uint64_t base,uint32_t dc,uint32_t width,uint32_t height);
uint32_t tribes_select_gdi_object(uint64_t base,uint32_t dc,uint32_t object);
uint32_t tribes_delete_gdi_object(uint64_t base,uint32_t object);
uint32_t tribes_delete_memory_dc(uint64_t base,uint32_t dc);
/* Host renderer inspection copies pixels while retaining the registry lock. */
int tribes_bitmap_snapshot(uint64_t base,uint32_t bitmap,uint32_t *width,uint32_t *height,uint32_t *stride,uint32_t *bits,void *pixels,size_t capacity);
#endif
