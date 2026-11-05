/* HaloPad Apple host, part 1 (G3/G4): the Metal core behind IDirect3DDevice9 (back buffer,
 * clear, present, readback for tests), shared by macOS and iOS. Host windows and events are
 * the platform hosts' (halopad_host_macos.m, halopad_host_ios.m).
 *
 * Each frame records into one command buffer; draws share a render encoder, which clears
 * end, and Present commits and waits.
 * Direct3D's back buffer is an offscreen BGRA8 texture of the size Halo asks for; the
 * depth/stencil buffer is Depth32Float_Stencil8. Present copies the back buffer into the
 * window's drawable through the window's gamma table. */
#import "halopad_host.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "halopad_metal.h"

typedef struct {
    struct hp_window *win;                   /* the host window it presents to */
    id<MTLTexture> back, depth;
    uint32_t width, height;
    id<MTLTexture> color, zs;                /* current attachments (Direct3D's render target and depth surface) */
    uint32_t level;                          /* mip level of color */
    id<MTLCommandBuffer> cb;                 /* the frame being recorded */
    id<MTLRenderCommandEncoder> enc;
    uint64_t gen;                            /* command buffers created so far (cb is number gen) */
    id<MTLBuffer> vis;                       /* occlusion counters, a ring of VIS_SLOTS */
    uint32_t vis_next, vis_first, vis_last;
    int vis_active, vis_used;                /* a query is counting; its current slot has an encoder */
} hp_target;
#define VIS_SLOTS 65536u

static id<MTLDevice> gpu;
static id<MTLCommandQueue> queue;
static void end_encoder(hp_target *t);
static id<MTLCommandBuffer> frame(hp_target *t);
static id<MTLCommandBuffer> in_flight;               /* TribesPad: the last presented frame, possibly still on the GPU */
static void warm_shaders(void);
static NSMutableDictionary *libraries;              /* compiled shader sources */
static NSLock *libraries_lock;                       /* pipelines build on other threads too */

static void ensure_app(void)
{
    static int done;
    if (done) return;
    done = 1;
    halopad_host_app();
    gpu = MTLCreateSystemDefaultDevice();
    queue = [gpu newCommandQueue];
    if (!gpu || !queue) { fprintf(stderr, "TribesPad trap: no Metal device\n"); abort(); }
    libraries_lock = [NSLock new]; libraries = [NSMutableDictionary new];
    warm_shaders();
}


static id<MTLRenderPipelineState> present_pipe;

static void present_texture(hp_window *win, id<MTLCommandBuffer> cb, id<MTLTexture> src)
{
    if (!present_pipe) {
        static const char msl[] =
            "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct V { float4 p [[position]]; };\n"
            "vertex V hp_vs(uint i [[vertex_id]]) { float2 k = float2((i << 1) & 2, i & 2); V o; o.p = float4(k * 2 - 1, 0, 1); return o; }\n"
            "fragment float4 hp_ps(V i [[stage_in]], texture2d<float> t [[texture(0)]], constant float *lut [[buffer(0)]]) {\n"
            "    float4 c = t.read(uint2(i.p.xy));\n"
            "    uint3 k = uint3(round(saturate(c.rgb) * 255.0));\n"
            "    return float4(lut[k.r], lut[256 + k.g], lut[512 + k.b], 1);\n"
            "}\n";
        NSError *e = nil;
        id<MTLLibrary> lib = [gpu newLibraryWithSource:[NSString stringWithUTF8String:msl] options:nil error:&e];
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"hp_vs"];
        pd.fragmentFunction = [lib newFunctionWithName:@"hp_ps"];
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        present_pipe = [gpu newRenderPipelineStateWithDescriptor:pd error:&e];
        if (!present_pipe) { fprintf(stderr, "TribesPad trap: present pipeline: %s\n", e.localizedDescription.UTF8String); abort(); }
    }
    static int traced = -1, n_off, n_nil, n_shown;
    if (traced < 0) traced = getenv("HALOPAD_TRACE_WINDOWS") != NULL;
    if (traced && (n_off + n_nil + n_shown) % 300 == 299)
        fprintf(stderr, "TribesPad present: %d off screen, %d without a drawable, %d shown (layer %.0fx%.0f, drawable %.0fx%.0f, src %lux%lu)\n",
                n_off, n_nil, n_shown, win->layer.bounds.size.width, win->layer.bounds.size.height,
                win->layer.drawableSize.width, win->layer.drawableSize.height, (unsigned long)src.width, (unsigned long)src.height);
    if (!win->on_screen) { n_off++; return; }                      /* nothing to show it on */
    if (win->layer.drawableSize.width != src.width || win->layer.drawableSize.height != src.height)
    {
        win->layer.drawableSize = CGSizeMake(src.width, src.height);
        [CATransaction flush];                                      /* committed from Halo's thread */
    }
    id<CAMetalDrawable> drawable = [win->layer nextDrawable];
    if (!drawable) { n_nil++; return; }
    n_shown++;
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = drawable.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
    [e setRenderPipelineState:present_pipe];
    [e setFragmentTexture:src atIndex:0];
    [e setFragmentBytes:win->lut length:sizeof win->lut atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
    [cb presentDrawable:drawable];
}

void halopad_host_window_unref(hp_window *w)
{
    if (--w->refs) return;
    free(w->gdi);
    w->gdi_tex = nil; w->layer = nil; w->window = nil;
    free(w);
}

void halopad_host_window_gamma(void *p, const uint16_t ramp[768])
{
    hp_window *w = p;
    for (int i = 0; i < 768; i++) w->lut[i] = ramp[i] / 65535.0f;
    if (getenv("HALOPAD_TRACE_WINDOWS"))
        fprintf(stderr, "TribesPad gamma: ramp r %u %u %u  g %u %u %u  b %u %u %u\n", ramp[0], ramp[128], ramp[255],
                ramp[256], ramp[384], ramp[511], ramp[512], ramp[640], ramp[767]);
}

/* GDI: copy (scaling, nearest) a region of a top-down BGRA image into the client area,
   clipped, and show it unless a Direct3D device owns the window's presentation. */
void halopad_host_window_blit(void *p, const uint32_t *src, uint32_t src_w, uint32_t src_h, int32_t dx, int32_t dy, int32_t dw, int32_t dh,
                              int32_t sx, int32_t sy, int32_t sw, int32_t sh)
{
    hp_window *w = p;
    if (!w->gdi) w->gdi = calloc((size_t)w->w * w->h, 4);
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for (int32_t y = 0; y < dh; y++) {
        int32_t ty = dy + y;
        if (ty < 0 || ty >= (int32_t)w->h) continue;
        int32_t fy = sy + (int32_t)(((int64_t)y * sh) / dh);
        if (fy < 0 || fy >= (int32_t)src_h) continue;
        for (int32_t x = 0; x < dw; x++) {
            int32_t tx = dx + x, fx = sx + (int32_t)(((int64_t)x * sw) / dw);
            if (tx < 0 || tx >= (int32_t)w->w || fx < 0 || fx >= (int32_t)src_w) continue;
            w->gdi[(size_t)ty * w->w + (size_t)tx] = src[(size_t)fy * src_w + (size_t)fx] | 0xFF000000u;
        }
    }
    if (w->has_target) return;                                      /* the device's next Present covers it */
    @autoreleasepool {
        if (!w->gdi_tex) {
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w->w height:w->h mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead;
            d.storageMode = MTLStorageModeShared;
            w->gdi_tex = [gpu newTextureWithDescriptor:d];
        }
        [w->gdi_tex replaceRegion:MTLRegionMake2D(0, 0, w->w, w->h) mipmapLevel:0 withBytes:w->gdi bytesPerRow:4 * w->w];
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        present_texture(w, cb, w->gdi_tex);
        [cb commit];
        [cb waitUntilCompleted];
    }
}

