/* Native GLES renderer evaluation, not original-engine acceptance. */
#import <UIKit/UIKit.h>
#import <QuartzCore/CAEAGLLayer.h>
#import <OpenGLES/ES2/gl.h>
#include <dlfcn.h>
#include "gl4esinit.h"

#define DECL(ret, name, args) extern ret gl4es_##name args
DECL(void, glGenFramebuffers, (GLsizei, GLuint *));
DECL(void, glGenRenderbuffers, (GLsizei, GLuint *));
DECL(void, glBindRenderbuffer, (GLenum, GLuint));
DECL(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei));
DECL(void, glBindFramebuffer, (GLenum, GLuint));
DECL(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint));
DECL(GLenum, glCheckFramebufferStatus, (GLenum));
DECL(void, glViewport, (GLint, GLint, GLsizei, GLsizei));
DECL(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
DECL(void, glClear, (GLbitfield));
DECL(void, glMatrixMode, (GLenum));
DECL(void, glLoadIdentity, (void));
DECL(void, glTranslatef, (GLfloat, GLfloat, GLfloat));
DECL(void, glGenTextures, (GLsizei, GLuint *));
DECL(void, glBindTexture, (GLenum, GLuint));
DECL(void, glTexParameteri, (GLenum, GLenum, GLint));
DECL(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
DECL(void, glEnable, (GLenum));
DECL(void, glColor4f, (GLfloat, GLfloat, GLfloat, GLfloat));
DECL(void, glBegin, (GLenum));
DECL(void, glTexCoord2f, (GLfloat, GLfloat));
DECL(void, glVertex3f, (GLfloat, GLfloat, GLfloat));
DECL(void, glEnd, (void));
DECL(void, glEnableClientState, (GLenum));
DECL(void, glVertexPointer, (GLint, GLenum, GLsizei, const void *));
DECL(void, glTexCoordPointer, (GLint, GLenum, GLsizei, const void *));
DECL(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *));
DECL(void, glFinish, (void));
DECL(GLenum, glGetError, (void));
DECL(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));

static int width, height;
static void *gles;
static void *native_proc(const char *name) { return dlsym(gles, name); }
static void framebuffer_size(int *w, int *h) { *w=width; *h=height; }

