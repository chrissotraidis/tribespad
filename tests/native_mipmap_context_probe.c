/* Native-only diagnostic of the Apple nonzero-base mipmap update; no guest execution. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <assert.h>
#include <stdio.h>
extern void glGenerateMipmap(GLenum);
static int run(int argc,char **argv){
 (void)argc;(void)argv;assert(SDL_Init(SDL_INIT_VIDEO));SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
 SDL_Window *w=SDL_CreateWindow("Native mipmap probe",64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(w);SDL_GLContext c=SDL_GL_CreateContext(w);assert(c);
 GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);unsigned char image[64],pixel[4];
 for(unsigned i=0;i<16;i++){image[4*i]=20;image[4*i+1]=40;image[4*i+2]=240;image[4*i+3]=255;}
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,image);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,1);
 for(int attempt=0;attempt<2;attempt++){
  if(attempt)for(unsigned i=0;i<16;i++){image[4*i]=240;image[4*i+2]=40;}
  glTexImage2D(GL_TEXTURE_2D,1,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,image);glGenerateMipmap(GL_TEXTURE_2D);glGetTexImage(GL_TEXTURE_2D,2,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
  printf("attempt=%d lower=%u,%u,%u,%u error=%x\n",attempt,pixel[0],pixel[1],pixel[2],pixel[3],glGetError());
 }
 glDeleteTextures(1,&t);SDL_GL_DestroyContext(c);SDL_DestroyWindow(w);SDL_Quit();return 0;
}
int main(int argc,char **argv){return SDL_RunApp(argc,argv,run,NULL);}