/* Test support: one pixel of the window's GDI surface, 0xAARRGGBB. */
uint32_t halopad_host_window_gdi_pixel(void *p, uint32_t x, uint32_t y)
{
    hp_window *w = p;
    return w->gdi && x < w->w && y < w->h ? w->gdi[(size_t)y * w->w + x] : 0;
}

/* ---- the Direct3D device's target: back buffer and depth, attached to a host window ---- */

void *halopad_metal_target_create(void *window, uint32_t width, uint32_t height, int depth_stencil)
{
    ensure_app();
    hp_target *t = calloc(1, sizeof *t);
    hp_window *w = window;
    w->refs++;
    w->has_target = 1;
    t->win = w;
    @autoreleasepool {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                     width:width height:height mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared;
        t->back = [gpu newTextureWithDescriptor:d];
        if (depth_stencil) {
            MTLTextureDescriptor *z = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                                         width:width height:height mipmapped:NO];
            z.usage = MTLTextureUsageRenderTarget;
            z.storageMode = MTLStorageModePrivate;
            t->depth = [gpu newTextureWithDescriptor:z];
        }
    }
    t->width = width;
    t->height = height;
    t->color = t->back;
    t->zs = t->depth;
    return t;
}

void halopad_metal_target_destroy(void *p)
{
    hp_target *t = p;
    end_encoder(t);
    if (t->cb) { [t->cb commit]; [t->cb waitUntilCompleted]; t->cb = nil; }
    t->win->has_target = 0;
    halopad_host_window_unref(t->win);
    t->back = nil; t->depth = nil; t->color = nil; t->zs = nil; t->vis = nil;
    free(t);
}

/* Clear the whole colour and/or depth/stencil attachment (Direct3D's D3DCLEAR_TARGET/ZBUFFER/STENCIL). */
void halopad_metal_clear(void *p, int color, int depth, int stencil, const float rgba[4], float z, uint32_t s)
{
    hp_target *t = p;
    @autoreleasepool {
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = t->color;
        rp.colorAttachments[0].level = t->level;
        rp.colorAttachments[0].loadAction = color ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(rgba[0], rgba[1], rgba[2], rgba[3]);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        if (t->zs) {
            rp.depthAttachment.texture = t->zs;
            rp.depthAttachment.loadAction = depth ? MTLLoadActionClear : MTLLoadActionLoad;
            rp.depthAttachment.clearDepth = z;
            rp.depthAttachment.storeAction = MTLStoreActionStore;
            rp.stencilAttachment.texture = t->zs;
            rp.stencilAttachment.loadAction = stencil ? MTLLoadActionClear : MTLLoadActionLoad;
            rp.stencilAttachment.clearStencil = s & 0xFF;
            rp.stencilAttachment.storeAction = MTLStoreActionStore;
        }
        end_encoder(t);
        [[frame(t) renderCommandEncoderWithDescriptor:rp] endEncoding];
    }
}

void halopad_host_window_gamma(void *p, const uint16_t ramp[768]);
void halopad_metal_target_gamma(void *p, const uint16_t ramp[768]) { halopad_host_window_gamma(((hp_target *)p)->win, ramp); }

/* Show the back buffer in the window (through its gamma table). */
/* HALOPAD_TRACE_FRAMES: shader and pipeline compilation in the current 10 s window */
static int trace_libs, trace_pipes;
static double trace_lib_s, trace_pipe_s;
int halopad_trace_decodes = 0;                        /* DXT decodes on upload (halopad_d3d9_draw.c) */
double halopad_trace_decode_s = 0, halopad_trace_decode_px = 0;
double halopad_trace_now(void) { return CFAbsoluteTimeGetCurrent(); }
static void arena_reset(void);                       /* the transient arena, below */
/* Running totals for the hitch log: a frame over 100 ms is reported with what it did. */
static double total_wait_s, total_upload_s, total_gpu_s, total_upload_bytes;
static int total_skipped, total_uploads, total_libs;
extern void tribes_trace_read_totals(uint32_t *reads, uint64_t *bytes, double *seconds);   /* ReadFile (port/core/guest_file.c) */
static void wait_in_flight(void)
{
    if (!in_flight) return;
    double t0 = CFAbsoluteTimeGetCurrent();
    [in_flight waitUntilCompleted];
    total_gpu_s += CFAbsoluteTimeGetCurrent() - t0;
    in_flight = nil;
}
static double wait_budget = 0.004;                   /* seconds left this frame for waiting on pipelines */
static int halopad_trace_skipped;                    /* this window: draws deferred */
/* TribesPad: a frame with a draw left out (its pipeline still building) is not shown:
   the screen keeps the last whole frame, for up to 100 ms, so a new object or effect
   no longer flickers in. The game runs on meanwhile; only that picture is dropped. */
static int frame_skipped;                            /* draws left out of the frame being built */
static double last_shown;                            /* when a frame last reached the screen */
static int trace_held;                               /* this window: frames held back */
static _Atomic int recorded_new;                             /* this window: pipelines recorded as missing from the list */

