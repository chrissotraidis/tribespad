/* Replay a captured clipped triangle without the guest runtime or GL4ES.
   Both fog variants must keep coverage when their programs are reused. */
#import <UIKit/UIKit.h>
#ifdef TB_ANGLE
#define EGL_EGLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#else
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#endif
#include <assert.h>

static GLuint shader(GLenum type, NSString *source) {
    GLuint s=glCreateShader(type); const char *text=source.UTF8String;
    glShaderSource(s,1,&text,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof log,NULL,log);fprintf(stderr,"%s\n",log);}
    assert(ok);return s;
}
static GLuint program(NSString *vs,NSString *fs) {
    GLuint p=glCreateProgram(),v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);
    glAttachShader(p,v);glAttachShader(p,f);
    glBindAttribLocation(p,0,"_gl4es_Vertex");glBindAttribLocation(p,3,"_gl4es_Color");
    glBindAttribLocation(p,5,"_gl4es_FogCoord");
    glBindAttribLocation(p,8,"_gl4es_MultiTexCoord0");glBindAttribLocation(p,9,"_gl4es_MultiTexCoord1");
    glLinkProgram(p);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);assert(ok);
    glDeleteShader(v);glDeleteShader(f);return p;
}
static void uniform1(GLuint p,const char *name,float value) {
    GLint location=glGetUniformLocation(p,name);if(location>=0)glUniform1f(location,value);
}
static void execute(void) {
#ifdef TB_ANGLE
    const EGLint attrs[]={EGL_PLATFORM_ANGLE_TYPE_ANGLE,EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,EGL_NONE};
    EGLDisplay display=eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE,NULL,attrs);assert(display!=EGL_NO_DISPLAY);assert(eglInitialize(display,NULL,NULL));
    EGLConfig config;EGLint count;const EGLint configAttrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};assert(eglChooseConfig(display,configAttrs,&config,1,&count)&&count==1);
    const EGLint surfaceAttrs[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttrs);assert(surface!=EGL_NO_SURFACE);
    const EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);assert(context!=EGL_NO_CONTEXT);assert(eglMakeCurrent(display,surface,surface,context));
