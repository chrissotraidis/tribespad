/* Native capability probe only: no guest execution or gameplay acceptance. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
static int probe(int argc,char **argv){
 (void)argc;(void)argv;assert(SDL_Init(SDL_INIT_VIDEO));
 for(int modern=0;modern<2;modern++){
  SDL_GL_ResetAttributes();assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,modern?3:2));
  assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,modern?2:1));
  assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,modern?SDL_GL_CONTEXT_PROFILE_CORE:SDL_GL_CONTEXT_PROFILE_COMPATIBILITY));
  SDL_Window *w=SDL_CreateWindow("TribesPad native buffer capability probe",64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(w);
  SDL_GLContext c=SDL_GL_CreateContext(w);assert(c);
  const unsigned char *(*string)(unsigned)=(void *)SDL_GL_GetProcAddress("glGetString");
  unsigned (*error)(void)=(void *)SDL_GL_GetProcAddress("glGetError");
  void (*gen)(int,unsigned *)=(void *)SDL_GL_GetProcAddress("glGenBuffers");
  void (*bind)(unsigned,unsigned)=(void *)SDL_GL_GetProcAddress("glBindBuffer");
  void (*data)(unsigned,intptr_t,const void *,unsigned)=(void *)SDL_GL_GetProcAddress("glBufferData");
  void (*get)(unsigned,unsigned,int *)=(void *)SDL_GL_GetProcAddress("glGetBufferParameteriv");
  void (*del)(int,const unsigned *)=(void *)SDL_GL_GetProcAddress("glDeleteBuffers");
  assert(string&&error&&gen&&bind&&data&&get&&del);
  unsigned name=0;gen(1,&name);assert(name&&error()==0);bind(0x8f37,name);unsigned bind_error=error();
  data(0x8f37,0x2000000,NULL,0x88e4);unsigned data_error=error();int size=-1;
  if(!bind_error&&!data_error){get(0x8f37,0x8764,&size);assert(error()==0);}
  printf("profile=%s GL_VERSION=%s COPY_WRITE_BUFFER name=%u bind_error=0x%x data_error=0x%x bytes=%d\n",modern?"core":"compatibility",string(0x1f02),name,bind_error,data_error,size);
  if(modern)assert(!bind_error&&!data_error&&size==0x2000000);
  const unsigned char *extensions=string(0x1f03);printf("legacy extension string present=%d error=0x%x\n",extensions!=NULL,error());
  del(1,&name);assert(error()==0);assert(SDL_GL_DestroyContext(c));SDL_DestroyWindow(w);
 }
 SDL_Quit();puts("PASS: native context capability comparison; no guest or gameplay claim");return 0;
}
int main(int argc,char **argv){return SDL_RunApp(argc,argv,probe,NULL);}
