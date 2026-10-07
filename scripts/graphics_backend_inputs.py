"""Finite native GL/GLU routes; no runtime native-code generation or dlsym escape."""
import importlib.util,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1]
EXPORTS=[('glGetString','tribes_gl_get_string',1,True,0x20040000),('glGetError','tribes_gl_get_error',0,True,0x20040000),('glGetIntegerv','tribes_gl_get_integer',2,False,0x20040000),('gluErrorString','tribes_glu_error_string',1,True,0x20050000),('wglGetExtensionsStringARB','tribes_wgl_extensions_arb',1,True,0x20040000),('wglGetExtensionsStringEXT','tribes_wgl_extensions_ext',0,True,0x20040000),('glClearColor','tribes_gl_clear_color',4,False,0x20040000),('glBindFramebuffer','tribes_gl_bind_framebuffer',2,False,0x20040000),('glClear','tribes_gl_clear',1,False,0x20040000),('glDrawBuffer','tribes_gl_draw_buffer',1,False,0x20040000),('glFinish','tribes_gl_finish',0,False,0x20040000),('glPixelStorei','tribes_gl_pixel_store',2,False,0x20040000)]
EXPORTS += [('wglGetProcAddress','tribes_wgl_get_proc',1,True,0x20040000),
 ('glActiveTextureARB','tribes_gl_active_texture',1,False,0x20040000),
 ('glClientActiveTextureARB','tribes_gl_client_active_texture',1,False,0x20040000),
 ('glMultiTexCoord2fARB','tribes_gl_multitex_coord2f',3,False,0x20040000),
 ('glMultiTexCoord2fvARB','tribes_gl_multitex_coord2fv',2,False,0x20040000),
 ('glMultiTexCoord3fvARB','tribes_gl_multitex_coord3fv',2,False,0x20040000)]

