/* HaloPad Apple host: C interface to the Metal layer (port/apple/halopad_metal.m). All
 * enum fields hold Metal enum values; the Direct3D -> Metal mapping is done by the caller
 * (port/runtime/halopad_d3d9_device.c). Returned objects are retained; release them with
 * halopad_metal_release. */
#ifndef HALOPAD_METAL_H
#define HALOPAD_METAL_H
#include <stdint.h>

typedef struct { uint8_t stream, format, reg, pad; uint32_t offset; } hp_vattr;   /* format: MTLVertexFormat */

typedef struct {
    const char *vs_msl, *ps_msl;
    uint32_t nattr;
    hp_vattr attr[16];
    uint32_t stride[16];
    uint32_t constant_regs;          /* shader inputs with no element: constant (0, 0, 0, 1) */
    uint8_t blend, src_rgb, dst_rgb, op_rgb, src_a, dst_a, op_a, write_mask;
    uint8_t depth;                   /* the target has a depth/stencil attachment */
    uint32_t color_format;           /* MTLPixelFormat of the colour attachment (0: BGRA8Unorm) */
} hp_pipeline_desc;

typedef struct {
    uint8_t depth_write, depth_func;          /* MTLCompareFunction (always when depth testing is off) */
    uint8_t stencil, fail[2], zfail[2], pass[2], func[2];   /* [0] front, [1] back */
    uint32_t read_mask, write_mask;
} hp_depth_desc;

typedef struct {
    uint8_t min, mag, mip, u, v, w, border, anisotropy;   /* MTLSamplerMinMagFilter, MipFilter, AddressMode, BorderColor */
    float lod_min;
} hp_sampler_desc;

typedef struct {
    void *pipeline, *depth_state;
    uint8_t cull, front_cw, lines;            /* MTLCullMode, front face clockwise, wireframe */
    float slope_bias;
    uint32_t stencil_ref;
    float blend_color[4];
    double viewport[6];                       /* x, y, width, height, znear, zfar */
    uint8_t scissor;
    uint32_t scissor_rect[4];
    void *vbuf[16];
    uint32_t voff[16];
    const void *vs_consts, *ps_consts;
    uint32_t vs_len, ps_len;
    void *tex[16], *smp[16];
    uint8_t prim;                             /* MTLPrimitiveType */
    uint32_t start, count;                    /* vertices, or indices when ibuf is set */
    void *ibuf;
    uint8_t index32;
    uint32_t index_offset;
    int32_t base_vertex;
} hp_draw_desc;

void *halopad_metal_pipeline(const hp_pipeline_desc *d, char *err, uint32_t errlen);
void *halopad_metal_depth_state(const hp_depth_desc *d);
void *halopad_metal_sampler(const hp_sampler_desc *d);
void *halopad_metal_buffer(const void *data, uint32_t length);
/* transient data that lives until the next Present: an unretained buffer and the offset in it */
void *halopad_metal_temp(const void *data, uint32_t length, uint32_t *offset);
void halopad_metal_release(void *o);
/* type: 2 = 2D, 3 = cube, 4 = 3D; format: MTLPixelFormat; swizzle: MTLTextureSwizzle x4 */
void *halopad_metal_texture(int type, uint32_t format, uint32_t w, uint32_t h, uint32_t d, uint32_t levels, const uint8_t swizzle[4]);
void halopad_metal_texture_upload(void *tex, uint32_t level, uint32_t slice, const void *data, uint32_t bytes_per_row,
                                  uint32_t bytes_per_image, uint32_t w, uint32_t h, uint32_t d);
void halopad_metal_draw(void *target, const hp_draw_desc *d);

/* Render targets. The back buffer and depth/stencil textures belong to the target (not
   retained for the caller). set_attachments selects what later clears and draws write:
   colour texture and mip level, and a depth/stencil texture or NULL. */
void *halopad_metal_target_back(void *target);
void *halopad_metal_target_depth(void *target);
void halopad_metal_set_attachments(void *target, void *color, uint32_t level, void *depth);
/* A 2D texture Metal can render into and sample (identity swizzle; see texture_view). */
void *halopad_metal_render_texture(uint32_t format, uint32_t w, uint32_t h, uint32_t levels);
/* A view of tex that samples with the given swizzle (MTLTextureSwizzle x4). */
void *halopad_metal_texture_view(void *tex, const uint8_t swizzle[4]);
/* Copy src level rect to dst level rect (x, y, w, h), scaling with point or linear filtering;
   opaque writes alpha 1. */
void halopad_metal_stretch(void *target, void *src, uint32_t slevel, const uint32_t srect[4], void *dst, uint32_t dlevel,
                           const uint32_t drect[4], int linear, int opaque);
/* Test support: read a region of a texture after all submitted work. */
void halopad_metal_read_texture(void *target, void *tex, uint32_t level, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                void *out, uint32_t bytes_per_row);

/* Occlusion counting (one query active at a time). begin/end bracket draws; end gives the
   counter slots used (first..last, one per render pass the query spanned, in ring order)
   and a generation that read uses to submit and wait for the frame if it is still being
   recorded. read returns the samples that passed depth and stencil. */
void halopad_metal_visibility_begin(void *target);
void halopad_metal_visibility_end(void *target, uint32_t *first, uint32_t *last, uint64_t *gen);
uint64_t halopad_metal_visibility_read(void *target, uint32_t first, uint32_t last, uint64_t gen);
#endif