int halopad_metal_present(void *p)
{
    hp_target *t = p;
    /* HALOPAD_TRACE_FRAMES: every 10 s, the frames presented, the longest gap between two
       presents and how many gaps passed 100 ms (input waits for the next pump) */
    static int trace = -1;
    static double last, since, longest;
    static int frames, long_gaps;
    if (trace < 0) trace = getenv("HALOPAD_TRACE_FRAMES") != NULL;
    if (trace) {
        double now = CFAbsoluteTimeGetCurrent();
        uint32_t tribes_trace_reads; uint64_t tribes_trace_read_bytes; double tribes_trace_read_s;
        tribes_trace_read_totals(&tribes_trace_reads, &tribes_trace_read_bytes, &tribes_trace_read_s);
        /* "TribesPad hitch": a frame over 100 ms, and where its time went. "other" is the
           game's own work (its code, loading objects from packages already read). */
        static double s_wait, s_upload, s_gpu, s_bytes, s_read;
        static int s_skipped, s_uploads, s_libs, s_frame;
        static uint32_t s_reads;
        static uint64_t s_read_bytes;
        if (last) {
            double gap = now - last; frames++; if (gap > longest) longest = gap;
            if (gap > 0.1) {
                long_gaps++;
                double wait = total_wait_s - s_wait, upload = total_upload_s - s_upload, gpu = total_gpu_s - s_gpu, read = tribes_trace_read_s - s_read;
                fprintf(stderr, "TribesPad hitch: %.0f ms at frame %d: shader waits %.0f ms, %d shaders compiled, %d draws deferred; "
                        "%d texture uploads (%.1f MB, %.0f ms); %u file reads (%.1f MB, %.0f ms); GPU wait %.0f ms; other %.0f ms\n",
                        gap * 1000, s_frame, wait * 1000, total_libs - s_libs, total_skipped - s_skipped,
                        total_uploads - s_uploads, (total_upload_bytes - s_bytes) / 1e6, upload * 1000,
                        tribes_trace_reads - s_reads, (double)(tribes_trace_read_bytes - s_read_bytes) / 1e6, read * 1000, gpu * 1000,
                        (gap - wait - upload - read - gpu) * 1000);
            }
        }
        else since = now;
        last = now;
        s_wait = total_wait_s; s_upload = total_upload_s; s_gpu = total_gpu_s; s_bytes = total_upload_bytes; s_read = tribes_trace_read_s;
        s_skipped = total_skipped; s_uploads = total_uploads; s_libs = total_libs; s_reads = tribes_trace_reads; s_read_bytes = tribes_trace_read_bytes;
        s_frame++;
        if (now - since >= 10) {
            fprintf(stderr, "TribesPad frames: %d in %.1f s (%.1f/s), longest gap %.0f ms, %d gaps over 100 ms; %d shader libraries (%.2f s), %d pipelines (%.2f s), %d DXT decodes (%.2f s, %.1f Mpx); %d draws deferred, %d frames held, %d new pipelines recorded\n",
                    frames, now - since, frames / (now - since), longest * 1000, long_gaps, trace_libs, trace_lib_s, trace_pipes, trace_pipe_s,
                    halopad_trace_decodes, halopad_trace_decode_s, halopad_trace_decode_px / 1e6, halopad_trace_skipped, trace_held, recorded_new);
            since = now; frames = 0; longest = 0; long_gaps = 0; trace_libs = trace_pipes = 0; trace_lib_s = trace_pipe_s = 0;
            halopad_trace_decodes = 0; halopad_trace_decode_s = halopad_trace_decode_px = 0; halopad_trace_skipped = 0; trace_held = 0; recorded_new = 0;
        }
    }
    wait_budget = 0.004;
    double shown_at = CFAbsoluteTimeGetCurrent();
    int hold = frame_skipped && last_shown && shown_at - last_shown < 0.1;
    frame_skipped = 0;
    if (hold) trace_held++;
    else last_shown = shown_at;
    @autoreleasepool {
        end_encoder(t);
        id<MTLCommandBuffer> cb = frame(t);
        t->cb = nil;
        if (!hold) present_texture(t->win, cb, t->back);
        [cb commit];
        /* TribesPad: one frame in flight. The CPU builds the next frame while the GPU
           draws this one; wait for the previous frame instead, whose transient arena
           is the one the next frame reuses. Buffers are renamed on upload and texture
           uploads wait for the frame in flight (halopad_metal_texture_upload). */
        wait_in_flight();
        in_flight = cb;
        arena_reset();                                    /* the arena the previous frame used: the GPU is done with it */
    }
    halopad_host_pump();
    return 1;
}

/* Test support: one back-buffer pixel as 0xAARRGGBB, after all submitted work. */
uint32_t halopad_metal_read_pixel(void *p, uint32_t x, uint32_t y)
{
    hp_target *t = p;
    @autoreleasepool {
        end_encoder(t);
        id<MTLCommandBuffer> cb = t->cb ? t->cb : [queue commandBuffer];
        t->cb = nil;
        [cb commit];
        [cb waitUntilCompleted];
        uint8_t bgra[4];
        [t->back getBytes:bgra bytesPerRow:4 fromRegion:MTLRegionMake2D(x, y, 1, 1) mipmapLevel:0];
        return (uint32_t)bgra[3] << 24 | (uint32_t)bgra[2] << 16 | (uint32_t)bgra[1] << 8 | bgra[0];
    }
}

/* Test support: the whole back buffer as 0xAARRGGBB rows (top-down), after all submitted work. */
void halopad_metal_read_image(void *p, uint32_t *out, uint32_t w, uint32_t h)
{
    hp_target *t = p;
    @autoreleasepool {
        end_encoder(t);
        id<MTLCommandBuffer> cb = t->cb ? t->cb : [queue commandBuffer];
        t->cb = nil;
        [cb commit];
        [cb waitUntilCompleted];
        [t->back getBytes:out bytesPerRow:4 * w fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];   /* BGRA8: 0xAARRGGBB in memory order */
    }
}

/* ---- frame, pipelines, resources and draws ---- */

static id<MTLBuffer> constant_attr;                 /* float4(0, 0, 0, 1) for missing vertex inputs */

/* Pipeline, depth-stencil and sampler states by their descriptions: C hash tables, since
   every draw looks all three up. Keyed by NSData (with the shaders' whole source text),
   the lookups were about a sixth of the game's thread. A pipeline's key holds its shaders'
   source addresses: those strings are kept for the life of the process
   (halopad_d3d9_resources.c), so an address names one source. */
typedef struct hp_cache_entry { struct hp_cache_entry *next; uint64_t hash; void *object; uint32_t length; uint8_t key[]; } hp_cache_entry;
typedef struct { hp_cache_entry *bucket[4096]; } hp_cache;
static hp_cache pipelines, depth_states, samplers;
static uint64_t cache_hash(const void *key, uint32_t length)
{
    /* Eight bytes a step: the pipeline key (about 230 bytes) is hashed on every draw. */
    uint64_t h = 1469598103934665603ull;
    uint32_t i = 0;
    for (; i + 8 <= length; i += 8) {
        uint64_t w;
        memcpy(&w, (const uint8_t *)key + i, 8);
        h = (h ^ w) * 1099511628211ull;
        h ^= h >> 29;
    }
    for (; i < length; i++) h = (h ^ ((const uint8_t *)key)[i]) * 1099511628211ull;
    return h;
}
static void *cache_get(hp_cache *c, const void *key, uint32_t length, uint64_t *hash)
{
    *hash = cache_hash(key, length);
    for (hp_cache_entry *e = c->bucket[*hash & 4095]; e; e = e->next)
        if (e->hash == *hash && e->length == length && !memcmp(e->key, key, length)) return e->object;
    return NULL;
}
static void cache_put(hp_cache *c, const void *key, uint32_t length, uint64_t hash, id object)
{
    hp_cache_entry *e = malloc(sizeof *e + length);
    if (!e) abort();
    e->hash = hash; e->length = length; memcpy(e->key, key, length);
    e->object = (__bridge_retained void *)object;
    e->next = c->bucket[hash & 4095]; c->bucket[hash & 4095] = e;
}

/* warm: shader warm-up (warm_shaders below), which keeps no library and counts its own time. */
static id<MTLFunction> function(const char *src, NSString *name, char *err, uint32_t errlen, int warm)
{
    NSString *s = [NSString stringWithUTF8String:src];
    [libraries_lock lock];
    id<MTLLibrary> lib = libraries[s];
    [libraries_lock unlock];
    if (!lib) {
        NSError *e = nil;
        double t0 = CFAbsoluteTimeGetCurrent();
        lib = [gpu newLibraryWithSource:s options:nil error:&e];
        if (!lib) { snprintf(err, errlen, "%s", e.localizedDescription.UTF8String); return nil; }
        if (!warm) {
            [libraries_lock lock];
            trace_libs++; total_libs++; trace_lib_s += CFAbsoluteTimeGetCurrent() - t0;
            libraries[s] = lib;
            [libraries_lock unlock];
        }
    }
    return [lib newFunctionWithName:name];
}

