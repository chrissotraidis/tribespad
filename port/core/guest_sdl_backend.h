#ifndef TRIBESPAD_GUEST_SDL_BACKEND_H
#define TRIBESPAD_GUEST_SDL_BACKEND_H
#include "guest_callback.h"
/* Public bridge calls retain one guest/owner thread. Native SDL workers own
   native memory and queue copied guest log notifications to this owner. */
uint32_t tribes_sdl_set_hint(_cpu *cpu,uint32_t name,uint32_t value);
uint32_t tribes_sdl_set_hint_priority(_cpu *cpu,uint32_t name,uint32_t value,uint32_t priority);
uint32_t tribes_sdl_reset_hint(_cpu *cpu,uint32_t name);
uint32_t tribes_sdl_get_hint(_cpu *cpu,uint32_t name);
uint32_t tribes_sdl_set_app_metadata(_cpu *cpu,uint32_t name,uint32_t version,uint32_t identifier);
uint32_t tribes_sdl_set_app_metadata_property(_cpu *cpu,uint32_t name,uint32_t value);
/* Copied cdecl log callbacks require a synchronous owner-thread SDL frame. */
void tribes_sdl_set_log_output(_cpu *cpu,uint32_t callback,uint32_t userdata);
void tribes_sdl_set_main_ready(_cpu *cpu);
uint32_t tribes_sdl_init(_cpu *cpu,uint32_t flags);
/* Trusted Apple host setup: call from the SDL_RunApp main callback (UIKit
   after didFinishLaunching), or the macOS process main before guest entry.
   Not a guest export. Video still requires the native main thread. */
void tribes_sdl_host_application_ready(void);
/* Host exit handoff requires guest SDL_Quit and no live public allocations.
   Afterwards guest SDL entry is forbidden for this process. */
void tribes_sdl_host_application_finished(void);
uint32_t tribes_sdl_was_init(_cpu *cpu,uint32_t flags);
/* Borrowed 32-bit public mode. internal is an opaque bridge token, never a
   native pointer. Storage belongs to the bridge until video shutdown. */
