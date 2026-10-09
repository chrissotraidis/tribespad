/* Isolate a captured game triangle in Apple's GLES implementation, without
   the guest runtime or GL4ES. A fragment-only fog change must retain coverage. */
#import <UIKit/UIKit.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
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
    EAGLContext *context=[[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    assert(context && [EAGLContext setCurrentContext:context]);
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
        unsigned coverage[2]={0};
        for(unsigned variant=0;variant<2;variant++){
            GLuint p=programs[variant];glUseProgram(p);
            glUniformMatrix4fv(glGetUniformLocation(p,"_gl4es_ModelViewProjectionMatrix"),1,GL_FALSE,mvp);
            glUniform1i(glGetUniformLocation(p,"_gl4es_TexSampler_0"),0);glUniform1i(glGetUniformLocation(p,"_gl4es_TexSampler_1"),1);
            GLint fog=glGetUniformLocation(p,"_gl4es_Fog.color");if(fog>=0)glUniform4f(fog,.65f,.65f,.7f,1);
            uniform1(p,"_gl4es_Fog.start",0);uniform1(p,"_gl4es_Fog.end",1);uniform1(p,"_gl4es_Fog.scale",1);uniform1(p,"_gl4es_Fog.density",1);
            glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
            glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_INT,ids);glFinish();
            glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels[variant]);assert(glGetError()==GL_NO_ERROR);
            for(size_t i=3;i<bytes;i+=4)coverage[variant]+=pixels[variant][i]!=0;
        }
        unsigned different=0;for(size_t i=3;i<bytes;i+=4)different+=(pixels[0][i]!=0)!=(pixels[1][i]!=0);
        BOOL ok=coverage[0]>0 && different==0;passed &= ok;
        [results addObject:@{@"width":@(w),@"height":@(h),@"reference_pixels":@(coverage[0]),@"fog_pixels":@(coverage[1]),@"coverage_difference":@(different),@"passed":@(ok)}];
        free(pixels[0]);free(pixels[1]);
    }
    NSDictionary *result=@{@"passed":@(passed),@"renderer":@((const char *)glGetString(GL_RENDERER)),@"cases":results};
    NSString *path=[NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject stringByAppendingPathComponent:@"result.json"];
    assert([[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil] writeToFile:path atomically:YES]);
    glDeleteRenderbuffers(1,&color);glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(2,textures);
    for(unsigned i=0;i<2;i++)glDeleteProgram(programs[i]);[EAGLContext setCurrentContext:nil];
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