static void record_pipeline(const hp_pipeline_desc *d);
static id<MTLRenderPipelineState> build_pipeline(const hp_pipeline_desc *d, char *err, uint32_t errlen, int warm)
{
    id<MTLRenderPipelineState> ps;
    @autoreleasepool {
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = function(d->vs_msl, @"hp_vs", err, errlen, warm);
        pd.fragmentFunction = function(d->ps_msl, @"hp_ps", err, errlen, warm);
        if (!pd.vertexFunction || !pd.fragmentFunction) return nil;
        MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
        for (uint32_t i = 0; i < d->nattr; i++) {
            vd.attributes[d->attr[i].reg].format = d->attr[i].format;
            vd.attributes[d->attr[i].reg].offset = d->attr[i].offset;
            vd.attributes[d->attr[i].reg].bufferIndex = d->attr[i].stream;
            vd.layouts[d->attr[i].stream].stride = d->stride[d->attr[i].stream];
        }
        for (int r = 0; r < 16; r++) {
            if (!(d->constant_regs >> r & 1)) continue;
            vd.attributes[r].format = MTLVertexFormatFloat4;
            vd.attributes[r].offset = 0;
            vd.attributes[r].bufferIndex = 18;
            vd.layouts[18].stride = 16;
            vd.layouts[18].stepFunction = MTLVertexStepFunctionConstant;
            vd.layouts[18].stepRate = 0;
        }
        pd.vertexDescriptor = vd;
        MTLRenderPipelineColorAttachmentDescriptor *ca = pd.colorAttachments[0];
        ca.pixelFormat = d->color_format ? d->color_format : MTLPixelFormatBGRA8Unorm;
        ca.blendingEnabled = d->blend;
        ca.sourceRGBBlendFactor = d->src_rgb; ca.destinationRGBBlendFactor = d->dst_rgb; ca.rgbBlendOperation = d->op_rgb;
        ca.sourceAlphaBlendFactor = d->src_a; ca.destinationAlphaBlendFactor = d->dst_a; ca.alphaBlendOperation = d->op_a;
        ca.writeMask = d->write_mask;
        if (d->depth) { pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8; pd.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8; }
        NSError *e = nil;
        double t0 = CFAbsoluteTimeGetCurrent();
        ps = [gpu newRenderPipelineStateWithDescriptor:pd error:&e];
        if (!warm) {
            [libraries_lock lock];
            trace_pipes++; trace_pipe_s += CFAbsoluteTimeGetCurrent() - t0;
            [libraries_lock unlock];
        }
        if (!ps) { snprintf(err, errlen, "%s", e.localizedDescription.UTF8String); return nil; }
        if (!warm) record_pipeline(d);
    }
    return ps;
}

/* TribesPad shader warm-up. iOS keeps compiled shaders between launches, but the first
   time a player meets a weapon, effect or map, its shaders compile while they play: on
   the iPad, 15 s of compiling in a mission's first 10 s, and objects drawn a frame or two
   late. VENGEANCE_SHADER_RECORD=<file> appends each pipeline the game builds (a test run
   on the Mac); scripts/vengeance/shader_warmup.py merges those runs into
   vengeance-shaders.txt, which the app bundles. At launch, three background threads
   build every pipeline in it once, so iOS has them all before play. A marker in Caches
   names the list done; a changed list (or VENGEANCE_SHADER_WARMUP=<file>) builds again.
   On iOS the app also records, during play, each pipeline the list does not hold, to
   Application Support/TribesPad/vengeance-new-shaders.txt, to merge into the list. */
#define DESC_TAIL (sizeof(hp_pipeline_desc) - offsetof(hp_pipeline_desc, nattr))
static uint64_t pipeline_key(const char *vs, size_t vs_len, const char *ps, size_t ps_len, const uint8_t *tail)
{
    uint64_t h = cache_hash(vs, (uint32_t)vs_len);
    h = (h ^ cache_hash(ps, (uint32_t)ps_len)) * 1099511628211ull;
    return (h ^ cache_hash(tail, (uint32_t)DESC_TAIL)) * 1099511628211ull;
}
static uint64_t *listed_keys;                        /* the bundled list's pipelines, sorted */
static uint32_t listed_count;
static _Atomic int listed_ready;
static int key_order(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}
static void record_pipeline(const hp_pipeline_desc *d)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static FILE *out;
    static int checked, only_new;
    pthread_mutex_lock(&lock);
    if (!checked) {
        checked = 1;
        const char *path = getenv("VENGEANCE_SHADER_RECORD");
        if (path && *path) out = fopen(path, "a");
#if TARGET_OS_IPHONE
        else {
            NSString *support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
            NSString *file = [support stringByAppendingPathComponent:@"TribesPad/vengeance-new-shaders.txt"];
            out = fopen(file.fileSystemRepresentation, "a");
            only_new = 1;
            if (out) fprintf(stderr, "TribesPad shaders: recording pipelines missing from the list to %s\n", file.fileSystemRepresentation);
        }
#endif
    }
    size_t vs = strlen(d->vs_msl), ps = strlen(d->ps_msl);
    const uint8_t *tail = (const uint8_t *)&d->nattr;
    int listed = 0;
    if (out && only_new && listed_ready) {
        uint64_t key = pipeline_key(d->vs_msl, vs, d->ps_msl, ps, tail);
        listed = bsearch(&key, listed_keys, listed_count, sizeof key, key_order) != NULL;
    }
    if (out && !listed) {
        fprintf(out, "R %zu %zu ", vs, ps);
        for (size_t i = 0; i < DESC_TAIL; i++) fprintf(out, "%02x", tail[i]);
        fputc('\n', out);
        fwrite(d->vs_msl, 1, vs, out); fwrite(d->ps_msl, 1, ps, out);
        fputc('\n', out);
        fflush(out);
        if (only_new) recorded_new++;
    }
    pthread_mutex_unlock(&lock);
}

