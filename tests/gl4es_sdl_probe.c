#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#ifdef TB_ANGLE
#include "../port/apple/angle_sdl.h"
#endif
#include <SDL3/SDL_main.h>
#include <GL/gl.h>
#include "../port/apple/gl4es_context.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static void draw(GLuint texture) {
    int width,height;
    assert(SDL_GetWindowSizeInPixels(SDL_GL_GetCurrentWindow(),&width,&height));
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,width,height);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glTranslatef(.25f,0,0);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,texture);
    glColor4f(1,1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex3f(-.5f,-.5f,0);
    glTexCoord2f(1,0); glVertex3f(.5f,-.5f,0);
    glTexCoord2f(1,1); glVertex3f(.5f,.5f,0);
    glTexCoord2f(0,1); glVertex3f(-.5f,.5f,0);
    glEnd();
    glFinish();
    unsigned char pixel[4]={0};
    glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[0]==40 && pixel[1]==160 && pixel[2]==220 && pixel[3]==255);
    glReadPixels(width/16,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    assert(pixel[0]==0 && pixel[1]==0 && pixel[2]==0 && pixel[3]==255);
    assert(glGetError()==GL_NO_ERROR);
    checks++;
}

static int application(int argc,char **argv) {
    (void)argc; (void)argv;
    assert(SDL_Init(SDL_INIT_VIDEO));
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES));
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2));
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0));
    assert(SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8));
    assert(SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8));
    assert(SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8));
    assert(SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8));
    SDL_Window *window=SDL_CreateWindow("TribesPad SDL legacy renderer",820,1180,SDL_WINDOW_OPENGL);
    assert(window);
    SDL_GLContext first=SDL_GL_CreateContext(window); assert(first);
    TribesGL4ESContext *a=tribes_gl4es_context_create(window,first,NULL); assert(a);
    assert(glGetError()==GL_NO_ERROR);
    GLuint texture;
    const unsigned char pixel[]={40,160,220,255};
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    draw(texture); assert(tribes_gl4es_context_swap(a)); draw(texture);
    assert(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,1));
    SDL_GLContext second=SDL_GL_CreateContext(window); assert(second);
    TribesGL4ESContext *b=tribes_gl4es_context_create(window,second,a); assert(b);
    draw(texture); assert(tribes_gl4es_context_swap(b)); draw(texture);
    assert(SDL_GL_MakeCurrent(window,first)); tribes_gl4es_context_activate(a);
    draw(texture);
    assert(SDL_GL_MakeCurrent(window,second)); tribes_gl4es_context_activate(b);
    tribes_gl4es_context_destroy(b); assert(SDL_GL_DestroyContext(second));
    assert(SDL_GL_MakeCurrent(window,first)); tribes_gl4es_context_activate(a);
    draw(texture); assert(tribes_gl4es_context_swap(a));
    char path[4096];
    assert(snprintf(path,sizeof path,"%s/Documents/rendered.json",getenv("HOME"))<(int)sizeof path);
    FILE *receipt=fopen(path,"w"); assert(receipt);
    fprintf(receipt,"{\"passed\":true,\"pixel_checks\":%u,\"shared_contexts\":2,\"original_engine_executed\":false}\n",checks);
    assert(fclose(receipt)==0);
    /* Keep the real SDL drawable visible briefly for screenshot inspection. */
    for(unsigned i=0;i<30;i++){SDL_PumpEvents();SDL_Delay(100);}
    glDeleteTextures(1,&texture);
#ifdef TB_ANGLE
    GLenum (*nativeFramebufferStatus)(GLenum)=(GLenum (*)(GLenum))SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    assert(nativeFramebufferStatus && nativeFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    tribes_gl4es_context_destroy(a);
    assert(nativeFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
#else
    GLuint framebuffer=(GLuint)SDL_GetNumberProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER,0);
    GLboolean (*nativeIsFramebuffer)(GLuint)=(GLboolean (*)(GLuint))SDL_GL_GetProcAddress("glIsFramebuffer");
    assert(nativeIsFramebuffer && nativeIsFramebuffer(framebuffer));
    tribes_gl4es_context_destroy(a);
    assert(nativeIsFramebuffer(framebuffer)); /* SDL drawable remains owned by SDL. */
#endif
    assert(SDL_GL_DestroyContext(first));
    SDL_DestroyWindow(window);SDL_Quit();
    assert(snprintf(path,sizeof path,"%s/Documents/result.json",getenv("HOME"))<(int)sizeof path);
    receipt=fopen(path,"w"); assert(receipt);
    fprintf(receipt,"{\"passed\":true,\"pixel_checks\":%u,\"shared_contexts\":2,\"cleanup_passed\":true,\"borrowed_framebuffer_preserved\":true,\"original_engine_executed\":false}\n",checks);
    assert(fclose(receipt)==0);
    return 0;
}
int main(int argc,char **argv) { return SDL_RunApp(argc,argv,application,NULL); }
