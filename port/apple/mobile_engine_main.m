/* Full original-engine entry through SDL's UIKit lifecycle. */
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#endif
#include "mobile_game_paths.h"
#include "sdl_main.h"
#include <stdio.h>
#include <unistd.h>
#include "mobile-engine-launch-config.h"
extern int tribes_mobile_engine_main(int,char **);
extern void tb_mobile_controls_install(void);
static int enter_engine(int argc,char **argv) {
    (void)argc;(void)argv;
#if TB_BUNDLED_GAME_DATA
    char *launch_arguments[9];
    NSURL *support = [NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
    if (!support) { fprintf(stderr,"TribesPad: no app support directory\n"); return 125; }
    NSString *storage = [support.path stringByAppendingPathComponent:@"TribesPad"];
    /* iOS: resourcePath is the bundle itself. Mac: Contents/Resources. */
    NSString *resources = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"];
    if (!tb_mobile_prepare_paths(resources.fileSystemRepresentation, storage.fileSystemRepresentation, launch_arguments)) return 125;
    const char *launch_workdir = storage.fileSystemRepresentation;
#endif
    if(chdir(launch_workdir)){perror("TribesPad working directory");return 125;}
    fprintf(stderr,"TribesPad iPad: entering full checked original engine\n");
    fprintf(stderr,"TribesPad iPad: GPU elapsed-time profiling unavailable; original pre-sample scheduling estimate retained\n");
    tb_mobile_controls_install();
    return tribes_mobile_engine_main(8,launch_arguments);
}
#if TARGET_OS_IOS
/* Apps built with the iOS 27 SDK must use the scene lifecycle (Info.plist
   declares one window scene with TBSceneDelegate). SDL 3.2 still creates its
   windows with -initWithFrame:, outside any scene, and its launch window
   exists before the scene connects. Attach such windows to the window scene,
   now if it is connected or when it connects. Windows UIKit creates for a
   scene already have one and are left alone. */
static IMP tb_window_init_frame;
static NSHashTable<UIWindow *> *tb_unattached_windows;
static UIWindowScene *tb_window_scene;
static id tb_window_init_in_scene(UIWindow *self,SEL cmd,CGRect frame) {
    self=((id(*)(id,SEL,CGRect))tb_window_init_frame)(self,cmd,frame);
    if(self&&!self.windowScene){if(tb_window_scene)self.windowScene=tb_window_scene;else [tb_unattached_windows addObject:self];}
    return self;
}
@interface TBSceneDelegate:UIResponder<UIWindowSceneDelegate>
@end
@implementation TBSceneDelegate
-(void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session;(void)options;if(![scene isKindOfClass:UIWindowScene.class])return;
    tb_window_scene=(UIWindowScene *)scene;
    for(UIWindow *window in tb_unattached_windows.allObjects)if(!window.windowScene)window.windowScene=tb_window_scene;
    [tb_unattached_windows removeAllObjects];
}
@end
static void tb_adopt_window_scene(void) {
    tb_unattached_windows=[NSHashTable weakObjectsHashTable];
    Method m=class_getInstanceMethod(UIWindow.class,@selector(initWithFrame:));
    tb_window_init_frame=method_setImplementation(m,(IMP)tb_window_init_in_scene);
}
#endif
int main(int argc,char **argv){
#if TARGET_OS_IOS
    tb_adopt_window_scene();
#endif
    return tribes_sdl_run_application(argc,argv,enter_engine);
}