static void warm_shaders(void)
{
    const char *forced = getenv("VENGEANCE_SHADER_WARMUP");
    NSString *path = forced ? [NSString stringWithUTF8String:forced] : [[NSBundle mainBundle] pathForResource:@"vengeance-shaders" ofType:@"txt" inDirectory:@"GameData/Vengeance"];
    if (!path) return;
    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_UTILITY, 0);
    dispatch_async(q, ^{
        NSData *data = [NSData dataWithContentsOfFile:path];
        if (!data.length) return;
        uint64_t h = cache_hash(data.bytes, (uint32_t)data.length);
        NSString *caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
        NSString *marker = [caches stringByAppendingPathComponent:[NSString stringWithFormat:@"TribesPad/shaders-%016llx.done", (unsigned long long)h]];
        int build = forced || ![[NSFileManager defaultManager] fileExistsAtPath:marker];

        /* "TribesPad shaders 1", then "S <length>" lines each followed by a source, and
           "P <vertex source> <pixel source> <descriptor hex>" lines. */
        char *text = malloc(data.length + 1);
        if (!text) return;
        memcpy(text, data.bytes, data.length); text[data.length] = 0;
        NSMutableData *sources = [NSMutableData new], *lengths = [NSMutableData new], *pipes = [NSMutableData new], *keys = [NSMutableData new];
        char *p = text, *end = text + data.length;
        int ok = data.length > 20 && !strncmp(p, "TribesPad shaders 1\n", 20);
        p += 20;
        while (ok && p < end) {
            char *line = p, *nl = memchr(p, '\n', (size_t)(end - p));
            if (!nl) { ok = 0; break; }
            *nl = 0; p = nl + 1;
            if (line[0] == 'S') {
                unsigned long n = strtoul(line + 2, NULL, 10);
                if (n >= (unsigned long)(end - p)) { ok = 0; break; }
                const char *src = p;
                [sources appendBytes:&src length:sizeof src];
                [lengths appendBytes:&n length:sizeof n];
                p[n] = 0; p += n + 1;
            } else if (line[0] == 'P') {
                char *r;
                unsigned long vs = strtoul(line + 2, &r, 10), ps = strtoul(r, &r, 10);
                unsigned long count = sources.length / sizeof(char *);
                if (vs >= count || ps >= count || strlen(r) != 1 + 2 * DESC_TAIL) { ok = 0; break; }
                hp_pipeline_desc d = {0};
                d.vs_msl = ((const char **)sources.bytes)[vs]; d.ps_msl = ((const char **)sources.bytes)[ps];
                uint8_t *tail = (uint8_t *)&d.nattr;
                for (size_t i = 0; i < DESC_TAIL; i++) { unsigned v = 0; sscanf(r + 1 + 2 * i, "%2x", &v); tail[i] = (uint8_t)v; }
                [pipes appendBytes:&d length:sizeof d];
                const unsigned long *len = lengths.bytes;
                uint64_t key = pipeline_key(d.vs_msl, len[vs], d.ps_msl, len[ps], tail);
                [keys appendBytes:&key length:sizeof key];
            } else ok = 0;
        }
        uint32_t n = (uint32_t)(pipes.length / sizeof(hp_pipeline_desc));
        if (!ok || !n) { fprintf(stderr, "TribesPad shaders: %s is not a shader list for this build; not warming\n", path.UTF8String); free(text); return; }
        uint64_t *sorted = malloc(keys.length);
        if (sorted) {
            memcpy(sorted, keys.bytes, keys.length);
            qsort(sorted, n, sizeof *sorted, key_order);
            listed_keys = sorted; listed_count = n; listed_ready = 1;
        }
        if (!build) { free(text); return; }
        fprintf(stderr, "TribesPad shaders: building %u pipelines (%lu shaders) in the background\n", n, (unsigned long)(sources.length / sizeof(char *)));
        const hp_pipeline_desc *list = pipes.bytes;
        dispatch_group_t group = dispatch_group_create();
        static _Atomic uint32_t failed;
        double t0 = CFAbsoluteTimeGetCurrent();
        enum { WORKERS = 3 };
        for (uint32_t w = 0; w < WORKERS; w++)
            dispatch_group_async(group, q, ^{
                for (uint32_t i = w; i < n; i += WORKERS) {
                    @autoreleasepool {
                        char err[512];
                        if (!build_pipeline(&list[i], err, sizeof err, 1)) failed++;
                    }
                }
            });
        dispatch_group_notify(group, q, ^{
            fprintf(stderr, "TribesPad shaders: built %u pipelines in %.1f s (%u failed)\n", n, CFAbsoluteTimeGetCurrent() - t0, (unsigned)failed);
            if (!forced) {
                [[NSFileManager defaultManager] createDirectoryAtPath:[marker stringByDeletingLastPathComponent] withIntermediateDirectories:YES attributes:nil error:nil];
                [[NSData data] writeToFile:marker atomically:YES];
            }
            (void)sources; (void)pipes;    /* the list and its sources live until every pipeline is built */
            free(text);
        });
    });
}

/* TribesPad: a new shader used to compile on the game's thread the first time it
   was drawn, 20 to 40 ms each (iOS keeps compiled shaders between launches, so this
   happens once per device): a new weapon, effect or map froze the game while dozens
   compiled one after another. Pipelines now build on other threads, several at once.
   A draw waits for its pipeline within a budget of a few milliseconds a frame (enough
   for one iOS already has); past that, a draw to the back buffer is skipped until its
   pipeline is ready, a frame or two later. A draw into a texture always waits, since
   the game may draw some of those only once. */
@interface HPPendingPipeline : NSObject {
@public
    dispatch_semaphore_t done;
    id<MTLRenderPipelineState> pipeline;
    char err[512];
}
@end
@implementation HPPendingPipeline
@end
static hp_cache pending_pipelines;

void *halopad_metal_pipeline_async(const hp_pipeline_desc *d, int may_skip, int *skipped, char *err, uint32_t errlen)
{
    ensure_app();
    *skipped = 0;
    uint64_t hash, pending_hash;
    void *found = cache_get(&pipelines, d, sizeof *d, &hash);
    if (found) return found;
    HPPendingPipeline *p = (__bridge HPPendingPipeline *)cache_get(&pending_pipelines, d, sizeof *d, &pending_hash);
    if (!p) {
        p = [HPPendingPipeline new];
        p->done = dispatch_semaphore_create(0);
        hp_pipeline_desc copy = *d;                   /* shader sources live for the process */
        HPPendingPipeline *q = p;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
            q->pipeline = build_pipeline(&copy, q->err, sizeof q->err, 0);
            dispatch_semaphore_signal(q->done);
        });
        cache_put(&pending_pipelines, d, sizeof *d, pending_hash, p);
    }
    double t0 = CFAbsoluteTimeGetCurrent();
    double budget = may_skip ? (wait_budget > 0 ? wait_budget : 0) : -1;
    long waited = dispatch_semaphore_wait(p->done, budget < 0 ? DISPATCH_TIME_FOREVER : dispatch_time(DISPATCH_TIME_NOW, (int64_t)(budget * 1e9)));
    double spent = CFAbsoluteTimeGetCurrent() - t0;
    total_wait_s += spent;
    if (may_skip) wait_budget -= spent;
    if (waited) { *skipped = 1; halopad_trace_skipped++; total_skipped++; frame_skipped++; return NULL; }
    dispatch_semaphore_signal(p->done);               /* stays signalled for any later look */
    if (!p->pipeline) { snprintf(err, errlen, "%s", p->err); return NULL; }
    cache_put(&pipelines, d, sizeof *d, hash, p->pipeline);
    return (__bridge void *)p->pipeline;
}

void *halopad_metal_pipeline(const hp_pipeline_desc *d, char *err, uint32_t errlen)
{
    int skipped;
    return halopad_metal_pipeline_async(d, 0, &skipped, err, errlen);
}

void *halopad_metal_depth_state(const hp_depth_desc *d)
{
    ensure_app();
    uint64_t hash;
    void *found = cache_get(&depth_states, d, sizeof *d, &hash);
    if (found) return found;
    MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = d->depth_func;
    dd.depthWriteEnabled = d->depth_write;
    if (d->stencil) {
        MTLStencilDescriptor *sd[2] = {[MTLStencilDescriptor new], [MTLStencilDescriptor new]};
        for (int i = 0; i < 2; i++) {
            sd[i].stencilFailureOperation = d->fail[i]; sd[i].depthFailureOperation = d->zfail[i];
            sd[i].depthStencilPassOperation = d->pass[i]; sd[i].stencilCompareFunction = d->func[i];
            sd[i].readMask = d->read_mask & 0xFF; sd[i].writeMask = d->write_mask & 0xFF;
        }
        dd.frontFaceStencil = sd[0];
        dd.backFaceStencil = sd[1];
    }
    id<MTLDepthStencilState> s = [gpu newDepthStencilStateWithDescriptor:dd];
    cache_put(&depth_states, d, sizeof *d, hash, s);
    return (__bridge void *)s;
}

void *halopad_metal_sampler(const hp_sampler_desc *d)
{
    ensure_app();
    uint64_t hash;
    void *found = cache_get(&samplers, d, sizeof *d, &hash);
    if (found) return found;
    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = d->min; sd.magFilter = d->mag; sd.mipFilter = d->mip;
    sd.sAddressMode = d->u; sd.tAddressMode = d->v; sd.rAddressMode = d->w;
    sd.borderColor = d->border;
    sd.maxAnisotropy = d->anisotropy ? d->anisotropy : 1;
    sd.lodMinClamp = d->lod_min;
    id<MTLSamplerState> s = [gpu newSamplerStateWithDescriptor:sd];
    cache_put(&samplers, d, sizeof *d, hash, s);
    return (__bridge void *)s;
}

