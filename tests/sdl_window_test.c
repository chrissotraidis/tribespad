#include "../port/apple/sdl_main.h"
#include "../port/core/guest_sdl_backend.h"
#ifdef GRAPHICS
#include "../port/core/guest_graphics.h"
#include "../port/core/guest_modules.h"
#endif
#include "../port/core/guest_sdl_memory.h"
#include "../port/core/guest_heap.h"
#include "../port/core/process_environment.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <SDL3/SDL.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#include <OpenGL/glu.h>
extern GLboolean glIsQueryARB(GLuint id);
extern GLuint glCreateShader(GLenum);
extern void glShaderSource(GLuint,GLsizei,const GLchar *const *,const GLint *);
extern void glCompileShader(GLuint);
extern void glGetShaderiv(GLuint,GLenum,GLint *);
extern GLuint glCreateProgram(void);
extern void glAttachShader(GLuint,GLuint);
extern void glLinkProgram(GLuint);
extern void glGetProgramiv(GLuint,GLenum,GLint *);
extern GLint glGetUniformLocation(GLuint,const GLchar *);
extern void glGetUniformiv(GLuint,GLint,GLint *);
extern void glGetUniformfv(GLuint,GLint,GLfloat *);
extern void glGetShaderSource(GLuint,GLsizei,GLsizei *,GLchar *);
extern void glGenBuffers(GLsizei,GLuint *);
extern void glFogCoordPointerEXT(GLenum,GLsizei,const GLvoid *);
extern void glVertexAttribPointer(GLuint,GLint,GLenum,GLboolean,GLsizei,const void *);
extern void glEnableVertexAttribArray(GLuint);
extern void glDisableVertexAttribArray(GLuint);
extern GLboolean glIsProgram(GLuint);
extern void glUseProgram(GLuint),glFogCoordf(GLfloat);
extern GLboolean glIsShader(GLuint);
extern void glDeleteProgram(GLuint);
extern void glDeleteShader(GLuint);
extern GLboolean glIsBuffer(GLuint id);
extern const GLubyte *glGetStringi(GLenum name,GLuint index);
extern void glBindBuffer(GLenum target,GLuint id);
extern void glBufferData(GLenum target,GLsizeiptr size,const void *data,GLenum usage);
extern void glGetBufferParameteriv(GLenum target,GLenum name,GLint *value);
extern void glGetBufferSubData(GLenum target,GLintptr offset,GLsizeiptr size,void *data);
extern void glBeginQueryARB(GLenum target,GLuint id);
extern void glEndQueryARB(GLenum target);
extern void glGetQueryivARB(GLenum target,GLenum pname,GLint *value);
extern void glGetQueryObjectui64v(GLuint id,GLenum pname,GLuint64 *value);
extern void glGetQueryObjectuivARB(GLuint id,GLenum pname,GLuint *params);
extern void glDeleteQueriesARB(GLsizei n,const GLuint *ids);
#endif
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#ifdef TB_GL4ES
#include "original-shader-fixture.h"
#endif
extern void tribes_enter(_cpu *,uint32_t);
extern unsigned sdl_test_live,sdl_test_fail;
#ifdef CHECKED
extern void bad_window(void),bad_context(void),bad_randf(void);
#ifdef TB_GL4ES
extern void bad_gl4es_color(void),bad_gl4es_adapter(void),gl4es_guest_pointer(void *);
#endif
#if defined(GRAPHICS) && !TARGET_OS_IPHONE
extern void bad_frustum(void),bad_mult_matrix(void),bad_mult_matrix_double(void),bad_depth_mask(void),bad_is_enabled(void),bad_scale(void),bad_polygon_offset(void);
extern void bad_alpha_func(void),bad_polygon_mode(void),bad_load_matrix_f(void),bad_tex_gen_i(void),bad_tex_gen_fv(void);
extern void bad_fog_i(void),bad_fog_f(void),bad_fog_fv(void);
extern void bad_light_fv(void),bad_light_f(void),bad_light_model_fv(void);
extern void bad_rotate_f(void),bad_translate_f(void),bad_tex_coord2f(void);
extern void bad_color3ub(void),bad_vertex3f(void),bad_restart(void);
extern void bad_color4f(void),bad_vertex2i(void),bad_tex_sub_image_2d(void);
#endif
#endif
static unsigned char *memory;static _cpu cpu;
static uint32_t call(unsigned id,const uint32_t *args,size_t count){
 cpu._esp=0xf0000;if(count)memcpy(memory+cpu._esp,args,count*4);_cpu before=cpu;
 tribes_set_last_error(0x19a5);tribes_enter(&cpu,id);
 if(id==8||id==17||id==19||id==41||id==55||id==57||id==62||id==70||id==86||id==89||id==91||id==93)assert(cpu._eax==before._eax);
 before._eax=cpu._eax;assert(!memcmp(&cpu,&before,sizeof cpu));
 assert((!count||!memcmp(memory+cpu._esp,args,count*4))&&tribes_get_last_error()==0x19a5);return cpu._eax;
}
#define CALL(id,...) call(id,(uint32_t[]){__VA_ARGS__},sizeof((uint32_t[]){__VA_ARGS__})/4)
static uint32_t word(unsigned p){uint32_t v;memcpy(&v,memory+p,4);return v;}
static void *wrong_thread(void *unused){(void)unused;CALL(40,0x2000,640,480,0x200a,0);return NULL;}
static bool reenter(void *unused,SDL_Event *event){(void)unused;if(event->type==SDL_EVENT_WINDOW_SHOWN)CALL(51);return true;}
/* Native rendering oracle: wrappers above create/select the real context.
   GL dispatch from the original game is a separate, still missing provider. */
static void pixels(void){
 typedef void (*ClearColor)(float,float,float,float);typedef void (*Clear)(unsigned);
 typedef void (*ReadPixels)(int,int,int,int,unsigned,unsigned,void *);typedef const unsigned char *(*GetString)(unsigned);
 ClearColor color=(ClearColor)SDL_GL_GetProcAddress("glClearColor");Clear clear=(Clear)SDL_GL_GetProcAddress("glClear");ReadPixels read=(ReadPixels)SDL_GL_GetProcAddress("glReadPixels");GetString string=(GetString)SDL_GL_GetProcAddress("glGetString");assert(color&&clear&&read&&string);
 unsigned char pixel[4]={0};color(.25f,.5f,.75f,1);clear(GL_COLOR_BUFFER_BIT);read(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
 assert(pixel[0]>=63&&pixel[0]<=65&&pixel[1]>=127&&pixel[1]<=129&&pixel[2]>=190&&pixel[2]<=192&&pixel[3]==255);
 printf("renderer=%s version=%s pixel=%u,%u,%u,%u\n",string(GL_RENDERER),string(GL_VERSION),pixel[0],pixel[1],pixel[2],pixel[3]);
#ifdef TB_GL4ES
 /* Exercise the checked guest backend's legacy matrix/immediate draw path,
    then read the actual EAGL drawable through the independent native oracle. */
 int width,height;assert(SDL_GetWindowSizeInPixels(SDL_GL_GetCurrentWindow(),&width,&height));
 tribes_gl_draw_buffer(&cpu,0x0405); /* UIKit's actual back-buffer destination. */
 tribes_gl_viewport(&cpu,0,0,width,height);
 tribes_gl_clear_color(&cpu,0,0,0,0x3f800000);tribes_gl_clear(&cpu,GL_COLOR_BUFFER_BIT);
 tribes_gl_matrix_mode(&cpu,0x1701);tribes_gl_load_identity(&cpu);
 tribes_gl_matrix_mode(&cpu,0x1700);tribes_gl_load_identity(&cpu);
 tribes_gl_color4f(&cpu,0x3f800000,0,0,0x3f800000);
 tribes_gl_begin(&cpu,0x0007);
 tribes_gl_vertex2f(&cpu,0xbf000000,0xbf000000);
 tribes_gl_vertex2f(&cpu,0x3f000000,0xbf000000);
 tribes_gl_vertex2f(&cpu,0x3f000000,0x3f000000);
 tribes_gl_vertex2f(&cpu,0xbf000000,0x3f000000);
 tribes_gl_end(&cpu);tribes_gl_finish(&cpu);
 read(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
 assert(pixel[0]==255&&pixel[1]==0&&pixel[2]==0&&pixel[3]==255);
 read(width/16,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
 assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==0&&pixel[3]==255);
 printf("PASS: checked mobile guest legacy draw with native pixel readback\n");
 /* The original GUI uses indexed client arrays. Its texture selector belongs
    to GL4ES, not native GLES2; exercise two units and a context-local reset. */
 const float vertices[]={-.5f,-.5f,.5f,-.5f,.5f,.5f,-.5f,.5f};
 const float coords[]={0,0,1,0,1,1,0,1};
 const unsigned char indices[]={0,1,2,0,2,3},texel[]={0,255,0,255};
 memcpy(memory+0x8000,vertices,sizeof vertices);memcpy(memory+0x8100,coords,sizeof coords);
 memcpy(memory+0x8020,vertices,sizeof vertices);memcpy(memory+0x8120,coords,sizeof coords);
 memcpy(memory+0x8200,indices,sizeof indices);memcpy(memory+0x8300,texel,sizeof texel);
 tribes_gl_gen_textures(&cpu,1,0x8400);uint32_t texture;memcpy(&texture,memory+0x8400,4);
 tribes_gl_bind_texture(&cpu,GL_TEXTURE_2D,texture);
 tribes_gl_tex_parameter_i(&cpu,GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
 tribes_gl_tex_parameter_i(&cpu,GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 tribes_gl_tex_image_2d(&cpu,GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,0x8300);
 tribes_gl_enable(&cpu,GL_TEXTURE_2D);tribes_gl_color4f(&cpu,0x3f800000,0x3f800000,0x3f800000,0x3f800000);
 tribes_gl_enable_client_state(&cpu,0x8074);tribes_gl_vertex_pointer(&cpu,2,GL_FLOAT,0,0x8000);
 for(unsigned unit=0;unit<2;unit++){
  tribes_gl_client_active_texture(&cpu,0x84c0+unit);
  tribes_gl_enable_client_state(&cpu,0x8078);tribes_gl_texcoord_pointer(&cpu,2,GL_FLOAT,0,0x8100);
 }
 tribes_gl_draw_elements(&cpu,GL_TRIANGLES,6,GL_UNSIGNED_BYTE,0x8200);tribes_gl_finish(&cpu);
 read(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
 assert(pixel[0]==0&&pixel[1]==255&&pixel[2]==0&&pixel[3]==255);
 for(unsigned variant=0;variant<3;variant++){
  uint32_t program=tribes_gl_create_program(&cpu),shaders[2];
  for(unsigned stage=0;stage<2;stage++){
   const char *source=stage?original_shaders[variant].fragment:original_shaders[variant].vertex;
   strcpy((char *)memory+0x9000,source);uint32_t address=0x9000;memcpy(memory+0x8800,&address,4);
   shaders[stage]=tribes_gl_create_shader(&cpu,stage?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
   tribes_gl_shader_source(&cpu,shaders[stage],1,0x8800,0);tribes_gl_compile_shader(&cpu,shaders[stage]);
   tribes_gl_get_shader_iv(&cpu,shaders[stage],GL_COMPILE_STATUS,0x8804);
   if(!word(0x8804)){tribes_gl_get_shader_log(&cpu,shaders[stage],4096,0,0xb000);fprintf(stderr,"GUI shader: %s\n",memory+0xb000);}
   assert(word(0x8804));tribes_gl_attach_shader(&cpu,program,shaders[stage]);
  }
  tribes_gl_link_program(&cpu,program);tribes_gl_get_program_iv(&cpu,program,GL_LINK_STATUS,0x8804);
  if(!word(0x8804)){tribes_gl_get_program_log(&cpu,program,4096,0,0xb000);fprintf(stderr,"GUI program: %s\n",memory+0xb000);}
  assert(word(0x8804));tribes_gl_use_program(&cpu,program);
  strcpy((char *)memory+0x8808,"uiScale");tribes_gl_uniform1f(&cpu,tribes_gl_get_uniform_location(&cpu,program,0x8808),0x40000000);
  strcpy((char *)memory+0x8808,"texRes");tribes_gl_uniform2i(&cpu,tribes_gl_get_uniform_location(&cpu,program,0x8808),1,1);
  strcpy((char *)memory+0x8808,"srcRect");tribes_gl_uniform4i(&cpu,tribes_gl_get_uniform_location(&cpu,program,0x8808),0,0,1,1);
  tribes_gl_color4f(&cpu,0x3f800000,0x3f800000,0x3f800000,0x3f000000);
  tribes_gl_clear(&cpu,GL_COLOR_BUFFER_BIT);tribes_gl_draw_elements(&cpu,GL_TRIANGLES,6,GL_UNSIGNED_BYTE,0x8200);tribes_gl_finish(&cpu);
  read(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
  assert(pixel[0]==0&&pixel[2]==0&&pixel[3]>=127&&pixel[3]<=128);
  assert(variant?(pixel[1]>=127&&pixel[1]<=128):pixel[1]==255);
  tribes_gl_clear(&cpu,GL_COLOR_BUFFER_BIT);
  _cpu array_cpu=cpu;uint32_t array_error=tribes_get_last_error();
  tribes_gl_primitive_restart_index(&cpu,5);tribes_gl_enable(&cpu,0x8f9d);
  tribes_gl_draw_arrays(&cpu,0x0007,4,4);tribes_gl_finish(&cpu);
  assert(!memcmp(&array_cpu,&cpu,sizeof cpu)&&tribes_get_last_error()==array_error);
  tribes_gl_disable(&cpu,0x8f9d);
  read(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
  assert(pixel[0]==0&&pixel[2]==0&&pixel[3]>=127&&pixel[3]<=128);
  assert(variant?(pixel[1]>=127&&pixel[1]<=128):pixel[1]==255);
  tribes_gl_use_program(&cpu,0);tribes_gl_delete_program(&cpu,program);
  for(unsigned stage=0;stage<2;stage++)tribes_gl_delete_shader(&cpu,shaders[stage]);
 }
 puts("PASS: three original GUI shader variants, indexed/sequential quads, nonzero first, ignored restart and expected alpha/color pixels");
 while(tribes_gl_get_error(&cpu)){}
 tribes_gl_draw_arrays(&cpu,0x0007,UINT32_MAX,4);assert(tribes_gl_get_error(&cpu)==GL_INVALID_VALUE);
 tribes_gl_draw_arrays(&cpu,0x0007,0,UINT32_MAX);assert(tribes_gl_get_error(&cpu)==GL_INVALID_VALUE);
 tribes_gl_draw_arrays(&cpu,0x0007,0x7fffffff,0);assert(!tribes_gl_get_error(&cpu));
 for(unsigned unit=0;unit<2;unit++){tribes_gl_client_active_texture(&cpu,0x84c0+unit);tribes_gl_disable_client_state(&cpu,0x8078);}
 tribes_gl_client_active_texture(&cpu,0x84c0);tribes_gl_disable_client_state(&cpu,0x8074);
 tribes_gl_disable(&cpu,GL_TEXTURE_2D);tribes_gl_delete_textures(&cpu,1,0x8400);
 printf("PASS: checked mobile indexed textured draw, two client texture units and native green pixel\n");
 tribes_gl_matrix_mode(&cpu,0x1701);tribes_gl_load_identity(&cpu);
 /* Six IA-32 doubles for a real perspective matrix, not a GLES float ABI. */
 _cpu projection_cpu=cpu;uint32_t projection_error=tribes_get_last_error();
 tribes_gl_frustum(&cpu,0,0xbff00000,0,0x3ff00000,0,0xbff00000,0,0x3ff00000,0,0x3ff00000,0,0x40240000);
 assert(!memcmp(&projection_cpu,&cpu,sizeof cpu)&&tribes_get_last_error()==projection_error);
 tribes_gl_get_float(&cpu,0x0ba7,0x9000);float projection[16];memcpy(projection,memory+0x9000,sizeof projection);
 const float expected_projection[16]={1,0,0,0,0,1,0,0,0,0,-11.0f/9,-1,0,0,-20.0f/9,0};
 for(unsigned i=0;i<16;i++)assert(fabsf(projection[i]-expected_projection[i])<0.00001f);
 assert(!tribes_gl_get_error(&cpu));tribes_gl_load_identity(&cpu);tribes_gl_matrix_mode(&cpu,0x1700);
 puts("PASS: mobile original double-argument perspective matrix and CPU ABI");

#endif

}
#ifdef GRAPHICS
static uint32_t graphics_expected_error=0x19a5;
static uint32_t gc(unsigned id,const uint32_t *args,size_t count,int returns){
 cpu._esp=0xf0000;memcpy(memory+cpu._esp,args,count*4);_cpu before=cpu;
 tribes_set_last_error(0x19a5);tribes_enter(&cpu,id);
 if(returns)before._eax=cpu._eax;before._esp+=4*count;
 assert(!memcmp(&before,&cpu,sizeof cpu)&&tribes_get_last_error()==graphics_expected_error);return cpu._eax;
}
#define GC(id,ret,...) gc(id,(uint32_t[]){__VA_ARGS__},sizeof((uint32_t[]){__VA_ARGS__})/4,ret)
/* Exercise the original twelve-DWORD stdcall without a current GL context. */
static void project_point(const char *fault){
#if !TARGET_OS_IPHONE
 assert(!SDL_GL_GetCurrentContext());
 const double point[3]={400.0,-861.79132080078125,222.054229736328125};
 uint32_t args[12];memcpy(args,point,sizeof point);
 args[6]=0x3001;args[7]=0x4001;args[8]=0x5001;
 args[9]=0x6001;args[10]=0x6011;args[11]=0x6021;
 for(unsigned sample=0;sample<20;sample++){
  double model[16]={0},projection[16]={0};
  for(unsigned i=0;i<4;i++)model[5*i]=projection[5*i]=1;
  if(sample){
   model[0]=1.5;model[5]=.75;model[10]=-2;model[12]=17;model[13]=-23;model[14]=45;
   projection[0]=.8;projection[5]=1.2;projection[10]=-1.01;projection[14]=-.2;
   if(sample>1){projection[11]=-1;projection[15]=0;}
   if(sample>2)for(unsigned i=0;i<16;i++){
    model[i]=((int)((i*11+sample*7)%31)-15)*.125;
    projection[i]=((int)((i*17+sample*13)%37)-18)*.0625;
   }
  }
  if(sample==19)memset(projection,0,sizeof projection);
  GLint viewport[4]={-17,29,1470,956};double expected[3]={123,456,789},actual[3];
  GLint ok=gluProject(point[0],point[1],point[2],model,projection,viewport,&expected[0],&expected[1],&expected[2]);
  memcpy(memory+args[6],model,sizeof model);memcpy(memory+args[7],projection,sizeof projection);memcpy(memory+args[8],viewport,sizeof viewport);
  memset(memory+0x6000,0xa5,48);
  for(unsigned i=0;i<3;i++){double initial=(i+1)*123; if(i==1)initial=456;if(i==2)initial=789;memcpy(memory+args[9+i],&initial,8);}
  if(fault&&sample==0){
   if(!strcmp(fault,"project-model")){args[6]=0x3fc1;assert(tribes_memory_permissions_set(0x4000,4096,0));}
   if(!strcmp(fault,"project-projection")){args[7]=0x4fc1;assert(tribes_memory_permissions_set(0x5000,4096,0));}
   if(!strcmp(fault,"project-viewport")){args[8]=0x5ff9;assert(tribes_memory_permissions_set(0x6000,4096,0));}
   if(!strcmp(fault,"project-output")){assert(tribes_memory_permissions_set(0x6000,4096,1));}
   if(!strcmp(fault,"project-output-cross")){args[11]=0x6ffc;assert(tribes_memory_permissions_set(0x7000,4096,0));}
   if(!strcmp(fault,"project-overflow"))args[6]=0xffffffc0;
  }
  assert(tribes_memory_permissions_set(0x3000,4096,1));
  assert(gc(0xfffa0750,args,12,1)==(uint32_t)ok);
  assert(tribes_memory_permissions_set(0x3000,4096,3));
  for(unsigned i=0;i<3;i++){
   memcpy(&actual[i],memory+args[9+i],8);
   assert(fabs(actual[i]-expected[i])<=1e-11*fmax(1,fabs(expected[i])));
   assert(memory[args[9+i]-1]==0xa5&&memory[args[9+i]+8]==0xa5);
  }
  assert(!memcmp(memory+args[6],model,sizeof model)&&!memcmp(memory+args[7],projection,sizeof projection)&&!memcmp(memory+args[8],viewport,sizeof viewport));
  if(ok){ /* Output aliases matrix input; all inputs must already be staged. */
   args[9]=args[6];assert(gc(0xfffa0750,args,12,1)==1);double alias;memcpy(&alias,memory+args[9],8);
   assert(fabs(alias-expected[0])<=1e-11*fmax(1,fabs(expected[0])));args[9]=0x6001;
  }else{ /* Failure does not access invalid output pointers. */
   uint32_t failure[12];memcpy(failure,args,sizeof args);failure[9]=failure[10]=failure[11]=0xffffffff;
   assert(!gc(0xfffa0750,failure,12,1));
  }
 }
 puts("GLU projection: 20 native oracle cases, general/orthographic/perspective matrices, zero-W failure, checked unaligned/aliased buffers and stdcall CPU preservation pass");
#else
 (void)fault;
#endif
}
static void guest_pixels(void){
 typedef void (*ReadPixels)(int,int,int,int,unsigned,unsigned,void *);
 ReadPixels read=(ReadPixels)SDL_GL_GetProcAddress("glReadPixels");assert(read);
 GC(0xfffa0070,0,0x8d40,0);
 GC(0xfffa00b0,0,GL_UNPACK_ALIGNMENT,1);GLint alignment=0;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);assert(alignment==1);GC(0xfffa00b0,0,GL_UNPACK_ALIGNMENT,4);
#if !TARGET_OS_IPHONE || defined(TB_GL4ES)
 GC(0xfffa0090,0,GL_BACK);
#else
 strcpy((char *)memory+0x24000,"glDrawBuffer");assert(!CALL(63,0x24000));
#endif
 GC(0xfffa0060,0,0x3e800000,0x3f000000,0x3f400000,0x3f800000);
 GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);GC(0xfffa00a0,0);
 unsigned char pixel[4]={0};read(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
 assert(pixel[0]>=63&&pixel[0]<=65&&pixel[1]>=127&&pixel[1]<=129&&pixel[2]>=190&&pixel[2]<=192&&pixel[3]==255);
 assert(GC(0xfffa0010,1)==0);printf("guest clear + native readback pixel=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
}
static void multitexture(const char *fault){
 const char *names[]={"glActiveTextureARB","glClientActiveTextureARB","glMultiTexCoord2fARB","glMultiTexCoord2fvARB","glMultiTexCoord3fvARB"};
 for(unsigned i=0;i<5;i++){
  strcpy((char *)memory+0x24000,names[i]);
#if TARGET_OS_IPHONE
  graphics_expected_error=i?127:0x19a5;
  assert(GC(0xfffa00c0,1,0x24000)==(i?0:0xfffa00d0));
#else
  assert(GC(0xfffa00c0,1,0x24000)==0xfffa00d0+i*16);
#endif
 }
 graphics_expected_error=0x19a5;
 GC(0xfffa00d0,0,GL_TEXTURE1);GLint active=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);assert(active==GL_TEXTURE1);
 GC(0xfffa00d0,0,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);assert(active==GL_TEXTURE1);
#if !TARGET_OS_IPHONE
 GC(0xfffa00e0,0,GL_TEXTURE1);glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&active);assert(active==GL_TEXTURE1);
 GLfloat v[4];GC(0xfffa00f0,0,GL_TEXTURE1,0x3e800000,0xbf000000);glGetFloatv(GL_CURRENT_TEXTURE_COORDS,v);
 assert(v[0]==.25f&&v[1]==-.5f&&v[2]==0&&v[3]==1);
 GLfloat input[]={-.75f,.125f,2};memcpy(memory+0x3001,input,sizeof input);
 if(fault&&!strcmp(fault,"unreadable-gl-vector")){assert(tribes_memory_permissions_set(0x3000,4096,0));GC(0xfffa0100,0,GL_TEXTURE1,0x3001);assert(0);}
 GC(0xfffa0100,0,GL_TEXTURE1,0x3001);glGetFloatv(GL_CURRENT_TEXTURE_COORDS,v);assert(v[0]==-.75f&&v[1]==.125f&&v[2]==0&&v[3]==1);
 GC(0xfffa0110,0,GL_TEXTURE1,0x3001);glGetFloatv(GL_CURRENT_TEXTURE_COORDS,v);assert(v[0]==-.75f&&v[1]==.125f&&v[2]==2&&v[3]==1);assert(!memcmp(memory+0x3001,input,sizeof input));
 GC(0xfffa00e0,0,GL_TEXTURE0);
#else
 (void)fault;
#endif
 GC(0xfffa00d0,0,GL_TEXTURE0);assert(glGetError()==GL_NO_ERROR);
 SDL_SetError("%s","resolver error sentinel");strcpy((char *)memory+0x24000,"notImplementedGL");graphics_expected_error=127;assert(!GC(0xfffa00c0,1,0x24000));
 graphics_expected_error=87;assert(!GC(0xfffa00c0,1,0));assert(!strcmp(SDL_GetError(),"resolver error sentinel"));graphics_expected_error=0x19a5;
 puts("WGL finite resolution, stdcall ABI, native texture-unit/coordinate state and platform availability verified");
}
static void query_objects(const char *fault){
 GC(0xfffa0130,0,0x8192,GL_NICEST);GLint hint=0;glGetIntegerv(0x8192,&hint);assert(hint==GL_NICEST);
 GC(0xfffa0130,0,0xffffffff,GL_NICEST);assert(glGetError()==GL_INVALID_ENUM);
 GC(0xfffa0130,0,0x8192,GL_DONT_CARE);assert(glGetError()==GL_NO_ERROR);
 strcpy((char *)memory+0x24000,"glGenQueries");
#if TARGET_OS_IPHONE
 (void)fault;graphics_expected_error=127;assert(!GC(0xfffa00c0,1,0x24000));graphics_expected_error=0x19a5;
#else
 assert(GC(0xfffa00c0,1,0x24000)==0xfffa0140);
 if(fault&&!strcmp(fault,"readonly-queries")){assert(tribes_memory_permissions_set(0x3000,4096,1));GC(0xfffa0140,0,32,0x3001);assert(0);}
 if(fault&&!strcmp(fault,"overflow-queries")){GC(0xfffa0140,0,0x40000000,0x3001);assert(0);}
 memset(memory+0x3000,0xa5,130);GC(0xfffa0140,0,0,0xffffffff);assert(memory[0x3001]==0xa5&&glGetError()==GL_NO_ERROR);
 GC(0xfffa0140,0,0xffffffff,0xffffffff);assert(glGetError()==GL_INVALID_VALUE&&memory[0x3001]==0xa5);
 GC(0xfffa0140,0,32,0x3001);GLuint names[32];memcpy(names,memory+0x3001,sizeof names);assert(memory[0x3000]==0xa5&&memory[0x3081]==0xa5);
 for(unsigned i=0;i<32;i++){assert(names[i]&&!glIsQueryARB(names[i]));for(unsigned j=0;j<i;j++)assert(names[i]!=names[j]);}
 glBeginQueryARB(GL_SAMPLES_PASSED_ARB,names[0]);glEndQueryARB(GL_SAMPLES_PASSED_ARB);assert(glIsQueryARB(names[0]));GLuint result=123;glGetQueryObjectuivARB(names[0],GL_QUERY_RESULT_ARB,&result);assert(result==0);
 glDeleteQueriesARB(32,names);assert(!glIsQueryARB(names[0])&&glGetError()==GL_NO_ERROR);
 puts("32 real query names, native query lifecycle, zero/negative counts, unaligned bounded output and hint state verified");
#endif
}
static void texture_objects(const char *fault){
 const char *names[]={"glGenTextures","glBindTexture","glTexParameteri","glTexParameterf","glTexImage2D","glDeleteTextures"};
 for(unsigned i=0;i<6;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa0150+16*i);}
 if(fault&&!strcmp(fault,"readonly-textures")){assert(tribes_memory_permissions_set(0x3000,4096,1));GC(0xfffa0150,0,1,0x3001);assert(0);}
 memset(memory+0x3000,0xa5,10);GC(0xfffa0150,0,0,0xffffffff);assert(glGetError()==GL_NO_ERROR);
 GC(0xfffa0150,0,0xffffffff,0xffffffff);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa0150,0,2,0x3001);GLuint textures[2];memcpy(textures,memory+0x3001,8);assert(textures[0]&&textures[1]&&textures[0]!=textures[1]&&memory[0x3000]==0xa5&&memory[0x3009]==0xa5);
 GC(0xfffa0160,0,GL_TEXTURE_2D,textures[0]);assert(glIsTexture(textures[0]));
 GC(0xfffa0170,0,GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
 GC(0xfffa0180,0,GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,0x46180000); /* 9728.0 = GL_NEAREST */
 GLint parameter=0;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&parameter);assert(parameter==GL_NEAREST);
 GC(0xfffa0170,0,GL_TEXTURE_2D,0xffffffff,0);assert(glGetError()==GL_INVALID_ENUM);
 const unsigned char rgb[]={255,0,0,0,255,0,0,0,255,255,255,0,0,255,255,255,0,255};
 memcpy(memory+0x3feb,rgb,9);memcpy(memory+0x3ff7,rgb+9,9);
 if(fault&&!strcmp(fault,"unreadable-texture")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0190,0,GL_TEXTURE_2D,0,GL_RGB,3,2,0,GL_RGB,GL_UNSIGNED_BYTE,0x3fec);assert(0);}
 if(fault&&!strcmp(fault,"overflow-texture")){GC(0xfffa0190,0,GL_TEXTURE_2D,0,GL_RGB,3,2,0,GL_RGB,GL_UNSIGNED_BYTE,0xfffffffa);assert(0);}
 GC(0xfffa00b0,0,GL_UNPACK_ALIGNMENT,4);
 assert(tribes_memory_permissions_set(0x4000,4096,0));
 GC(0xfffa0190,0,GL_TEXTURE_2D,0,GL_RGB,3,2,0,GL_RGB,GL_UNSIGNED_BYTE,0x3feb);
 assert(tribes_memory_permissions_set(0x4000,4096,3));
 unsigned char pixels[24]={0};
#if TARGET_OS_IPHONE
 GLuint fbo=0;GLint previous=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 glReadPixels(0,0,3,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);glBindFramebuffer(GL_FRAMEBUFFER,previous);glDeleteFramebuffers(1,&fbo);
#else
 glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
 /* Non-default row/skip state reads precisely the selected input rectangle. */
 unsigned char padded[60];memset(padded,0xa5,sizeof padded);memcpy(padded+19,rgb,9);memcpy(padded+35,rgb+9,9);memcpy(memory+0x3101,padded,sizeof padded);
 GC(0xfffa00b0,0,GL_UNPACK_ROW_LENGTH,5);GC(0xfffa00b0,0,GL_UNPACK_SKIP_ROWS,1);GC(0xfffa00b0,0,GL_UNPACK_SKIP_PIXELS,1);
 GC(0xfffa0190,0,GL_TEXTURE_2D,0,GL_RGB,3,2,0,GL_RGB,GL_UNSIGNED_BYTE,0x3101);
 unsigned char second[24];glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,second);assert(!memcmp(pixels,second,24));
 glGetIntegerv(GL_UNPACK_ROW_LENGTH,&parameter);assert(parameter==5);
 GC(0xfffa00b0,0,GL_UNPACK_ROW_LENGTH,0);GC(0xfffa00b0,0,GL_UNPACK_SKIP_ROWS,0);GC(0xfffa00b0,0,GL_UNPACK_SKIP_PIXELS,0);
#endif
 for(unsigned i=0;i<6;i++){assert(!memcmp(pixels+i*4,rgb+i*3,3));assert(pixels[i*4+3]==255);}
 assert(glGetError()==GL_NO_ERROR);
 GC(0xfffa01a0,0,2,0x3001);assert(!glIsTexture(textures[0])&&!glIsTexture(textures[1]));
 puts("Native texture names, parameters, bounded padded RGB upload, pixel readback and deletion verified through guest ABI");
}
/* Independent native readback checks the exact reached 64x64 update and
   a partial rectangle with unreadable skipped/padding pages. */
static void subregion_pixels(unsigned char *pixels){
#if TARGET_OS_IPHONE
 GLuint fbo=0;GLint previous=0,texture=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&previous);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
 glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
 assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
 glBindFramebuffer(GL_FRAMEBUFFER,previous);glDeleteFramebuffers(1,&fbo);
#else
 glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
#endif
 assert(glGetError()==GL_NO_ERROR);
}
static void texture_subregions(const char *fault){
 strcpy((char *)memory+0x24000,"glTexSubImage2D");uint32_t sub=CALL(63,0x24000);assert(sub==0xfffa06c0);
 GC(0xfffa0150,0,1,0x3001);uint32_t texture=word(0x3001);GC(0xfffa0160,0,GL_TEXTURE_2D,texture);
 GC(0xfffa00b0,0,GL_UNPACK_ALIGNMENT,4);
 GC(0xfffa0190,0,GL_TEXTURE_2D,0,GL_RGBA,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
 if(fault&&!strcmp(fault,"subregion-unreadable")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(sub,0,GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,0x3ff9);assert(0);}
 if(fault&&!strcmp(fault,"subregion-overflow")){GC(sub,0,GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,0xfffffff9);assert(0);}
 if(fault&&!strcmp(fault,"subregion-null")){GC(sub,0,GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,0);assert(0);}
#if !TARGET_OS_IPHONE
 if(fault&&!strcmp(fault,"subregion-pbo")){GC(0xfffa01b0,0,1,0x3101);GC(0xfffa01c0,0,GL_PIXEL_UNPACK_BUFFER,word(0x3101));GC(sub,0,GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,0x3101);assert(0);}
 if(fault&&!strcmp(fault,"subregion-stride-overflow")){GC(0xfffa00b0,0,GL_UNPACK_ROW_LENGTH,0x7fffffff);GC(sub,0,GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,0x3101);assert(0);}
#endif
 unsigned char expected[64*64*4],actual[sizeof expected];
 for(unsigned i=0;i<64*64;i++){expected[4*i]=(unsigned char)i;expected[4*i+1]=(unsigned char)(i/64);expected[4*i+2]=(unsigned char)(i*7);expected[4*i+3]=255;}
 memcpy(memory+0x3101,expected,sizeof expected);assert(tribes_memory_permissions_set(0x3000,4096,1));
 GC(sub,0,GL_TEXTURE_2D,0,0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,0x3101);
 assert(tribes_memory_permissions_set(0x3000,4096,3));subregion_pixels(actual);assert(!memcmp(actual,expected,sizeof expected)&&!memcmp(memory+0x3101,expected,sizeof expected));
#if !TARGET_OS_IPHONE
 const unsigned char patch[]={251,3,9,255,17,241,11,255,31,7,229,255,193,83,41,255};
 memcpy(memory+0xa005,patch,8);memcpy(memory+0xc005,patch+8,8);
 GC(0xfffa00b0,0,GL_UNPACK_ALIGNMENT,8);GC(0xfffa00b0,0,GL_UNPACK_ROW_LENGTH,2048);GC(0xfffa00b0,0,GL_UNPACK_SKIP_ROWS,1);GC(0xfffa00b0,0,GL_UNPACK_SKIP_PIXELS,1);
 assert(tribes_memory_permissions_set(0x8000,4096,0)&&tribes_memory_permissions_set(0xb000,4096,0));
 GC(sub,0,GL_TEXTURE_2D,0,7,11,2,2,GL_RGBA,GL_UNSIGNED_BYTE,0x8001);
 assert(tribes_memory_permissions_set(0x8000,4096,3)&&tribes_memory_permissions_set(0xb000,4096,3));
 memcpy(expected+(11*64+7)*4,patch,8);memcpy(expected+(12*64+7)*4,patch+8,8);
 subregion_pixels(actual);assert(!memcmp(actual,expected,sizeof expected)&&!memcmp(memory+0xa005,patch,8)&&!memcmp(memory+0xc005,patch+8,8));
 GLint state=0;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&state);assert(state==2048);
 GC(0xfffa00b0,0,GL_UNPACK_ALIGNMENT,4);GC(0xfffa00b0,0,GL_UNPACK_ROW_LENGTH,0);GC(0xfffa00b0,0,GL_UNPACK_SKIP_ROWS,0);GC(0xfffa00b0,0,GL_UNPACK_SKIP_PIXELS,0);
#endif
 assert(tribes_memory_permissions_set(0x9000,4096,0));GC(sub,0,GL_TEXTURE_2D,0,0,0,0,1,GL_RGBA,GL_UNSIGNED_BYTE,0x9000);assert(!GC(0xfffa0010,1));
 GC(sub,0,GL_TEXTURE_2D,0,0,0,0xffffffff,1,GL_RGBA,GL_UNSIGNED_BYTE,0x9000);assert(GC(0xfffa0010,1)==GL_INVALID_VALUE&&!GC(0xfffa0010,1));assert(tribes_memory_permissions_set(0x9000,4096,3));
 GC(sub,0,GL_TEXTURE_2D,0,0xffffffff,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,0x3101);assert(GC(0xfffa0010,1)==GL_INVALID_VALUE&&!GC(0xfffa0010,1));
 GC(sub,0,GL_TEXTURE_2D,0,64,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,0x3101);assert(GC(0xfffa0010,1)==GL_INVALID_VALUE&&!GC(0xfffa0010,1));
 subregion_pixels(actual);assert(!memcmp(actual,expected,sizeof expected));
 glGetString(0xffffffff);GC(sub,0,GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,0x3101);assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));
 GC(0xfffa01a0,0,1,0x3001);assert(!glIsTexture(texture)&&glGetError()==0);
 puts("2D subregions: original 64x64 RGBA pixels, partial rectangle, unreadable skipped/padding pages, source preservation, native errors and guest ABI pass");
}
static void buffer_objects(const char *fault){
 const char *names[]={"glGenBuffers","glBindBuffer","glBufferData","glDeleteBuffers"};
 for(unsigned i=0;i<4;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa01b0+16*i);}
 if(fault&&!strcmp(fault,"readonly-buffers")){assert(tribes_memory_permissions_set(0x3000,4096,1));GC(0xfffa01b0,0,2,0x3001);assert(0);}
 if(fault&&!strcmp(fault,"overflow-buffers")){GC(0xfffa01b0,0,0x40000000,0x3001);assert(0);}
 memset(memory+0x3000,0xa5,10);GC(0xfffa01b0,0,0,0xffffffff);assert(glGetError()==GL_NO_ERROR&&word(0x3001)==0xa5a5a5a5);
 GC(0xfffa01b0,0,0xffffffff,0xffffffff);assert(glGetError()==GL_INVALID_VALUE&&word(0x3001)==0xa5a5a5a5);
 GC(0xfffa01b0,0,2,0x3001);GLuint ids[2];memcpy(ids,memory+0x3001,8);assert(ids[0]&&ids[1]&&ids[0]!=ids[1]&&memory[0x3000]==0xa5&&memory[0x3009]==0xa5);
 assert(!glIsBuffer(ids[0]));GC(0xfffa01c0,0,GL_ARRAY_BUFFER,ids[0]);assert(glIsBuffer(ids[0]));
 unsigned char payload[37];for(unsigned i=0;i<sizeof payload;i++)payload[i]=(unsigned char)(i*17+3);memcpy(memory+0x3ff1,payload,sizeof payload);
 if(fault&&!strcmp(fault,"unreadable-buffer-data")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa01d0,0,GL_ARRAY_BUFFER,sizeof payload,0x3ff1,GL_STATIC_DRAW);assert(0);}
 if(fault&&!strcmp(fault,"overflow-buffer-data")){GC(0xfffa01d0,0,GL_ARRAY_BUFFER,37,0xfffffff0,GL_STATIC_DRAW);assert(0);}
 GC(0xfffa01d0,0,GL_ARRAY_BUFFER,sizeof payload,0x3ff1,GL_STATIC_DRAW);
 GLint size=-1,usage=-1;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_USAGE,&usage);assert(size==sizeof payload&&usage==GL_STATIC_DRAW&&glGetError()==GL_NO_ERROR);
 assert(!memcmp(payload,memory+0x3ff1,sizeof payload));