#else
    EAGLContext *context=[[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    assert(context && [EAGLContext setCurrentContext:context]);
#endif
    NSString *fixture=[[NSBundle mainBundle] pathForResource:@"fixture" ofType:@"json"];
    NSDictionary *data=[NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:fixture] options:0 error:nil];assert(data);
    NSString *fragment=data[@"fragment_shader"];
    NSString *without=[fragment stringByReplacingOccurrencesOfString:@"fColor.rgb = mix(_gl4es_Fog.color.rgb, fColor.rgb, FogF);" withString:@"/* Fog disabled for coverage reference. */"];
    assert(![fragment isEqualToString:without]);
    GLuint programs[]={program(data[@"vertex_shader"],without),program(data[@"vertex_shader"],fragment)};
    GLfloat mvp[16];for(unsigned i=0;i<16;i++)mvp[i]=[data[@"mvp"][i] floatValue];
    /* Preserve the captured sparse vertex identities, strides and locations. */
    float arrays[4][88*3]={{0}};const unsigned ids[]={67,66,68};unsigned array=0;
    for(NSDictionary *a in data[@"attributes"]){
        unsigned slot=[a[@"slot"] unsignedIntValue],size=[a[@"size"] unsignedIntValue];assert(size<=3 && array<4);
        for(unsigned i=0;i<3;i++)for(unsigned j=0;j<size;j++)arrays[array][ids[i]*size+j]=[a[@"values"][i][j] floatValue];
        glEnableVertexAttribArray(slot);glVertexAttribPointer(slot,size,GL_FLOAT,GL_FALSE,size*4,arrays[array++]);
    }
    glDisableVertexAttribArray(3);glVertexAttrib4f(3,1,1,1,1);
    GLuint textures[2];glGenTextures(2,textures);
    const unsigned char texels[2][4]={{128,167,166,255},{84,67,48,255}};
    for(unsigned i=0;i<2;i++){
        glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,texels[i]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    GLuint framebuffer,color;glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glGenRenderbuffers(1,&color);glBindRenderbuffer(GL_RENDERBUFFER,color);
    NSMutableArray *results=[NSMutableArray array];BOOL passed=YES;
    const unsigned sizes[][2]={{64,64},{2360,1640}};
    for(unsigned s=0;s<2;s++){
        unsigned w=sizes[s][0],h=sizes[s][1];size_t bytes=(size_t)w*h*4;
        glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8_OES,w,h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glViewport(0,0,w,h);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);
        unsigned char *pixels[2]={calloc(1,bytes),calloc(1,bytes)};assert(pixels[0]&&pixels[1]);
        unsigned coverage[2]={0},initial[2]={0};BOOL stable=YES;NSMutableArray *draws=[NSMutableArray array];
        for(unsigned draw=0;draw<6;draw++){
            unsigned variant=draw%2;coverage[variant]=0;
            GLuint p=programs[variant];glUseProgram(p);
            glUniformMatrix4fv(glGetUniformLocation(p,"_gl4es_ModelViewProjectionMatrix"),1,GL_FALSE,mvp);
            glUniform1i(glGetUniformLocation(p,"_gl4es_TexSampler_0"),0);glUniform1i(glGetUniformLocation(p,"_gl4es_TexSampler_1"),1);
            GLint fog=glGetUniformLocation(p,"_gl4es_Fog.color");if(fog>=0)glUniform4f(fog,.65f,.65f,.7f,1);
            uniform1(p,"_gl4es_Fog.start",0);uniform1(p,"_gl4es_Fog.end",1);uniform1(p,"_gl4es_Fog.scale",1);uniform1(p,"_gl4es_Fog.density",1);
            glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
            glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_INT,ids);glFinish();
            glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels[variant]);assert(glGetError()==GL_NO_ERROR);
            for(size_t i=3;i<bytes;i+=4)coverage[variant]+=pixels[variant][i]!=0;
            if(draw<2)initial[variant]=coverage[variant];else stable &= coverage[variant]==initial[variant];
            [draws addObject:@{@"fog":@(variant),@"coverage":@(coverage[variant])}];
        }
        unsigned different=0;for(size_t i=3;i<bytes;i+=4)different+=(pixels[0][i]!=0)!=(pixels[1][i]!=0);
        BOOL ok=stable && coverage[0]>0 && different==0;passed &= ok;
        [results addObject:@{@"width":@(w),@"height":@(h),@"draws":draws,@"stable_coverage":@(stable),@"reference_pixels":@(coverage[0]),@"fog_pixels":@(coverage[1]),@"coverage_difference":@(different),@"passed":@(ok)}];
        free(pixels[0]);free(pixels[1]);
    }
    NSDictionary *result=@{@"passed":@(passed),@"renderer":@((const char *)glGetString(GL_RENDERER)),@"cases":results};
    NSString *path=[NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject stringByAppendingPathComponent:@"result.json"];
    assert([[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil] writeToFile:path atomically:YES]);
    glDeleteRenderbuffers(1,&color);glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(2,textures);
    for(unsigned i=0;i<2;i++)glDeleteProgram(programs[i]);
#ifdef TB_ANGLE
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
#else
    [EAGLContext setCurrentContext:nil];
#endif
}
@interface ProbeDelegate:UIResponder<UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation ProbeDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app;(void)options;self.window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController=[UIViewController new];[self.window makeKeyAndVisible];
    dispatch_async(dispatch_get_main_queue(),^{execute();});return YES;
}
@end
int main(int argc,char **argv){@autoreleasepool{return UIApplicationMain(argc,argv,nil,NSStringFromClass(ProbeDelegate.class));}}