/* Transient data for one frame: shader constants and the vertex and index data Direct3D's
   DrawPrimitiveUP passes by pointer. Bump-allocated from 4 MB shared buffers, two sets used in
   turn, each reused once the GPU has finished the frame that used it; one Metal buffer per draw was three to five driver
   round trips on the Simulator, and a rules screen ran at 5 frames a second. */
#define ARENA_CHUNK (4u << 20)
static NSMutableArray<id<MTLBuffer>> *arenas[2], *arenas_big[2];   /* two sets: one per frame in flight */
static int arena_set;
#define arena arenas[arena_set]
#define arena_big arenas_big[arena_set]
static uint32_t arena_i, arena_used;

static id<MTLBuffer> arena_alloc(const void *data, uint32_t len, uint32_t *offset)
{
    if (!arenas[0]) for (int i = 0; i < 2; i++) { arenas[i] = [NSMutableArray new]; arenas_big[i] = [NSMutableArray new]; }
    uint32_t need = (len + 255) & ~255u;
    if (!need) need = 256;
    if (need > ARENA_CHUNK) {                             /* larger than a chunk: its own buffer, dropped at the reset */
        id<MTLBuffer> b = [gpu newBufferWithBytes:data length:len options:MTLResourceStorageModeShared];
        [arena_big addObject:b];
        *offset = 0;
        return b;
    }
    if (arena_i < arena.count && arena_used + need > ARENA_CHUNK) { arena_i++; arena_used = 0; }
    while (arena_i >= arena.count) [arena addObject:[gpu newBufferWithLength:ARENA_CHUNK options:MTLResourceStorageModeShared]];
    id<MTLBuffer> b = arena[arena_i];
    if (len) memcpy((uint8_t *)b.contents + arena_used, data, len);
    *offset = arena_used;
    arena_used += need;
    return b;
}
static uint32_t arena_frame;
static void arena_reset(void) { arena_set ^= 1; arena_i = 0; arena_used = 0; [arena_big removeAllObjects]; arena_frame++; }
uint32_t halopad_metal_frame(void) { return arena_frame; }

/* For the runtime: data that lives until the next Present, as an unretained buffer and offset */
void *halopad_metal_temp(const void *data, uint32_t length, uint32_t *offset)
{
    ensure_app();
    return (__bridge void *)arena_alloc(data, length, offset);
}

void *halopad_metal_buffer(const void *data, uint32_t length)
{
    ensure_app();
    id<MTLBuffer> b = [gpu newBufferWithBytes:data length:length ? length : 4 options:MTLResourceStorageModeShared];
    return (__bridge_retained void *)b;
}

void halopad_metal_release(void *o) { if (o) CFRelease(o); }

/* BC (DXT) textures: Apple silicon Macs and M-series iPads; not the iPad Simulator or earlier
   iPads, where Direct3D's DXT textures are decoded on upload (halopad_d3d9_draw.c).
   HALOPAD_NO_BC=1 forces the decoding path (tests compare it with the GPU's). */
int halopad_metal_supports_bc(void)
{
    static int v = -1;
    if (v < 0) {
        ensure_app();
        const char *e = getenv("HALOPAD_NO_BC");
        v = (e && *e == '1') ? 0 : (int)gpu.supportsBCTextureCompression;
    }
    return v;
}

void *halopad_metal_texture(int type, uint32_t format, uint32_t w, uint32_t h, uint32_t d, uint32_t levels, const uint8_t swizzle[4])
{
    ensure_app();
    MTLTextureDescriptor *td = [MTLTextureDescriptor new];
    td.textureType = type == 3 ? MTLTextureTypeCube : type == 4 ? MTLTextureType3D : MTLTextureType2D;
    td.pixelFormat = format;
    td.width = w; td.height = h; td.depth = type == 4 ? d : 1;
    td.mipmapLevelCount = levels;
    td.storageMode = MTLStorageModeShared;
    td.usage = MTLTextureUsageShaderRead;
    td.swizzle = MTLTextureSwizzleChannelsMake(swizzle[0], swizzle[1], swizzle[2], swizzle[3]);
    id<MTLTexture> t = [gpu newTextureWithDescriptor:td];
    return (__bridge_retained void *)t;
}

void halopad_metal_texture_upload(void *tex, uint32_t level, uint32_t slice, const void *data, uint32_t bytes_per_row,
                                  uint32_t bytes_per_image, uint32_t w, uint32_t h, uint32_t d)
{
    id<MTLTexture> t = (__bridge id<MTLTexture>)tex;
    wait_in_flight();                                 /* the frame in flight may still sample it */
    double t0 = CFAbsoluteTimeGetCurrent();
    [t replaceRegion:MTLRegionMake3D(0, 0, 0, w, h, d) mipmapLevel:level slice:slice withBytes:data
         bytesPerRow:bytes_per_row bytesPerImage:bytes_per_image];
    total_upload_s += CFAbsoluteTimeGetCurrent() - t0; total_uploads++;
    total_upload_bytes += (double)(bytes_per_image ? bytes_per_image : bytes_per_row * h) * d;
}

static void end_encoder(hp_target *t)
{
    if (t->enc) { [t->enc endEncoding]; t->enc = nil; }
}

static id<MTLCommandBuffer> frame(hp_target *t)
{
    if (!t->cb) { t->cb = [queue commandBuffer]; t->gen++; }
    return t->cb;
}

void halopad_metal_draw(void *target, const hp_draw_desc *d)
{
    hp_target *t = target;
    if (!t->enc) {
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = t->color;
        rp.colorAttachments[0].level = t->level;
        rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        if (t->zs) {
            rp.depthAttachment.texture = t->zs; rp.depthAttachment.loadAction = MTLLoadActionLoad;
            rp.depthAttachment.storeAction = MTLStoreActionStore;
            rp.stencilAttachment.texture = t->zs; rp.stencilAttachment.loadAction = MTLLoadActionLoad;
            rp.stencilAttachment.storeAction = MTLStoreActionStore;
        }
        if (!t->vis) t->vis = [gpu newBufferWithLength:8 * VIS_SLOTS options:MTLResourceStorageModeShared];
        rp.visibilityResultBuffer = t->vis;
        t->enc = [frame(t) renderCommandEncoderWithDescriptor:rp];
        /* TribesPad: depth clamped, not clipped, at the near and far planes. Tribes:
           Vengeance's first-person weapons reach through the near plane, and Metal cut
           those parts off: the Blaster, Spinfusor and grenade launcher looked torn.
           HALOPAD_DEPTH_CLIP=1 keeps clipping (to compare). */
        { static int clip = -1; if (clip < 0) clip = getenv("HALOPAD_DEPTH_CLIP") != NULL;
          if (!clip) [t->enc setDepthClipMode:MTLDepthClipModeClamp]; }
        if (t->vis_active) {                                /* a query spanning passes: a new counter per pass */
            if (t->vis_used) { t->vis_last = t->vis_next++ % VIS_SLOTS; ((uint64_t *)t->vis.contents)[t->vis_last] = 0; }
            t->vis_used = 1;
            [t->enc setVisibilityResultMode:MTLVisibilityResultModeCounting offset:8 * t->vis_last];
        }
    }
    id<MTLRenderCommandEncoder> e = t->enc;
    [e setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)d->pipeline];
    if (d->depth_state) [e setDepthStencilState:(__bridge id<MTLDepthStencilState>)d->depth_state];
    [e setCullMode:d->cull];
    [e setFrontFacingWinding:d->front_cw ? MTLWindingClockwise : MTLWindingCounterClockwise];
    [e setTriangleFillMode:d->lines ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
    [e setDepthBias:0 slopeScale:d->slope_bias clamp:0];
    [e setStencilReferenceValue:d->stencil_ref];
    [e setBlendColorRed:d->blend_color[0] green:d->blend_color[1] blue:d->blend_color[2] alpha:d->blend_color[3]];
    [e setViewport:(MTLViewport){d->viewport[0], d->viewport[1], d->viewport[2], d->viewport[3], d->viewport[4], d->viewport[5]}];
    MTLScissorRect sc = d->scissor ? (MTLScissorRect){d->scissor_rect[0], d->scissor_rect[1], d->scissor_rect[2], d->scissor_rect[3]}
                                   : (MTLScissorRect){0, 0, MAX(t->color.width >> t->level, 1u), MAX(t->color.height >> t->level, 1u)};
    [e setScissorRect:sc];
    for (int s = 0; s < 16; s++) if (d->vbuf[s]) [e setVertexBuffer:(__bridge id<MTLBuffer>)d->vbuf[s] offset:d->voff[s] atIndex:s];
    if (!constant_attr) { float v[4] = {0, 0, 0, 1}; constant_attr = [gpu newBufferWithBytes:v length:16 options:MTLResourceStorageModeShared]; }
    [e setVertexBuffer:constant_attr offset:0 atIndex:18];
    /* constant blocks exceed setVertexBytes' 4 KB guideline: transient arena space per draw */
    uint32_t vo, po;
    id<MTLBuffer> vb = arena_alloc(d->vs_consts, d->vs_len, &vo), pb = arena_alloc(d->ps_consts, d->ps_len, &po);
    [e setVertexBuffer:vb offset:vo atIndex:16];
    [e setFragmentBuffer:pb offset:po atIndex:0];
    for (int s = 0; s < 16; s++) {
        if (d->tex[s]) [e setFragmentTexture:(__bridge id<MTLTexture>)d->tex[s] atIndex:s];
        if (d->smp[s]) [e setFragmentSamplerState:(__bridge id<MTLSamplerState>)d->smp[s] atIndex:s];
    }
    if (d->ibuf)
        [e drawIndexedPrimitives:d->prim indexCount:d->count indexType:d->index32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16
                     indexBuffer:(__bridge id<MTLBuffer>)d->ibuf indexBufferOffset:d->index_offset instanceCount:1
                      baseVertex:d->base_vertex baseInstance:0];
    else
        [e drawPrimitives:d->prim vertexStart:d->start vertexCount:d->count];
}

