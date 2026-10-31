/* Tribes: Vengeance's Direct3D 9 (port/vengeance/d3d9: HaloPad's Direct3D 9 on
 * Metal, c4aad68) on TribesPad. This file supplies what HaloPad's runtime gave it:
 * guest memory and the guest heap, the guest stack trace after a trap, the
 * desktop size the game sees, and a host window for the game's window.
 *
 * The host window is the game window's SDL drawable, created the way Starsiege
 * creates its GL drawable (ss_get_dc: hidden when STARSIEGE_HEADLESS=1, full
 * screen on iOS), with a Metal view whose layer Present draws into. A hidden
 * window presents nothing; VENGEANCE_CAPTURE=folder writes chosen frames to PNG
 * (VENGEANCE_CAPTURE_FRAMES=first:every, default 1:60) for headless checks. */
#import <QuartzCore/QuartzCore.h>
#import <ImageIO/ImageIO.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../llasm-support/llasm_cpu.h"
#include "../core/guest_heap.h"
#include "d3d9/halopad_host.h"
#include "d3d9/halopad_win32.h"
#include "d3d9/halopad_d3d9_internal.h"

extern uint64_t vengeance_guest_base(void);
extern _Noreturn void vengeance_d3d9_stop(_cpu *c);
extern uint32_t tribes_get_last_error(void);
extern uint32_t tribes_set_last_error(uint32_t value);
extern uint32_t tribes_get_system_metrics(uint64_t base, uint32_t index);
extern uint32_t ss_get_dc(_cpu *cpu, uint32_t window);
extern uint32_t ss_window_client_size(uint64_t base, uint32_t handle, int32_t *width, int32_t *height);
extern SDL_Window *tribes_sdl_native_window(uint32_t token);
extern void *halopad_com_state(const char *iface, uint32_t g);
extern void halopad_metal_read_image(void *target, uint32_t *out, uint32_t w, uint32_t h);
extern void (*halopad_d3d9_present_hook)(uint32_t device);
extern uint32_t halopad_d3d9_frame;

_Thread_local _cpu *halopad_cpu;   /* set by each COM method's wrapper (scripts/vengeance/build_engine.py) */

void *halopad_guest_ptr(uint32_t guest) { return (void *)(uintptr_t)(vengeance_guest_base() + guest); }

/* COM objects and vtables live in a private growable guest heap (HeapCreate). */
extern uint32_t tribes_heap_create(uint64_t base, uint32_t options, uint32_t initial, uint32_t maximum);
extern uint32_t tribes_heap_alloc(uint64_t base, uint32_t heap, uint32_t flags, uint32_t bytes);
extern uint32_t tribes_heap_free(uint64_t base, uint32_t heap, uint32_t flags, uint32_t address);
static uint32_t com_heap(void)
{
    static uint32_t heap;
    if (!heap) heap = tribes_heap_create(vengeance_guest_base(), 0, 0, 0);
    if (!heap) hp_unsupported("guest heap", "creating the COM heap (error %u)", tribes_get_last_error());
    return heap;
}

uint32_t halopad_heap_alloc(uint32_t size, int zero)
{
    uint32_t error = tribes_get_last_error();
    uint32_t address = tribes_heap_alloc(vengeance_guest_base(), com_heap(), zero ? 0x8u : 0u, size);   /* HEAP_ZERO_MEMORY */
    tribes_set_last_error(error);
    if (!address) hp_unsupported("guest heap", "an allocation of %u bytes that failed", size);
    return address;
}

void halopad_heap_free(uint32_t address)
{
    uint32_t error = tribes_get_last_error();
    if (address && !tribes_heap_free(vengeance_guest_base(), com_heap(), 0, address)) hp_unsupported("guest heap", "freeing 0x%08x", address);
    tribes_set_last_error(error);
}

void halopad_trace_guest_stack(void) { vengeance_d3d9_stop(halopad_cpu); }

/* The primary display as the game's GetSystemMetrics reports it. */
void halopad_desktop_size(int32_t *w, int32_t *h)
{
    *w = (int32_t)tribes_get_system_metrics(vengeance_guest_base(), 0);
    *h = (int32_t)tribes_get_system_metrics(vengeance_guest_base(), 1);
}

