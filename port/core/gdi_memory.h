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
uint32_t tribes_create_font_a(uint64_t base,uint32_t height,uint32_t width,uint32_t escapement,uint32_t orientation,uint32_t weight,uint32_t italic,uint32_t underline,uint32_t strikeout,uint32_t charset,uint32_t out_precision,uint32_t clip_precision,uint32_t quality,uint32_t pitch,uint32_t face);
uint32_t tribes_set_bk_color(uint64_t base,uint32_t dc,uint32_t color);
uint32_t tribes_set_text_color(uint64_t base,uint32_t dc,uint32_t color);
uint32_t tribes_get_text_metrics_a(uint64_t base,uint32_t dc,uint32_t output);
uint32_t tribes_get_glyph_outline_a(uint64_t base,uint32_t dc,uint32_t character,uint32_t format,uint32_t metrics,uint32_t capacity,uint32_t buffer,uint32_t transform);
uint32_t tribes_get_text_extent_point32_a(uint64_t base,uint32_t dc,uint32_t text,uint32_t count,uint32_t output);
/* Host renderer inspection copies pixels while retaining the registry lock. */
int tribes_bitmap_snapshot(uint64_t base,uint32_t bitmap,uint32_t *width,uint32_t *height,uint32_t *stride,uint32_t *bits,void *pixels,size_t capacity);
#endif