/* ---- render targets ---- */

void *halopad_metal_target_back(void *p) { return (__bridge void *)((hp_target *)p)->back; }
void *halopad_metal_target_depth(void *p) { return (__bridge void *)((hp_target *)p)->depth; }

void halopad_metal_set_attachments(void *p, void *color, uint32_t level, void *depth)
{
    hp_target *t = p;
    id<MTLTexture> c = (__bridge id<MTLTexture>)color, z = (__bridge id<MTLTexture>)depth;
    if (c == t->color && level == t->level && z == t->zs) return;
    end_encoder(t);
    t->color = c; t->level = level; t->zs = z;
}

void *halopad_metal_render_texture(uint32_t format, uint32_t w, uint32_t h, uint32_t levels)
{
    ensure_app();
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
    td.mipmapLevelCount = levels;
    td.storageMode = MTLStorageModeShared;
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    return (__bridge_retained void *)[gpu newTextureWithDescriptor:td];
}

/* A depth/stencil texture of its own (IDirect3DDevice9::CreateDepthStencilSurface), as the target's. */
void *halopad_metal_depth_texture(uint32_t w, uint32_t h)
{
    ensure_app();
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    return (__bridge_retained void *)[gpu newTextureWithDescriptor:td];
}

void *halopad_metal_texture_view(void *tex, const uint8_t sw[4])
{
    id<MTLTexture> t = (__bridge id<MTLTexture>)tex;
    id<MTLTexture> v = [t newTextureViewWithPixelFormat:t.pixelFormat textureType:t.textureType levels:NSMakeRange(0, t.mipmapLevelCount)
                                                 slices:NSMakeRange(0, 1) swizzle:MTLTextureSwizzleChannelsMake(sw[0], sw[1], sw[2], sw[3])];
    return (__bridge_retained void *)v;
}