#if !TARGET_OS_IPHONE
 unsigned char readback[sizeof payload];glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof readback,readback);assert(!memcmp(readback,payload,sizeof payload)&&glGetError()==GL_NO_ERROR);
#endif
 GC(0xfffa01d0,0,GL_ARRAY_BUFFER,0xffffffff,0xffffffff,GL_STATIC_DRAW);assert(glGetError()==GL_INVALID_VALUE);
 glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);assert(size==sizeof payload);
 GC(0xfffa01d0,0,GL_ARRAY_BUFFER,0,0xffffffff,GL_STATIC_DRAW);assert(glGetError()==GL_NO_ERROR);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);assert(size==0);
 GC(0xfffa01d0,0,GL_ARRAY_BUFFER,0x2000000,0,GL_STATIC_DRAW);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);assert(size==0x2000000&&glGetError()==GL_NO_ERROR);
 GC(0xfffa01d0,0,GL_ARRAY_BUFFER,16,0,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);assert(size==0x2000000);
 /* Legacy Mac copy targets use native storage through an isolated binding;
    core and GLES still retain their native target semantics. */
 int legacy=0;
#if !TARGET_OS_IPHONE
 legacy=!strncmp((char *)glGetString(GL_VERSION),"2.1",3);
#endif
 glBindBuffer(0x8f37,ids[0]);GLenum native_bind=glGetError();GC(0xfffa01c0,0,0x8f37,ids[0]);GLenum guest_bind=glGetError();assert(legacy?(native_bind==GL_INVALID_ENUM&&guest_bind==0):guest_bind==native_bind);
 glBufferData(0x8f37,0x2000000,NULL,GL_STATIC_DRAW);GLenum native_data=glGetError();GC(0xfffa01d0,0,0x8f37,0x2000000,0,GL_STATIC_DRAW);GLenum guest_data=glGetError();assert(legacy?(native_data==GL_INVALID_ENUM&&guest_data==0):guest_data==native_data);
 printf("original COPY_WRITE_BUFFER guest bind error=0x%x storage error=0x%x legacy_adapter=%d\n",guest_bind,guest_data,legacy);
 if(!guest_bind){GLint original_size=-1;glGetBufferParameteriv(legacy?GL_ARRAY_BUFFER:0x8f37,GL_BUFFER_SIZE,&original_size);assert(original_size==0x2000000&&glGetError()==GL_NO_ERROR);GC(0xfffa01c0,0,0x8f37,0);}
 GC(0xfffa01c0,0,GL_ARRAY_BUFFER,0);
 if(fault&&!strcmp(fault,"unreadable-buffer-names")){assert(tribes_memory_permissions_set(0x3000,4096,0));GC(0xfffa01e0,0,2,0x3001);assert(0);}
 GC(0xfffa01e0,0,0,0xffffffff);assert(glGetError()==GL_NO_ERROR);GC(0xfffa01e0,0,0xffffffff,0xffffffff);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa01e0,0,2,0x3001);assert(!glIsBuffer(ids[0])&&!glIsBuffer(ids[1])&&glGetError()==GL_NO_ERROR);
 puts("Native buffer names, upload/storage/lifetime, signed sizes and stdcall CPU/LastError preservation verified");
}
#if !TARGET_OS_IPHONE
static void mipmap_level(int level,int size,const unsigned char *color){
 GLint width=-1,height=-1;glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&width);glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&height);assert(width==size&&height==size);
 if(size){unsigned char pixels[64]={0};assert(size<=4);glGetTexImage(GL_TEXTURE_2D,level,GL_RGBA,GL_UNSIGNED_BYTE,pixels);for(int i=0;i<size*size;i++){if(memcmp(pixels+4*i,color,4))fprintf(stderr,"mipmap level %d pixel %d actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",level,i,pixels[4*i],pixels[4*i+1],pixels[4*i+2],pixels[4*i+3],color[0],color[1],color[2],color[3]);assert(!memcmp(pixels+4*i,color,4));}}
}
static void mipmap_upload(int level,int size,const unsigned char *color){
 for(int i=0;i<16;i++)memcpy(memory+0x3100+4*i,color,4);
 GC(0xfffa0190,0,GL_TEXTURE_2D,level,GL_RGBA,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,0x3100);
}
static void explicit_mipmaps(void){
 const unsigned char red[]={240,20,40,255},blue[]={20,40,240,255};
 strcpy((char *)memory+0x24000,"glGenerateMipmap");uint32_t generate=CALL(63,0x24000);assert(generate==0xfffa05a0);
 assert(!GC(0xfffa0010,1));GC(0xfffa0150,0,1,0x3001);uint32_t texture=word(0x3001);
 GC(0xfffa0160,0,GL_TEXTURE_2D,texture);
 mipmap_upload(0,4,red);mipmap_upload(1,2,blue);mipmap_upload(2,1,blue);
 /* The original texture loader uses base zero and the default level range. */
 GC(generate,0,GL_TEXTURE_2D);assert(!GC(0xfffa0010,1));
 mipmap_level(0,4,red);mipmap_level(1,2,red);mipmap_level(2,1,red);
 glEnable(0xffffffff);GC(generate,0,GL_TEXTURE_2D);
 assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));mipmap_level(2,1,red);
 /* Reupload does not generate automatically; explicit generation updates it. */
 mipmap_upload(0,4,blue);mipmap_level(1,2,red);
 GC(generate,0,0xffffffff);assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));mipmap_level(1,2,red);
 GC(generate,0,GL_TEXTURE_2D);assert(!GC(0xfffa0010,1));
 mipmap_level(0,4,blue);mipmap_level(1,2,blue);mipmap_level(2,1,blue);
 for(int i=0;i<16;i++)memcpy(memory+0x3100+4*i,red,4);
 GC(0xfffa06c0,0,GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,0x3100);mipmap_level(0,4,red);mipmap_level(2,1,blue);
 GC(generate,0,GL_TEXTURE_2D);mipmap_level(2,1,red);
 if(!strncmp((char *)glGetString(GL_VERSION),"2.1",3)){
  GC(0xfffa0170,0,GL_TEXTURE_2D,0x8191,1);for(int i=0;i<16;i++)memcpy(memory+0x3100+4*i,blue,4);
  GC(0xfffa06c0,0,GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,0x3100);mipmap_level(0,4,blue);mipmap_level(1,2,blue);mipmap_level(2,1,blue);
 }
 memcpy(memory+0x3001,&texture,4);GC(0xfffa01a0,0,1,0x3001);assert(!GC(0xfffa0010,1));
 puts("Explicit mipmaps: native levels/pixels, replacement, base retention and preserved errors through finite ABI pass");
}
static void automatic_mipmaps(uint32_t window,uint32_t context){
 const unsigned char red[]={240,20,40,255},blue[]={20,40,240,255};
 assert(!GC(0xfffa0010,1));GC(0xfffa0020,0,0x8192,0x3001);assert(word(0x3001)==GL_DONT_CARE);
 GC(0xfffa0130,0,0x8192,GL_NICEST);GC(0xfffa0020,0,0x8192,0x3001);assert(word(0x3001)==GL_NICEST);
 GC(0xfffa0120,0,0x8192,0x3001);float hint=0;memcpy(&hint,memory+0x3001,4);assert(hint==GL_NICEST);
 GC(0xfffa0130,0,0x8192,0xffffffff);assert(GC(0xfffa0010,1)==GL_INVALID_ENUM);GC(0xfffa0020,0,0x8192,0x3001);assert(word(0x3001)==GL_NICEST);
 GC(0xfffa0150,0,2,0x3001);GLuint names[2];memcpy(names,memory+0x3001,8);
 GC(0xfffa0160,0,GL_TEXTURE_2D,names[0]);GC(0xfffa0170,0,GL_TEXTURE_2D,0x8191,1);
 mipmap_upload(0,4,red);mipmap_level(0,4,red);mipmap_level(1,2,red);mipmap_level(2,1,red);
 for(int i=0;i<16;i++)memcpy(memory+0x3100+4*i,blue,4);
 GC(0xfffa06c0,0,GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,0x3100);mipmap_level(0,4,blue);mipmap_level(1,2,blue);mipmap_level(2,1,blue);
 GC(0xfffa06c0,0,GL_TEXTURE_2D,0,4,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,0x3100);assert(GC(0xfffa0010,1)==GL_INVALID_VALUE&&!GC(0xfffa0010,1));mipmap_level(2,1,blue);
 for(int i=0;i<16;i++)memcpy(memory+0x3100+4*i,red,4);glGetString(0xffffffff);
 GC(0xfffa06c0,0,GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,0x3100);assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));mipmap_level(1,2,red);mipmap_level(2,1,red);
 /* A non-base upload must retain its own pixels and not regenerate others. */
 mipmap_upload(1,2,blue);mipmap_level(1,2,blue);mipmap_level(2,1,red);
 /* Invalid upload must neither lose the native error nor overwrite level 1. */
 GC(0xfffa0190,0,GL_TEXTURE_2D,0,GL_RGBA,0xffffffff,4,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
 assert(GC(0xfffa0010,1)==GL_INVALID_VALUE&&!GC(0xfffa0010,1));mipmap_level(1,2,blue);
 glGetString(0xffffffff);mipmap_upload(0,4,red);assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));mipmap_level(1,2,red);
 /* The flag is per object; another texture starts disabled. */
 GC(0xfffa0160,0,GL_TEXTURE_2D,names[1]);mipmap_upload(0,4,blue);mipmap_level(1,0,blue);
 GC(0xfffa0180,0,GL_TEXTURE_2D,0x8191,0x3f800000);
 mipmap_upload(0,4,blue);assert(!GC(0xfffa0010,1));mipmap_level(1,2,blue);mipmap_level(2,1,blue);mipmap_level(0,4,blue);
 /* Shared contexts see the texture flag, but keep hint/error state separate. */
 assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,1));uint32_t shared=CALL(47,window);assert(shared&&shared!=context);assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0));
 GC(0xfffa0020,0,0x8192,0x3001);assert(word(0x3001)==GL_DONT_CARE);
 GC(0xfffa0160,0,GL_TEXTURE_2D,names[0]);mipmap_upload(0,4,blue);mipmap_level(1,2,blue);
 GC(0xfffa0180,0,GL_TEXTURE_2D,0x8191,0);glFinish();assert(CALL(49,window,context));GC(0xfffa0160,0,GL_TEXTURE_2D,names[1]);mipmap_upload(0,4,red);assert(!GC(0xfffa0010,1));mipmap_level(2,1,red);
 GC(0xfffa0160,0,GL_TEXTURE_2D,names[0]);mipmap_upload(0,4,red);mipmap_level(1,2,blue);
 assert(CALL(48,shared));assert(!GC(0xfffa0010,1));
 /* Delete through the guest API; a newly generated object starts disabled. */
 memcpy(memory+0x3001,names,8);GC(0xfffa01a0,0,2,0x3001);GC(0xfffa0150,0,1,0x3001);GC(0xfffa0160,0,GL_TEXTURE_2D,word(0x3001));mipmap_upload(0,4,red);mipmap_level(1,0,red);GC(0xfffa01a0,0,1,0x3001);
 assert(!GC(0xfffa0010,1));
 puts("Core automatic mipmaps: native level dimensions/pixels, non-base preservation, object/share lifetime, disabling, failed uploads and preserved native errors pass");
}
#endif
/* GL_MAX_LIGHTS is scalar. Modern profiles may reject it; preserve both the
   seeded output and the actual native error instead of inventing a limit. */
static void light_limit_query(void){
 while(glGetError()!=GL_NO_ERROR){}
 GLint expected=0x13579bdf;glGetIntegerv(0x0d31,&expected);GLenum error=glGetError();
 uint32_t seed=0x13579bdf;memcpy(memory+0x3001,&seed,4);memory[0x3000]=0xa5;memory[0x3005]=0x5a;
 GC(0xfffa0020,0,0x0d31,0x3001);assert(word(0x3001)==(uint32_t)expected&&GC(0xfffa0010,1)==error&&!GC(0xfffa0010,1));
 assert(memory[0x3000]==0xa5&&memory[0x3005]==0x5a);
 printf("GL_MAX_LIGHTS: exact native value=%d error=0x%x; output extent preserved\n",expected,error);
}
#if !TARGET_OS_IPHONE
static void quad_program(void){
 const char *vs="#version 120\nvoid main(){gl_Position=gl_Vertex;}";
 const char *fs="#version 120\nuniform ivec2 u2; uniform ivec4 u4; void main(){gl_FragColor=vec4(vec2(u2),0.0,1.0)+vec4(u4);}";
 GLuint shaders[]={glCreateShader(GL_VERTEX_SHADER),glCreateShader(GL_FRAGMENT_SHADER)};const char *sources[]={vs,fs};
 for(unsigned i=0;i<2;i++){assert(shaders[i]);glShaderSource(shaders[i],1,&sources[i],NULL);glCompileShader(shaders[i]);GLint ok=0;glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&ok);assert(ok);}
 GLuint program=glCreateProgram();assert(program);glAttachShader(program,shaders[0]);glAttachShader(program,shaders[1]);glLinkProgram(program);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);assert(ok);
 GLint u2=glGetUniformLocation(program,"u2"),u4=glGetUniformLocation(program,"u4");assert(u2>=0&&u4>=0);
 GC(0xfffa0320,0,program);GC(0xfffa0330,0,(uint32_t)u2,3,(uint32_t)-7);GC(0xfffa0340,0,(uint32_t)u4,11,22,(uint32_t)-33,44);
 GLint values[4]={0};glGetUniformiv(program,u2,values);assert(values[0]==3&&values[1]==-7);glGetUniformiv(program,u4,values);assert(values[0]==11&&values[1]==22&&values[2]==-33&&values[3]==44&&glGetError()==0);
 GC(0xfffa0330,0,0xffffffff,0,0);assert(glGetError()==0);GC(0xfffa0330,0,0x7fffffff,0,0);assert(glGetError()==GL_INVALID_OPERATION);
 GLuint unlinked=glCreateProgram();GC(0xfffa0320,0,unlinked);assert(glGetError()==GL_INVALID_OPERATION);glGetIntegerv(GL_CURRENT_PROGRAM,&ok);assert((GLuint)ok==program);
 GC(0xfffa0320,0,0);glDeleteProgram(unlinked);glDeleteProgram(program);glDeleteShader(shaders[0]);glDeleteShader(shaders[1]);assert(glGetError()==0);
}