EXPORTS += [('glGetFloatv','tribes_gl_get_float',2,False,0x20040000)]
EXPORTS += [('glHint','tribes_gl_hint',2,False,0x20040000),('glGenQueries','tribes_gl_gen_queries',2,False,0x20040000)]
EXPORTS += [('glGenTextures','tribes_gl_gen_textures',2,False,0x20040000),('glBindTexture','tribes_gl_bind_texture',2,False,0x20040000),('glTexParameteri','tribes_gl_tex_parameter_i',3,False,0x20040000),('glTexParameterf','tribes_gl_tex_parameter_f',3,False,0x20040000),('glTexImage2D','tribes_gl_tex_image_2d',9,False,0x20040000),('glDeleteTextures','tribes_gl_delete_textures',2,False,0x20040000)]
EXPORTS += [('glGenBuffers','tribes_gl_gen_buffers',2,False,0x20040000),('glBindBuffer','tribes_gl_bind_buffer',2,False,0x20040000),('glBufferData','tribes_gl_buffer_data',4,False,0x20040000),('glDeleteBuffers','tribes_gl_delete_buffers',2,False,0x20040000)]
EXPORTS += [('wglSwapIntervalEXT','tribes_wgl_swap_interval',1,True,0x20040000),('wglGetSwapIntervalEXT','tribes_wgl_get_swap_interval',0,True,0x20040000)]
EXPORTS += [('glBeginQuery','tribes_gl_begin_query',2,False,0x20040000),('glEndQuery','tribes_gl_end_query',1,False,0x20040000),('glGetQueryObjectui64v','tribes_gl_get_query_object_ui64',3,False,0x20040000)]
EXPORTS += [('glMatrixMode','tribes_gl_matrix_mode',1,False,0x20040000),('glLoadIdentity','tribes_gl_load_identity',0,False,0x20040000),('glLoadMatrixd','tribes_gl_load_matrix_d',1,False,0x20040000),('glViewport','tribes_gl_viewport',4,False,0x20040000),('glDisable','tribes_gl_disable',1,False,0x20040000),('glEnable','tribes_gl_enable',1,False,0x20040000)]
EXPORTS += [('glActiveTexture', 'tribes_gl_active_texture', 1, False, 537133056), ('glTexEnvi', 'tribes_gl_tex_env_i', 3, False, 537133056), ('glBlendFuncSeparate', 'tribes_gl_blend_func_separate', 4, False, 537133056), ('glColor4ub', 'tribes_gl_color4ub', 4, False, 537133056), ('glEnableClientState', 'tribes_gl_enable_client_state', 1, False, 537133056), ('glDisableClientState', 'tribes_gl_disable_client_state', 1, False, 537133056), ('glVertexPointer', 'tribes_gl_vertex_pointer', 4, False, 537133056), ('glTexCoordPointer', 'tribes_gl_texcoord_pointer', 4, False, 537133056), ('glUseProgram', 'tribes_gl_use_program', 1, False, 537133056), ('glUniform2i', 'tribes_gl_uniform2i', 3, False, 537133056), ('glUniform4i', 'tribes_gl_uniform4i', 5, False, 537133056), ('glDrawElements', 'tribes_gl_draw_elements', 4, False, 537133056)]
# The Windows caller stores this opaque handle to gate shader initialization.
EXPORTS += [('wglGetCurrentContext','tribes_sdl_gl_get_current_context',0,True,0x20040000)]
EXPORTS += [('glDepthRange', 'tribes_gl_depth_range', 4, False, 537133056), ('glClearDepth', 'tribes_gl_clear_depth', 2, False, 537133056), ('glDepthFunc', 'tribes_gl_depth_func', 1, False, 537133056), ('glCreateShader', 'tribes_gl_create_shader', 1, True, 537133056), ('glShaderSource', 'tribes_gl_shader_source', 4, False, 537133056), ('glCompileShader', 'tribes_gl_compile_shader', 1, False, 537133056), ('glGetShaderiv', 'tribes_gl_get_shader_iv', 3, False, 537133056), ('glGetShaderInfoLog', 'tribes_gl_get_shader_log', 4, False, 537133056), ('glDeleteShader', 'tribes_gl_delete_shader', 1, False, 537133056), ('glCreateProgram', 'tribes_gl_create_program', 0, True, 537133056), ('glAttachShader', 'tribes_gl_attach_shader', 2, False, 537133056), ('glLinkProgram', 'tribes_gl_link_program', 1, False, 537133056), ('glGetProgramiv', 'tribes_gl_get_program_iv', 3, False, 537133056), ('glGetProgramInfoLog', 'tribes_gl_get_program_log', 4, False, 537133056), ('glDeleteProgram', 'tribes_gl_delete_program', 1, False, 537133056), ('glGetActiveUniform', 'tribes_gl_get_active_uniform', 7, False, 537133056), ('glGetActiveAttrib', 'tribes_gl_get_active_attrib', 7, False, 537133056), ('glGetUniformLocation', 'tribes_gl_get_uniform_location', 2, True, 537133056), ('glGetAttribLocation', 'tribes_gl_get_attrib_location', 2, True, 537133056), ('glUniform1i', 'tribes_gl_uniform1i', 2, False, 537133056), ('glUniform1f', 'tribes_gl_uniform1f', 2, False, 537133056)]
EXPORTS += [('glUniform4fv','tribes_gl_uniform4fv',3,False,0x20040000)]
EXPORTS += [('glTexImage3D','tribes_gl_tex_image_3d',10,False,0x20040000),('glTexSubImage3D','tribes_gl_tex_sub_image_3d',11,False,0x20040000)]
EXPORTS += [('glBlendFunc','tribes_gl_blend_func',2,False,0x20040000),('glBegin','tribes_gl_begin',1,False,0x20040000),('glVertex2f','tribes_gl_vertex2f',2,False,0x20040000),('glEnd','tribes_gl_end',0,False,0x20040000)]
EXPORTS += [('glFlush','tribes_gl_flush',0,False,0x20040000)]
EXPORTS += [('glFenceSync','tribes_gl_fence_sync',2,True,0x20040000),('glClientWaitSync','tribes_gl_client_wait_sync',4,True,0x20040000),('glDeleteSync','tribes_gl_delete_sync',1,False,0x20040000)]
EXPORTS += [('glGetSynciv','tribes_gl_get_sync_iv',5,False,0x20040000)]
EXPORTS += [('glColorPointer','tribes_gl_color_pointer',4,False,0x20040000)]
EXPORTS += [('glIsSync','tribes_gl_is_sync',1,True,0x20040000)]
EXPORTS += [('glGenerateMipmap','tribes_gl_generate_mipmap',1,False,0x20040000)]
EXPORTS += [('glFrustum','tribes_gl_frustum',12,False,0x20040000)]
EXPORTS += [('glMultMatrixf','tribes_gl_mult_matrix_f',1,False,0x20040000)]
EXPORTS += [('glFrontFace', 'tribes_gl_front_face', 1, False, 537133056)]
EXPORTS += [('glDepthMask', 'tribes_gl_depth_mask', 1, False, 537133056)]
EXPORTS += [('glIsEnabled', 'tribes_gl_is_enabled', 1, True, 537133056)]
EXPORTS += [('glColor4fv', 'tribes_gl_color4fv', 1, False, 537133056)]
EXPORTS += [('glMaterialfv', 'tribes_gl_material_fv', 3, False, 537133056)]
EXPORTS += [('glTexEnvfv', 'tribes_gl_tex_env_fv', 3, False, 537133056)]
EXPORTS += [('glTexEnvf', 'tribes_gl_tex_env_f', 3, False, 537133056)]
EXPORTS += [('glNormalPointer', 'tribes_gl_normal_pointer', 3, False, 537133056)]
EXPORTS += [('glPushMatrix', 'tribes_gl_push_matrix', 0, False, 537133056)]
EXPORTS += [('glPopMatrix', 'tribes_gl_pop_matrix', 0, False, 537133056)]
EXPORTS += [('glScalef', 'tribes_gl_scale_f', 3, False, 537133056)]
EXPORTS += [('glMultMatrixd','tribes_gl_mult_matrix_d',1,False,0x20040000)]
EXPORTS += [('glPolygonOffset','tribes_gl_polygon_offset',2,False,0x20040000)]
EXPORTS += [('glColor4f','tribes_gl_color4f',4,False,0x20040000),('glVertex2i','tribes_gl_vertex2i',2,False,0x20040000)]
EXPORTS += [('glTexSubImage2D','tribes_gl_tex_sub_image_2d',9,False,0x20040000)]
EXPORTS += [('glColor3ub','tribes_gl_color3ub',3,False,0x20040000),('glVertex3f','tribes_gl_vertex3f',3,False,0x20040000)]
EXPORTS += [('glTranslatef','tribes_gl_translate_f',3,False,0x20040000)]
EXPORTS += [('glTexCoord2f','tribes_gl_tex_coord2f',2,False,0x20040000)]
EXPORTS += [('glRotatef','tribes_gl_rotate_f',4,False,0x20040000)]
EXPORTS += [('glLightfv','tribes_gl_light_fv',3,False,0x20040000),('glLightf','tribes_gl_light_f',3,False,0x20040000),('glLightModelfv','tribes_gl_light_model_fv',2,False,0x20040000)]
EXPORTS += [('gluProject','tribes_glu_project',12,True,0x20050000)]
EXPORTS += [('glVertex3fv','tribes_gl_vertex3fv',1,False,0x20040000)]
def prepare(out):
 bindings={};rows=[]
 for i,(name,helper,n,returns,module) in enumerate(EXPORTS):
  bindings[name]=dict(helper=helper,arguments=n,returns=returns,helper_returns='i32' if returns else 'void',guest_base=False,cpu_argument=True,calling_convention='stdcall')
  rows.append((name,0xfffa0000+16*i,module))
 for name,helper,n,returns in [('SDL_LoadObject','tribes_graphics_load_object',1,True),('SDL_LoadFunction','tribes_graphics_load_function',2,True),('SDL_UnloadObject','tribes_graphics_unload_object',1,False),('SDL_GL_GetProcAddress','tribes_graphics_get_proc',1,True),('SDL_GetPointerProperty','tribes_sdl_get_pointer_property',3,True)]:
  bindings[name]=dict(helper=helper,arguments=n,returns=returns,helper_returns='i32' if returns else 'void',guest_base=False,cpu_argument=True,calling_convention='cdecl')
 spec=importlib.util.spec_from_file_location('graphics_bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
 wrapper=out/'graphics-bindings.ll';wrapper.write_text(gen.generate(bindings,False))
 table='/* Finite precompiled exports generated by graphics_backend_inputs.py. */\n'
 for name,_,_ in rows:table+='extern void tbimp_'+name+'(void);\n'
 table+='static const GraphicsExport graphics_exports[]={\n'+''.join(' {%s,0x%xu,0x%xu,(void *)tbimp_%s},\n'%(json.dumps(name),address,module,name) for name,address,module in rows)+'};\n'
 (out/'graphics-exports.inc').write_text(table)
 return [R/'port/core/guest_graphics.c',wrapper],bindings