static const char stretch_msl[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct V { float4 p [[position]]; float2 uv; };\n"
    "struct P { float4 r; float lod; };\n"
    "vertex V hp_vs(uint i [[vertex_id]], constant P &c [[buffer(0)]]) {\n"
    "    float2 k = float2(i & 1, i >> 1);\n"
    "    V o; o.p = float4(k.x * 2 - 1, 1 - k.y * 2, 0, 1); o.uv = mix(c.r.xy, c.r.zw, k); return o;\n"
    "}\n"
    "fragment float4 hp_ps(V i [[stage_in]], constant P &c [[buffer(0)]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {\n"
    "    return t.sample(s, i.uv, level(c.lod));\n"
    "}\n"
    "fragment float4 hp_ps_opaque(V i [[stage_in]], constant P &c [[buffer(0)]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {\n"
    "    return float4(t.sample(s, i.uv, level(c.lod)).rgb, 1);\n"
    "}\n";

void halopad_metal_stretch(void *p, void *src, uint32_t slevel, const uint32_t sr[4], void *dst, uint32_t dlevel,
                           const uint32_t dr[4], int linear, int opaque)
{
    hp_target *t = p;
    id<MTLTexture> s = (__bridge id<MTLTexture>)src, d = (__bridge id<MTLTexture>)dst;
    static NSMutableDictionary *stretch_pipes;
    static id<MTLSamplerState> smp[2];
    @autoreleasepool {
        end_encoder(t);
        id<MTLCommandBuffer> cb = frame(t);
        if (!opaque && sr[2] == dr[2] && sr[3] == dr[3] && s.pixelFormat == d.pixelFormat) {
            id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
            [b copyFromTexture:s sourceSlice:0 sourceLevel:slevel sourceOrigin:MTLOriginMake(sr[0], sr[1], 0)
                    sourceSize:MTLSizeMake(sr[2], sr[3], 1) toTexture:d destinationSlice:0 destinationLevel:dlevel
             destinationOrigin:MTLOriginMake(dr[0], dr[1], 0)];
            [b endEncoding];
            return;
        }
        if (!stretch_pipes) {
            stretch_pipes = [NSMutableDictionary new];
            for (int i = 0; i < 2; i++) {
                MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
                sd.minFilter = sd.magFilter = i ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
                sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
                smp[i] = [gpu newSamplerStateWithDescriptor:sd];
            }
        }
        NSNumber *key = @(d.pixelFormat | (opaque ? 0x10000u : 0u));
        id<MTLRenderPipelineState> ps = stretch_pipes[key];
        if (!ps) {
            char err[512];
            MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = function(stretch_msl, @"hp_vs", err, sizeof err, 0);
            pd.fragmentFunction = function(stretch_msl, opaque ? @"hp_ps_opaque" : @"hp_ps", err, sizeof err, 0);
            pd.colorAttachments[0].pixelFormat = d.pixelFormat;
            NSError *e = nil;
            ps = [gpu newRenderPipelineStateWithDescriptor:pd error:&e];
            if (!ps) { fprintf(stderr, "TribesPad trap: stretch pipeline: %s\n", e.localizedDescription.UTF8String); abort(); }
            stretch_pipes[key] = ps;
        }
        float sw = (float)MAX(s.width >> slevel, 1u), sh = (float)MAX(s.height >> slevel, 1u);
        struct { float r[4]; float lod; float pad[3]; } c = {{sr[0] / sw, sr[1] / sh, (sr[0] + sr[2]) / sw, (sr[1] + sr[3]) / sh}, (float)slevel, {0}};
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = d;
        rp.colorAttachments[0].level = dlevel;
        rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
        [e setRenderPipelineState:ps];
        [e setViewport:(MTLViewport){dr[0], dr[1], dr[2], dr[3], 0, 1}];
        [e setVertexBytes:&c length:sizeof c atIndex:0];
        [e setFragmentBytes:&c length:sizeof c atIndex:0];
        [e setFragmentTexture:s atIndex:0];
        [e setFragmentSamplerState:smp[linear ? 1 : 0] atIndex:0];
        [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [e endEncoding];
    }
}

void halopad_metal_read_texture(void *p, void *tex, uint32_t level, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                void *out, uint32_t bytes_per_row)
{
    hp_target *t = p;
    @autoreleasepool {
        end_encoder(t);
        id<MTLCommandBuffer> cb = t->cb ? t->cb : [queue commandBuffer];
        t->cb = nil;
        [cb commit];
        [cb waitUntilCompleted];
        [(__bridge id<MTLTexture>)tex getBytes:out bytesPerRow:bytes_per_row fromRegion:MTLRegionMake2D(x, y, w, h) mipmapLevel:level];
    }
}

/* ---- occlusion queries ---- */

void halopad_metal_visibility_begin(void *p)
{
    hp_target *t = p;
    if (t->vis_active) { fprintf(stderr, "TribesPad trap: occlusion query: a second query begun while one is counting\n"); abort(); }
    if (!t->vis) t->vis = [gpu newBufferWithLength:8 * VIS_SLOTS options:MTLResourceStorageModeShared];
    t->vis_first = t->vis_last = t->vis_next++ % VIS_SLOTS;
    ((uint64_t *)t->vis.contents)[t->vis_last] = 0;
    t->vis_active = 1;
    t->vis_used = t->enc != nil;
    if (t->enc) [t->enc setVisibilityResultMode:MTLVisibilityResultModeCounting offset:8 * t->vis_last];
}

void halopad_metal_visibility_end(void *p, uint32_t *first, uint32_t *last, uint64_t *gen)
{
    hp_target *t = p;
    if (t->enc) [t->enc setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
    t->vis_active = 0;
    *first = t->vis_first; *last = t->vis_last;
    *gen = t->cb ? t->gen : 0;                              /* 0: nothing of it is still being recorded */
}

uint64_t halopad_metal_visibility_read(void *p, uint32_t first, uint32_t last, uint64_t gen)
{
    hp_target *t = p;
    if (gen && t->cb && t->gen == gen) {                    /* the frame is still being recorded: submit it and wait */
        @autoreleasepool {
            end_encoder(t);
            id<MTLCommandBuffer> cb = t->cb;
            t->cb = nil;
            [cb commit];
            [cb waitUntilCompleted];
        }
    }
    uint64_t n = 0;
    const uint64_t *v = t->vis.contents;
    for (uint32_t s = first;; s = (s + 1) % VIS_SLOTS) { n += v[s]; if (s == last) break; }
    return n;
}

id<MTLDevice> halopad_metal_gpu(void) { ensure_app(); return gpu; }


/* Tribes: Vengeance: a clear of part of the targets (Direct3D's viewport or rectangles, or a
   depth clear when the depth buffer is larger than the colour target): a triangle over the
   rectangle that writes the chosen planes, in the current pass, which then ends so the next
   draw starts with its own state. rect: x, y, w, h. */
static NSMutableDictionary *clear_pipes;
static id<MTLDepthStencilState> clear_depth[4];
void halopad_metal_clear_area(void *p, const uint32_t rect[4], int color, int depth, int stencil, const float rgba[4], float z, uint32_t s)
{
    hp_target *t = p;
    @autoreleasepool {
        if (!clear_pipes) clear_pipes = [NSMutableDictionary new];
        NSString *key = [NSString stringWithFormat:@"%lu/%d/%d", (unsigned long)t->color.pixelFormat, t->zs != nil, color];
        id<MTLRenderPipelineState> pipe = clear_pipes[key];
        if (!pipe) {
            static id<MTLLibrary> lib;
            NSError *err = nil;
            if (!lib) lib = [gpu newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nstruct V { float4 p [[position]]; };\n"
                                                      "vertex V hp_clear_vs(uint i [[vertex_id]], constant float &z [[buffer(0)]]) {\n"
                                                      "  float2 k = float2((i << 1) & 2, i & 2); V o; o.p = float4(k * 2 - 1, z, 1); return o; }\n"
                                                      "fragment float4 hp_clear_ps(V i [[stage_in]], constant float4 &c [[buffer(0)]]) { return c; }\n"
                                              options:nil error:&err];
            MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = [lib newFunctionWithName:@"hp_clear_vs"];
            pd.fragmentFunction = [lib newFunctionWithName:@"hp_clear_ps"];
            pd.colorAttachments[0].pixelFormat = t->color.pixelFormat;
            pd.colorAttachments[0].writeMask = color ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
            if (t->zs) { pd.depthAttachmentPixelFormat = t->zs.pixelFormat; pd.stencilAttachmentPixelFormat = t->zs.pixelFormat; }
            pipe = [gpu newRenderPipelineStateWithDescriptor:pd error:&err];
            if (!pipe) { fprintf(stderr, "TribesPad trap: clear pipeline: %s\n", err.localizedDescription.UTF8String); abort(); }
            clear_pipes[key] = pipe;
        }
        int which = (depth ? 1 : 0) | (stencil ? 2 : 0);
        if (!clear_depth[which]) {
            MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
            dd.depthCompareFunction = MTLCompareFunctionAlways;
            dd.depthWriteEnabled = depth != 0;
            if (stencil) {
                MTLStencilDescriptor *sd = [MTLStencilDescriptor new];
                sd.stencilCompareFunction = MTLCompareFunctionAlways;
                sd.depthStencilPassOperation = MTLStencilOperationReplace;
                sd.readMask = 0xFF; sd.writeMask = 0xFF;
                dd.frontFaceStencil = sd; dd.backFaceStencil = sd;
            }
            clear_depth[which] = [gpu newDepthStencilStateWithDescriptor:dd];
        }
        if (!t->enc) {
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = t->color;
            rp.colorAttachments[0].level = t->level;
            rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            if (t->zs) {
                rp.depthAttachment.texture = t->zs; rp.depthAttachment.loadAction = MTLLoadActionLoad;
                rp.depthAttachment.storeAction = MTLStoreActionStore;
                rp.stencilAttachment.texture = t->zs; rp.stencilAttachment.loadAction = MTLLoadActionLoad;
                rp.stencilAttachment.storeAction = MTLStoreActionStore;
            }
            t->enc = [frame(t) renderCommandEncoderWithDescriptor:rp];
        }
        id<MTLRenderCommandEncoder> e = t->enc;
        [e setRenderPipelineState:pipe];
        [e setDepthStencilState:clear_depth[which]];
        [e setCullMode:MTLCullModeNone];
        [e setTriangleFillMode:MTLTriangleFillModeFill];
        [e setStencilReferenceValue:s & 0xFF];
        [e setViewport:(MTLViewport){rect[0], rect[1], rect[2], rect[3], 0.0, 1.0}];
        [e setScissorRect:(MTLScissorRect){rect[0], rect[1], rect[2], rect[3]}];
        [e setVertexBytes:&z length:sizeof z atIndex:0];
        [e setFragmentBytes:rgba length:16 atIndex:0];
        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        end_encoder(t);
    }
}