typedef struct TribesSDLDisplayMode {
    uint32_t display_id,format;
    int32_t width,height;
    float pixel_density,refresh_rate;
    int32_t refresh_numerator,refresh_denominator;
    uint32_t internal;
} TribesSDLDisplayMode;
/* i686 SDL_Surface public layout; reserved never contains a native pointer. */
typedef struct TribesSDLSurface {
    uint32_t flags,format;
    int32_t width,height,pitch;
    uint32_t pixels;
    int32_t refcount;
    uint32_t reserved;
} TribesSDLSurface;
uint32_t tribes_sdl_create_surface_from(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_convert_surface(_cpu *,uint32_t,uint32_t);
uint32_t tribes_sdl_premultiply_surface_alpha(_cpu *,uint32_t,uint32_t);
void tribes_sdl_destroy_surface(_cpu *,uint32_t);
void tribes_sdl_remove_surface_alternate_images(_cpu *,uint32_t);
uint32_t tribes_sdl_create_color_cursor(_cpu *,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_set_cursor(_cpu *,uint32_t);
void tribes_sdl_destroy_cursor(_cpu *,uint32_t);
/* Native float values are returned in the original x87 ST0 register. */
void tribes_sdl_get_window_display_scale(_cpu *,uint32_t);
void tribes_sdl_ceilf(_cpu *,uint32_t);
uint32_t tribes_sdl_get_display_for_window(_cpu *,uint32_t);
uint32_t tribes_sdl_get_current_display_mode(_cpu *,uint32_t);
uint32_t tribes_sdl_get_closest_fullscreen_mode(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_sync_window(_cpu *,uint32_t);
uint32_t tribes_sdl_set_window_fullscreen(_cpu *,uint32_t,uint32_t);
uint32_t tribes_sdl_set_window_size(_cpu *,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_set_window_bordered(_cpu *,uint32_t,uint32_t);
uint32_t tribes_sdl_set_window_relative_mouse_mode(_cpu *,uint32_t,uint32_t);
uint32_t tribes_sdl_set_window_position(_cpu *,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_set_window_fullscreen_mode(_cpu *,uint32_t,uint32_t);
uint32_t tribes_wgl_swap_interval(_cpu *,uint32_t);
uint32_t tribes_wgl_get_swap_interval(_cpu *);
uint32_t tribes_sdl_gl_set_swap_interval(_cpu *,uint32_t);
uint32_t tribes_sdl_get_primary_display(_cpu *cpu);
uint32_t tribes_sdl_get_desktop_display_mode(_cpu *cpu,uint32_t display);
/* One caller-owned pointer array + mode copies, released with SDL_free. */
uint32_t tribes_sdl_get_fullscreen_display_modes(_cpu *,uint32_t display,uint32_t count_address);
/* Trusted startup validation; high32=height, low32=width from the private
   native snapshot. Zero for an unknown/expired guest mode; not a guest API. */
uint64_t tribes_sdl_desktop_dimensions(_cpu *cpu,uint32_t mode);
void tribes_sdl_quit_subsystem(_cpu *cpu,uint32_t flags);
void tribes_sdl_backend_quit(_cpu *cpu);
/* Borrowed keyboard/name copies; gamepad enumeration is caller-owned and
   released through SDL_free. The event bridge must refresh keyboard state
   after native pumping before guest handlers run. */
uint32_t tribes_sdl_get_keyboard_state(_cpu *cpu,uint32_t count_address);
uint32_t tribes_sdl_poll_event(_cpu *,uint32_t);
void tribes_sdl_pump_events(_cpu *);
uint32_t tribes_sdl_get_gamepads(_cpu *cpu,uint32_t count_address);
uint32_t tribes_sdl_has_joystick(_cpu *cpu);
uint32_t tribes_sdl_open_gamepad(_cpu *cpu,uint32_t instance);
uint32_t tribes_sdl_get_gamepad_name(_cpu *cpu,uint32_t token);
void tribes_sdl_close_gamepad(_cpu *cpu,uint32_t token);
int tribes_sdl_has_window_handle(uint64_t guest_base,uint32_t hwnd);
/* Native window/context objects use typed opaque guest tokens. */
uint32_t tribes_sdl_create_properties(_cpu *);
void tribes_sdl_destroy_properties(_cpu *,uint32_t);
uint32_t tribes_sdl_set_string_property(_cpu *,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_set_boolean_property(_cpu *,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_set_number_property(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_create_window_properties(_cpu *,uint32_t);
uint32_t tribes_sdl_create_window(_cpu *cpu,uint32_t arg0,uint32_t arg1,uint32_t arg2,uint32_t arg3,uint32_t arg4);
void tribes_sdl_destroy_window(_cpu *cpu,uint32_t arg0);
uint32_t tribes_sdl_show_window(_cpu *cpu,uint32_t arg0);
uint32_t tribes_sdl_get_window_id(_cpu *cpu,uint32_t arg0);
uint32_t tribes_sdl_get_window_properties(_cpu *cpu,uint32_t arg0);
uint32_t tribes_sdl_get_window_size(_cpu *cpu,uint32_t arg0,uint32_t arg1,uint32_t arg2);
uint32_t tribes_sdl_get_window_size_pixels(_cpu *cpu,uint32_t arg0,uint32_t arg1,uint32_t arg2);
uint32_t tribes_sdl_gl_create_context(_cpu *cpu,uint32_t arg0);
uint32_t tribes_sdl_gl_destroy_context(_cpu *cpu,uint32_t arg0);
uint32_t tribes_sdl_gl_make_current(_cpu *cpu,uint32_t arg0,uint32_t arg1);
uint32_t tribes_sdl_gl_get_current_window(_cpu *cpu);
uint32_t tribes_sdl_gl_get_current_context(_cpu *cpu);
uint32_t tribes_sdl_gl_swap_window(_cpu *cpu,uint32_t arg0);
uint32_t tribes_sdl_gl_set_attribute(_cpu *cpu,uint32_t arg0,uint32_t arg1);
uint32_t tribes_sdl_gl_get_attribute(_cpu *cpu,uint32_t arg0,uint32_t arg1);
void tribes_sdl_gl_reset_attributes(_cpu *cpu);
void tribes_gl_clear_color(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_bind_framebuffer(_cpu *,uint32_t,uint32_t);
void tribes_gl_clear(_cpu *,uint32_t);
void tribes_gl_draw_buffer(_cpu *,uint32_t);
void tribes_gl_pixel_store(_cpu *,uint32_t,uint32_t);
uint32_t tribes_gl_is_sync(_cpu *,uint32_t);
void tribes_gl_generate_mipmap(_cpu *,uint32_t);
uint32_t tribes_gl_fence_sync(_cpu *,uint32_t,uint32_t);
uint32_t tribes_gl_client_wait_sync(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_get_sync_iv(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_delete_sync(_cpu *,uint32_t);
void tribes_gl_flush(_cpu *);
void tribes_gl_finish(_cpu *);
void tribes_gl_hint(_cpu *,uint32_t,uint32_t);
void tribes_gl_gen_queries(_cpu *,uint32_t,uint32_t);
uint32_t tribes_gl_get_string(_cpu *,uint32_t);
uint32_t tribes_gl_get_error(_cpu *);
void tribes_gl_get_integer(_cpu *,uint32_t,uint32_t);
void tribes_gl_get_float(_cpu *,uint32_t,uint32_t);
uint32_t tribes_glu_error_string(_cpu *,uint32_t);
void tribes_gl_vertex3fv(_cpu *,uint32_t);
uint32_t tribes_glu_project(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_get_pointer_property(_cpu *,uint32_t,uint32_t,uint32_t);
uint32_t tribes_wgl_extensions_arb(_cpu *,uint32_t);
uint32_t tribes_wgl_extensions_ext(_cpu *);
void tribes_gl_active_texture(_cpu *,uint32_t);
void tribes_gl_client_active_texture(_cpu *,uint32_t);
void tribes_gl_multitex_coord2f(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_multitex_coord2fv(_cpu *,uint32_t,uint32_t);
void tribes_gl_multitex_coord3fv(_cpu *,uint32_t,uint32_t);

void tribes_gl_gen_textures(_cpu *,uint32_t,uint32_t);
void tribes_gl_delete_textures(_cpu *,uint32_t,uint32_t);
void tribes_gl_bind_texture(_cpu *,uint32_t,uint32_t);
void tribes_gl_tex_parameter_i(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_tex_parameter_f(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_tex_image_2d(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_gen_buffers(_cpu *,uint32_t,uint32_t);
void tribes_gl_bind_buffer(_cpu *,uint32_t,uint32_t);
void tribes_gl_buffer_data(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_delete_buffers(_cpu *,uint32_t,uint32_t);
void tribes_gl_begin_query(_cpu *,uint32_t,uint32_t);
void tribes_gl_end_query(_cpu *,uint32_t);
void tribes_gl_get_query_object_ui64(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_matrix_mode(_cpu *,uint32_t);
void tribes_gl_load_identity(_cpu *);
void tribes_gl_load_matrix_d(_cpu *,uint32_t);
void tribes_gl_mult_matrix_f(_cpu *,uint32_t);
void tribes_gl_mult_matrix_d(_cpu *,uint32_t);
void tribes_gl_frustum(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_viewport(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_disable(_cpu *,uint32_t);
void tribes_gl_enable(_cpu *,uint32_t);
void tribes_gl_tex_env_i(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_blend_func(_cpu *,uint32_t,uint32_t);
void tribes_gl_begin(_cpu *,uint32_t);
void tribes_gl_vertex2f(_cpu *,uint32_t,uint32_t);
void tribes_gl_end(_cpu *);
void tribes_gl_blend_func_separate(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_color4ub(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_color4f(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_vertex2i(_cpu *,uint32_t,uint32_t);
void tribes_gl_enable_client_state(_cpu *,uint32_t);
void tribes_gl_disable_client_state(_cpu *,uint32_t);
void tribes_gl_vertex_pointer(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_color_pointer(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_texcoord_pointer(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_use_program(_cpu *,uint32_t);
void tribes_gl_uniform2i(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_uniform4i(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_draw_elements(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);

/* Original renderer shader initialization, with host-staged pointer arguments. */
void tribes_gl_depth_range(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_clear_depth(_cpu *,uint32_t,uint32_t);
void tribes_gl_depth_func(_cpu *,uint32_t);
uint32_t tribes_gl_create_shader(_cpu *,uint32_t);
void tribes_gl_shader_source(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_compile_shader(_cpu *,uint32_t);
void tribes_gl_get_shader_iv(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_get_shader_log(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_delete_shader(_cpu *,uint32_t);
uint32_t tribes_gl_create_program(_cpu *);
void tribes_gl_attach_shader(_cpu *,uint32_t,uint32_t);
void tribes_gl_link_program(_cpu *,uint32_t);
void tribes_gl_get_program_iv(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_get_program_log(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_delete_program(_cpu *,uint32_t);
void tribes_gl_get_active_uniform(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_get_active_attrib(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_gl_get_uniform_location(_cpu *,uint32_t,uint32_t);
uint32_t tribes_gl_get_attrib_location(_cpu *,uint32_t,uint32_t);
void tribes_gl_uniform1i(_cpu *,uint32_t,uint32_t);
void tribes_gl_uniform1f(_cpu *,uint32_t,uint32_t);
void tribes_gl_uniform4fv(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_tex_image_3d(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_gl_tex_sub_image_3d(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);

/* Original 3D render state and checked normal-array descriptors. */
void tribes_gl_front_face(_cpu *,uint32_t);
void tribes_gl_depth_mask(_cpu *,uint32_t);
void tribes_gl_polygon_offset(_cpu *,uint32_t,uint32_t);
uint32_t tribes_gl_is_enabled(_cpu *,uint32_t);
void tribes_gl_color4fv(_cpu *,uint32_t);
void tribes_gl_material_fv(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_tex_env_fv(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_tex_env_f(_cpu *,uint32_t,uint32_t,uint32_t);
void tribes_gl_normal_pointer(_cpu *,uint32_t,uint32_t,uint32_t);

void tribes_gl_push_matrix(_cpu *);
void tribes_gl_pop_matrix(_cpu *);
void tribes_gl_scale_f(_cpu *,uint32_t,uint32_t,uint32_t);
/* Checked original SDL audio specifications, samples and owned stream tokens. */
uint32_t tribes_sdl_open_audio_device(_cpu *,uint32_t,uint32_t);
uint32_t tribes_sdl_get_audio_device_format(_cpu *,uint32_t,uint32_t,uint32_t);
uint32_t tribes_sdl_pause_audio_device(_cpu *,uint32_t);
void tribes_sdl_close_audio_device(_cpu *,uint32_t);
uint32_t tribes_sdl_create_audio_stream(_cpu *,uint32_t,uint32_t);
uint32_t tribes_sdl_bind_audio_stream(_cpu *,uint32_t,uint32_t);
void tribes_sdl_destroy_audio_stream(_cpu *,uint32_t);
uint32_t tribes_sdl_resume_audio_stream_device(_cpu *,uint32_t);
uint32_t tribes_sdl_pause_audio_stream_device(_cpu *,uint32_t);
uint32_t tribes_sdl_clear_audio_stream(_cpu *,uint32_t);
uint32_t tribes_sdl_get_audio_stream_available(_cpu *,uint32_t);
uint32_t tribes_sdl_get_audio_stream_data(_cpu *,uint32_t,uint32_t,uint32_t);
#endif