#include "original-shader-fixture.h"
static void original_shader_variants(void){
 for(unsigned i=0;i<sizeof original_shaders/sizeof *original_shaders;i++){
  GLuint program=GC(0xfffa0400,1);const char *sources[]={original_shaders[i].vertex,original_shaders[i].fragment};
  for(unsigned j=0;j<2;j++){
   GLuint shader=GC(0xfffa03a0,1,j?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);size_t n=strlen(sources[j]);assert(n<0x6000);memcpy(memory+0x10000,sources[j],n+1);uint32_t pointer=0x10000;memcpy(memory+0x7800,&pointer,4);
   GC(0xfffa03b0,0,shader,1,0x7800,0);assert(!memcmp(memory+0x10000,sources[j],n+1));GC(0xfffa03c0,0,shader);GC(0xfffa03d0,0,shader,GL_COMPILE_STATUS,0x3001);
   if(!word(0x3001)){GC(0xfffa03e0,0,shader,4096,0,0x18000);fprintf(stderr,"original %s %u: %s\n",original_shaders[i].name,j,memory+0x18000);}assert(word(0x3001));
   GC(0xfffa0410,0,program,shader);GC(0xfffa03f0,0,shader);
  }
  GC(0xfffa0420,0,program);GC(0xfffa0430,0,program,GL_LINK_STATUS,0x3001);assert(word(0x3001));
  if(!strcmp(original_shaders[i].name,"gui")){
   /* Draw with the actual original GUI program, native fixed-function inputs,
      guest uniform setters and an independently checked solid texture. */
   GC(0xfffa0320,0,program);
   strcpy((char *)memory+0x8000,"uiScale");uint32_t scale=GC(0xfffa0480,1,program,0x8000);GC(0xfffa04b0,0,scale,0x40000000);
   strcpy((char *)memory+0x8000,"texRes");uint32_t res=GC(0xfffa0480,1,program,0x8000);GC(0xfffa0330,0,res,2,2);
   strcpy((char *)memory+0x8000,"srcRect");uint32_t rect=GC(0xfffa0480,1,program,0x8000);GC(0xfffa0340,0,rect,0,0,2,2);
   strcpy((char *)memory+0x8000,"colorTex");uint32_t texloc=GC(0xfffa0480,1,program,0x8000);GC(0xfffa04a0,0,texloc,0);
   GLuint tex=0;glGenTextures(1,&tex);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);unsigned char data[16]={0,255,0,255,0,255,0,255,0,255,0,255,0,255,0,255};glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
   glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();glColor4ub(255,255,255,255);glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_QUADS);glTexCoord4f(0,0,0,0);glVertex2f(-1,-1);glTexCoord4f(1,0,1,0);glVertex2f(1,-1);glTexCoord4f(1,1,1,1);glVertex2f(1,1);glTexCoord4f(0,1,0,1);glVertex2f(-1,1);glEnd();unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==255&&pixel[2]==0&&pixel[3]==255);
   /* Original ONE blend requires premultiplied output even for mode zero.
      Test transparent bright RGB and fractional texture/vertex alpha against
      independent blend arithmetic. The no-define control must reproduce the
      observed bright-pixel failure, not accidentally pass as transparent. */
   int premultiply = strstr(sources[1], "#define PREMULTIPLY_ALPHA") != NULL;
   glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
   for(unsigned a=0;a<3;a++){
    unsigned char alpha[]={0,128,255};
    for(unsigned k=0;k<4;k++){data[4*k]=200;data[4*k+1]=100;data[4*k+2]=50;data[4*k+3]=alpha[a];}
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glClearColor(16.0f/255,32.0f/255,48.0f/255,1);glClear(GL_COLOR_BUFFER_BIT);glColor4ub(255,255,255,128);
    glBegin(GL_QUADS);glTexCoord4f(0,0,0,0);glVertex2f(-1,-1);glTexCoord4f(1,0,1,0);glVertex2f(1,-1);glTexCoord4f(1,1,1,1);glVertex2f(1,1);glTexCoord4f(0,1,0,1);glVertex2f(-1,1);glEnd();
    glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    float opacity=(alpha[a]/255.0f)*(128.0f/255.0f);unsigned char bg[]={16,32,48};
    for(unsigned c=0;c<3;c++){int expected=(int)lroundf(data[c]*(premultiply?opacity:1)+bg[c]*(1-opacity));int delta=(int)pixel[c]-expected;assert(delta>=-2&&delta<=2);}
    assert(pixel[3]==255);
   }
   glDisable(GL_BLEND);glBlendFunc(GL_ONE,GL_ZERO);glClearColor(0,0,0,0);glColor4ub(255,255,255,255);
   GC(0xfffa0320,0,0);glDeleteTextures(1,&tex);
  }
  GC(0xfffa0450,0,program);assert(glGetError()==0);
 }
 /* A body edit must not be silently substituted with a known template. */
 GLuint changed=GC(0xfffa03a0,1,GL_VERTEX_SHADER);strcpy((char *)memory+0x10000,original_shaders[0].vertex);strcat((char *)memory+0x10000,"\n// edited source\n");uint32_t pointer=0x10000;memcpy(memory+0x7800,&pointer,4);GC(0xfffa03b0,0,changed,1,0x7800,0);GC(0xfffa03c0,0,changed);GC(0xfffa03d0,0,changed,GL_COMPILE_STATUS,0x3001);assert(!word(0x3001));GC(0xfffa03f0,0,changed);
 puts("original shader dialect: 12 injected variants across 6 pairs compile/link, GUI opaque/transparent/fractional pixels including define zero, source preservation and edited-source rejection PASS");
}
static void shader_lifecycle(const char *fault){
 const uint32_t create=0xfffa03a0,source=0xfffa03b0,compile=0xfffa03c0,shaderiv=0xfffa03d0,shaderlog=0xfffa03e0;
 GC(0xfffa0370,0,0,0,0,0x3ff00000);GLdouble depth[2]={-1,-1};glGetDoublev(GL_DEPTH_RANGE,depth);assert(depth[0]==0&&depth[1]==1);
 GC(0xfffa0370,0,0,0xbff00000,0,0x40000000);glGetDoublev(GL_DEPTH_RANGE,depth);assert(depth[0]==0&&depth[1]==1);
 GC(0xfffa0380,0,0,0x3fe00000);glGetDoublev(GL_DEPTH_CLEAR_VALUE,depth);assert(depth[0]==0.5);GC(0xfffa0380,0,0,0x3ff00000);
 GC(0xfffa0390,0,GL_LEQUAL);GLint function=0;glGetIntegerv(GL_DEPTH_FUNC,&function);assert(function==GL_LEQUAL);GC(0xfffa0390,0,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);GC(0xfffa0390,0,GL_LESS);
 GLuint vertex=GC(create,1,GL_VERTEX_SHADER),fragment=GC(create,1,GL_FRAGMENT_SHADER);assert(vertex&&fragment);
 const char *head="#version 120\n";const char *body="attribute vec2 position;uniform float scale;void main(){gl_Position=vec4(position*scale,0.,1.);}";
 memcpy(memory+0x8000,head,strlen(head));strcpy((char *)memory+0x8200,body);
 uint32_t addresses[]={0x8000,0x8200};int32_t sizes[]={(int32_t)strlen(head),-1};memcpy(memory+0x7800,addresses,8);memcpy(memory+0x7900,sizes,8);
 if(fault&&!strcmp(fault,"shader-pointers")){assert(tribes_memory_permissions_set(0x7000,4096,0));GC(source,0,vertex,2,0x7800,0x7900);assert(0);}
 if(fault&&!strcmp(fault,"shader-lengths")){assert(tribes_memory_permissions_set(0x9000,4096,0));GC(source,0,vertex,2,0x7800,0x9000);assert(0);}
 if(fault&&!strcmp(fault,"shader-string")){assert(tribes_memory_permissions_set(0x8000,4096,0));GC(source,0,vertex,2,0x7800,0x7900);assert(0);}
 GC(source,0,vertex,2,0x7800,0x7900);memset(memory+0x8200,'?',strlen(body));
 char native_source[512]={0};GLsizei n=0;glGetShaderSource(vertex,sizeof native_source,&n,native_source);GLuint reference=glCreateShader(GL_VERTEX_SHADER);const char *reference_parts[]={head,body};glShaderSource(reference,2,reference_parts,sizes);char reference_source[512]={0};GLsizei reference_n=0;glGetShaderSource(reference,sizeof reference_source,&reference_n,reference_source);assert(n==reference_n&&!strcmp(native_source,reference_source));glDeleteShader(reference);
 GC(compile,0,vertex);GC(shaderiv,0,vertex,GL_COMPILE_STATUS,0x3001);assert(word(0x3001)==1);
 if(fault&&!strcmp(fault,"shader-status")){assert(tribes_memory_permissions_set(0x3000,4096,1));GC(shaderiv,0,vertex,GL_COMPILE_STATUS,0x3001);assert(0);}
 const char *frag="#version 120\nuniform int shade;void main(){gl_FragColor=shade==7?vec4(0.,1.,0.,1.):vec4(1.,0.,0.,1.);}";
 strcpy((char *)memory+0x8000,frag);uint32_t at=0x8000;memcpy(memory+0x7800,&at,4);GC(source,0,fragment,1,0x7800,0);GC(compile,0,fragment);GC(shaderiv,0,fragment,GL_COMPILE_STATUS,0x3001);assert(word(0x3001)==1);
 GLuint program=GC(0xfffa0400,1);assert(program);GC(0xfffa0410,0,program,vertex);GC(0xfffa0410,0,program,fragment);GC(0xfffa0420,0,program);GC(0xfffa0430,0,program,GL_LINK_STATUS,0x3001);assert(word(0x3001)==1);
 GC(0xfffa0320,0,program);strcpy((char *)memory+0x8000,"shade");GLint shade=(GLint)GC(0xfffa0480,1,program,0x8000);assert(shade>=0);GC(0xfffa04a0,0,(uint32_t)shade,7);GLint iv=0;glGetUniformiv(program,shade,&iv);assert(iv==7);
 strcpy((char *)memory+0x8000,"scale");GLint scale=(GLint)GC(0xfffa0480,1,program,0x8000);assert(scale>=0);GC(0xfffa04b0,0,(uint32_t)scale,0x3f800000);GLfloat fv=0;glGetUniformfv(program,scale,&fv);assert(fv==1);
 strcpy((char *)memory+0x8000,"position");GLint pos=(GLint)GC(0xfffa0490,1,program,0x8000);assert(pos>=0);
 /* Vector-array upload preserves exact values and only consumes count*16 bytes. */
 GLuint vectors=glCreateShader(GL_VERTEX_SHADER);const char *vector_source="#version 120\nuniform vec4 offsets[2];void main(){gl_Position=offsets[0]+offsets[1];}";glShaderSource(vectors,1,&vector_source,NULL);glCompileShader(vectors);
 GLuint vp=glCreateProgram();glAttachShader(vp,vectors);glAttachShader(vp,fragment);glLinkProgram(vp);GLint linked=0;glGetProgramiv(vp,GL_LINK_STATUS,&linked);assert(linked);GC(0xfffa0320,0,vp);GLint location=glGetUniformLocation(vp,"offsets[0]");assert(location>=0);
 float offsets[8]={2,2,-1,-1,.25f,.5f,.75f,1};memcpy(memory+0x8fe0,offsets,32);
 if(fault&&!strcmp(fault,"uniform-vector")){assert(tribes_memory_permissions_set(0x9000,4096,0));GC(0xfffa04c0,0,location,2,0x8ff0);assert(0);}
 if(fault&&!strcmp(fault,"uniform-overflow")){GC(0xfffa04c0,0,location,0x10000000,0);assert(0);}
 assert(tribes_memory_permissions_set(0x9000,4096,0));GC(0xfffa04c0,0,location,2,0x8fe0);memset(memory+0x8fe0,0,32);
 GLfloat actual[4];glGetUniformfv(vp,location,actual);assert(!memcmp(actual,offsets,16));glGetUniformfv(vp,glGetUniformLocation(vp,"offsets[1]"),actual);assert(!memcmp(actual,offsets+4,16));
 GC(0xfffa04c0,0,UINT32_MAX,1,0x9000);GC(0xfffa04c0,0,location,0,0x9000);assert(glGetError()==0);GC(0xfffa04c0,0,location,UINT32_MAX,0x9000);assert(glGetError()==GL_INVALID_VALUE);assert(tribes_memory_permissions_set(0x9000,4096,3));
 GC(0xfffa0320,0,program);glDeleteProgram(vp);glDeleteShader(vectors);
 /* Actual pixels from a program created/compiled/linked solely via guest ABI. */
 const GLfloat vertices[]={-1,-1,1,-1,0,1};glBindBuffer(GL_ARRAY_BUFFER,0);glVertexAttribPointer((GLuint)pos,2,GL_FLOAT,GL_FALSE,0,vertices);glEnableVertexAttribArray((GLuint)pos);
 glViewport(0,0,64,64);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==255&&pixel[2]==0&&pixel[3]==255);glDisableVertexAttribArray((GLuint)pos);glVertexAttribPointer((GLuint)pos,2,GL_FLOAT,GL_FALSE,0,NULL);
 GC(0xfffa0470,0,program,0,64,0x3001,0x3011,0x3021,0x8100);assert(word(0x3001)==8&&word(0x3011)==1&&word(0x3021)==GL_FLOAT_VEC2&&!strcmp((char *)memory+0x8100,"position"));
 GC(0xfffa0430,0,program,GL_ACTIVE_UNIFORMS,0x3001);uint32_t uniforms=word(0x3001);assert(uniforms==2);
 for(uint32_t i=0;i<uniforms;i++){GC(0xfffa0460,0,program,i,64,0x3001,0x3011,0x3021,0x8100);assert(word(0x3011)==1&&(!strcmp((char *)memory+0x8100,"shade")||!strcmp((char *)memory+0x8100,"scale")));}
 if(fault&&!strcmp(fault,"shader-active")){assert(tribes_memory_permissions_set(0x8000,4096,1));GC(0xfffa0460,0,program,0,64,0x3001,0x3011,0x3021,0x8100);assert(0);}
 strcpy((char *)memory+0x8000,"missingUniform");assert(GC(0xfffa0480,1,program,0x8000)==UINT32_MAX&&glGetError()==0);
 /* Real compile failure and log, including truncation and error preservation. */
 GLuint bad=GC(create,1,GL_VERTEX_SHADER);strcpy((char *)memory+0x8000,"#version 120\nthis is not valid shader source");GC(source,0,bad,1,0x7800,0);GC(compile,0,bad);GC(shaderiv,0,bad,GL_COMPILE_STATUS,0x3001);assert(word(0x3001)==0);GC(shaderiv,0,bad,GL_INFO_LOG_LENGTH,0x3001);assert(word(0x3001)>1);
 memset(memory+0x8100,0x5a,512);GC(shaderlog,0,bad,512,0x3001,0x8100);assert(word(0x3001)>0&&memory[0x8100+word(0x3001)]==0);
 if(fault&&!strcmp(fault,"shader-log")){assert(tribes_memory_permissions_set(0x9000,4096,0));GC(shaderlog,0,bad,64,0x3001,0x8fe0);assert(0);}
 memset(memory+0x8100,0x5a,32);GC(shaderlog,0,bad,8,0,0x8100);assert(memory[0x8107]==0&&memory[0x8108]==0x5a);
 uint32_t sentinel=0x12345678;memcpy(memory+0x3001,&sentinel,4);GC(shaderiv,0,bad,0xffffffff,0x3001);assert(word(0x3001)==sentinel&&glGetError()==GL_INVALID_ENUM);
 GC(source,0,bad,UINT32_MAX,0,0);assert(glGetError()==GL_INVALID_VALUE);GC(shaderlog,0,bad,UINT32_MAX,0,0);assert(glGetError()==GL_INVALID_VALUE);
 GLuint unlinked=GC(0xfffa0400,1);GC(0xfffa0410,0,unlinked,bad);GC(0xfffa0420,0,unlinked);GC(0xfffa0430,0,unlinked,GL_LINK_STATUS,0x3001);assert(!word(0x3001));GC(0xfffa0440,0,unlinked,512,0x3001,0x8100);assert(word(0x3001)>0);
 GC(0xfffa03f0,0,vertex);assert(glIsShader(vertex));GC(0xfffa0450,0,program);assert(glIsProgram(program));GC(0xfffa0320,0,0);assert(!glIsProgram(program)&&!glIsShader(vertex));GC(0xfffa03f0,0,fragment);GC(0xfffa0450,0,unlinked);GC(0xfffa03f0,0,bad);assert(glGetError()==0);
 original_shader_variants();
 puts("guest shader lifecycle: staged multi-string source, native compile/link/logs/reflection/uniforms/deferred deletion and green pixel PASS");
}