@interface ProbeView : UIView
@property(nonatomic, strong) EAGLContext *context;
@property(nonatomic) BOOL rendered;
@end
@implementation ProbeView
+ (Class)layerClass { return [CAEAGLLayer class]; }
- (void)layoutSubviews {
    [super layoutSubviews];
    if (self.rendered || self.bounds.size.width < 1) return;
    self.rendered=YES;
    CAEAGLLayer *layer=(CAEAGLLayer *)self.layer;
    layer.opaque=YES;
    layer.drawableProperties=@{kEAGLDrawablePropertyRetainedBacking:@YES,
                               kEAGLDrawablePropertyColorFormat:kEAGLColorFormatRGBA8};
    self.context=[[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    NSCAssert([EAGLContext setCurrentContext:self.context], @"GLES context");
    GLuint renderbuffer, framebuffer;
    width=(int)(self.bounds.size.width*layer.contentsScale);
    height=(int)(self.bounds.size.height*layer.contentsScale);
    /* Capability detection requires a complete current native framebuffer. */
    GLuint bootstrapFramebuffer, bootstrapRenderbuffer;
    glGenRenderbuffers(1,&bootstrapRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER,bootstrapRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA4,1,1);
    glGenFramebuffers(1,&bootstrapFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,bootstrapFramebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,bootstrapRenderbuffer);
    NSCAssert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE, @"bootstrap framebuffer");
    gles=dlopen("/System/Library/Frameworks/OpenGLES.framework/OpenGLES",RTLD_NOW|RTLD_LOCAL);
    NSCAssert(gles, @"GLES symbols");
    set_getprocaddress(native_proc);
    set_getmainfbsize(framebuffer_size);
    /* EAGL owns the drawable renderbuffer; it cannot become a texture. */
    setenv("LIBGL_FBOFORCETEX","0",1);
    initialize_gl4es();
    GLenum initializationError=gl4es_glGetError();
    NSCAssert(initializationError==GL_NO_ERROR, @"initialization: %x",initializationError);
    gl4es_glGenRenderbuffers(1,&renderbuffer);
    gl4es_glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);
    gl4es_glRenderbufferStorage(GL_RENDERBUFFER,0x8058,width,height); /* RGBA8 */
    NSCAssert([self.context renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer], @"drawable");
    GLint actualWidth, actualHeight;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&actualWidth);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&actualHeight);
    NSCAssert(actualWidth==width && actualHeight==height, @"drawable dimensions");
    gl4es_glGenFramebuffers(1,&framebuffer);
    gl4es_glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    gl4es_glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,renderbuffer);
    GLenum framebufferStatus=gl4es_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    NSCAssert(framebufferStatus==GL_FRAMEBUFFER_COMPLETE, @"framebuffer: %x",framebufferStatus);
    glDeleteFramebuffers(1,&bootstrapFramebuffer);
    glDeleteRenderbuffers(1,&bootstrapRenderbuffer);
    gl4es_glViewport(0,0,width,height);
    gl4es_glClearColor(0,0,1,1);
    gl4es_glClear(GL_COLOR_BUFFER_BIT);
    gl4es_glMatrixMode(0x1701); /* projection */
    gl4es_glLoadIdentity();
    gl4es_glMatrixMode(0x1700); /* modelview */
    gl4es_glLoadIdentity();
    GLuint texture;
    const GLubyte pixels[]={255,128,64,255,255,128,64,255,255,128,64,255,255,128,64,255};
    gl4es_glGenTextures(1,&texture);
    gl4es_glBindTexture(GL_TEXTURE_2D,texture);
    gl4es_glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    gl4es_glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    gl4es_glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    gl4es_glEnable(GL_TEXTURE_2D);
    gl4es_glTranslatef(-0.5f,0,0);
    gl4es_glColor4f(1,0,0,1);
    gl4es_glBegin(0x0007); /* desktop GL_QUADS */
    gl4es_glTexCoord2f(0,0); gl4es_glVertex3f(-.35f,-.5f,0);
    gl4es_glTexCoord2f(1,0); gl4es_glVertex3f(.35f,-.5f,0);
    gl4es_glTexCoord2f(1,1); gl4es_glVertex3f(.35f,.5f,0);
    gl4es_glTexCoord2f(0,1); gl4es_glVertex3f(-.35f,.5f,0);
    gl4es_glEnd();
    gl4es_glLoadIdentity();
    gl4es_glTranslatef(.5f,0,0);
    gl4es_glColor4f(0,1,0,1);
    const GLfloat vertices[]={-.35f,-.5f,0, .35f,-.5f,0, .35f,.5f,0, -.35f,.5f,0};
    const GLfloat uv[]={0,0,1,0,1,1,0,1};
    const GLushort indices[]={0,1,2,0,2,3};
    gl4es_glEnableClientState(0x8074); /* vertex */
    gl4es_glEnableClientState(0x8078); /* texcoord */
    gl4es_glVertexPointer(3,GL_FLOAT,0,vertices);
    gl4es_glTexCoordPointer(2,GL_FLOAT,0,uv);
    gl4es_glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,indices);
    gl4es_glFinish();
    GLubyte left[4], right[4], background[4];
    gl4es_glReadPixels(width/4,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left);
    gl4es_glReadPixels(3*width/4,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
    gl4es_glReadPixels(width/2,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,background);
    GLenum error=gl4es_glGetError();
    BOOL passed=error==GL_NO_ERROR && left[0]==255 && left[1]==0 && left[2]==0 &&
        right[0]==0 && abs((int)right[1]-128)<=1 && right[2]==0 &&
        background[0]==0 && background[1]==0 && background[2]==255;
    NSDictionary *receipt=@{@"passed":@(passed),@"width":@(width),@"height":@(height),
        @"error":@(error),@"initialization_error":@(initializationError),@"framebuffer_status":@(framebufferStatus),
        @"immediate_pixel":@[@(left[0]),@(left[1]),@(left[2]),@(left[3])],
        @"indexed_pixel":@[@(right[0]),@(right[1]),@(right[2]),@(right[3])],
        @"background_pixel":@[@(background[0]),@(background[1]),@(background[2]),@(background[3])],
        @"original_engine_executed":@NO};
    NSString *path=[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/result.json"];
    [[NSJSONSerialization dataWithJSONObject:receipt options:NSJSONWritingPrettyPrinted error:nil] writeToFile:path atomically:YES];
    NSLog(@"GL4ES renderer probe: %@",receipt);
    glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);
    [self.context presentRenderbuffer:GL_RENDERBUFFER];
}
@end

@interface ProbeDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation ProbeDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options;
    self.window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController *controller=[UIViewController new];
    controller.view=[[ProbeView alloc] initWithFrame:self.window.bounds];
    self.window.rootViewController=controller;
    [self.window makeKeyAndVisible];
    return YES;
}
@end
int main(int argc,char **argv) {
    @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(ProbeDelegate.class)); }
}