int halopad_window_client_size(uint32_t hwnd, int32_t *w, int32_t *h)
{
    return ss_window_client_size(vengeance_guest_base(), hwnd, w, h) != 0;
}

const char *halopad_window_text(uint32_t hwnd) { (void)hwnd; return ""; }

void halopad_host_app(void) {}
void halopad_host_pump(void) {}   /* the game pumps its own messages (PeekMessage) */

static int headless(void) { const char *v = getenv("STARSIEGE_HEADLESS"); return v && !strcmp(v, "1"); }

/* The host window behind a game window: one per window, kept for the process. */
void *halopad_window_host(uint32_t hwnd, uint32_t width, uint32_t height)
{
    static struct { uint32_t hwnd; hp_window *w; } known[4];
    for (int i = 0; i < 4; i++)
        if (known[i].hwnd == hwnd) { known[i].w->w = width; known[i].w->h = height; return known[i].w; }
    if (!halopad_cpu) hp_unsupported("Direct3D window", "window 0x%08x outside a COM call", hwnd);
    uint32_t dc = ss_get_dc(halopad_cpu, hwnd);
    if (!dc) hp_unsupported("Direct3D window", "window 0x%08x without a drawable", hwnd);
    SDL_Window *native = tribes_sdl_native_window(dc - 0x02000000u);
    SDL_MetalView view = native ? SDL_Metal_CreateView(native) : NULL;
    if (!view) hp_unsupported("Direct3D window", "a Metal view for window 0x%08x (%s)", hwnd, SDL_GetError());
    hp_window *w = calloc(1, sizeof *w);
    w->layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(view);
    w->layer.device = halopad_metal_gpu();
    w->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    w->layer.framebufferOnly = YES;
    w->w = width;
    w->h = height;
    w->refs = 1;
    for (int i = 0; i < 768; i++) w->lut[i] = (float)(i & 255) / 255.0f;
    w->on_screen = !headless();
    for (int i = 0; i < 4; i++)
        if (!known[i].hwnd) { known[i].hwnd = hwnd; known[i].w = w; w->refs++; break; }
    fprintf(stderr, "vengeance d3d9: window 0x%x draws %ux%u through Metal%s\n", hwnd, width, height, w->on_screen ? "" : " (hidden)");
    return w;
}

/* VENGEANCE_CAPTURE: completed frames to PNG, before they are presented. */
static void capture(uint32_t g)
{
    static int first = -1, every = 60;
    static const char *folder;
    if (first < 0) {
        folder = getenv("VENGEANCE_CAPTURE");
        first = 1;
        const char *f = getenv("VENGEANCE_CAPTURE_FRAMES");
        if (f) sscanf(f, "%d:%d", &first, &every);
        if (every < 1) every = 1;
    }
    int frame = (int)halopad_d3d9_frame;
    if (!folder || frame < first || (frame - first) % every) return;
    device *d = halopad_com_state("IDirect3DDevice9", g);
    uint32_t w = d->pp[0], h = d->pp[1];
    uint32_t *pixels = malloc((size_t)w * h * 4);
    halopad_metal_read_image(d->target, pixels, w, h);
    char path[1024];
    snprintf(path, sizeof path, "%s/frame-%05d.png", folder, frame);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap = CGBitmapContextCreate(pixels, w, h, 8, 4 * w, space, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef image = CGBitmapContextCreateImage(bitmap);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
    CGImageDestinationRef out = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL);
    CGImageDestinationAddImage(out, image, NULL);
    if (!CGImageDestinationFinalize(out)) fprintf(stderr, "vengeance d3d9: could not write %s\n", path);
    else fprintf(stderr, "vengeance d3d9: frame %d -> %s\n", frame, path);
    CFRelease(out); CFRelease(url); CGImageRelease(image); CGContextRelease(bitmap); CGColorSpaceRelease(space);
    free(pixels);
}

__attribute__((constructor)) static void install_capture(void) { halopad_d3d9_present_hook = capture; }