static void quad_draw(const char *fault){
 const char *names[]={"glActiveTexture","glTexEnvi","glBlendFuncSeparate","glColor4ub","glEnableClientState","glDisableClientState","glVertexPointer","glTexCoordPointer","glUseProgram","glUniform2i","glUniform4i","glDrawElements"};
 for(unsigned i=0;i<12;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa02a0+16*i);}
 GC(0xfffa0240,0,GL_PROJECTION);GC(0xfffa0250,0);GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0250,0);GC(0xfffa0270,0,0,0,64,64);
 GC(0xfffa02a0,0,GL_TEXTURE0);GC(0xfffa00e0,0,GL_TEXTURE0);GC(0xfffa0320,0,0);
 GC(0xfffa0150,0,1,0x3001);GLuint texture=word(0x3001);GC(0xfffa0160,0,GL_TEXTURE_2D,texture);
 const unsigned char colors[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255};memcpy(memory+0x3100,colors,sizeof colors);
 GC(0xfffa0170,0,GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);GC(0xfffa0170,0,GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 GC(0xfffa0190,0,GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,0x3100);
 GC(0xfffa02b0,0,GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);GC(0xfffa02c0,0,GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
 GC(0xfffa0290,0,GL_TEXTURE_2D);GC(0xfffa0290,0,GL_BLEND);GC(0xfffa0280,0,GL_DEPTH_TEST);GC(0xfffa02d0,0,0x1ff,255,255,255);
 const GLfloat vertices[]={-1,-1,0,0,0,1,123,456, 1,-1,1,0,0,1,123,456, 1,1,1,1,0,1,123,456, -1,1,0,1,0,1,123,456};
 const uint16_t indices[]={0,1,2,0,2,3};memset(memory+0x5f80,0,sizeof vertices);memcpy(memory+0x7001,indices,sizeof indices);
 GC(0xfffa01c0,0,GL_ARRAY_BUFFER,0);GC(0xfffa0300,0,2,GL_FLOAT,32,0x5f80);GC(0xfffa0310,0,4,GL_FLOAT,32,0x5f88);
 GC(0xfffa02e0,0,GL_VERTEX_ARRAY);GC(0xfffa02e0,0,GL_TEXTURE_COORD_ARRAY);
 GC(0xfffa00e0,0,GL_TEXTURE1);GC(0xfffa0310,0,4,GL_FLOAT,32,0x5f88);GC(0xfffa02e0,0,GL_TEXTURE_COORD_ARRAY);
 memcpy(memory+0x5f80,vertices,sizeof vertices);assert(tribes_memory_permissions_set(0x6000,4096,0));
 if(fault&&!strcmp(fault,"quad-indices")){assert(tribes_memory_permissions_set(0x7000,4096,0));GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);assert(0);}
 if(fault&&!strcmp(fault,"quad-vertices")){assert(tribes_memory_permissions_set(0x5000,4096,0));GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);assert(0);}
 if(fault&&!strcmp(fault,"quad-texcoord-range")){GC(0xfffa0310,0,4,GL_FLOAT,32,0x5f98);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);assert(0);}
 GC(0xfffa0300,0,1,GL_FLOAT,32,0x5f80);assert(glGetError()==GL_INVALID_VALUE);GC(0xfffa0310,0,4,GL_FLOAT,0xffffffff,0x5f88);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa01b0,0,1,0x3011);GLuint buffer=word(0x3011);GC(0xfffa01c0,0,GL_ARRAY_BUFFER,buffer);
 if(fault&&!strcmp(fault,"quad-vbo-pointer")){GC(0xfffa0300,0,2,GL_FLOAT,32,0);assert(0);}
 if(fault&&!strcmp(fault,"quad-element-buffer")){GC(0xfffa01c0,0,GL_ELEMENT_ARRAY_BUFFER,buffer);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0);assert(0);}
 GC(0xfffa0060,0,0,0,0,0x3f800000);GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);
 for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++){unsigned char px[4];glReadPixels(16+32*x,16+32*y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);assert(!memcmp(px,colors+4*(2*y+x),4));}
 GLint state=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&state);assert((GLuint)state==buffer);glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&state);assert(state==GL_TEXTURE1);assert(glGetError()==0);
 assert(!memcmp(memory+0x5f80,vertices,sizeof vertices)&&!memcmp(memory+0x7001,indices,sizeof indices));
 /* Every draw reads the current guest data, not a setter-time snapshot. */
 for(unsigned i=0;i<4;i++){GLfloat offscreen=4;memcpy(memory+0x5f80+32*i,&offscreen,4);}
 GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==0);
 memcpy(memory+0x5f80,vertices,sizeof vertices);GC(0xfffa0350,0,GL_TRIANGLES,0,GL_UNSIGNED_SHORT,0xffffffff);assert(glGetError()==0);GC(0xfffa0350,0,GL_TRIANGLES,0xffffffff,GL_UNSIGNED_SHORT,0xffffffff);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa02f0,0,GL_TEXTURE_COORD_ARRAY);GC(0xfffa00e0,0,GL_TEXTURE0);GC(0xfffa02f0,0,GL_TEXTURE_COORD_ARRAY);GC(0xfffa0280,0,GL_TEXTURE_2D);GC(0xfffa02d0,0,0,0,255,255);
 GC(0xfffa01c0,0,GL_ARRAY_BUFFER,0);GC(0xfffa0310,0,4,GL_FLOAT,32,0xfffffffc);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==255&&glGetError()==0);
 /* Original UI uses normalized RGBA bytes interleaved at stride24. */
 strcpy((char *)memory+0x24000,"glColorPointer");assert(CALL(63,0x24000)==0xfffa0580);
 unsigned char color_bytes[96];memset(color_bytes,0xa5,sizeof color_bytes);
 for(unsigned i=0;i<4;i++){color_bytes[24*i]=255;color_bytes[24*i+1]=0;color_bytes[24*i+2]=0;color_bytes[24*i+3]=255;}
 memcpy(memory+0x9101,color_bytes,sizeof color_bytes);GC(0xfffa0580,0,4,GL_UNSIGNED_BYTE,24,0x9101);GC(0xfffa02e0,0,GL_COLOR_ARRAY);
 for(unsigned i=0;i<4;i++){color_bytes[24*i]=0;color_bytes[24*i+1]=255;}memcpy(memory+0x9101,color_bytes,sizeof color_bytes);
 if(fault&&!strcmp(fault,"quad-color-unreadable")){assert(tribes_memory_permissions_set(0x9000,4096,0));GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);assert(0);}
 if(fault&&!strcmp(fault,"quad-color-range")){GC(0xfffa0580,0,4,GL_UNSIGNED_BYTE,24,0x5ffc);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);assert(0);}
 GC(0xfffa0580,0,2,GL_UNSIGNED_BYTE,24,0xffffffff);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa0580,0,4,GL_BOOL,24,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);
 GC(0xfffa0580,0,4,GL_UNSIGNED_BYTE,0xffffffff,0xffffffff);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);
 glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==255&&pixel[2]==0&&pixel[3]==255&&glGetError()==0);
 void *color_pointer=(void *)1;glGetPointerv(GL_COLOR_ARRAY_POINTER,&color_pointer);assert(color_pointer==NULL);
 assert(!memcmp(memory+0x9101,color_bytes,sizeof color_bytes));
 GC(0xfffa02f0,0,GL_COLOR_ARRAY);GC(0xfffa0580,0,4,GL_UNSIGNED_BYTE,24,0xfffffffc);GC(0xfffa02d0,0,0,0,255,255);
 GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0x7001);glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==255&&glGetError()==0);
 GC(0xfffa02f0,0,GL_VERTEX_ARRAY);GC(0xfffa0280,0,GL_BLEND);GC(0xfffa02d0,0,255,255,255,255);
 GC(0xfffa01e0,0,1,0x3011);GC(0xfffa01a0,0,1,0x3001);assert(tribes_memory_permissions_set(0x6000,4096,3));quad_program();
 puts("guest textured quad: four native pixel colors, draw-time guest updates, two client units, normalized color arrays, binding restoration, disabled-array no-read and real shader uniforms PASS");
}
static void texture_array(const char *fault){
 const uint32_t image=0xfffa04d0,sub=0xfffa04e0;const GLenum target=GL_TEXTURE_2D_ARRAY;
 GC(0xfffa0150,0,1,0x3001);GLuint name=word(0x3001);assert(name);GC(0xfffa0160,0,target,name);
 /* Exact original font allocation: native array storage, no guest pixel input. */
 GC(image,0,target,0,GL_RED,256,256,256,0,GL_RED,GL_UNSIGNED_BYTE,0);assert(glGetError()==0);GLint size=0;
 glGetTexLevelParameteriv(target,0,GL_TEXTURE_WIDTH,&size);assert(size==256);glGetTexLevelParameteriv(target,0,GL_TEXTURE_DEPTH,&size);assert(size==256);
 glGetTexLevelParameteriv(target,0,GL_TEXTURE_RED_SIZE,&size);assert(size==8);
 /* Nontrivial row/image skips and alignment. Native compact readback is the oracle. */
 glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,5);glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,3);glPixelStorei(GL_UNPACK_SKIP_IMAGES,1);glPixelStorei(GL_UNPACK_SKIP_ROWS,1);glPixelStorei(GL_UNPACK_SKIP_PIXELS,1);
 unsigned char expected[12];for(unsigned z=0;z<2;z++)for(unsigned y=0;y<2;y++)for(unsigned x=0;x<3;x++)memory[0x8001+33+z*24+y*8+x]=expected[z*6+y*3+x]=(unsigned char)(1+z*60+y*10+x);
 if(fault&&!strcmp(fault,"array-pixels")){assert(tribes_memory_permissions_set(0x8000,4096,0));GC(image,0,target,0,GL_RED,3,2,2,0,GL_RED,GL_UNSIGNED_BYTE,0x8001);assert(0);}
 if(fault&&!strcmp(fault,"array-overflow")){glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,INT32_MAX);GC(image,0,target,0,GL_RED,3,2,2,0,GL_RED,GL_UNSIGNED_BYTE,0x8001);assert(0);}
 GC(image,0,target,0,GL_RED,3,2,2,0,GL_RED,GL_UNSIGNED_BYTE,0x8001);assert(glGetError()==0);memset(memory+0x8001,0,100);
 glPixelStorei(GL_PACK_ALIGNMENT,1);unsigned char actual[16]={0};glGetTexImage(target,0,GL_RED,GL_UNSIGNED_BYTE,actual);assert(!memcmp(actual,expected,12));
 memory[0x8001+33]=200;memory[0x8001+41]=201;
 GC(sub,0,target,0,1,0,1,1,2,1,GL_RED,GL_UNSIGNED_BYTE,0x8001);expected[7]=200;expected[10]=201;glGetTexImage(target,0,GL_RED,GL_UNSIGNED_BYTE,actual);assert(!memcmp(actual,expected,12));
 glPixelStorei(GL_UNPACK_SKIP_IMAGES,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_ROW_LENGTH,4096);glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,2);
 memory[0x8000]=7;memory[0xa000]=9;assert(tribes_memory_permissions_set(0x9000,4096,0));
 GC(image,0,target,0,GL_RED,1,1,2,0,GL_RED,GL_UNSIGNED_BYTE,0x8000);glGetTexImage(target,0,GL_RED,GL_UNSIGNED_BYTE,actual);assert(actual[0]==7&&actual[1]==9);
 if(fault&&!strcmp(fault,"array-sub-pixels")){GC(sub,0,target,0,0,0,0,1,1,1,GL_RED,GL_UNSIGNED_BYTE,0x9000);assert(0);}
 glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,0);
 GC(image,0,target,0,GL_RED,UINT32_MAX,1,1,0,GL_RED,GL_UNSIGNED_BYTE,0x9000);assert(glGetError()==GL_INVALID_VALUE);
 GC(sub,0,target,0,0,0,0,0,1,1,GL_RED,GL_UNSIGNED_BYTE,0x9000);assert(glGetError()==0);assert(tribes_memory_permissions_set(0x9000,4096,3));
 /* Original material array consumers also upload RGBA float components. */
 float values[8]={1,0,0,1,0,1,0,1};memcpy(memory+0x8001,values,sizeof values);GC(image,0,target,0,GL_RGBA8,1,1,2,0,GL_RGBA,GL_FLOAT,0x8001);
 const unsigned char rgba[]={255,0,0,255,0,255,0,255};glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,actual);assert(!memcmp(actual,rgba,8)&&glGetError()==0);
 if(fault&&!strcmp(fault,"array-pbo")){GC(0xfffa01b0,0,1,0x3101);GC(0xfffa01c0,0,GL_PIXEL_UNPACK_BUFFER,word(0x3101));GC(image,0,target,0,GL_RED,1,1,1,0,GL_RED,GL_UNSIGNED_BYTE,0);assert(0);}
 glPixelStorei(GL_PACK_ALIGNMENT,4);memcpy(memory+0x3001,&name,4);GC(0xfffa01a0,0,1,0x3001);assert(!glIsTexture(name)&&glGetError()==0);
 puts("Original font texture array: native 256 layers/red storage, exact staged pixels, sublayers, float components, skipped-page isolation and native errors PASS");
}
static void matrix_queries(const char *fault){
 const uint32_t query=0xfffa0120,output=0x3001;
 glMatrixMode(GL_MODELVIEW);
 if(fault&&!strcmp(fault,"matrix-query-readonly")){assert(tribes_memory_permissions_set(0x3000,4096,1));GC(query,0,GL_MODELVIEW_MATRIX,output);assert(0);}
 if(fault&&!strcmp(fault,"matrix-query-cross-page")){assert(tribes_memory_permissions_set(0x4000,4096,1));GC(query,0,GL_MODELVIEW_MATRIX,0x3fc1);assert(0);}
 if(fault&&!strcmp(fault,"matrix-query-unreadable")){assert(tribes_memory_permissions_set(0x3000,4096,0));GC(query,0,GL_MODELVIEW_MATRIX,output);assert(0);}
 if(fault&&!strcmp(fault,"matrix-query-overflow")){GC(query,0,GL_MODELVIEW_MATRIX,0xffffffe1);assert(0);}
 const GLdouble matrix[16]={1.25,-.125,.5,.25,2,1.5,-.75,.125,-2,.25,.625,.5,.75,-.5,3,1};
 const GLenum modes[]={GL_MODELVIEW,GL_PROJECTION},names[]={GL_MODELVIEW_MATRIX,GL_PROJECTION_MATRIX};
 for(unsigned mode=0;mode<2;mode++){
 glMatrixMode(modes[mode]);glPushMatrix();GLfloat expected[16];
 for(unsigned i=0;i<3;i++){
  if(i==0)glLoadIdentity();else if(i==1)glLoadMatrixd(matrix);else{glTranslated(.25,-.75,2);glRotated(23,1,2,3);}
  glGetFloatv(names[mode],expected);assert(glGetError()==GL_NO_ERROR);
  memset(memory+0x3000,0xa5,66);GC(query,0,names[mode],output);
  assert(!memcmp(memory+output,expected,sizeof expected)&&memory[0x3000]==0xa5&&memory[0x3041]==0xa5);
  assert(GC(0xfffa0010,1)==GL_NO_ERROR);
 }
 glEnable(0xffffffff);GC(query,0,names[mode],output);
 assert(!memcmp(memory+output,expected,sizeof expected)&&GC(0xfffa0010,1)==GL_INVALID_ENUM&&GC(0xfffa0010,1)==GL_NO_ERROR);
 /* A native invalid operation must preserve every supplied output byte. */
 memset(expected,0x5a,sizeof expected);glBegin(GL_TRIANGLES);glGetFloatv(names[mode],expected);glEnd();
 assert(glGetError()==GL_INVALID_OPERATION);memset(memory+output,0x5a,sizeof expected);
 glBegin(GL_TRIANGLES);GC(query,0,names[mode],output);glEnd();
 assert(!memcmp(memory+output,expected,sizeof expected)&&GC(0xfffa0010,1)==GL_INVALID_OPERATION&&GC(0xfffa0010,1)==GL_NO_ERROR);
 assert(memory[0x3000]==0xa5&&memory[0x3041]==0xa5);glPopMatrix();
 }
 glMatrixMode(GL_MODELVIEW);
 puts("Model-view/projection queries: native sixteen-float matrices, unaligned output/canaries, retained errors, full guest CPU/stack/LastError and invalid-operation preservation PASS");
}
static void primitive_restart(void){
 const uint32_t restart=0xfffa0800;
 strcpy((char *)memory+0x24000,"glPrimitiveRestartIndex");assert(CALL(63,0x24000)==restart);
 assert(!GC(0xfffa05f0,1,0x8f9d));GC(0xfffa0020,0,0x8f9e,0x3000);assert(word(0x3000)==0);
 glUseProgram(0);glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
 glDisable(GL_TEXTURE_2D);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);glDisable(GL_FOG);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();glViewport(0,0,64,64);glColor4f(1,0,0,1);
 const GLfloat vertices[]={-.9f,-.8f,-.1f,-.8f,-.9f,.8f,-.1f,.8f,.1f,-.8f,.9f,-.8f,.1f,.8f,.9f,.8f};
 const GLuint strips[]={0,1,2,3,4,5,6,7};unsigned char expected[64*64*4],actual[sizeof expected];
 glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glEnableClientState(GL_VERTEX_ARRAY);glVertexPointer(2,GL_FLOAT,0,vertices);
 glDrawElements(GL_TRIANGLE_STRIP,4,GL_UNSIGNED_INT,strips);glDrawElements(GL_TRIANGLE_STRIP,4,GL_UNSIGNED_INT,strips+4);glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,expected);glVertexPointer(2,GL_FLOAT,0,NULL);glDisableClientState(GL_VERTEX_ARRAY);
 memcpy(memory+0x3fc0,vertices,sizeof vertices);GC(0xfffa0300,0,2,GL_FLOAT,0,0x3fc0);GC(0xfffa02e0,0,GL_VERTEX_ARRAY);
 assert(tribes_memory_permissions_set(0x4000,4096,0));
 const GLenum types[]={GL_UNSIGNED_BYTE,GL_UNSIGNED_SHORT,GL_UNSIGNED_INT};
 for(unsigned w=0;w<3;w++){
  unsigned width=1u<<w;uint32_t marker=w==0?255:w==1?65535:0xffffffff;
  uint32_t indices[]={marker,0,1,2,3,marker,marker,4,5,6,7,marker};
  for(unsigned i=0;i<12;i++)memcpy(memory+0x6001+i*width,&indices[i],width);
  GC(restart,0,marker);GC(0xfffa0290,0,0x8f9d);assert(GC(0xfffa05f0,1,0x8f9d));
  GC(0xfffa0020,0,0x8f9e,0x3000);assert(word(0x3000)==marker);
  glClear(GL_COLOR_BUFFER_BIT);glEnable(0xffffffff);GC(0xfffa0350,0,GL_TRIANGLE_STRIP,12,types[w],0x6001);
  assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,actual);assert(!memcmp(actual,expected,sizeof actual));
  /* All markers consume no vertex data, even with an unreadable descriptor. */
  GC(0xfffa0300,0,2,GL_FLOAT,0,0x4000);GC(0xfffa0350,0,GL_TRIANGLE_STRIP,1,types[w],0x6001);GC(0xfffa0300,0,2,GL_FLOAT,0,0x3fc0);assert(!GC(0xfffa0010,1));
 }
 /* State writes inside Begin/End fail without consuming errors or changing it. */
 GC(restart,0,65535);glBegin(GL_POINTS);GC(restart,0,7);GC(0xfffa0280,0,0x8f9d);glEnd();assert(GC(0xfffa0010,1)==GL_INVALID_OPERATION);GC(0xfffa0020,0,0x8f9e,0x3000);assert(word(0x3000)==65535&&GC(0xfffa05f0,1,0x8f9d));
 /* A 32-bit marker is not truncated when indices are bytes. */
 memcpy(memory+0x6001,"\0\1\2\3",4);GC(restart,0,256);glClear(GL_COLOR_BUFFER_BIT);GC(0xfffa0350,0,GL_TRIANGLE_STRIP,4,GL_UNSIGNED_BYTE,0x6001);glReadPixels(8,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);assert(actual[0]==255);
 GC(0xfffa0280,0,0x8f9d);GC(restart,0,0);assert(!GC(0xfffa05f0,1,0x8f9d));glClear(GL_COLOR_BUFFER_BIT);GC(0xfffa0350,0,GL_TRIANGLE_STRIP,4,GL_UNSIGNED_BYTE,0x6001);glReadPixels(8,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);assert(actual[0]==255);
 GC(0xfffa02f0,0,GL_VERTEX_ARRAY);assert(tribes_memory_permissions_set(0x4000,4096,3));glColor4f(1,1,1,1);assert(!GC(0xfffa0010,1));
 puts("Primitive restart: native split-strip framebuffer oracle, 8/16/32-bit markers, no marker vertex reads, all-marker draw, unaligned indices, disabled state and retained errors PASS");
}
static void vector_texcoords(const char *fault){
 strcpy((char *)memory+0x24000,"glTexCoord2fv");assert(CALL(63,0x24000)==0xfffa0810);
 if(fault&&!strcmp(fault,"texcoord-vector-read")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0810,0,0x3ff9);assert(0);}
 if(fault&&!strcmp(fault,"texcoord-vector-null")){GC(0xfffa0810,0,0);assert(0);}
 if(fault&&!strcmp(fault,"texcoord-vector-overflow")){GC(0xfffa0810,0,0xfffffffc);assert(0);}
 glActiveTexture(GL_TEXTURE0);glMatrixMode(GL_TEXTURE);glLoadIdentity();
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
 glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glColor4f(1,1,1,1);
 GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
 const unsigned char pixels[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glEnable(GL_TEXTURE_2D);
 const GLfloat uv[4][2]={{0,0},{1,0},{1,1},{0,1}};unsigned char expected[64*64*4],actual[sizeof expected];
 for(unsigned route=0;route<2;route++){
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glEnable(0xffffffff);glBegin(GL_QUADS);
  for(unsigned i=0;i<4;i++){
   if(!route)glTexCoord2f(uv[i][0],uv[i][1]);
   else {uint32_t address=i&1?0x3ff8:0x3001;memcpy(memory+address,uv[i],8);
    assert(tribes_memory_permissions_set(0x3000,4096,1)&&tribes_memory_permissions_set(0x4000,4096,0));
    GC(0xfffa0810,0,address);assert(!memcmp(memory+address,uv[i],8));
    assert(tribes_memory_permissions_set(0x3000,4096,3)&&tribes_memory_permissions_set(0x4000,4096,3));}
   glVertex2f(uv[i][0]*2-1,uv[i][1]*2-1);
  }glEnd();assert(glGetError()==GL_INVALID_ENUM&&glGetError()==0);
  GLfloat current[4];glGetFloatv(GL_CURRENT_TEXTURE_COORDS,current);assert(current[0]==0&&current[1]==1&&current[2]==0&&current[3]==1);
  glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,route?actual:expected);
 }
 assert(!memcmp(actual,expected,sizeof actual));
 assert(actual[(16*64+16)*4]==255&&actual[(16*64+48)*4+1]==255&&actual[(48*64+16)*4+2]==255);
 glDisable(GL_TEXTURE_2D);glDeleteTextures(1,&texture);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
 puts("Vector texture coordinates: native textured framebuffer, current coordinate defaults, exact-page-end readonly/unaligned input, retained errors and guest ABI PASS");
}
static void vector_vertices(const char *fault){
 strcpy((char *)memory+0x24000,"glVertex3fv");assert(CALL(63,0x24000)==0xfffa0760);
 if(fault&&!strcmp(fault,"vertex-vector-read")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0760,0,0x3ff5);assert(0);}
 if(fault&&!strcmp(fault,"vertex-vector-null")){GC(0xfffa0760,0,0);assert(0);}
 if(fault&&!strcmp(fault,"vertex-vector-overflow")){GC(0xfffa0760,0,0xfffffff8);assert(0);}
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
 glDisable(GL_BLEND);glDisable(GL_TEXTURE_2D);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glClearDepth(1);
 const GLfloat corners[4][2]={{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,.5f}};
 unsigned char native[64*64*4],actual[sizeof native];
 for(unsigned route=0;route<2;route++){
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(0xffffffff);
  for(unsigned pass=0;pass<3;pass++){
   glColor3ub(pass==0?255:0,pass==1?255:0,pass==2?255:0);glBegin(GL_QUADS);
   for(unsigned i=0;i<4;i++){
    GLfloat v[3]={corners[i][0],corners[i][1],pass==1?-.5f:.5f};
    if(!route)glVertex3fv(v);
    else{
     uint32_t address=i&1?0x3ff4:0x3001;memcpy(memory+address,v,sizeof v);
     assert(tribes_memory_permissions_set(0x3000,4096,1)&&tribes_memory_permissions_set(0x4000,4096,0));
     GC(0xfffa0760,0,address);
     assert(!memcmp(memory+address,v,sizeof v));
     assert(tribes_memory_permissions_set(0x3000,4096,3)&&tribes_memory_permissions_set(0x4000,4096,3));
    }
   }glEnd();
  }
  assert(glGetError()==GL_INVALID_ENUM&&glGetError()==0);
  glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,route?actual:native);
 }
 assert(!memcmp(native,actual,sizeof native));
 unsigned center=(32*64+32)*4;assert(actual[center]==0&&actual[center+1]==255&&actual[center+2]==0&&actual[center+3]==255);
 assert(actual[0]==0&&actual[1]==0&&actual[2]==0);
 glDisable(GL_DEPTH_TEST);glColor4f(1,1,1,1);
 puts("Vector vertices: native full-frame quad/depth comparison, unaligned and exact-page-end readonly sources, preserved errors and CPU ABI PASS");
}
static void immediate_rectangle(void){
 const char *names[]={"glBlendFunc","glBegin","glVertex2f","glEnd"};
 for(unsigned i=0;i<4;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa04f0+16*i);}
 GC(0xfffa0240,0,GL_PROJECTION);GC(0xfffa0250,0);
 GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0250,0);
 GC(0xfffa0280,0,GL_TEXTURE_2D);GC(0xfffa0290,0,GL_BLEND);
 GC(0xfffa0060,0,0,0,0x3f800000,0x3f800000);GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);
 GC(0xfffa04f0,0,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);GC(0xfffa02d0,0,255,0,0,128);
 GC(0xfffa0500,0,GL_TRIANGLE_FAN);
 GC(0xfffa0510,0,0xbf000000,0xbf000000);GC(0xfffa0510,0,0x3f000000,0xbf000000);
 GC(0xfffa0510,0,0x3f000000,0x3f000000);GC(0xfffa0510,0,0xbf000000,0x3f000000);
 GC(0xfffa0520,0);
 strcpy((char *)memory+0x24000,"glFlush");assert(CALL(63,0x24000)==0xfffa0530);GC(0xfffa0530,0);
 unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
 assert(pixel[0]>=127&&pixel[0]<=129&&pixel[1]==0&&pixel[2]>=126&&pixel[2]<=128);
 glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==255&&glGetError()==0);
 GC(0xfffa04f0,0,0xffffffff,GL_ZERO);assert(glGetError()==GL_INVALID_ENUM);
 GC(0xfffa0500,0,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);
 GC(0xfffa0520,0);GC(0xfffa0530,0);assert(glGetError()==GL_INVALID_OPERATION);
 GC(0xfffa0500,0,GL_TRIANGLES);GC(0xfffa0500,0,GL_TRIANGLES);GC(0xfffa0520,0);GC(0xfffa0530,0);assert(glGetError()==GL_INVALID_OPERATION);
 GC(0xfffa0280,0,GL_BLEND);GC(0xfffa02d0,0,255,255,255,255);
 /* The original renderer's next draw uses float colour and signed integer
    polygon corners. Compare native colour state and actual bounded pixels. */
 const char *additional[]={"glColor4f","glVertex2i"};
 for(unsigned i=0;i<2;i++){strcpy((char *)memory+0x24000,additional[i]);assert(CALL(63,0x24000)==0xfffa06a0+16*i);}
 GLfloat expected[4],actual[4];glColor4f(-.25f,.25f,2,.5f);glGetFloatv(GL_CURRENT_COLOR,expected);
 GC(0xfffa06a0,0,0xbe800000,0x3e800000,0x40000000,0x3f000000);glGetFloatv(GL_CURRENT_COLOR,actual);assert(!memcmp(expected,actual,sizeof actual));
 GC(0xfffa0060,0,0,0,0,0x3f800000);GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);
 GC(0xfffa0290,0,GL_BLEND);glScalef(.5f,.5f,1);
 GC(0xfffa0500,0,GL_POLYGON);
 GC(0xfffa06a0,0,0,0x3f000000,0x3f800000,0x3f000000);
 GC(0xfffa06b0,0,UINT32_MAX,UINT32_MAX);GC(0xfffa06b0,0,1,UINT32_MAX);
 GC(0xfffa06b0,0,1,1);GC(0xfffa06b0,0,UINT32_MAX,1);GC(0xfffa0520,0);
 glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]>=63&&pixel[1]<=65&&pixel[2]>=127&&pixel[2]<=129);
 glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==0&&glGetError()==0);
 GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0250,0);GC(0xfffa0280,0,GL_BLEND);GC(0xfffa02d0,0,255,255,255,255);
 /* Original fan uses byte arguments with live high register bits and
    three binary32 coordinates. Depth overlap distinguishes a real z path. */
 const char *fan_names[]={"glColor3ub","glVertex3f"};
 for(unsigned i=0;i<2;i++){strcpy((char *)memory+0x24000,fan_names[i]);assert(CALL(63,0x24000)==0xfffa06d0+16*i);}
 glColor3ub(128,128,128);glGetFloatv(GL_CURRENT_COLOR,expected);
 GC(0xfffa06d0,0,0xb80,0x580,0xb80);glGetFloatv(GL_CURRENT_COLOR,actual);
 assert(!memcmp(actual,expected,sizeof actual)&&actual[3]==1);
 glColor3ub(17,65,255);glGetFloatv(GL_CURRENT_COLOR,expected);
 GC(0xfffa06d0,0,0xdead0011,0xbeef0041,0xffffffff);glGetFloatv(GL_CURRENT_COLOR,actual);
 assert(!memcmp(actual,expected,sizeof actual));
 glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glClearDepth(1);
 GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 glScalef(.5f,.5f,1);glEnable(0xffffffff);
 for(unsigned pass=0;pass<3;pass++){
  /* Near green must survive the final farther blue fan. */
  GC(0xfffa06d0,0,pass==0?255:0,pass==1?255:0,pass==2?255:0);
  GC(0xfffa0500,0,GL_TRIANGLE_FAN);
  uint32_t z=pass==1?0xbf000000:0x3f000000;
  GC(0xfffa06e0,0,0xbf800000,0xbf800000,z);GC(0xfffa06e0,0,0x3f800000,0xbf800000,z);
  GC(0xfffa06e0,0,0x3f800000,0x3f800000,z);GC(0xfffa06e0,0,0xbf800000,0x3f800000,z);
  GC(0xfffa0520,0);
 }
 assert(glGetError()==GL_INVALID_ENUM&&glGetError()==0);
 glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==255&&pixel[2]==0&&pixel[3]==255);
 glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==0&&glGetError()==0);
 glDisable(GL_DEPTH_TEST);GC(0xfffa0250,0);GC(0xfffa02d0,0,255,255,255,255);
 strcpy((char *)memory+0x24000,"glTranslatef");assert(CALL(63,0x24000)==0xfffa06f0);
 /* Native post-multiplication oracle with nonidentity scale catches order,
    sign and bit-conversion mistakes. Error must survive the helper. */
 GLfloat transform[16],translated[16];glLoadIdentity();glScalef(2,3,4);
 glTranslatef(.25f,-.5f,2);glGetFloatv(GL_MODELVIEW_MATRIX,transform);
 glLoadIdentity();glScalef(2,3,4);glEnable(0xffffffff);
 GC(0xfffa06f0,0,0x3e800000,0xbf000000,0x40000000);glGetFloatv(GL_MODELVIEW_MATRIX,translated);
 assert(!memcmp(transform,translated,sizeof transform));assert(translated[12]==.5f&&translated[13]==-1.5f&&translated[14]==8);
 assert(glGetError()==GL_INVALID_ENUM&&glGetError()==0);glLoadIdentity();
 /* Texture coordinates address unit zero even when another unit is active.
    Use distinct four-component sentinels and non-normalized coordinates. */
 strcpy((char *)memory+0x24000,"glTexCoord2f");assert(CALL(63,0x24000)==0xfffa0700);
 glMultiTexCoord4f(GL_TEXTURE1,7,8,9,10);glActiveTexture(GL_TEXTURE1);
 glTexCoord2f(-.25f,2);glActiveTexture(GL_TEXTURE0);glGetFloatv(GL_CURRENT_TEXTURE_COORDS,expected);
 glTexCoord4f(9,8,7,6);glActiveTexture(GL_TEXTURE1);glEnable(0xffffffff);
 GC(0xfffa0700,0,0xbe800000,0x40000000);
 glGetFloatv(GL_CURRENT_TEXTURE_COORDS,actual);assert(actual[0]==7&&actual[1]==8&&actual[2]==9&&actual[3]==10);
 glActiveTexture(GL_TEXTURE0);glGetFloatv(GL_CURRENT_TEXTURE_COORDS,actual);
 assert(!memcmp(actual,expected,sizeof actual)&&actual[2]==0&&actual[3]==1);
 assert(glGetError()==GL_INVALID_ENUM&&glGetError()==0);
 /* The reached sky quad interleaves texcoords and 3D vertices inside Begin.
    Four different texels prove coordinates reach actual native sampling. */
 GLuint sky_texture;glGenTextures(1,&sky_texture);glBindTexture(GL_TEXTURE_2D,sky_texture);
 const unsigned char texels[]={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255};
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,texels);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glClear(GL_COLOR_BUFFER_BIT);
 const uint32_t uv[4][2]={{0,0},{0x3f800000,0},{0x3f800000,0x3f800000},{0,0x3f800000}};
 const uint32_t xy[4][2]={{0xbf000000,0xbf000000},{0x3f000000,0xbf000000},{0x3f000000,0x3f000000},{0xbf000000,0x3f000000}};
 glEnable(0xffffffff);GC(0xfffa0500,0,GL_QUADS);
 for(unsigned i=0;i<4;i++){GC(0xfffa0700,0,uv[i][0],uv[i][1]);GC(0xfffa06e0,0,xy[i][0],xy[i][1],0);}
 GC(0xfffa0520,0);assert(glGetError()==GL_INVALID_ENUM&&glGetError()==0);
 const int sample[4][2]={{24,24},{40,24},{24,40},{40,40}};
 for(unsigned i=0;i<4;i++){glReadPixels(sample[i][0],sample[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(!memcmp(pixel,texels+4*i,4));}
 glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==0);
 glDisable(GL_TEXTURE_2D);glDeleteTextures(1,&sky_texture);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);assert(glGetError()==0);
 puts("Original sky texcoords: unit-zero state, implicit R/Q, signed floats, native textured quad pixels, CPU ABI and retained errors PASS");
 strcpy((char *)memory+0x24000,"glRotatef");assert(CALL(63,0x24000)==0xfffa0710);
 /* Reached zero-angle Z rotation and nonunit/negative-axis cases, in all
    three legacy matrix modes. Nonidentity input detects multiplication order. */
 const GLenum rotation_modes[]={GL_MODELVIEW,GL_PROJECTION,GL_TEXTURE};
 const GLfloat rotations[3][4]={{0,0,0,1},{31.25f,2,-3,4},{-90,0,1,0}};
 for(unsigned mode=0;mode<3;mode++)for(unsigned i=0;i<3;i++){
  GLenum query=mode==0?GL_MODELVIEW_MATRIX:mode==1?GL_PROJECTION_MATRIX:GL_TEXTURE_MATRIX;
  glMatrixMode(rotation_modes[mode]);glLoadIdentity();glScalef(2,3,4);glTranslatef(.25f,-.5f,2);
  glRotatef(rotations[i][0],rotations[i][1],rotations[i][2],rotations[i][3]);glGetFloatv(query,transform);
  glLoadIdentity();glScalef(2,3,4);glTranslatef(.25f,-.5f,2);glEnable(0xffffffff);
  uint32_t bits[4];memcpy(bits,rotations[i],sizeof bits);GC(0xfffa0710,0,bits[0],bits[1],bits[2],bits[3]);glGetFloatv(query,translated);
  assert(!memcmp(transform,translated,sizeof transform));assert(glGetError()==GL_INVALID_ENUM&&glGetError()==0);
  glBegin(GL_TRIANGLES);GC(0xfffa0710,0,0x42b40000,0,0,0x3f800000);glEnd();
  glGetFloatv(query,translated);assert(!memcmp(transform,translated,sizeof transform)&&glGetError()==GL_INVALID_OPERATION);
  glLoadIdentity();
 }
 glMatrixMode(GL_MODELVIEW);assert(glGetError()==0);
 puts("Original sky rotation: native matrix product in three modes, degree angles/nonunit axes, invalid-state preservation and CPU ABI/errors PASS");
 puts("Original translation: float XYZ, native post-multiplied matrix, CPU ABI and retained error PASS");
 puts("Original 3D fan: low-byte colour/alpha, float XYZ/depth, bounded native pixels, CPU ABI and retained error PASS");
 puts("Original lighting polygon: float colour state/alpha and signed integer vertices through guest ABI, bounded native pixels PASS");
 puts("Original GUI rectangle: guest immediate vertices, alpha blending, bounded coverage and native errors PASS");
}
static void gpu_sync(uint32_t window,uint32_t context,const char *fault){
 const uint32_t fence=0xfffa0540,wait=0xfffa0550,del=0xfffa0560;
 const char *names[]={"glFenceSync","glClientWaitSync","glDeleteSync"};
 for(unsigned i=0;i<3;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==fence+16*i);}
 assert(!GC(fence,1,0xffffffff,0)&&GC(0xfffa0010,1)==GL_INVALID_ENUM);
 assert(!GC(fence,1,0x9117,1)&&GC(0xfffa0010,1)==GL_INVALID_VALUE);
 GC(0xfffa0060,0,0,0x3f800000,0,0x3f800000);GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);
 strcpy((char *)memory+0x24000,"glIsSync");uint32_t is_sync=CALL(63,0x24000);assert(is_sync==0xfffa0590);
 assert(!GC(is_sync,1,0)&&!GC(is_sync,1,0xffffffff)&&GC(0xfffa0010,1)==0);
 uint32_t sync=GC(fence,1,0x9117,0);assert(sync>=0xdd000001&&sync<0xdd100000&&GC(is_sync,1,sync));
 uint32_t status=GC(wait,1,sync,0,0,0);assert(status==0x911a||status==0x911b||status==0x911c);
 status=GC(wait,1,sync,1,0,1);assert(status==0x911a||status==0x911c);
 GC(0xfffa00a0,0);assert(GC(wait,1,sync,0,0xffffffff,0xffffffff)==0x911a);
 unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==255&&pixel[2]==0&&pixel[3]==255);
 assert(GC(wait,1,sync,2,0,0)==0x911d&&GC(0xfffa0010,1)==GL_INVALID_VALUE);
 strcpy((char *)memory+0x24000,"glGetSynciv");assert(CALL(63,0x24000)==0xfffa0570);
 if(fault&&!strcmp(fault,"sync-output")){assert(tribes_memory_permissions_set(0x3000,4096,1));GC(0xfffa0570,0,sync,0x9114,4,0,0x3001);assert(0);}
 if(fault&&!strcmp(fault,"sync-length")){assert(tribes_memory_permissions_set(0x4000,4096,1));GC(0xfffa0570,0,sync,0x9114,4,0x4001,0x3001);assert(0);}
 memset(memory+0x3ff8,0xa5,8);assert(tribes_memory_permissions_set(0x4000,4096,0));
 const uint32_t props[]={0x9112,0x9113,0x9114,0x9115},values[]={0x9116,0x9117,0x9119,0};
 for(unsigned i=0;i<4;i++){GC(0xfffa0570,0,sync,props[i],4,0x3001,0x3ffc);assert(word(0x3001)==1&&word(0x3ffc)==values[i]&&word(0x3ff8)==0xa5a5a5a5);}
 GC(0xfffa0570,0,sync,0x9114,0,0x3001,0x4000);assert(word(0x3001)==0);
 GC(0xfffa0570,0,sync,0x9114,0xffffffff,0x3001,0x4000);assert(GC(0xfffa0010,1)==GL_INVALID_VALUE&&word(0x3001)==0);
 uint32_t prior=word(0x3ffc);GC(0xfffa0570,0,sync,0xffffffff,4,0x3001,0x3ffc);assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&word(0x3001)==0&&word(0x3ffc)==prior);
 assert(tribes_memory_permissions_set(0x4000,4096,3));
 GC(del,0,sync);assert(!GC(is_sync,1,sync)&&GC(0xfffa0010,1)==0);assert(GC(wait,1,sync,0,0,0)==0x911d&&GC(0xfffa0010,1)==GL_INVALID_VALUE);
 GC(del,0,sync);assert(GC(0xfffa0010,1)==GL_INVALID_VALUE);GC(del,0,0);assert(GC(0xfffa0010,1)==0);
 glEnable(0xffffffff);assert(!GC(is_sync,1,0));assert(GC(wait,1,0,0,0,0)==0x911d);
 assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&GC(0xfffa0010,1)==GL_INVALID_VALUE&&GC(0xfffa0010,1)==0);
 sync=GC(fence,1,0x9117,0);assert(sync);GC(0xfffa0530,0);
 assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0));uint32_t isolated=CALL(47,window);assert(isolated);assert(!GC(is_sync,1,sync)&&GC(0xfffa0010,1)==0);
 assert(GC(wait,1,sync,0,0,0)==0x911d&&GC(0xfffa0010,1)==GL_INVALID_VALUE);
 uint32_t isolated_sync=GC(fence,1,0x9117,0);assert(isolated_sync&&isolated_sync!=sync);GC(0xfffa0530,0);
 assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,1));uint32_t shared=CALL(47,window);assert(shared);assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0));
 assert(CALL(48,isolated));assert(GC(is_sync,1,isolated_sync));status=GC(wait,1,isolated_sync,1,1000000000,0);assert(status==0x911a||status==0x911c);
 /* Last native context destruction owns outstanding native sync cleanup. */
 assert(CALL(48,shared));assert(CALL(49,window,context));assert(!GC(is_sync,1,isolated_sync)&&GC(is_sync,1,sync)&&GC(0xfffa0010,1)==0);
 assert(GC(wait,1,isolated_sync,0,0,0)==0x911d&&GC(0xfffa0010,1)==GL_INVALID_VALUE);
 status=GC(wait,1,sync,1,1000000000,0);assert(status==0x911a||status==0x911c);GC(del,0,sync);assert(GC(0xfffa0010,1)==0);
 puts("GPU sync: native fence completion, 64-bit timeout, pixels, error retention, shared lifetime and stale/foreign tokens PASS");
}
static void perspective_projection(void){
 const uint32_t address=0xfffa05b0;
 strcpy((char *)memory+0x24000,"opengl32");uint32_t module=CALL(60,0x24000);assert(module==0x20040000);
 strcpy((char *)memory+0x24000,"glFrustum");assert(CALL(63,0x24000)==address);
 assert(tribes_get_proc_address(cpu._pointer_offset,module,0x24000)==address);CALL(62,module);
 /* Six IA-32 doubles use twelve little-endian argument words. First fixture
    is the actual model-preview stack captured at original return 0x447446. */
 const uint32_t captured[]={0x9fb3c819,0xbf98a7e1,0x9fb3c819,0x3f98a7e1,0x9fb3c819,0xbf98a7e1,0x9fb3c819,0x3f98a7e1,0xa0000000,0x3fa99999,0,0x40a06800};
 double cases[3][6];memcpy(cases[0],captured,sizeof captured);
 const double asymmetric[]={-.4,.7,-.2,.5,.3,37},symmetric[]={-1,1,-1,1,1,10};
 memcpy(cases[1],asymmetric,sizeof asymmetric);memcpy(cases[2],symmetric,sizeof symmetric);
 for(unsigned c=0;c<3;c++){
  double l=cases[c][0],r=cases[c][1],b=cases[c][2],t=cases[c][3],n=cases[c][4],f=cases[c][5];
  GLdouble projection[16]={2*n/(r-l),0,0,0,0,2*n/(t-b),0,0,(r+l)/(r-l),(t+b)/(t-b),-(f+n)/(f-n),-1,0,0,-2*f*n/(f-n),0};
  GLdouble seed[16]={2,0,0,0,0,3,0,0,0,0,4,0,.25,-.5,1,1},actual[16];
  memcpy(memory+0x3000,seed,sizeof seed);GC(0xfffa0240,0,GL_PROJECTION);GC(0xfffa0260,0,0x3000);
  uint32_t words[12];memcpy(words,cases[c],sizeof words);gc(address,words,12,0);
  glGetDoublev(GL_PROJECTION_MATRIX,actual);
  for(unsigned col=0;col<4;col++)for(unsigned row=0;row<4;row++){
   double expected=0;for(unsigned k=0;k<4;k++)expected+=seed[k*4+row]*projection[col*4+k];
   assert(fabs(actual[col*4+row]-expected)<1e-6*fmax(1,fabs(expected)));
  }
  assert(!glGetError());
 }
 const double invalid[][6]={{0,0,-1,1,1,10},{-1,1,0,0,1,10},{-1,1,-1,1,1,1},{-1,1,-1,1,0,10},{-1,1,-1,1,1,-1}};
 for(unsigned c=0;c<sizeof invalid/sizeof *invalid;c++){
  GLdouble before[16],after[16];glGetDoublev(GL_PROJECTION_MATRIX,before);
  uint32_t words[12];memcpy(words,invalid[c],sizeof words);gc(address,words,12,0);
  assert(GC(0xfffa0010,1)==GL_INVALID_VALUE&&!GC(0xfffa0010,1));glGetDoublev(GL_PROJECTION_MATRIX,after);assert(!memcmp(before,after,sizeof before));
 }
 GC(0xfffa0250,0);glGetString(0xffffffff);uint32_t words[12];memcpy(words,symmetric,sizeof words);gc(address,words,12,0);
 assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));
 GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0250,0);GC(0xfffa0270,0,0,0,64,64);
 GC(0xfffa0280,0,GL_DEPTH_TEST);GC(0xfffa0280,0,GL_TEXTURE_2D);GC(0xfffa0280,0,GL_BLEND);
 GC(0xfffa0060,0,0,0,0,0x3f800000);GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);
 glColor4f(0,1,0,1);glBegin(GL_TRIANGLES);glVertex3f(-1,-1,-2);glVertex3f(1,-1,-2);glVertex3f(0,1,-2);glEnd();
 unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==255&&pixel[2]==0);
 glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==0&&pixel[2]==0);
 GC(0xfffa0240,0,GL_PROJECTION);GC(0xfffa0250,0);GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0250,0);assert(!glGetError());
 puts("Perspective projection: original/asymmetric arguments, native matrix product, invalid-state preservation, retained error and projected pixels pass");
}
static void matrix_stack(void){
 const char *names[]={"glPushMatrix","glPopMatrix","glScalef"};
 for(unsigned i=0;i<3;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa0650+16*i);}
 const GLenum modes[]={GL_MODELVIEW,GL_PROJECTION,GL_TEXTURE},queries[]={GL_MODELVIEW_MATRIX,GL_PROJECTION_MATRIX,GL_TEXTURE_MATRIX};
 const GLenum depths[]={GL_MODELVIEW_STACK_DEPTH,GL_PROJECTION_STACK_DEPTH,GL_TEXTURE_STACK_DEPTH},limits[]={GL_MAX_MODELVIEW_STACK_DEPTH,GL_MAX_PROJECTION_STACK_DEPTH,GL_MAX_TEXTURE_STACK_DEPTH};
 GLdouble seed[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},actual[16];memcpy(memory+0x3100,seed,sizeof seed);
 for(unsigned m=0;m<3;m++){
  GC(0xfffa0240,0,modes[m]);GC(0xfffa0260,0,0x3100);GC(0xfffa0650,0);GC(0xfffa0670,0,0x40000000,0xbf000000,0x3f400000);
  glGetDoublev(queries[m],actual);for(unsigned c=0;c<4;c++)for(unsigned r=0;r<4;r++)assert(actual[4*c+r]==seed[4*c+r]*(c==0?2:c==1?-.5:c==2?.75:1));
  GC(0xfffa0660,0);glGetDoublev(queries[m],actual);assert(!memcmp(actual,seed,sizeof seed));GLint depth=0,limit=0;glGetIntegerv(depths[m],&depth);glGetIntegerv(limits[m],&limit);assert(depth==1&&limit>=2&&limit<1024);
  for(int i=1;i<limit;i++)GC(0xfffa0650,0);GC(0xfffa0650,0);assert(glGetError()==GL_STACK_OVERFLOW);glGetIntegerv(depths[m],&depth);assert(depth==limit);
  glGetDoublev(queries[m],actual);assert(!memcmp(actual,seed,sizeof seed));for(int i=1;i<limit;i++)GC(0xfffa0660,0);GC(0xfffa0660,0);assert(glGetError()==GL_STACK_UNDERFLOW);
  glGetDoublev(queries[m],actual);assert(!memcmp(actual,seed,sizeof seed));
  GC(0xfffa0500,0,GL_TRIANGLES);GC(0xfffa0650,0);GC(0xfffa0660,0);GC(0xfffa0670,0,0x40000000,0x40000000,0x40000000);GC(0xfffa0520,0);assert(glGetError()==GL_INVALID_OPERATION);glGetDoublev(queries[m],actual);assert(!memcmp(actual,seed,sizeof seed));GC(0xfffa0250,0);
 }
 GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0280,0,GL_DEPTH_TEST);GC(0xfffa0280,0,GL_TEXTURE_2D);GC(0xfffa0280,0,GL_BLEND);GC(0xfffa0270,0,0,0,64,64);
 GC(0xfffa0650,0);GC(0xfffa0670,0,0x3f000000,0x3f000000,0x3f800000);GC(0xfffa0060,0,0,0,0,0x3f800000);GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);glColor4f(0,1,0,1);
 glBegin(GL_TRIANGLE_FAN);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();unsigned char px[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);assert(px[1]==255);glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);assert(px[1]==0);
 GC(0xfffa0660,0);glBegin(GL_TRIANGLE_FAN);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);assert(px[1]==255&&!glGetError());
 puts("Model matrix stack: native per-mode push/scale/pop, finite limits, error preservation, rejected begin-state changes and projected coverage PASS");
}
static void scene_texgen(const char *fault){
 const char *names[]={"glTexGeni","glTexGenfv"};
 for(unsigned i=0;i<2;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa07d0+16*i);}
 if(fault&&!strcmp(fault,"texgen-range")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa07e0,0,GL_S,GL_OBJECT_PLANE,0x3ff1);assert(0);}
 if(fault&&!strcmp(fault,"texgen-null")){GC(0xfffa07e0,0,GL_S,GL_OBJECT_PLANE,0);assert(0);}
 if(fault&&!strcmp(fault,"texgen-overflow")){GC(0xfffa07e0,0,GL_S,GL_OBJECT_PLANE,0xfffffff8);assert(0);}
 GLfloat plane[]={.5f,.25f,.125f,.5f},zero[4]={0},expected[4],actual[4];
 GLenum coords[]={GL_S,GL_T,GL_R,GL_Q};
 glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(2,3,4);glScalef(2,3,4);
 for(unsigned c=0;c<4;c++)for(unsigned p=0;p<3;p++){
  GLenum name=p==0?GL_TEXTURE_GEN_MODE:p==1?GL_OBJECT_PLANE:GL_EYE_PLANE;unsigned count=p?4:1;GLfloat value[4];memcpy(value,plane,16);if(!p)value[0]=GL_OBJECT_LINEAR;
  glTexGenfv(coords[c],name,value);glGetTexGenfv(coords[c],name,expected);
  if(p)glTexGenfv(coords[c],name,zero);else glTexGeni(coords[c],name,GL_EYE_LINEAR);
  uint32_t address=0x4000-count*4;memcpy(memory+address,value,count*4);
  assert(tribes_memory_permissions_set(0x3000,4096,1)&&tribes_memory_permissions_set(0x4000,4096,0));
  glEnable(0xffffffff);GC(0xfffa07e0,0,coords[c],name,address);glGetTexGenfv(coords[c],name,actual);
  assert(!memcmp(actual,expected,count*4)&&!memcmp(memory+address,value,count*4));assert(glGetError()==GL_INVALID_ENUM&&!glGetError());
  assert(tribes_memory_permissions_set(0x3000,4096,3)&&tribes_memory_permissions_set(0x4000,4096,3));
  if(!p){GC(0xfffa07d0,0,coords[c],name,GL_EYE_LINEAR);GLint mode;glGetTexGeniv(coords[c],name,&mode);assert(mode==GL_EYE_LINEAR);}
 }
 assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa07e0,0,0xffffffff,GL_OBJECT_PLANE,0x4000);assert(glGetError()==GL_INVALID_ENUM);GC(0xfffa07e0,0,GL_S,0xffffffff,0x4000);assert(glGetError()==GL_INVALID_ENUM);assert(tribes_memory_permissions_set(0x4000,4096,3));
 GC(0xfffa07d0,0,GL_Q,GL_TEXTURE_GEN_MODE,GL_SPHERE_MAP);assert(glGetError()==GL_INVALID_ENUM);
 memcpy(memory+0x3001,plane,16);glGetTexGenfv(GL_S,GL_OBJECT_PLANE,expected);glBegin(GL_POINTS);GC(0xfffa07e0,0,GL_S,GL_OBJECT_PLANE,0x3001);GC(0xfffa07d0,0,GL_S,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);glEnd();assert(glGetError()==GL_INVALID_OPERATION);glGetTexGenfv(GL_S,GL_OBJECT_PLANE,actual);assert(!memcmp(expected,actual,16));
 glUseProgram(0);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);
 glMatrixMode(GL_TEXTURE);glLoadIdentity();glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();glViewport(0,0,64,64);glActiveTexture(GL_TEXTURE0);
 GLuint texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 unsigned char texels[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255},pixels[2][64*64*4];glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,texels);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glEnable(GL_TEXTURE_2D);glEnable(GL_TEXTURE_GEN_S);glEnable(GL_TEXTURE_GEN_T);
 GLfloat planes[2][4]={{.5f,0,0,.5f},{0,.5f,0,.5f}};
 for(unsigned route=0;route<2;route++){
  for(unsigned c=0;c<2;c++){
   glTexGenfv(coords[c],GL_OBJECT_PLANE,zero);glTexGeni(coords[c],GL_TEXTURE_GEN_MODE,GL_EYE_LINEAR);
   if(!route){glTexGeni(coords[c],GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);glTexGenfv(coords[c],GL_OBJECT_PLANE,planes[c]);}
   else{memcpy(memory+0x3001,planes[c],16);GC(0xfffa07d0,0,coords[c],GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);GC(0xfffa07e0,0,coords[c],GL_OBJECT_PLANE,0x3001);}
  }
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels[route]);
 }
 assert(!memcmp(pixels[0],pixels[1],sizeof pixels[0]));
 for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)assert(!memcmp(pixels[1]+((16+y*32)*64+16+x*32)*4,texels+(y*2+x)*4,4));
 glDisable(GL_TEXTURE_GEN_S);glDisable(GL_TEXTURE_GEN_T);glDisable(GL_TEXTURE_2D);glDeleteTextures(1,&texture);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);assert(!glGetError());
 puts("Texgen: checked mode/object/eye planes, transformed eye state, protected page ends, native textured framebuffer, invalid/begin errors and CPU ABI PASS");
}
static void scene_polygon_mode(void){
 strcpy((char *)memory+0x24000,"glPolygonMode");assert(CALL(63,0x24000)==0xfffa07c0);
 GLenum faces[]={GL_FRONT,GL_BACK,GL_FRONT_AND_BACK},modes[]={GL_POINT,GL_LINE,GL_FILL};
 for(unsigned f=0;f<3;f++)for(unsigned m=0;m<3;m++){
  GLint expected[2],actual[2];glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glPolygonMode(faces[f],modes[m]);glGetIntegerv(GL_POLYGON_MODE,expected);
  glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);GC(0xfffa07c0,0,faces[f],modes[m]);glGetIntegerv(GL_POLYGON_MODE,actual);assert(!memcmp(actual,expected,sizeof actual));
 }
 GC(0xfffa07c0,0,GL_FRONT_AND_BACK,GL_FILL);GC(0xfffa07c0,0,0xffffffff,GL_LINE);assert(glGetError()==GL_INVALID_ENUM);GC(0xfffa07c0,0,GL_FRONT_AND_BACK,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);
 glBegin(GL_POINTS);GC(0xfffa07c0,0,GL_FRONT_AND_BACK,GL_LINE);glEnd();assert(glGetError()==GL_INVALID_OPERATION);GLint mode[2];glGetIntegerv(GL_POLYGON_MODE,mode);assert(mode[0]==GL_FILL&&mode[1]==GL_FILL);
 glUseProgram(0);glDisable(GL_LIGHTING);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_FOG);glDisable(GL_ALPHA_TEST);glDisable(GL_CULL_FACE);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();glViewport(0,0,64,64);glColor4f(0,1,0,1);unsigned char pixels[2][64*64*4];unsigned coverage[3]={0};
 for(unsigned m=0;m<3;m++){
  for(unsigned route=0;route<2;route++){
   glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
   if(!route)glPolygonMode(GL_FRONT_AND_BACK,modes[m]);else{glEnable(0xffffffff);GC(0xfffa07c0,0,GL_FRONT_AND_BACK,modes[m]);assert(glGetError()==GL_INVALID_ENUM);}
   glBegin(GL_TRIANGLES);glVertex2f(-.75f,-.75f);glVertex2f(.75f,-.75f);glVertex2f(0,.75f);glEnd();glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels[route]);
  }
  assert(!memcmp(pixels[0],pixels[1],sizeof pixels[0]));for(unsigned i=0;i<64*64;i++)if(pixels[1][i*4+1])coverage[m]++;
 }
 assert(coverage[0]>0&&coverage[1]>coverage[0]&&coverage[2]>coverage[1]);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glColor4f(1,1,1,1);assert(!glGetError());
 puts("Polygon mode: independent faces, fill/line/point native framebuffer coverage, invalid and begin-state errors, retained errors and CPU ABI PASS");
}
static void scene_alpha(void){
 strcpy((char *)memory+0x24000,"glAlphaFunc");assert(CALL(63,0x24000)==0xfffa07b0);
 GLfloat refs[]={-1,0,.1f,.5f,1,2};
 for(unsigned fn=GL_NEVER;fn<=GL_ALWAYS;fn++)for(unsigned i=0;i<6;i++){
  GLfloat expected,actual;uint32_t bits;memcpy(&bits,&refs[i],4);glAlphaFunc(fn,refs[i]);glGetFloatv(GL_ALPHA_TEST_REF,&expected);
  GC(0xfffa07b0,0,fn,bits);GLint function;glGetIntegerv(GL_ALPHA_TEST_FUNC,&function);glGetFloatv(GL_ALPHA_TEST_REF,&actual);assert(function==(GLint)fn&&actual==expected);
 }
 GC(0xfffa07b0,0,GL_GREATER,0x3dcccccd);GC(0xfffa07b0,0,0xffffffff,0);assert(glGetError()==GL_INVALID_ENUM);GLint fn;glGetIntegerv(GL_ALPHA_TEST_FUNC,&fn);assert(fn==GL_GREATER);
 glBegin(GL_POINTS);GC(0xfffa07b0,0,GL_LESS,0);glEnd();assert(glGetError()==GL_INVALID_OPERATION);glGetIntegerv(GL_ALPHA_TEST_FUNC,&fn);assert(fn==GL_GREATER);
 glUseProgram(0);glDisable(GL_LIGHTING);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_FOG);glEnable(GL_ALPHA_TEST);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();glViewport(0,0,64,64);unsigned char pixels[2][64*64*4];
 for(unsigned route=0;route<2;route++){
  glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glAlphaFunc(GL_ALWAYS,0);
  if(!route)glAlphaFunc(GL_GREATER,.1f);else{glEnable(0xffffffff);GC(0xfffa07b0,0,GL_GREATER,0x3dcccccd);assert(glGetError()==GL_INVALID_ENUM);}
  glBegin(GL_QUADS);glColor4f(1,0,0,.05f);glVertex2f(-1,-1);glVertex2f(0,-1);glVertex2f(0,1);glVertex2f(-1,1);glColor4f(0,1,0,.5f);glVertex2f(0,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(0,1);glEnd();
  glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels[route]);
 }
 assert(!memcmp(pixels[0],pixels[1],sizeof pixels[0]));assert(pixels[1][(32*64+16)*4+2]==255&&pixels[1][(32*64+48)*4+1]==255);
 glDisable(GL_ALPHA_TEST);glAlphaFunc(GL_ALWAYS,0);glColor4f(1,1,1,1);assert(!glGetError());
 puts("Alpha test: all comparisons and clamped references, original GREATER/0.1 pixel rejection, native framebuffer equality, retained errors and CPU ABI PASS");
}
static void fog_arrays(const char *fault){
 strcpy((char *)memory+0x24000,"glFogCoordPointerEXT");assert(CALL(63,0x24000)==0xfffa07a0);
 GLfloat vertices[]={-.8f,-.8f,.8f,-.8f,.8f,.8f,-.8f,.8f},blue[]={0,0,1,1};
 unsigned char indices[]={0,1,2,0,2,3},pixels[2][64*64*4];
 memcpy(memory+0x7200,vertices,sizeof vertices);memcpy(memory+0x7300,indices,sizeof indices);
 GC(0xfffa0300,0,2,GL_FLOAT,0,0x7200);GC(0xfffa02e0,0,GL_VERTEX_ARRAY);GC(0xfffa02e0,0,GL_FOG_COORD_ARRAY);
 if(fault&&!strcmp(fault,"fog-array-undefined")){GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_BYTE,0x7300);assert(0);}
 if(fault&&!strcmp(fault,"fog-array-vbo")){GLuint b;glGenBuffers(1,&b);glBindBuffer(GL_ARRAY_BUFFER,b);GC(0xfffa07a0,0,GL_FLOAT,32,0x8f9c);assert(0);}
 if(fault&&!strcmp(fault,"fog-array-null")){GC(0xfffa07a0,0,GL_FLOAT,0,0);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_BYTE,0x7300);assert(0);}
 if(fault&&!strcmp(fault,"fog-array-overflow")){GC(0xfffa07a0,0,GL_DOUBLE,32,0xfffffff8);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_BYTE,0x7300);assert(0);}
 if(fault&&!strcmp(fault,"fog-array-range")){assert(tribes_memory_permissions_set(0x9000,4096,0));GC(0xfffa07a0,0,GL_FLOAT,32,0x8f9d);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_BYTE,0x7300);assert(0);}
 glUseProgram(0);glDisable(GL_LIGHTING);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glEnable(GL_FOG);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();glViewport(0,0,64,64);
 glFogi(GL_FOG_COORDINATE_SOURCE,GL_FOG_COORDINATE);glFogi(GL_FOG_MODE,GL_LINEAR);glFogf(GL_FOG_START,0);glFogf(GL_FOG_END,1);glFogfv(GL_FOG_COLOR,blue);glColor4f(1,0,0,1);
 for(unsigned kind=0;kind<3;kind++){
  GLenum type=kind==1?GL_DOUBLE:GL_FLOAT;unsigned bytes=kind==1?8:4,address=0x9000-96-bytes-(kind==2);unsigned char source[104]={0};
  for(unsigned i=0;i<4;i++){double d=i/3.;float f=(float)d;memcpy(source+32*i,kind==1?(void *)&d:(void *)&f,bytes);}
  memcpy(memory+address,source,96+bytes);
  assert(tribes_memory_permissions_set(0x8000,4096,1)&&tribes_memory_permissions_set(0x9000,4096,0));
  for(unsigned route=0;route<2;route++){
   glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
   if(!route){glVertexPointer(2,GL_FLOAT,0,vertices);glFogCoordPointerEXT(type,32,source);glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_BYTE,indices);}
   else{GC(0xfffa07a0,0,type,32,address);GC(0xfffa07a0,0,GL_INT,32,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);GC(0xfffa07a0,0,type,0xffffffff,0xffffffff);assert(glGetError()==GL_INVALID_VALUE);glEnable(0xffffffff);GC(0xfffa0350,0,GL_TRIANGLES,6,GL_UNSIGNED_BYTE,0x7300);assert(glGetError()==GL_INVALID_ENUM);void *ptr=(void *)1;glGetPointerv(GL_FOG_COORD_ARRAY_POINTER,&ptr);assert(!ptr);}
   glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels[route]);assert(!glGetError());
  }
  assert(!memcmp(pixels[0],pixels[1],sizeof pixels[0]));unsigned center=(32*64+32)*4;
  assert(pixels[1][center]>20&&pixels[1][center]<235&&pixels[1][center+2]>20&&pixels[1][center+2]<235);
  assert(!memcmp(memory+address,source,96+bytes));
  assert(tribes_memory_permissions_set(0x8000,4096,3)&&tribes_memory_permissions_set(0x9000,4096,3));
 }
 GC(0xfffa02f0,0,GL_FOG_COORD_ARRAY);GC(0xfffa02f0,0,GL_VERTEX_ARRAY);glDisable(GL_FOG);glFogi(GL_FOG_COORDINATE_SOURCE,GL_FRAGMENT_DEPTH);glColor4f(1,1,1,1);
 puts("Fog arrays: checked indexed float/double stride32, unaligned/page-end reads, native framebuffer equality, errors, pointer detachment and CPU ABI PASS");
}
static void scene_fog(const char *fault){
 const char *names[]={"glFogi","glFogf","glFogfv"};
 for(unsigned i=0;i<3;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa0770+16*i);}
 if(fault&&!strcmp(fault,"fog-range")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0790,0,GL_FOG_COLOR,0x3ff1);assert(0);}
 if(fault&&!strcmp(fault,"fog-null")){GC(0xfffa0790,0,GL_FOG_COLOR,0);assert(0);}
 if(fault&&!strcmp(fault,"fog-overflow")){GC(0xfffa0790,0,GL_FOG_COLOR,0xfffffff8);assert(0);}
 GLfloat color[4]={.25f,.5f,.75f,1},expected[4],actual[4];
 const GLenum props[]={GL_FOG_COLOR,GL_FOG_MODE,GL_FOG_DENSITY,GL_FOG_START,GL_FOG_END,GL_FOG_INDEX,GL_FOG_COORDINATE_SOURCE};
 const GLfloat scalars[]={0,GL_LINEAR,.375f,-2,7,2,GL_FOG_COORDINATE};
 for(unsigned i=0;i<7;i++){
  unsigned count=i?1:4;GLfloat value[4];memcpy(value,color,16);if(i)value[0]=scalars[i];
  glFogfv(props[i],value);glGetFloatv(props[i],expected);
  if(i==0){GLfloat clear[4]={0};glFogfv(props[i],clear);}else glFogf(props[i],i==1?GL_EXP:i==6?GL_FRAGMENT_DEPTH:0);
  uint32_t address=0x4000-count*4;memcpy(memory+address,value,count*4);
  assert(tribes_memory_permissions_set(0x3000,4096,1)&&tribes_memory_permissions_set(0x4000,4096,0));
  glEnable(0xffffffff);GC(0xfffa0790,0,props[i],address);glGetFloatv(props[i],actual);
  assert(!memcmp(actual,expected,count*4)&&!memcmp(memory+address,value,count*4));
  assert(glGetError()==GL_INVALID_ENUM&&!glGetError());
  assert(tribes_memory_permissions_set(0x3000,4096,3)&&tribes_memory_permissions_set(0x4000,4096,3));
  if(i){uint32_t bits;memcpy(&bits,value,4);GC(0xfffa0780,0,props[i],bits);glGetFloatv(props[i],actual);assert(actual[0]==expected[0]);}
 }
 GC(0xfffa0770,0,GL_FOG_COORDINATE_SOURCE,GL_FOG_COORDINATE);GLint integer=0;glGetIntegerv(GL_FOG_COORDINATE_SOURCE,&integer);assert(integer==GL_FOG_COORDINATE);
 GC(0xfffa0770,0,GL_FOG_MODE,GL_LINEAR);glGetIntegerv(GL_FOG_MODE,&integer);assert(integer==GL_LINEAR);
 GC(0xfffa0770,0,GL_FOG_START,(uint32_t)-3);glGetFloatv(GL_FOG_START,actual);assert(actual[0]==-3);
 GC(0xfffa0780,0,GL_FOG_DENSITY,0xbf800000);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa0770,0,GL_FOG_MODE,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);
 assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0790,0,0xffffffff,0x4000);assert(glGetError()==GL_INVALID_ENUM);assert(tribes_memory_permissions_set(0x4000,4096,3));
 glGetFloatv(GL_FOG_COLOR,expected);memcpy(memory+0x3001,color,16);glBegin(GL_POINTS);GC(0xfffa0790,0,GL_FOG_COLOR,0x3001);glEnd();assert(glGetError()==GL_INVALID_OPERATION);glGetFloatv(GL_FOG_COLOR,actual);assert(!memcmp(actual,expected,16));
 glUseProgram(0);glDisable(GL_LIGHTING);glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glEnable(GL_FOG);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();glViewport(0,0,64,64);
 unsigned char pixels[2][64*64*4];GLfloat blue[4]={0,0,1,1};
 for(unsigned route=0;route<2;route++){
  glFogi(GL_FOG_COORDINATE_SOURCE,GL_FRAGMENT_DEPTH);glFogi(GL_FOG_MODE,GL_EXP);glFogf(GL_FOG_START,-1);glFogf(GL_FOG_END,4);
  if(!route){glFogi(GL_FOG_COORDINATE_SOURCE,GL_FOG_COORDINATE);glFogi(GL_FOG_MODE,GL_LINEAR);glFogf(GL_FOG_START,0);glFogf(GL_FOG_END,1);glFogfv(GL_FOG_COLOR,blue);}
  else{memcpy(memory+0x3001,blue,16);GC(0xfffa0770,0,GL_FOG_COORDINATE_SOURCE,GL_FOG_COORDINATE);GC(0xfffa0770,0,GL_FOG_MODE,GL_LINEAR);GC(0xfffa0780,0,GL_FOG_START,0);GC(0xfffa0780,0,GL_FOG_END,0x3f800000);GC(0xfffa0790,0,GL_FOG_COLOR,0x3001);}
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glColor4f(1,0,0,1);glFogCoordf(.5f);
  glBegin(GL_QUADS);glVertex2f(-.5f,-.5f);glVertex2f(.5f,-.5f);glVertex2f(.5f,.5f);glVertex2f(-.5f,.5f);glEnd();glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels[route]);
 }
 assert(!memcmp(pixels[0],pixels[1],sizeof pixels[0]));unsigned center=(32*64+32)*4;
 assert(pixels[1][center]>120&&pixels[1][center]<136&&pixels[1][center+1]==0&&pixels[1][center+2]>120&&pixels[1][center+2]<136);
 assert(!glGetError());glDisable(GL_FOG);glFogi(GL_FOG_COORDINATE_SOURCE,GL_FRAGMENT_DEPTH);glColor4f(1,1,1,1);
 puts("Fog: native integer/float/vector state, signed scalars, checked exact extents, errors/CPU preservation and native full framebuffer comparison PASS");
}

