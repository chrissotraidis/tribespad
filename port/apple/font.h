#ifndef TRIBESPAD_APPLE_FONT_H
#define TRIBESPAD_APPLE_FONT_H
#include <stdint.h>
/* Private native font storage; these layouts are the original x86 GDI records. */
typedef struct {
    int32_t height,ascent,descent,internal_leading,external_leading,average_width,
        maximum_width,weight,overhang,aspect_x,aspect_y;
    uint8_t first,last,default_char,break_char,italic,underline,strikeout,pitch_family,charset,padding[3];
} TBFontMetrics;
typedef struct {uint32_t width,height;int32_t x,y;int16_t advance_x,advance_y;} TBGlyphMetrics;
_Static_assert(sizeof(TBFontMetrics)==56,"x86 TEXTMETRICA");
_Static_assert(sizeof(TBGlyphMetrics)==20,"x86 GLYPHMETRICS");
void *tb_apple_font_create(const char *face,int32_t height);
void tb_apple_font_release(void *font);
int tb_apple_font_metrics(void *font,TBFontMetrics *output);
/* Host-owned output only: returns a DWORD-aligned top-down 0..64 mask. */
int tb_apple_font_glyph(void *font,uint32_t character,TBGlyphMetrics *metrics,
    unsigned char **pixels,uint32_t *bytes);
int tb_apple_font_extent(void *font,const unsigned char *text,uint32_t count,int32_t output[2]);
#endif