static void scene_lighting(const char *fault){
 const char *names[]={"glLightfv","glLightf","glLightModelfv"};
 for(unsigned i=0;i<3;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa0720+16*i);}
 GLfloat actual[4],expected[4],zero[4]={0},value[4]={.25f,.5f,.75f,1};
 memcpy(memory+0x3ff0,value,16);
 if(fault&&!strcmp(fault,"light-vector-range")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0720,0,GL_LIGHT0,GL_POSITION,0x3ff1);assert(0);}
 if(fault&&!strcmp(fault,"light-model-range")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0740,0,GL_LIGHT_MODEL_AMBIENT,0x3ff1);assert(0);}
 if(fault&&!strcmp(fault,"light-vector-overflow")){GC(0xfffa0720,0,GL_LIGHT0,GL_POSITION,0xfffffff8);assert(0);}
 GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0250,0);glTranslatef(2,-3,4);glRotatef(31,1,2,3);
 const GLenum props[]={GL_AMBIENT,GL_DIFFUSE,GL_SPECULAR,GL_POSITION,GL_SPOT_DIRECTION,GL_SPOT_EXPONENT,GL_SPOT_CUTOFF,GL_CONSTANT_ATTENUATION,GL_LINEAR_ATTENUATION,GL_QUADRATIC_ATTENUATION};
 for(unsigned i=0;i<10;i++){
  unsigned count=i<4?4:i==4?3:1;GLenum name=props[i];
  value[0]=i==6?45:.25f;value[1]=-.5f;value[2]=.75f;value[3]=1;
  glLightfv(GL_LIGHT0,name,value);glGetLightfv(GL_LIGHT0,name,expected);
  glLightfv(GL_LIGHT0,name,zero);
  uint32_t address=0x4000-4*count;memcpy(memory+address,value,4*count);
  assert(tribes_memory_permissions_set(0x3000,4096,1)&&tribes_memory_permissions_set(0x4000,4096,0));
  glEnable(0xffffffff);GC(0xfffa0720,0,GL_LIGHT0,name,address);glGetLightfv(GL_LIGHT0,name,actual);
  assert(!memcmp(actual,expected,4*count)&&glGetError()==GL_INVALID_ENUM&&!glGetError());
  assert(tribes_memory_permissions_set(0x3000,4096,3)&&tribes_memory_permissions_set(0x4000,4096,3));
 }
 for(unsigned i=5;i<10;i++){
  GLfloat scalar=i==6?90:2;uint32_t bits;memcpy(&bits,&scalar,4);
  glLightf(GL_LIGHT1,props[i],scalar);glGetLightfv(GL_LIGHT1,props[i],expected);glLightf(GL_LIGHT1,props[i],0);
  GC(0xfffa0730,0,GL_LIGHT1,props[i],bits);glGetLightfv(GL_LIGHT1,props[i],actual);assert(actual[0]==expected[0]);
 }
 assert(!glGetError());GC(0xfffa0730,0,GL_LIGHT1,GL_LINEAR_ATTENUATION,0xbf800000);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa0730,0,GL_LIGHT1,GL_POSITION,0);assert(glGetError()==GL_INVALID_ENUM);
 assert(tribes_memory_permissions_set(0x4000,4096,0));
 GC(0xfffa0720,0,0xffffffff,GL_POSITION,0x4000);assert(glGetError()==GL_INVALID_ENUM);
 GC(0xfffa0720,0,GL_LIGHT0,0xffffffff,0x4000);assert(glGetError()==GL_INVALID_ENUM);
 GC(0xfffa0740,0,0xffffffff,0x4000);assert(glGetError()==GL_INVALID_ENUM);
 assert(tribes_memory_permissions_set(0x4000,4096,3));
 const GLenum models[]={GL_LIGHT_MODEL_AMBIENT,GL_LIGHT_MODEL_LOCAL_VIEWER,GL_LIGHT_MODEL_TWO_SIDE,GL_LIGHT_MODEL_COLOR_CONTROL};
 for(unsigned i=0;i<4;i++){
  GLfloat model[4]={i==3?(GLfloat)GL_SEPARATE_SPECULAR_COLOR:1,.2f,.3f,1};unsigned count=i?1:4;
  glLightModelfv(models[i],model);glGetFloatv(models[i],expected);
  GLfloat reset[4]={i==3?(GLfloat)GL_SINGLE_COLOR:0,0,0,0};glLightModelfv(models[i],reset);
  uint32_t address=0x4000-count*4;memcpy(memory+address,model,count*4);assert(tribes_memory_permissions_set(0x4000,4096,0));
  GC(0xfffa0740,0,models[i],address);glGetFloatv(models[i],actual);assert(!memcmp(expected,actual,count*4));
  assert(tribes_memory_permissions_set(0x4000,4096,3));
 }
 /* A real lit triangle: ambient-only illumination isolates the light from
    normals and proves that native lighting affects framebuffer pixels. */
 glLoadIdentity();glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);
 glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_COLOR_MATERIAL);glDisable(GL_CULL_FACE);
 glViewport(0,0,64,64);GLint maximum=0;glGetIntegerv(GL_MAX_LIGHTS,&maximum);for(GLint i=0;i<maximum;i++)glDisable(GL_LIGHT0+i);
 GLfloat white[4]={1,1,1,1},ambient[4]={.5f,.25f,.75f,1};glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT,white);
 glMaterialfv(GL_FRONT_AND_BACK,GL_DIFFUSE,zero);glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,zero);glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,zero);
 glLightModelfv(GL_LIGHT_MODEL_AMBIENT,zero);glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,0);glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER,0);glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL,GL_SINGLE_COLOR);
 glLightfv(GL_LIGHT0,GL_DIFFUSE,zero);glLightfv(GL_LIGHT0,GL_SPECULAR,zero);glLightfv(GL_LIGHT0,GL_AMBIENT,zero);
 GLfloat directional[4]={0,0,1,0};glLightfv(GL_LIGHT0,GL_POSITION,directional);glLightf(GL_LIGHT0,GL_SPOT_CUTOFF,180);
 memcpy(memory+0x3001,ambient,16);GC(0xfffa0720,0,GL_LIGHT0,GL_AMBIENT,0x3001);
 glEnable(GL_LIGHT0);glEnable(GL_LIGHTING);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
 glBegin(GL_TRIANGLES);glVertex2f(-.8f,-.8f);glVertex2f(.8f,-.8f);glVertex2f(0,.8f);glEnd();
 unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
 assert(pixel[0]>=127&&pixel[0]<=129&&pixel[1]>=63&&pixel[1]<=65&&pixel[2]>=190&&pixel[2]<=192);
 glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(!pixel[0]&&!pixel[1]&&!pixel[2]);assert(!glGetError());
 glGetLightfv(GL_LIGHT0,GL_AMBIENT,expected);glBegin(GL_TRIANGLES);GC(0xfffa0720,0,GL_LIGHT0,GL_AMBIENT,0x3001);GC(0xfffa0730,0,GL_LIGHT0,GL_SPOT_CUTOFF,0);GC(0xfffa0740,0,GL_LIGHT_MODEL_AMBIENT,0x3001);glEnd();
 assert(glGetError()==GL_INVALID_OPERATION);glGetLightfv(GL_LIGHT0,GL_AMBIENT,actual);assert(!memcmp(actual,expected,16));
 glDisable(GL_LIGHTING);glDisable(GL_LIGHT0);assert(!glGetError());
 puts("Original scene lights: exact checked vector widths, transformed position/direction, scalar attenuation, model state, lit pixels, errors and CPU ABI PASS");
}
static void render_state(const char *fault){
 /* Original reset at0x447239: native fog disable retains pointer/other state
    and consumes no inaccessible client data. */
 glFogCoordPointerEXT(GL_FLOAT,0,memory+0x23000);glEnableClientState(GL_FOG_COORD_ARRAY);
 glEnableClientState(GL_VERTEX_ARRAY);GC(0xfffa00e0,0,GL_TEXTURE1);
 assert(tribes_memory_permissions_set(0x23000,4096,0));
 GC(0xfffa02f0,0,GL_FOG_COORD_ARRAY);assert(!glIsEnabled(GL_FOG_COORD_ARRAY)&&glIsEnabled(GL_VERTEX_ARRAY));
 void *fog_pointer=NULL;glGetPointerv(GL_FOG_COORD_ARRAY_POINTER,&fog_pointer);assert(fog_pointer==memory+0x23000);
 GLint fog_unit=0;glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&fog_unit);assert(fog_unit==GL_TEXTURE1&&!glGetError());
 glGetString(0xffffffff);GC(0xfffa02f0,0,GL_FOG_COORD_ARRAY);assert(glGetError()==GL_INVALID_ENUM);
 GC(0xfffa02f0,0,GL_FOG_COORD_ARRAY);assert(!glGetError());
 assert(tribes_memory_permissions_set(0x23000,4096,3));glFogCoordPointerEXT(GL_FLOAT,0,NULL);
 glDisableClientState(GL_VERTEX_ARRAY);GC(0xfffa00e0,0,GL_TEXTURE0);
 puts("Original fog-array reset: native disable, protected data isolation, pointer/client-unit preservation and retained errors PASS");
 const char *names[]={"glFrontFace","glDepthMask","glIsEnabled","glColor4fv","glMaterialfv","glTexEnvfv","glTexEnvf","glNormalPointer"};
 for(unsigned i=0;i<8;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa05d0+16*i);}
 GC(0xfffa05d0,0,GL_CW);GLint winding=0;glGetIntegerv(GL_FRONT_FACE,&winding);assert(winding==GL_CW);
 GC(0xfffa05d0,0,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);glGetIntegerv(GL_FRONT_FACE,&winding);assert(winding==GL_CW);GC(0xfffa05d0,0,GL_CCW);
 /* Windows GLboolean is an eight-bit argument, rather than a 32-bit bool. */
 GC(0xfffa05e0,0,0x100);GLboolean depth=1;glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);assert(!depth);
 GC(0xfffa05e0,0,0x101);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);assert(depth);
 GC(0xfffa0280,0,GL_LIGHTING);assert(!GC(0xfffa05f0,1,GL_LIGHTING));GC(0xfffa0290,0,GL_LIGHTING);assert(GC(0xfffa05f0,1,GL_LIGHTING)==1);GC(0xfffa0280,0,GL_LIGHTING);
 glGetString(0xffffffff);assert(!GC(0xfffa05f0,1,GL_LIGHTING)&&glGetError()==GL_INVALID_ENUM);assert(!GC(0xfffa05f0,1,0xffffffff)&&glGetError()==GL_INVALID_ENUM);
 const GLfloat color[]={.25f,.5f,.75f,1};memcpy(memory+0x3ff0,color,sizeof color);
 if(fault&&!strcmp(fault,"state-color")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0600,0,0x3ff1);assert(0);}
 if(fault&&!strcmp(fault,"state-material")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0610,0,GL_FRONT,GL_DIFFUSE,0x3ff1);assert(0);}
 if(fault&&!strcmp(fault,"state-texenv")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0620,0,GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,0x3ff1);assert(0);}
 assert(tribes_memory_permissions_set(0x3000,4096,1)&&tribes_memory_permissions_set(0x4000,4096,0));
 GLfloat actual[4];GC(0xfffa0600,0,0x3ff0);glGetFloatv(GL_CURRENT_COLOR,actual);assert(!memcmp(actual,color,sizeof color));
 const GLenum materials[]={GL_AMBIENT,GL_DIFFUSE,GL_SPECULAR,GL_EMISSION,GL_AMBIENT_AND_DIFFUSE};
 for(unsigned i=0;i<5;i++){GC(0xfffa0610,0,GL_FRONT_AND_BACK,materials[i],0x3ff0);glGetMaterialfv(GL_FRONT,materials[i]==GL_AMBIENT_AND_DIFFUSE?GL_DIFFUSE:materials[i],actual);assert(!memcmp(actual,color,sizeof color));}
 GC(0xfffa0610,0,GL_BACK,GL_SHININESS,0x3ffc);glGetMaterialfv(GL_BACK,GL_SHININESS,actual);assert(actual[0]==1);
 GC(0xfffa0610,0,GL_FRONT,GL_COLOR_INDEXES,0x3ff4);glGetMaterialfv(GL_FRONT,GL_COLOR_INDEXES,actual);assert(!memcmp(actual,color+1,12));
 GC(0xfffa0610,0,0xffffffff,GL_DIFFUSE,0x4000);assert(glGetError()==GL_INVALID_ENUM);
 GC(0xfffa0620,0,GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,0x3ff0);glGetTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,actual);assert(!memcmp(actual,color,sizeof color));
 GC(0xfffa0620,0,GL_TEXTURE_ENV,GL_RGB_SCALE,0x3ffc);glGetTexEnvfv(GL_TEXTURE_ENV,GL_RGB_SCALE,actual);assert(actual[0]==1);
 GC(0xfffa0630,0,GL_TEXTURE_ENV,GL_RGB_SCALE,0x40000000);glGetTexEnvfv(GL_TEXTURE_ENV,GL_RGB_SCALE,actual);assert(actual[0]==2);GC(0xfffa0630,0,GL_TEXTURE_ENV,GL_RGB_SCALE,0x3f800000);
 GC(0xfffa0620,0,0xffffffff,GL_TEXTURE_ENV_COLOR,0x4000);assert(glGetError()==GL_INVALID_ENUM);
 GC(0xfffa0630,0,GL_TEXTURE_ENV,GL_RGB_SCALE,0x40400000);assert(glGetError()==GL_INVALID_VALUE);
 assert(tribes_memory_permissions_set(0x3000,4096,3)&&tribes_memory_permissions_set(0x4000,4096,3));
 GC(0xfffa0240,0,GL_PROJECTION);GC(0xfffa0250,0);GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0250,0);GC(0xfffa0270,0,0,0,64,64);
 glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_TEXTURE_2D);
 GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER),program=glCreateProgram();
 const char *v="#version 120\nvarying vec3 n;void main(){gl_Position=gl_Vertex;n=abs(gl_Normal);}",*f="#version 120\nvarying vec3 n;void main(){gl_FragColor=vec4(n,1.0);}";
 glShaderSource(vs,1,&v,NULL);glCompileShader(vs);glShaderSource(fs,1,&f,NULL);glCompileShader(fs);glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);GC(0xfffa0320,0,program);
 const GLfloat vertices[]={-1,-1,0,0,1,-1,0,0,0,1};const unsigned char indices[]={0,2,4};memcpy(memory+0x8101,vertices,sizeof vertices);memcpy(memory+0x7101,indices,sizeof indices);
 GC(0xfffa01c0,0,GL_ARRAY_BUFFER,0);GC(0xfffa0300,0,2,GL_FLOAT,0,0x8101);GC(0xfffa02e0,0,GL_VERTEX_ARRAY);GC(0xfffa02e0,0,GL_NORMAL_ARRAY);assert(GC(0xfffa05f0,1,GL_NORMAL_ARRAY));
 const GLenum types[]={GL_BYTE,GL_SHORT,GL_INT,GL_FLOAT,GL_DOUBLE};
 for(unsigned t=0;t<5;t++){
  unsigned bytes=t==0?1:t==1?2:t==4?8:4;unsigned char value[24]={0};
  if(t==0){int8_t x=127;memcpy(value+bytes,&x,bytes);}else if(t==1){int16_t x=32767;memcpy(value+bytes,&x,bytes);}else if(t==2){int32_t x=INT32_MAX;memcpy(value+bytes,&x,bytes);}else if(t==3){GLfloat x=1;memcpy(value+bytes,&x,bytes);}else{GLdouble x=1;memcpy(value+bytes,&x,bytes);}
  memset(memory+0xa001,0,3*bytes);GC(0xfffa0640,0,types[t],4096,0xa001);
  for(unsigned i=0;i<3;i++)memcpy(memory+0xa001+8192*i,value,3*bytes);
  assert(tribes_memory_permissions_set(0xb000,4096,0)&&tribes_memory_permissions_set(0xd000,4096,0));
  if(fault&&!strcmp(fault,"normal-unreadable")){assert(tribes_memory_permissions_set(0xc000,4096,0));GC(0xfffa0350,0,GL_TRIANGLES,3,GL_UNSIGNED_BYTE,0x7101);assert(0);}
  if(fault&&!strcmp(fault,"normal-range")){GC(0xfffa0640,0,GL_FLOAT,4096,0xaff8);GC(0xfffa0350,0,GL_TRIANGLES,3,GL_UNSIGNED_BYTE,0x7101);assert(0);}
  if(fault&&!strcmp(fault,"normal-overflow")){GC(0xfffa0640,0,GL_FLOAT,4096,0xfffffff8);GC(0xfffa0350,0,GL_TRIANGLES,3,GL_UNSIGNED_BYTE,0x7101);assert(0);}
  GC(0xfffa0640,0,GL_UNSIGNED_BYTE,0,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);GC(0xfffa0640,0,GL_FLOAT,0xffffffff,0xffffffff);assert(glGetError()==GL_INVALID_VALUE);
  GC(0xfffa01b0,0,1,0x3011);GLuint buffer=word(0x3011);GC(0xfffa01c0,0,GL_ARRAY_BUFFER,buffer);
  if(fault&&!strcmp(fault,"normal-vbo")){GC(0xfffa0640,0,GL_FLOAT,0,0);assert(0);}
  GC(0xfffa00e0,0,GL_TEXTURE1);GC(0xfffa0060,0,0,0,0,0x3f800000);GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);GC(0xfffa0350,0,GL_TRIANGLES,3,GL_UNSIGNED_BYTE,0x7101);
  unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==255&&pixel[2]==0&&pixel[3]==255);
  void *pointer=(void *)1;glGetPointerv(GL_NORMAL_ARRAY_POINTER,&pointer);assert(!pointer);GLint bound=0,unit=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&bound);glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&unit);assert((GLuint)bound==buffer&&unit==GL_TEXTURE1);
  GC(0xfffa01e0,0,1,0x3011);assert(tribes_memory_permissions_set(0xb000,4096,3)&&tribes_memory_permissions_set(0xd000,4096,3));
 }
 GC(0xfffa02f0,0,GL_NORMAL_ARRAY);assert(!GC(0xfffa05f0,1,GL_NORMAL_ARRAY));GC(0xfffa0640,0,GL_FLOAT,0,0xfffffff8);glNormal3f(1,0,0);
 GC(0xfffa0350,0,GL_TRIANGLES,3,GL_UNSIGNED_BYTE,0x7101);unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==255&&pixel[1]==0&&pixel[2]==0);
 GC(0xfffa02f0,0,GL_VERTEX_ARRAY);GC(0xfffa00e0,0,GL_TEXTURE0);GC(0xfffa0320,0,0);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);assert(!glGetError());
 puts("Original 3D state: native winding/depth/enable/color/material/texture state and five normal formats, sparse checked draw-time data, pixels and detached pointers PASS");
}
static void polygon_offset(void){
 const uint32_t address=0xfffa0690;
 strcpy((char *)memory+0x24000,"opengl32");uint32_t module=CALL(60,0x24000);assert(module==0x20040000);
 strcpy((char *)memory+0x24000,"glPolygonOffset");assert(CALL(63,0x24000)==address&&tribes_get_proc_address(cpu._pointer_offset,module,0x24000)==address);CALL(62,module);
 const GLfloat values[][2]={{-2,-2},{.25f,-7.5f},{0,0},{-1.25f,16}};
 for(unsigned i=0;i<sizeof values/sizeof *values;i++){
  uint32_t bits[2];memcpy(bits,values[i],sizeof bits);GC(address,0,bits[0],bits[1]);
  GLfloat factor,units;glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&units);
  assert(factor==values[i][0]&&units==values[i][1]&&!glGetError());
 }
 glGetString(0xffffffff);GC(address,0,0xc0000000,0xc0000000);assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));
 glBegin(GL_TRIANGLES);GC(address,0,0,0);glEnd();assert(GC(0xfffa0010,1)==GL_INVALID_OPERATION&&!GC(0xfffa0010,1));
 GLfloat factor,units;glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&factor);glGetFloatv(GL_POLYGON_OFFSET_UNITS,&units);assert(factor==-2&&units==-2);GC(address,0,0,0);
 puts("Polygon offset: original shadow arguments, native factor/units, retained errors and rejected begin-state mutation PASS");
}
static void matrix_multiplication(const char *fault,int kind){
 const int doubles=kind==1,load=kind==2;
 const uint32_t address=load?0xfffa07f0:doubles?0xfffa0680:0xfffa05c0;
 const char *prefix=load?"matrix-load-":doubles?"matrix-double-":"matrix-mult-";
 const size_t bytes=doubles?128:64;
 if(fault){
  if(!strncmp(fault,prefix,strlen(prefix))){
   const GLdouble identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};if(doubles)memcpy(memory+0x3ff1,identity,128);else{GLfloat f[16];for(unsigned i=0;i<16;i++)f[i]=(GLfloat)identity[i];memcpy(memory+0x3ff1,f,64);}
   if(strstr(fault,"unreadable")!=NULL){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(address,0,0x3ff1);}
   else if(strstr(fault,"overflow")!=NULL)GC(address,0,0xfffffff0);
   else if(strstr(fault,"null")!=NULL)GC(address,0,0);
   assert(0);
  }return;
 }
 strcpy((char *)memory+0x24000,"opengl32");uint32_t module=CALL(60,0x24000);assert(module==0x20040000);
 strcpy((char *)memory+0x24000,load?"glLoadMatrixf":doubles?"glMultMatrixd":"glMultMatrixf");assert(CALL(63,0x24000)==address&&tribes_get_proc_address(cpu._pointer_offset,module,0x24000)==address);CALL(62,module);
 const GLdouble original[16]={1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1},asymmetric[16]={1,2,0,0,.25,1,0,0,0,.5,2,0,3,-2,1,1};
 if(doubles){
  memcpy(memory+0x3f80,original,128);assert(tribes_memory_permissions_set(0x4000,4096,0));
  GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0250,0);GC(address,0,0x3f80);
  GLdouble actual[16];glGetDoublev(GL_MODELVIEW_MATRIX,actual);assert(!memcmp(original,actual,sizeof actual));
  assert(tribes_memory_permissions_set(0x4000,4096,3));GC(0xfffa0250,0);
 }
 // Original TribesNEXT input captured at glMultMatrixd, return0x10032ac8.
 const GLdouble model[16]={0.0018420678097754717,0.005896706599742174,0.999961256980896,0.0,0.017905060201883316,0.9998221397399902,-0.005928400903940201,0.0,-0.9998183250427246,0.017915522679686546,0.0017361606005579233,0.0,-4.069979020471237e-09,1.6039540469137137e-06,1.4516024589538574,1.0};
 if(load){GLfloat f[16];for(unsigned i=0;i<16;i++)f[i]=(GLfloat)asymmetric[i];memcpy(memory+0x3fc0,f,64);assert(tribes_memory_permissions_set(0x3000,4096,1)&&tribes_memory_permissions_set(0x4000,4096,0));GC(address,0,0x3fc0);assert(tribes_memory_permissions_set(0x3000,4096,3)&&tribes_memory_permissions_set(0x4000,4096,3));}
 const GLenum modes[]={GL_PROJECTION,GL_MODELVIEW,GL_TEXTURE},queries[]={GL_PROJECTION_MATRIX,GL_MODELVIEW_MATRIX,GL_TEXTURE_MATRIX};
 for(unsigned mode=0;mode<3;mode++){
  GLdouble current[16]={2,0,0,0,0,3,0,0,0,0,4,0,.25,-.5,1,1},actual[16];
  memcpy(memory+0x3000,current,sizeof current);GC(0xfffa0240,0,modes[mode]);GC(0xfffa0260,0,0x3000);
  for(unsigned pass=0;pass<(doubles?3u:2u);pass++){
   const GLdouble *input=pass==2?model:pass?asymmetric:original;GLfloat f[16];for(unsigned i=0;i<16;i++)f[i]=(GLfloat)input[i];
   memcpy(memory+0x3ff1,doubles?(const void *)input:(const void *)f,bytes);
   unsigned char unchanged[128];memcpy(unchanged,memory+0x3ff1,bytes);assert(tribes_memory_permissions_set(0x4000,4096,1));
   GC(address,0,0x3ff1);assert(!memcmp(unchanged,memory+0x3ff1,bytes));assert(tribes_memory_permissions_set(0x4000,4096,3));
   GLdouble product[16];for(unsigned col=0;col<4;col++)for(unsigned row=0;row<4;row++){
    double value=0;for(unsigned k=0;k<4;k++)value+=current[k*4+row]*input[col*4+k];product[col*4+row]=load?f[col*4+row]:value;
   }
   glGetDoublev(queries[mode],actual);for(unsigned i=0;i<16;i++)assert(fabs(actual[i]-product[i])<1e-6*fmax(1,fabs(product[i])));
   memcpy(current,product,sizeof current);assert(!glGetError());
  }
  /* GL keeps its own matrix value; changing guest storage cannot alter it. */
  memset(memory+0x3ff1,0,bytes);GLdouble retained[16];glGetDoublev(queries[mode],retained);assert(!memcmp(actual,retained,sizeof actual));
  GC(0xfffa0250,0);
 }
 GC(0xfffa0240,0,GL_MODELVIEW);if(doubles)memcpy(memory+0x3ff1,original,128);else{GLfloat f[16];for(unsigned i=0;i<16;i++)f[i]=(GLfloat)original[i];memcpy(memory+0x3ff1,f,64);}glGetString(0xffffffff);GC(address,0,0x3ff1);assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));
 GLdouble before[16],after[16];glGetDoublev(GL_MODELVIEW_MATRIX,before);glBegin(GL_TRIANGLES);GC(address,0,0x3ff1);glEnd();assert(GC(0xfffa0010,1)==GL_INVALID_OPERATION&&!GC(0xfffa0010,1));
 glGetDoublev(GL_MODELVIEW_MATRIX,after);assert(!memcmp(before,after,sizeof before));GC(0xfffa0250,0);
 printf("Matrix operation (%s): original coordinates, noncommuting repeated products in all modes, unaligned/read-only guest staging, retained values/errors and invalid-operation preservation pass\n",load?"float load":doubles?"double multiply":"float multiply");
}
static void legacy_route(uint32_t original_window,uint32_t original_context,const char *fault){
 assert(CALL(53,SDL_GL_CONTEXT_MAJOR_VERSION,3)&&CALL(53,SDL_GL_CONTEXT_MINOR_VERSION,2)&&CALL(53,SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY));
 uint32_t window=CALL(40,0x2000,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN,0);assert(window);
 uint32_t context=CALL(47,window);if(!context)fprintf(stderr,"Context creation failed: %s\n",SDL_GetError());assert(context);assert(!strncmp((char *)glGetString(GL_VERSION),"2.1",3));
 assert(CALL(54,SDL_GL_CONTEXT_MAJOR_VERSION,0x3001)&&word(0x3001)==2);
 const char *exports[]={"glMatrixMode","glLoadIdentity","glLoadMatrixd","glViewport","glDisable","glEnable"};
 for(unsigned i=0;i<6;i++){strcpy((char *)memory+0x24000,exports[i]);assert(CALL(63,0x24000)==0xfffa0240+16*i);}
 GLdouble matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,.25,0,0,1},actual[16];
 memcpy(memory+0x3f80,matrix,sizeof matrix);
 if(fault&&!strcmp(fault,"unreadable-matrix")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0260,0,0x3f81);assert(0);}
 assert(tribes_memory_permissions_set(0x4000,4096,0));
 GC(0xfffa0240,0,GL_PROJECTION);GC(0xfffa0260,0,0x3f80);glGetDoublev(GL_PROJECTION_MATRIX,actual);assert(!memcmp(matrix,actual,sizeof matrix));
 assert(tribes_memory_permissions_set(0x4000,4096,3));
 if(!fault)perspective_projection();
 matrix_multiplication(fault,0);
 matrix_multiplication(fault,1);matrix_multiplication(fault,2);
 render_state(fault);
 scene_alpha();scene_polygon_mode();scene_texgen(fault);fog_arrays(fault);scene_fog(fault);scene_lighting(fault);
 if(!fault){matrix_stack();polygon_offset();}
 GC(0xfffa0240,0,GL_MODELVIEW);GC(0xfffa0250,0);glGetDoublev(GL_MODELVIEW_MATRIX,actual);for(unsigned i=0;i<16;i++)assert(actual[i]==(i%5==0?1:0));
 GC(0xfffa0270,0,0,0,64,64);GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);assert(viewport[0]==0&&viewport[1]==0&&viewport[2]==64&&viewport[3]==64);
 GC(0xfffa0270,0,0,0,0xffffffff,64);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa0290,0,GL_DEPTH_TEST);assert(glIsEnabled(GL_DEPTH_TEST));GC(0xfffa0280,0,GL_DEPTH_TEST);assert(!glIsEnabled(GL_DEPTH_TEST));
 GC(0xfffa0060,0,0,0,0,0x3f800000);GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);
 glColor4f(1,0,0,1);glBegin(GL_TRIANGLES);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(0,1);glEnd();
 unsigned char pixel[4]={0};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==255&&pixel[1]==0&&pixel[2]==0&&pixel[3]==255&&glGetError()==0);
 GC(0xfffa01b0,0,3,0x3001);GLuint names[3];memcpy(names,memory+0x3001,sizeof names);
 GC(0xfffa01c0,0,GL_ARRAY_BUFFER,names[0]);GC(0xfffa01c0,0,0x8f36,names[1]);GC(0xfffa01c0,0,0x8f37,names[2]);
 GC(0xfffa0020,0,GL_ARRAY_BUFFER_BINDING,0x3021);assert(word(0x3021)==names[0]);
 GC(0xfffa0020,0,0x8f36,0x3021);assert(word(0x3021)==names[1]);GC(0xfffa0020,0,0x8f37,0x3021);assert(word(0x3021)==names[2]);
 unsigned char data[]={11,22,33,44};memcpy(memory+0x3101,data,4);
 GC(0xfffa01d0,0,0x8f36,4,0x3101,GL_STATIC_DRAW);GC(0xfffa01d0,0,0x8f37,0x2000000,0,GL_STATIC_DRAW);
 GLint binding=0,size=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);assert((GLuint)binding==names[0]);
 glBindBuffer(GL_ARRAY_BUFFER,names[1]);unsigned char readback[4]={0};glGetBufferSubData(GL_ARRAY_BUFFER,0,4,readback);assert(!memcmp(data,readback,4));
 glBindBuffer(GL_ARRAY_BUFFER,names[2]);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);assert(size==0x2000000);glBindBuffer(GL_ARRAY_BUFFER,names[0]);
 GC(0xfffa01d0,0,0x8f37,0xffffffff,0xffffffff,GL_STATIC_DRAW);assert(glGetError()==GL_INVALID_VALUE);
 GC(0xfffa01d0,0,0x8f37,4,0,0xffffffff);assert(glGetError()==GL_INVALID_ENUM);
 glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);assert((GLuint)binding==names[0]);
 assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,1));uint32_t shared=CALL(47,window);assert(shared);assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0));
 GC(0xfffa0020,0,0x8f37,0x3021);assert(word(0x3021)==0);GC(0xfffa01c0,0,0x8f37,names[2]);
 GC(0xfffa01d0,0,0x8f37,4,0x3101,GL_STATIC_DRAW);assert(CALL(49,window,context));
 if(fault&&!strcmp(fault,"shared-array-delete")){
  assert(CALL(49,window,shared));GC(0xfffa01c0,0,0x8f37,0);GC(0xfffa01c0,0,GL_ARRAY_BUFFER,names[1]);
  assert(CALL(49,window,context));GC(0xfffa01e0,0,3,0x3001);assert(0);
 }
 GC(0xfffa0020,0,0x8f37,0x3021);assert(word(0x3021)==names[2]);
 if(fault&&!strcmp(fault,"shared-copy-delete")){GC(0xfffa01e0,0,3,0x3001);assert(0);}
 assert(CALL(48,shared));
 glBindBuffer(GL_ARRAY_BUFFER,names[2]);glGetBufferSubData(GL_ARRAY_BUFFER,0,4,readback);assert(!memcmp(data,readback,4));glBindBuffer(GL_ARRAY_BUFFER,names[0]);
 GC(0xfffa01e0,0,3,0x3001);GC(0xfffa0020,0,0x8f36,0x3021);assert(word(0x3021)==0);GC(0xfffa0020,0,0x8f37,0x3021);assert(word(0x3021)==0);
 GC(0xfffa01d0,0,0x8f37,4,0,GL_STATIC_DRAW);assert(glGetError()==GL_INVALID_OPERATION);
 texture_subregions(fault);matrix_queries(fault);gpu_sync(window,context,fault);vector_vertices(fault);vector_texcoords(fault);primitive_restart();immediate_rectangle();texture_array(fault);quad_draw(fault);shader_lifecycle(fault);
 assert(CALL(48,context));CALL(41,window);assert(CALL(49,original_window,original_context));
 assert(CALL(53,SDL_GL_CONTEXT_MAJOR_VERSION,3)&&CALL(53,SDL_GL_CONTEXT_MINOR_VERSION,2)&&CALL(53,SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE));
 puts("legacy fallback: guest matrices/viewport/depth state render red pixel; 32MiB copy-target storage, binding isolation, errors, sharing and cleanup PASS");
}
static void timer_queries(const char *fault){
 const char *names[]={"glBeginQuery","glEndQuery","glGetQueryObjectui64v"};
 for(unsigned i=0;i<3;i++){strcpy((char *)memory+0x24000,names[i]);assert(CALL(63,0x24000)==0xfffa0210+16*i);}
 GC(0xfffa0140,0,1,0x3001);GLuint name=word(0x3001);assert(name);
 GC(0xfffa0210,0,0x88bf,name);GLint active=0;glGetQueryivARB(0x88bf,GL_CURRENT_QUERY_ARB,&active);assert((GLuint)active==name&&glGetError()==0);
 memset(memory+0x3ff7,0xa5,9);
 GC(0xfffa0230,0,name,GL_QUERY_RESULT_ARB,0x3ff8);assert(glGetError()==GL_INVALID_OPERATION);
 for(unsigned i=0;i<9;i++)assert(memory[0x3ff7+i]==0xa5);
 GC(0xfffa0080,0,GL_COLOR_BUFFER_BIT);
 GC(0xfffa0220,0,0x88bf);glGetQueryivARB(0x88bf,GL_CURRENT_QUERY_ARB,&active);assert(active==0&&glGetError()==0);
 if(fault&&!strcmp(fault,"readonly-query-result")){assert(tribes_memory_permissions_set(0x3000,4096,1));GC(0xfffa0230,0,name,GL_QUERY_RESULT_ARB,0x3ff8);assert(0);}
 if(fault&&!strcmp(fault,"cross-page-query-result")){assert(tribes_memory_permissions_set(0x4000,4096,0));GC(0xfffa0230,0,name,GL_QUERY_RESULT_ARB,0x3ffc);assert(0);}
 assert(tribes_memory_permissions_set(0x4000,4096,0));
 GC(0xfffa0230,0,name,GL_QUERY_RESULT_ARB,0x3ff8);GLuint64 actual=0,expected=0;memcpy(&actual,memory+0x3ff8,8);
 glGetQueryObjectui64v(name,GL_QUERY_RESULT_ARB,&expected);assert(actual==expected&&glGetError()==0&&memory[0x3ff7]==0xa5);
 GC(0xfffa0230,0,name,GL_QUERY_RESULT_AVAILABLE_ARB,0x3ff8);memcpy(&actual,memory+0x3ff8,8);assert(actual==1&&glGetError()==0);
 assert(tribes_memory_permissions_set(0x4000,4096,3));
 memset(memory+0x3001,0xa5,8);GC(0xfffa0230,0,name,0xffffffff,0x3001);assert(glGetError()==GL_INVALID_ENUM);
 for(unsigned i=0;i<8;i++)assert(memory[0x3001+i]==0xa5);
 glDeleteQueriesARB(1,&name);assert(!glIsQueryARB(name)&&glGetError()==0);
 GC(0xfffa0210,0,0xffffffff,name);assert(glGetError()==GL_INVALID_ENUM);
 printf("real GPU timer query matches native 64-bit result %llu ns; lifecycle, errors and 8-byte output verified\n",(unsigned long long)expected);
}
static void swap_intervals(uint32_t window,uint32_t context){
 const uint32_t setter=0xfffa01f0,getter=0xfffa0200,current=0xfffa0360;
 strcpy((char *)memory+0x24000,"wglGetCurrentContext");assert(CALL(63,0x24000)==current);assert(GC(current,1)==context);
 strcpy((char *)memory+0x24000,"wglSwapIntervalEXT");assert(CALL(63,0x24000)==setter&&GC(0xfffa00c0,1,0x24000)==setter);
 strcpy((char *)memory+0x24000,"wglGetSwapIntervalEXT");assert(CALL(63,0x24000)==getter);
 int native=-9;assert(GC(getter,1)==1&&SDL_GL_GetSwapInterval(&native)&&native==1);
 for(int i=0;i<=2;i++){assert(GC(setter,1,(uint32_t)i));assert(GC(getter,1)==(uint32_t)i&&SDL_GL_GetSwapInterval(&native)&&native==i);}
 graphics_expected_error=13;assert(!GC(setter,1,UINT32_MAX));graphics_expected_error=0x19a5;assert(GC(getter,1)==2);
 uint32_t second=CALL(40,0x2000,320,240,(uint32_t)(SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN),0);assert(second);uint32_t context2=CALL(47,second);assert(context2);
 assert(GC(current,1)==context2);assert(GC(getter,1)==1);assert(GC(setter,1,0)&&GC(getter,1)==0);
 assert(CALL(49,window,context2)&&GC(getter,1)==2&&SDL_GL_GetSwapInterval(&native)&&native==2);
 assert(CALL(49,second,context)&&GC(getter,1)==0&&SDL_GL_GetSwapInterval(&native)&&native==0);assert(GC(current,1)==context);
 assert(CALL(49,0,0));assert(!GC(current,1));SDL_ClearError();assert(!CALL(74,1)&&*SDL_GetError());graphics_expected_error=1425;assert(!GC(setter,1,1)&&!GC(getter,1));graphics_expected_error=0x19a5;
 assert(CALL(49,window,context)&&GC(getter,1)==2);assert(CALL(48,context2));CALL(41,second);
 uint32_t recreated=CALL(47,window);assert(recreated&&GC(getter,1)==2);assert(GC(current,1)==recreated);assert(CALL(49,window,context)&&CALL(48,recreated));
 assert(CALL(74,1)&&GC(getter,1)==1&&SDL_GL_GetSwapInterval(&native)&&native==1);
 puts("WGL swap interval: native 0/1/2 state, signed/context errors, per-window restoration, recreation, stdcall ABI and SDL interoperability passed");
}
#endif
static void graphics_queries(uint32_t window,const char *fault){
 light_limit_query();
 multitexture(fault);query_objects(fault);texture_objects(fault);texture_subregions(fault);buffer_objects(fault);
 strcpy((char *)memory+0x24000,"glGetString");uint32_t string=CALL(63,0x24000);assert(string==0xfffa0000);
 uint32_t version=GC(string,1,GL_VERSION);assert(version&&!strcmp((char *)memory+version,(char *)glGetString(GL_VERSION)));
 assert(GC(string,1,GL_VERSION)==version);printf("guest GL_VERSION=%s\n",memory+version);
 strcpy((char *)memory+0x24000,"unsupportedGLFunction");assert(!CALL(63,0x24000)&&*SDL_GetError());
 strcpy((char *)memory+0x24000,"SDL.window.win32.hdc");uint32_t properties=CALL(44,window),dc=CALL(64,properties,0x24000,0);assert(dc&&dc!=window);
 assert(CALL(64,0,0x24000,0x123)==0x123);
 strcpy((char *)memory+0x24000,"SDL.window.win32.hwnd");assert(CALL(64,properties,0x24000,0)==window+0x03000000u);
 if(fault&&!strcmp(fault,"wrong-dc")){GC(0xfffa0040,1,window);assert(0);}
 uint32_t extensions=GC(0xfffa0040,1,dc);assert(extensions&&strstr((char *)memory+extensions,"WGL_EXT_swap_control")&&GC(0xfffa0050,1)==extensions);
 if(fault&&!strcmp(fault,"readonly-gl")){assert(tribes_memory_permissions_set(0x3000,4096,1));GC(0xfffa0020,0,GL_MAX_TEXTURE_SIZE,0x3001);assert(0);}
 GLfloat expected_float=0;glGetFloatv(GL_MAX_TEXTURE_SIZE,&expected_float);GC(0xfffa0120,0,GL_MAX_TEXTURE_SIZE,0x3001);assert(!memcmp(memory+0x3001,&expected_float,4));
#if !TARGET_OS_IPHONE
 glGetFloatv(0x84ff,&expected_float);GC(0xfffa0120,0,0x84ff,0x3001);assert(!memcmp(memory+0x3001,&expected_float,4)&&expected_float>=1);
 GLint units=0;glGetIntegerv(0x84e2,&units);GC(0xfffa0020,0,0x84e2,0x3001);assert(word(0x3001)==(uint32_t)units&&units>=2);
#endif
 if(fault&&!strcmp(fault,"readonly-gl-float")){assert(tribes_memory_permissions_set(0x3000,4096,1));GC(0xfffa0120,0,GL_MAX_TEXTURE_SIZE,0x3001);assert(0);}
 if(fault&&!strcmp(fault,"unsupported-gl-float")){GC(0xfffa0120,0,0x0ba8,0x3001);assert(0);}
 GLint expected=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&expected);GC(0xfffa0020,0,GL_MAX_TEXTURE_SIZE,0x3001);assert(word(0x3001)==(uint32_t)expected);
 while(glGetError()!=GL_NO_ERROR){}
 assert(!GC(string,1,0xffffffff));assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&GC(0xfffa0010,1)==0);
 uint32_t text=GC(0xfffa0030,1,GL_INVALID_ENUM);assert(text&&strstr((char *)memory+text,"invalid"));assert(!GC(0xfffa0030,1,0x1234));
 if(fault&&!strcmp(fault,"mutated-string")){memory[version]='!';GC(string,1,GL_VERSION);assert(0);}
}
#endif
static void fullscreen_modes(const char *fault){
 uint32_t display=CALL(20);int count=0;SDL_DisplayMode **native=SDL_GetFullscreenDisplayModes(display,&count);assert(native&&count>=0);
 if(fault&&!strcmp(fault,"readonly-count")){assert(tribes_memory_permissions_set(0x3000,4096,1));CALL(56,display,0x3001);assert(0);}
 unsigned live=sdl_test_live;uint32_t array=CALL(56,display,0x3001);assert(array&&word(0x3001)==(uint32_t)count&&sdl_test_live==live+1);
 assert(tribes_global_size(cpu._pointer_offset,array)>=((uint32_t)count+1)*4+(uint32_t)count*36);
 for(int n=0;n<count;n++){
  uint32_t p=word(array+4*n);assert(p==array+((uint32_t)count+1)*4+(uint32_t)n*36);
  TribesSDLDisplayMode m;memcpy(&m,memory+p,36);SDL_DisplayMode *v=native[n];
  assert(m.display_id==v->displayID&&m.format==v->format&&m.width==v->w&&m.height==v->h);
  assert(!memcmp(&m.pixel_density,&v->pixel_density,4)&&!memcmp(&m.refresh_rate,&v->refresh_rate,4));
  assert(m.refresh_numerator==v->refresh_rate_numerator&&m.refresh_denominator==v->refresh_rate_denominator&&!!m.internal==!!v->internal);
 }
 assert(!word(array+4*count));CALL(57,array);assert(sdl_test_live==live);SDL_free(native);
 sdl_test_fail=UINT32_MAX;assert(!CALL(56,display,0x3001)&&!word(0x3001)&&sdl_test_live==live);sdl_test_fail=0;
 array=CALL(56,display,0);assert(array);CALL(57,array);assert(!CALL(56,UINT32_MAX,0x3001)&&!word(0x3001));
 printf("fullscreen modes=%d; native fields/order, single guest allocation, free, OOM/retry and invalid ID verified\n",count);
}
static uint32_t property_window(const char *fault){
 uint32_t id=CALL(65);assert(id);strcpy((char *)memory+0x26000,SDL_PROP_WINDOW_CREATE_TITLE_STRING);assert(CALL(66,id,0x26000,0x2000));
 const char *names[]={SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN,SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN,SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN};
 for(unsigned i=0;i<3;i++){strcpy((char *)memory+0x26000,names[i]);assert(CALL(67,id,0x26000,1));}
 /* An i686 bool occupies the low byte; upper stack bytes can be unrelated. */
 strcpy((char *)memory+0x26000,SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN);assert(CALL(67,id,0x26000,0x2fd600));assert(!SDL_GetBooleanProperty(id,SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN,true));
 assert(CALL(67,id,0x26000,0x2fd601));assert(SDL_GetBooleanProperty(id,SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN,false));
 strcpy((char *)memory+0x26000,SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER);assert(CALL(68,id,0x26000,640,0));
 strcpy((char *)memory+0x26000,SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER);assert(CALL(68,id,0x26000,480,0));
 strcpy((char *)memory+0x26000,SDL_PROP_WINDOW_CREATE_X_NUMBER);assert(CALL(68,id,0x26000,0xffffffff,0xffffffff));assert(SDL_GetNumberProperty(id,SDL_PROP_WINDOW_CREATE_X_NUMBER,0)==-1);
 assert(CALL(68,id,0x26000,SDL_WINDOWPOS_CENTERED,0));
 if(fault&&!strcmp(fault,"unknown-property")){strcpy((char *)memory+0x26000,"SDL.window.create.native_pointer");CALL(68,id,0x26000,1,0);assert(0);}
 if(fault&&!strcmp(fault,"stale-properties")){CALL(70,id);CALL(69,id);assert(0);}
 uint32_t window=CALL(69,id);assert(window);CALL(70,id);assert(!strcmp(SDL_GetWindowTitle(SDL_GetWindowFromID(CALL(43,window))),"TribesPad real graphics component test"));return window;
}
static void display_setup(uint32_t window,SDL_Window *native,const char *fault){
 uint32_t display=CALL(71,window);assert(display&&display==SDL_GetDisplayForWindow(native));
 unsigned live=sdl_test_live;sdl_test_fail=UINT32_MAX;assert(!CALL(72,display)&&sdl_test_live==live);sdl_test_fail=0;
 uint32_t mode=CALL(72,display);assert(mode&&CALL(72,display)==mode&&sdl_test_live==live+1);
 const SDL_DisplayMode *v=SDL_GetCurrentDisplayMode(display);assert(v);TribesSDLDisplayMode m;memcpy(&m,memory+mode,sizeof m);
 assert(m.display_id==v->displayID&&m.format==v->format&&m.width==v->w&&m.height==v->h);
 assert(!memcmp(&m.pixel_density,&v->pixel_density,4)&&!memcmp(&m.refresh_rate,&v->refresh_rate,4));
 assert(m.refresh_numerator==v->refresh_rate_numerator&&m.refresh_denominator==v->refresh_rate_denominator&&!!m.internal==!!v->internal);
 if(fault&&!strcmp(fault,"mutated-mode")){memory[mode+8]^=1;CALL(73,window,mode);assert(0);}
 if(fault&&!strcmp(fault,"unknown-mode")){CALL(73,window,0x29000);assert(0);}
 bool expected=SDL_SetWindowFullscreenMode(native,v);assert(CALL(73,window,mode)==expected);
 const SDL_DisplayMode *selected=SDL_GetWindowFullscreenMode(native);
 if(expected){assert(selected&&selected->displayID==v->displayID&&selected->w==v->w&&selected->h==v->h);}
 assert(CALL(73,window,0)&&!SDL_GetWindowFullscreenMode(native));
 assert(!CALL(72,UINT32_MAX)&&!CALL(71,0));
 printf("display=%u current mode=%dx%d; native fullscreen-mode selection/clear and borrowed mode checks pass\n",display,m.width,m.height);
}
/* Check the ABI against explicit IEEE encodings, independently of the runtime
   conversion helper. Existing extended-arithmetic tests cover x87 exceptions. */
static void float_call(unsigned id,uint32_t arg,uint32_t bits,unsigned top){
 cpu._esp=0xf0000;memcpy(memory+cpu._esp,&arg,4);cpu._eax=0x5aa50ff0;
 cpu._st_extended=1;cpu._st_top=top;cpu._st_cw=0x37f;cpu._st_sw_cond=0x4200;cpu._st_tag=0xffff;
 for(unsigned n=0;n<8;n++){uint64_t value=UINT64_C(0x1234567800000000)+n;memcpy(&cpu._st[n],&value,8);cpu._st_exp[n]=(uint16_t)(n+10);}
 _cpu expected=cpu;unsigned at=(top+7)&7,e=(bits>>23)&255;uint64_t fraction=bits&0x7fffff,significand;uint16_t exponent=(bits>>16)&0x8000;unsigned tag;
 if(!e){assert(!fraction);significand=0;tag=1;}
 else {significand=(UINT64_C(0x800000)|fraction)<<40;exponent|=e==255?0x7fff:(uint16_t)(e-127+16383);tag=e==255?2:0;}
 expected._st_top=at;expected._st_sw_cond&=~0x200u;expected._st_tag=(expected._st_tag&~(3u<<(2*at)))|(tag<<(2*at));memcpy(&expected._st[at],&significand,8);expected._st_exp[at]=exponent;
 tribes_set_last_error(0x19a5);tribes_enter(&cpu,id);assert(!memcmp(&cpu,&expected,sizeof cpu)&&tribes_get_last_error()==0x19a5&&word(0xf0000)==arg);
}
static void float_returns(uint32_t window,SDL_Window *native){
 float scale=SDL_GetWindowDisplayScale(native);uint32_t scale_bits;memcpy(&scale_bits,&scale,4);assert(scale>0);
 const uint32_t cases[][2]={{0x3fa00000,0x40000000},{0xbfa00000,0xbf800000},{0,0},{0x80000000,0x80000000},{0x3f800001,0x40000000},{0x7f7fffff,0x7f7fffff},{1,0x3f800000},{0x80000001,0x80000000},{0x7f800000,0x7f800000},{0xff800000,0xff800000},{0x7fc12345,0x7fc12345}};
 for(unsigned top=0;top<8;top++){
  float_call(75,window,scale_bits,top);float_call(75,0,0,top);
  for(unsigned n=0;n<sizeof cases/sizeof *cases;n++)float_call(76,cases[n][0],cases[n][1],top);
 }
 /* Native seeded oracle checks the shared generator advances exactly once per
    no-argument guest call. The stack sentinel must remain caller-owned. */
 uint32_t random_bits[128];SDL_srand(UINT64_C(0x123456789abcdef));
 for(unsigned n=0;n<128;n++){float v=SDL_randf();assert(v>=0&&v<1);memcpy(random_bits+n,&v,4);}
 SDL_srand(UINT64_C(0x123456789abcdef));
 for(unsigned n=0;n<128;n++)float_call(95,0xcafebabe,random_bits[n],n&7);
 printf("128 seeded SDL_randf returns match native sequence and preserve guest state\n");
 cpu._st_top=0;cpu._st_tag=0xffff;cpu._st_sw_cond=0;
 printf("native display scale=%g; 104 x87 float-return cases preserve EAX, cdecl stack and unrelated CPU state\n",(double)scale);
}
static void window_setup(uint32_t window,SDL_Window *native,const char *fault){
 uint32_t display=SDL_GetDisplayForWindow(native);SDL_DisplayMode closest={0};bool expected=SDL_GetClosestFullscreenDisplayMode(display,640,480,0,true,&closest);
 if(fault&&!strcmp(fault,"readonly-closest")){assert(tribes_memory_permissions_set(0x3000,4096,1));CALL(77,display,640,480,0,1,0x3001);assert(0);}
 assert(CALL(77,display,640,480,0,0x2fd601,0x3001)==expected);
 if(expected){
  TribesSDLDisplayMode copy;memcpy(&copy,memory+0x3001,36);assert(!memcmp(&copy,&closest,32)&&!!copy.internal==!!closest.internal);
  memcpy(memory+0x3101,&copy,36); /* Original client passes a stack copy. */
  if(fault&&!strcmp(fault,"mutated-closest")){memory[0x3101+8]^=1;CALL(73,window,0x3101);assert(0);}
  bool set=SDL_SetWindowFullscreenMode(native,&closest);assert(CALL(73,window,0x3101)==set);assert(CALL(73,window,0));
 }
 memset(memory+0x3001,0xa5,36);assert(!CALL(77,UINT32_MAX,640,480,0,1,0x3001));for(unsigned i=0;i<36;i++)assert(!memory[0x3001+i]);assert(!CALL(77,display,640,480,0,1,0));
 assert(CALL(78,window,0x2fd600)==SDL_SetWindowFullscreen(native,false));
 assert(CALL(79,window,640,480)==SDL_SetWindowSize(native,640,480));
 assert(CALL(80,window,0x2fd601)==SDL_SetWindowBordered(native,true));
 bool relative=SDL_SetWindowRelativeMouseMode(native,true);
 assert(CALL(94,window,0x2fd601)==relative);
 if(relative)assert(SDL_GetWindowRelativeMouseMode(native));
 assert(CALL(94,window,0x2fd600)&&!SDL_GetWindowRelativeMouseMode(native));
 assert(!CALL(94,0,0));
 assert(CALL(81,window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED)==SDL_SetWindowPosition(native,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED));
 assert(CALL(82,window)&&!CALL(82,0));
 puts("closest-mode stack copy, native fullscreen/size/border/position/sync results and invalid outputs verified");
}
#include "cursor-fixture.h"
static void cursor_surfaces(const char *fault){
 unsigned initial=sdl_test_live;
 memcpy(memory+0x3000,cursor_fixture,sizeof cursor_fixture);
 if(fault&&!strcmp(fault,"unreadable-cursor"))assert(tribes_memory_permissions_set(0x3000,4096,0));
 uint32_t surface=CALL(83,16,32,SDL_PIXELFORMAT_ABGR8888,0x3000,64);assert(surface);
 assert(word(surface+4)==SDL_PIXELFORMAT_ABGR8888&&word(surface+8)==16&&word(surface+12)==32&&word(surface+16)==64&&word(surface+20)==0x3000&&word(surface+24)==1&&!word(surface+28));
 if(fault&&!strcmp(fault,"mutated-surface")){uint32_t forged=0x4000;memcpy(memory+surface+20,&forged,4);CALL(84,surface,SDL_PIXELFORMAT_ARGB8888);assert(0);}
 if(fault&&!strcmp(fault,"readonly-cursor")){assert(tribes_memory_permissions_set(0x3000,4096,1));CALL(85,surface,0);assert(0);}
 /* Conversion uses the latest guest pixels, not the creation-time snapshot. */
 memory[0x3000]=200;memory[0x3001]=100;memory[0x3002]=50;memory[0x3003]=128;
 uint32_t converted=CALL(84,surface,SDL_PIXELFORMAT_ARGB8888);assert(converted);
 uint32_t pixels=word(converted+20),pitch=word(converted+16);
 if(fault&&!strcmp(fault,"mutated-remove-surface")){memory[surface+8]^=1;CALL(93,surface);assert(0);}
 /* Removal neither synchronizes pixels nor changes public ownership/refcounts. */
 unsigned char headers[64];memcpy(headers,memory+surface,32);memcpy(headers+32,memory+converted,32);
 assert(tribes_memory_permissions_set(0x3000,4096,0));
 CALL(93,0);CALL(93,surface);CALL(93,converted);CALL(93,surface);
 assert(tribes_memory_permissions_set(0x3000,4096,3));
 assert(!memcmp(headers,memory+surface,32)&&!memcmp(headers+32,memory+converted,32));
 assert(pixels!=0x3000&&word(converted+4)==SDL_PIXELFORMAT_ARGB8888);
 assert(memory[pixels]==50&&memory[pixels+1]==100&&memory[pixels+2]==200&&memory[pixels+3]==128);
 SDL_Surface *oracle=SDL_CreateSurfaceFrom(16,32,SDL_PIXELFORMAT_ABGR8888,memory+0x3000,64);assert(oracle);
 SDL_Surface *expected=SDL_ConvertSurface(oracle,SDL_PIXELFORMAT_ARGB8888);assert(expected);
 for(unsigned y=0;y<32;y++)assert(!memcmp(memory+pixels+y*pitch,(char *)expected->pixels+y*expected->pitch,64));
 unsigned char borrowed[2048];memcpy(borrowed,memory+0x3000,sizeof borrowed);
 SDL_Surface *premult=SDL_CreateSurfaceFrom(16,32,SDL_PIXELFORMAT_ABGR8888,borrowed,64);
 assert(premult&&SDL_PremultiplySurfaceAlpha(premult,false)&&CALL(85,surface,0));
 assert(!memcmp(memory+0x3000,borrowed,sizeof borrowed));SDL_DestroySurface(premult);
 /* Converted storage also stays writable through its guest address. */
 SDL_DestroySurface(oracle);SDL_DestroySurface(expected);
 memcpy(memory+0x3000,cursor_fixture,sizeof cursor_fixture);
 unsigned char input[2048];memcpy(input,cursor_fixture,sizeof input);input[0]=200;input[1]=100;input[2]=50;input[3]=128;
 memcpy(memory+pixels,input,64);oracle=SDL_CreateSurfaceFrom(16,1,SDL_PIXELFORMAT_ARGB8888,input,64);assert(oracle&&SDL_PremultiplySurfaceAlpha(oracle,false));
 assert(CALL(85,converted,0));assert(!memcmp(memory+pixels,input,64));SDL_DestroySurface(oracle);
 assert(!CALL(87,converted,16,0));
 uint32_t cursor=CALL(87,converted,0,0);assert(cursor);
 /* Native cursor image owns its data after both guest surfaces are freed. */
 uint32_t refs=2;memcpy(memory+surface+24,&refs,4);CALL(93,surface);assert(word(surface+24)==2);CALL(86,surface);assert(word(surface+24)==1);
 CALL(86,surface);assert(!memcmp(memory+0x3000,cursor_fixture,sizeof cursor_fixture));CALL(86,converted);
 assert(CALL(88,cursor));SDL_Cursor *native=SDL_GetCursor();assert(native&&native!=SDL_GetDefaultCursor());
 assert(CALL(88,0)&&SDL_GetCursor()==native);CALL(89,cursor);assert(SDL_GetCursor()==SDL_GetDefaultCursor());
 if(fault&&!strcmp(fault,"stale-cursor")){CALL(88,cursor);assert(0);}
 if(fault&&!strcmp(fault,"stale-surface")){CALL(84,surface,SDL_PIXELFORMAT_ARGB8888);assert(0);}
 if(fault&&!strcmp(fault,"stale-remove-surface")){CALL(93,surface);assert(0);}
 CALL(86,0);CALL(89,0);assert(sdl_test_live==initial);
 sdl_test_fail=1;assert(!CALL(83,16,32,SDL_PIXELFORMAT_ABGR8888,0x3000,64));assert(sdl_test_live==initial);
 /* Quit must dispose of a live cursor and its independent surface storage. */
 surface=CALL(83,16,32,SDL_PIXELFORMAT_ABGR8888,0x3000,64);assert(surface);
 converted=CALL(84,surface,SDL_PIXELFORMAT_ARGB8888);assert(converted);cursor=CALL(87,converted,0,0);assert(cursor&&CALL(88,cursor));CALL(86,surface);
 puts("cursor: captured 16x32 pixels, conversion, premultiplication, refcount, borrowed/owned storage and native cursor lifetime PASS");
}
/* Push into SDL's actual queue; consume only through the emitted guest ABI.
   Assert explicit Windows32 offsets instead of casting a native SDL_Event. */
static void event_tests(SDL_Window *window,const char *fault){
 const unsigned out=0x7001;uint32_t id=SDL_GetWindowID(window);unsigned drained=0;
 while(CALL(90,out)){assert(++drained<4096);}CALL(91);SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
 uint32_t keyboard=CALL(92,0x3001);assert(keyboard&&word(0x3001)==SDL_SCANCODE_COUNT);
 SDL_Event e={0};e.window.type=SDL_EVENT_WINDOW_RESIZED;e.window.windowID=id;e.window.data1=711;e.window.data2=423;e.common.timestamp=123456789;
 assert(SDL_PushEvent(&e));
 if(fault&&!strcmp(fault,"readonly-event")){assert(tribes_memory_permissions_set(0x7000,4096,1));CALL(90,out);assert(0);}
 if(fault&&!strcmp(fault,"cross-page-event")){assert(tribes_memory_permissions_set(0x8000,4096,1));CALL(90,0x7ff0);assert(0);}
 assert(CALL(90,0));assert(CALL(90,0));assert(CALL(90,out));
 assert(word(out)==SDL_EVENT_WINDOW_RESIZED&&word(out+16)==id&&word(out+20)==711&&word(out+24)==423);uint64_t timestamp;memcpy(&timestamp,memory+out+8,8);assert(timestamp==123456789);
 while(CALL(90,out)){}CALL(91);SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
 memset(&e,0,sizeof e);e.key.type=SDL_EVENT_KEY_DOWN;e.key.windowID=id;e.key.scancode=SDL_SCANCODE_W;e.key.key=SDLK_W;e.key.mod=SDL_KMOD_LSHIFT;e.key.raw=0x1234;e.key.down=true;e.key.repeat=true;assert(SDL_PushEvent(&e));
 memset(&e,0,sizeof e);e.motion.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=id;e.motion.state=SDL_BUTTON_LMASK;e.motion.x=12.5f;e.motion.y=18.25f;e.motion.xrel=-2.5f;e.motion.yrel=3.75f;assert(SDL_PushEvent(&e));
 memset(&e,0,sizeof e);e.gaxis.type=SDL_EVENT_GAMEPAD_AXIS_MOTION;e.gaxis.which=37;e.gaxis.axis=SDL_GAMEPAD_AXIS_LEFTX;e.gaxis.value=-12345;assert(SDL_PushEvent(&e));
 memset(&e,0,sizeof e);e.text.type=SDL_EVENT_TEXT_INPUT;e.text.windowID=id;e.text.text="UTF-8: \xc3\xa9";assert(SDL_PushEvent(&e));
 memset(&e,0,sizeof e);e.drop.type=SDL_EVENT_DROP_TEXT;e.drop.windowID=id;e.drop.source="test source";e.drop.data="payload";e.drop.x=.5f;e.drop.y=.25f;assert(SDL_PushEvent(&e));
 memset(&e,0,sizeof e);e.type=SDL_EVENT_QUIT;assert(SDL_PushEvent(&e));
 assert(CALL(90,out)&&word(out)==SDL_EVENT_KEY_DOWN&&word(out+24)==SDL_SCANCODE_W&&word(out+28)==SDLK_W);assert(memory[out+36]==1&&memory[out+37]==1);uint16_t raw;memcpy(&raw,memory+out+34,2);assert(raw==0x1234);
 assert(CALL(90,out)&&word(out)==SDL_EVENT_MOUSE_MOTION);float motion[4];memcpy(motion,memory+out+28,16);int width,height,pixel_width,pixel_height;assert(SDL_GetWindowSize(window,&width,&height)&&SDL_GetWindowSizeInPixels(window,&pixel_width,&pixel_height));
 float sx=(float)pixel_width/width,sy=(float)pixel_height/height;
 assert(motion[0]==12.5f*sx&&motion[1]==18.25f*sy&&motion[2]==-2.5f&&motion[3]==3.75f);
 printf("Mouse absolute coordinates: window=%dx%d pixels=%dx%d scale=%g,%g; relative deltas unchanged\n",width,height,pixel_width,pixel_height,sx,sy);
 assert(CALL(90,out)&&word(out)==SDL_EVENT_GAMEPAD_AXIS_MOTION&&word(out+16)==37);int16_t axis;memcpy(&axis,memory+out+24,2);assert(axis==-12345);
 assert(CALL(90,out)&&word(out)==SDL_EVENT_TEXT_INPUT);uint32_t text=word(out+20);assert(text&&!strcmp((char *)memory+text,"UTF-8: \xc3\xa9"));unsigned live=sdl_test_live;
 if(fault&&!strcmp(fault,"mutated-event-text")){memory[text]='x';CALL(90,out);assert(0);}
 assert(CALL(90,out)&&word(out)==SDL_EVENT_DROP_TEXT);assert(!strcmp((char *)memory+word(out+28),"test source")&&!strcmp((char *)memory+word(out+32),"payload"));
 assert(!strcmp((char *)memory+text,"UTF-8: \xc3\xa9")&&sdl_test_live==live+2);assert(CALL(90,out)&&word(out)==SDL_EVENT_QUIT);
 while(CALL(90,out)){}assert(!strcmp((char *)memory+text,"UTF-8: \xc3\xa9"));
 memset(memory+out,0xa5,128);CALL(91);assert(sdl_test_live==live-1);
 SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);assert(!CALL(90,out));for(unsigned i=0;i<128;i++)assert(memory[out+i]==0xa5);
 int count=0;const bool *native=SDL_GetKeyboardState(&count);assert(count==SDL_SCANCODE_COUNT&&!memcmp(memory+keyboard,native,(size_t)count));
 if(fault&&!strcmp(fault,"unsupported-event")){memset(&e,0,sizeof e);e.type=SDL_EVENT_USER;e.user.data1=window;assert(SDL_PushEvent(&e));while(CALL(90,out)){}assert(0);}
 /* Button and wheel pointer positions use the same pixel coordinate space;
    wheel scrolling itself remains in SDL scroll units. */
 memset(&e,0,sizeof e);e.button.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=id;e.button.button=SDL_BUTTON_LEFT;e.button.down=true;e.button.x=-2.5f;e.button.y=31.25f;assert(SDL_PushEvent(&e));
 memset(&e,0,sizeof e);e.wheel.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.windowID=id;e.wheel.x=1.5f;e.wheel.y=-2.5f;e.wheel.mouse_x=11.25f;e.wheel.mouse_y=37.5f;assert(SDL_PushEvent(&e));
 assert(CALL(90,out)&&word(out)==SDL_EVENT_MOUSE_BUTTON_DOWN);float point[2];memcpy(point,memory+out+28,8);assert(point[0]==-2.5f*sx&&point[1]==31.25f*sy);
 assert(CALL(90,out)&&word(out)==SDL_EVENT_MOUSE_WHEEL);memcpy(point,memory+out+36,8);assert(point[0]==11.25f*sx&&point[1]==37.5f*sy);memcpy(point,memory+out+24,8);assert(point[0]==1.5f&&point[1]==-2.5f);
 puts("Guest SDL events: queue/NULL peek, Windows32 fields, keyboard snapshot, UTF-8/drop lifetime and empty-output preservation passed");
}
#ifdef TB_GL4ES
static void mobile_gpu_sync(uint32_t window,uint32_t context,const char *fault){
 assert(strstr((const char *)glGetString(GL_EXTENSIONS),"GL_APPLE_sync"));
 tribes_gl_clear_color(&cpu,0,0x3f800000,0,0x3f800000);tribes_gl_clear(&cpu,GL_COLOR_BUFFER_BIT);
 uint32_t token=tribes_gl_fence_sync(&cpu,0x9117,0);assert(token&&tribes_gl_is_sync(&cpu,token));
 uint32_t status=tribes_gl_client_wait_sync(&cpu,token,1,0,1);assert(status==0x911a||status==0x911c);
 unsigned char pixel[4];glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(pixel[0]==0&&pixel[1]==255&&pixel[2]==0&&pixel[3]==255);
 if(fault&&!strcmp(fault,"sync-output")){assert(tribes_memory_permissions_set(0x3000,4096,1));tribes_gl_get_sync_iv(&cpu,token,0x9114,1,0,0x3001);assert(0);}
 if(fault&&!strcmp(fault,"sync-length")){assert(tribes_memory_permissions_set(0x4000,4096,1));tribes_gl_get_sync_iv(&cpu,token,0x9114,1,0x4001,0x3001);assert(0);}
 tribes_gl_get_sync_iv(&cpu,token,0x9114,1,0x3001,0x3005);assert(word(0x3001)==1&&word(0x3005)==0x9119);
 assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0));uint32_t isolated=CALL(47,window);assert(isolated&&!tribes_gl_is_sync(&cpu,token));
 assert(tribes_gl_client_wait_sync(&cpu,token,0,0,0)==0x911d&&tribes_gl_get_error(&cpu)==GL_INVALID_VALUE);
 assert(CALL(49,window,context));assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,1));uint32_t shared=CALL(47,window);assert(shared&&tribes_gl_is_sync(&cpu,token));
 assert(CALL(53,SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0));tribes_gl_delete_sync(&cpu,token);assert(!tribes_gl_is_sync(&cpu,token));
 assert(CALL(48,shared)&&CALL(48,isolated)&&CALL(49,window,context));
 assert(tribes_gl_client_wait_sync(&cpu,token,0,0,0)==0x911d&&tribes_gl_get_error(&cpu)==GL_INVALID_VALUE);
 assert(tribes_gl_get_error(&cpu)==0);
 puts("Apple GPU sync: native completion, 64-bit wait, pixels, checked outputs and shared/foreign/stale tokens PASS");
}
#endif
static int window_main(int argc,char **argv){
 assert(pthread_main_np());memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));memset(&cpu,0,sizeof cpu);cpu._pointer_offset=(uintptr_t)memory;cpu._esp=0xf0000;cpu.stack_bottom=memory+0x10000;cpu.stack_top=memory+0x100000;cpu._eflags=0x246;
 assert(tribes_memory_checks_configure(cpu._pointer_offset,UINT64_C(0x100000000)));
#ifdef CHECKED
 tribes_memory_external_configure(cpu._pointer_offset,UINT64_C(0x100000000));tribes_memory_external_enable();
#ifdef TB_GL4ES
 if(argc==2&&!strcmp(argv[1],"bad-gl4es-color")){bad_gl4es_color();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-gl4es-adapter")){bad_gl4es_adapter();return 99;}
 if(argc==2&&!strcmp(argv[1],"gl4es-guest-pointer")){gl4es_guest_pointer(memory);return 99;}
#endif
 if(argc==2&&!strcmp(argv[1],"bad-window")){bad_window();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-context")){bad_context();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-randf")){bad_randf();return 99;}
#if defined(GRAPHICS) && !TARGET_OS_IPHONE
 if(argc==2&&!strcmp(argv[1],"bad-frustum")){bad_frustum();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-mult-matrix-double")){bad_mult_matrix_double();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-mult-matrix")){bad_mult_matrix();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-depth-mask")){bad_depth_mask();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-is-enabled")){bad_is_enabled();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-polygon-offset")){bad_polygon_offset();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-tex-coord2f")){bad_tex_coord2f();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-restart")){bad_restart();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-load-matrix-f")){bad_load_matrix_f();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-tex-gen-i")){bad_tex_gen_i();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-tex-gen-fv")){bad_tex_gen_fv();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-polygon-mode")){bad_polygon_mode();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-alpha-func")){bad_alpha_func();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-fog-i")){bad_fog_i();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-fog-f")){bad_fog_f();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-fog-fv")){bad_fog_fv();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-light-fv")){bad_light_fv();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-light-f")){bad_light_f();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-light-model-fv")){bad_light_model_fv();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-rotate-f")){bad_rotate_f();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-translate-f")){bad_translate_f();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-color3ub")){bad_color3ub();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-vertex3f")){bad_vertex3f();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-color4f")){bad_color4f();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-vertex2i")){bad_vertex2i();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-tex-sub-image-2d")){bad_tex_sub_image_2d();return 99;}
 if(argc==2&&!strcmp(argv[1],"bad-scale")){bad_scale();return 99;}
#endif
#endif
 static const char env[]="TRIBESPAD_WINDOW_TEST=1\0";assert(tribes_environment_configure(cpu._pointer_offset,env,sizeof env));
 #ifdef GRAPHICS
 assert(tribes_graphics_configure(cpu._pointer_offset));
 strcpy((char *)memory+0x24000,"glu32");uint32_t module=CALL(60,0x24000);assert(module==0x20050000);
 strcpy((char *)memory+0x24000,"gluErrorString");assert(CALL(61,module,0x24000)==0xfffa0030);
 strcpy((char *)memory+0x24000,"gluProject");assert(CALL(61,module,0x24000)==0xfffa0750);
 strcpy((char *)memory+0x24000,"unknown");assert(!CALL(61,module,0x24000)&&*SDL_GetError());CALL(62,module);
 assert(!CALL(61,module,0x24000));strcpy((char *)memory+0x24000,"unknown-library");assert(!CALL(60,0x24000)&&*SDL_GetError());
 /* The original passes a Win32-loaded GLU handle to SDL_LoadFunction. */
 strcpy((char *)memory+0x24000,"glu32");module=tribes_load_library(cpu._pointer_offset,0x24000);
 strcpy((char *)memory+0x24000,"gluErrorString");assert(CALL(61,module,0x24000)==0xfffa0030);assert(tribes_free_library(cpu._pointer_offset,module));
#endif
 assert(tribes_sdl_set_memory_functions(cpu._pointer_offset,11,12,13,14));CALL(8);assert(CALL(18,SDL_INIT_VIDEO));
 #ifdef GRAPHICS
 project_point(argc==2?argv[1]:NULL);
 strcpy((char *)memory+0x24000,"opengl32");module=tribes_load_library(cpu._pointer_offset,0x24000);assert(module==0x20040000);
 strcpy((char *)memory+0x24000,"wglGetProcAddress");assert(tribes_get_proc_address(cpu._pointer_offset,module,0x24000)==0xfffa00c0);
 strcpy((char *)memory+0x24000,"glActiveTextureARB");graphics_expected_error=6;assert(!GC(0xfffa00c0,1,0x24000));graphics_expected_error=0x19a5;
 assert(tribes_free_library(cpu._pointer_offset,module));
#endif
 strcpy((char *)memory+0x2000,"TribesPad real graphics component test");
 fullscreen_modes(argc==2?argv[1]:NULL);
 if(argc==2&&!strcmp(argv[1],"wrong-thread")){pthread_t t;assert(!pthread_create(&t,NULL,wrong_thread,NULL));pthread_join(t,NULL);return 99;}
 CALL(55);assert(CALL(53,SDL_GL_DOUBLEBUFFER,1));
 uint32_t window=property_window(argc==2?argv[1]:NULL);assert((window&0xff000000)==0xda000000);
 uint32_t id=CALL(43,window);assert(id);assert(CALL(44,window));SDL_Window *native=SDL_GetWindowFromID(id);assert(native);int w,h;
 event_tests(native,argc==2?argv[1]:NULL);
 display_setup(window,native,argc==2?argv[1]:NULL);
 float_returns(window,native);
 window_setup(window,native,argc==2?argv[1]:NULL);
 assert(SDL_GetWindowSize(native,&w,&h)&&CALL(45,window,0x3001,0x3009)&&word(0x3001)==(uint32_t)w&&word(0x3009)==(uint32_t)h);
 assert(SDL_GetWindowSizeInPixels(native,&w,&h)&&CALL(46,window,0x3001,0x3009)&&word(0x3001)==(uint32_t)w&&word(0x3009)==(uint32_t)h);
 if(argc==2&&!strcmp(argv[1],"readonly-output")){assert(tribes_memory_permissions_set(0x3000,4096,1));CALL(45,window,0x3001,0);return 99;}
 if(argc==2&&!strcmp(argv[1],"wrong-token")){CALL(42,0xdb000001);return 99;}
 assert(!CALL(47,0)&&*SDL_GetError());
#if !TARGET_OS_IPHONE
 assert(CALL(53,SDL_GL_CONTEXT_MAJOR_VERSION,3)&&CALL(53,SDL_GL_CONTEXT_MINOR_VERSION,2)&&CALL(53,SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY)&&CALL(53,SDL_GL_CONTEXT_FLAGS,SDL_GL_CONTEXT_DEBUG_FLAG));
 assert(!SDL_GL_CreateContext(native)&&!strcmp(SDL_GetError(),"Failed creating OpenGL context at version requested"));
 assert(CALL(53,SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE));
#endif
 uint32_t context=CALL(47,window);if(!context)fprintf(stderr,"Context creation failed: %s\n",SDL_GetError());assert((context&0xff000000)==0xdb000000);
 assert(CALL(50)==window&&CALL(51)==context);
#if !TARGET_OS_IPHONE
 GLint profile=0;glGetIntegerv(0x9126,&profile);assert(profile&1);
 assert(CALL(54,SDL_GL_CONTEXT_PROFILE_MASK,0x3001)&&word(0x3001)==SDL_GL_CONTEXT_PROFILE_CORE);
 puts("explicit native core context retains existing modern graphics coverage");
#ifdef GRAPHICS
 if(argc==2&&!strcmp(argv[1],"tex-coord2f-core")){GC(0xfffa0700,0,0,0);assert(0);}
 if(argc==2&&!strcmp(argv[1],"light-fv-core")){GC(0xfffa0720,0,GL_LIGHT0,GL_POSITION,0x3001);assert(0);}
 if(argc==2&&!strcmp(argv[1],"light-f-core")){GC(0xfffa0730,0,GL_LIGHT0,GL_SPOT_CUTOFF,0);assert(0);}
 if(argc==2&&!strcmp(argv[1],"light-model-core")){GC(0xfffa0740,0,GL_LIGHT_MODEL_AMBIENT,0x3001);assert(0);}
 if(argc==2&&!strcmp(argv[1],"rotate-f-core")){GC(0xfffa0710,0,0,0,0,0x3f800000);assert(0);}
 if(argc==2&&!strcmp(argv[1],"translate-f-core")){GC(0xfffa06f0,0,0,0,0);assert(0);}
 if(argc==2&&!strcmp(argv[1],"color3ub-core")){GC(0xfffa06d0,0,128,128,128);assert(0);}
 if(argc==2&&!strcmp(argv[1],"texgen-i-core")){GC(0xfffa07d0,0,GL_S,GL_TEXTURE_GEN_MODE,GL_OBJECT_LINEAR);assert(0);}
 if(argc==2&&!strcmp(argv[1],"texgen-fv-core")){GC(0xfffa07e0,0,GL_S,GL_OBJECT_PLANE,0x3001);assert(0);}
 if(argc==2&&!strcmp(argv[1],"polygon-core")){GC(0xfffa07c0,0,GL_FRONT_AND_BACK,GL_FILL);assert(0);}
 if(argc==2&&!strcmp(argv[1],"alpha-core")){GC(0xfffa07b0,0,GL_GREATER,0);assert(0);}
 if(argc==2&&!strcmp(argv[1],"fog-array-core")){GC(0xfffa07a0,0,GL_FLOAT,32,0x3000);assert(0);}
 if(argc==2&&!strcmp(argv[1],"fog-i-core")){GC(0xfffa0770,0,GL_FOG_MODE,0x3001);assert(0);}
 if(argc==2&&!strcmp(argv[1],"fog-f-core")){GC(0xfffa0780,0,GL_FOG_MODE,0x3001);assert(0);}
 if(argc==2&&!strcmp(argv[1],"fog-fv-core")){GC(0xfffa0790,0,GL_FOG_MODE,0x3001);assert(0);}
 if(argc==2&&!strcmp(argv[1],"vertex-vector-core")){GC(0xfffa0760,0,0x3001);assert(0);}
 if(argc==2&&!strcmp(argv[1],"vertex3f-core")){GC(0xfffa06e0,0,0,0,0);assert(0);}
 if(argc==2&&!strcmp(argv[1],"matrix-query-core")){GC(0xfffa0120,0,GL_MODELVIEW_MATRIX,0x3001);assert(0);}
 if(argc==1||(argc==2&&(!strncmp(argv[1],"texgen-",7)||!strncmp(argv[1],"fog-",4)||!strncmp(argv[1],"texcoord-vector-",16)||!strncmp(argv[1],"vertex-vector-",14)||!strncmp(argv[1],"matrix-query-",13)||!strncmp(argv[1],"sync-",5)||!strncmp(argv[1],"array-",6)||!strncmp(argv[1],"uniform-",8)||!strncmp(argv[1],"shader-",7)||!strncmp(argv[1],"quad-",5)||!strncmp(argv[1],"matrix-load-",12)||!strncmp(argv[1],"matrix-mult-",12)||!strncmp(argv[1],"matrix-double-",14)||!strncmp(argv[1],"state-",6)||!strncmp(argv[1],"normal-",7)||!strncmp(argv[1],"light-",6)||!strcmp(argv[1],"unreadable-matrix")||!strcmp(argv[1],"shared-copy-delete")||!strcmp(argv[1],"shared-array-delete"))))legacy_route(window,context,argc==2?argv[1]:NULL);
 if(argc==2&&!strcmp(argv[1],"mipmap-nonzero-base")){
  GC(0xfffa0150,0,1,0x3001);GC(0xfffa0160,0,GL_TEXTURE_2D,word(0x3001));GC(0xfffa0170,0,GL_TEXTURE_2D,0x8191,1);GC(0xfffa0170,0,GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,1);
  const unsigned char color[]={1,2,3,255};mipmap_upload(1,2,color);assert(0);
 }
 if(argc==1||(argc==2&&strstr(argv[1],"query-result")))timer_queries(argc==2?argv[1]:NULL);
 if(argc==1){
  /* Core contexts use the real driver entry; legacy emulation cannot leak. */
  GC(0xfffa0020,0,0x8f9e,0x3000);assert(word(0x3000)==0&&!GC(0xfffa05f0,1,0x8f9d));
  glEnable(0xffffffff);GC(0xfffa0800,0,65535);GC(0xfffa0290,0,0x8f9d);
  GLint restart_index=0;glGetIntegerv(0x8f9e,&restart_index);assert(restart_index==65535&&glIsEnabled(0x8f9d));
  assert(GC(0xfffa0010,1)==GL_INVALID_ENUM&&!GC(0xfffa0010,1));GC(0xfffa0280,0,0x8f9d);
  uint32_t version=GC(0xfffa0000,1,GL_VERSION);assert(version&&!strcmp((char *)memory+version,(char *)glGetString(GL_VERSION)));
  GLint count=0;glGetIntegerv(0x821d,&count);assert(count>0&&glGetError()==GL_NO_ERROR);
  uint32_t extensions=GC(0xfffa0000,1,GL_EXTENSIONS);assert(extensions);const char *cursor=(char *)memory+extensions;
  for(GLint i=0;i<count;i++){const char *native=(const char *)glGetStringi(GL_EXTENSIONS,i);size_t n=strlen(native);assert(!strncmp(cursor,native,n));cursor+=n;if(i+1<count)assert(*cursor++==' ');}
  assert(!*cursor&&glGetError()==GL_NO_ERROR);
  glGetString(0xffffffff);assert(GC(0xfffa0000,1,GL_EXTENSIONS)==extensions);assert(glGetError()==GL_INVALID_ENUM);
  buffer_objects(NULL);
  swap_intervals(window,context);
  automatic_mipmaps(window,context);
  explicit_mipmaps();
  light_limit_query();
  GLuint name=0;GC(0xfffa01b0,0,1,0x3001);name=word(0x3001);GC(0xfffa01c0,0,0x8f37,name);GC(0xfffa01d0,0,0x8f37,0x2000000,0,GL_STATIC_DRAW);
  GLint bytes=-1;glGetBufferParameteriv(0x8f37,GL_BUFFER_SIZE,&bytes);assert(bytes==0x2000000&&glGetError()==GL_NO_ERROR);GC(0xfffa01e0,0,1,0x3001);
  guest_pixels();
  printf("core guest extension string matches all %d native entries; existing GL error preserved; original copy buffer has %d bytes\n",count,bytes);
 }
#endif
 /* Retain coverage of the supported legacy fixed-function entrypoints on a
    separately requested legacy context, with no fallback involved. */
 assert(CALL(48,context));assert(CALL(53,SDL_GL_CONTEXT_MAJOR_VERSION,2)&&CALL(53,SDL_GL_CONTEXT_MINOR_VERSION,1)&&CALL(53,SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_COMPATIBILITY));
 context=CALL(47,window);assert(context&&!strncmp((char *)glGetString(GL_VERSION),"2.1",3));
 #ifdef GRAPHICS
 if(argc==1)explicit_mipmaps();
 #endif
#endif
 for(int interval=-1;interval<=1;interval++){bool expected=SDL_GL_SetSwapInterval(interval);assert(CALL(74,(uint32_t)interval)==expected);if(expected){int actual=99;assert(SDL_GL_GetSwapInterval(&actual)&&actual==interval);}}
 assert(CALL(54,SDL_GL_DOUBLEBUFFER,0x3001)&&word(0x3001)==1);
 if(argc==2&&!strcmp(argv[1],"live-subsystem-quit")){CALL(17,SDL_INIT_VIDEO);return 99;}
 #ifdef GRAPHICS
 graphics_queries(window,argc==2?argv[1]:NULL);
 guest_pixels();
#endif
 #ifdef TB_GL4ES
 mobile_gpu_sync(window,context,argc==2?argv[1]:NULL);
#endif
 cursor_surfaces(argc==2?argv[1]:NULL);pixels();if(argc==2&&!strcmp(argv[1],"reentrant-window"))assert(SDL_AddEventWatch(reenter,NULL));assert(CALL(42,window));assert(CALL(52,window));SDL_PumpEvents();
#if TARGET_OS_IPHONE
 /* UIKit supports one window per display; exercise two contexts on it. */
 assert(!CALL(40,0x2000,320,240,0x200a,0)&&*SDL_GetError());uint32_t second=window;
#else
 uint32_t second=CALL(40,0x2000,320,240,0x200a,0);assert(second&&second!=window);
#endif
 uint32_t other=CALL(47,second);assert(other&&other!=context);assert(CALL(50)==second&&CALL(51)==other);
 assert(CALL(49,window,context));assert(CALL(50)==window&&CALL(51)==context);pixels();
#ifdef GRAPHICS
 guest_pixels();
#endif
 assert(CALL(49,0,0));assert(!CALL(50)&&!CALL(51));
#ifdef GRAPHICS
 GC(0xfffa0060,0,0,0,0,0);GC(0xfffa0070,0,0x8d40,0);GC(0xfffa0080,0,0x4100);GC(0xfffa00a0,0);
#if !TARGET_OS_IPHONE
 GC(0xfffa0090,0,GL_BACK);
#endif
#endif
 assert(CALL(49,second,other));assert(CALL(48,other));if(second!=window)CALL(41,second);
 if(argc==2&&!strcmp(argv[1],"stale-context")){CALL(48,other);return 99;}
 assert(CALL(49,window,context));assert(CALL(48,context));CALL(41,window);assert(!CALL(50)&&!CALL(51));
 if(argc==2&&!strcmp(argv[1],"stale-window")){CALL(42,window);return 99;}
 assert(!CALL(48,0)&&*SDL_GetError());CALL(41,0);
 /* Full SDL_Quit owns cleanup even if the guest leaves objects open. */
 window=CALL(40,0x2000,320,240,0x200a,0);assert(window&&CALL(47,window));assert(CALL(65));CALL(19);assert(!sdl_test_live&&!SDL_WasInit(0));
 assert(CALL(18,SDL_INIT_VIDEO));second=CALL(40,0x2000,320,240,0x200a,0);assert(second>window&&CALL(47,second));pixels();CALL(19);assert(!sdl_test_live);
 puts("PASS: guest window/context ABIs, native drawable readback, current-context switching, dimensions, lifecycle/reinit and CPU/LastError preservation; component only");fflush(stdout);return 0;
}
int main(int argc,char **argv){return tribes_sdl_run_application(argc,argv,window_main);}
