/* Full original-engine entry through SDL's UIKit lifecycle. */
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#include <SDL3/SDL.h>
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
/* iPadOS also delivers game controller buttons (B, Menu) as UIKit presses,
   which SDL turns into Escape and Return. TribesPad maps the controller
   itself (mobile_controls.m), so SDL's view only receives keyboard presses. */
static IMP tb_presses[4];
static NSSet<UIPress *> *tb_keyboard_presses(NSSet<UIPress *> *presses){
    return [presses objectsPassingTest:^BOOL(UIPress *press,BOOL *stop){(void)stop;return press.key!=nil;}];
}
#define TB_PRESS_FILTER(n) static void tb_press_##n(id self,SEL cmd,NSSet<UIPress *> *presses,UIPressesEvent *event){ \
    NSSet *keys=tb_keyboard_presses(presses);if(keys.count)((void(*)(id,SEL,NSSet *,UIPressesEvent *))tb_presses[n])(self,cmd,keys,event);}
TB_PRESS_FILTER(0) TB_PRESS_FILTER(1) TB_PRESS_FILTER(2) TB_PRESS_FILTER(3)
static void tb_filter_controller_presses(void){
    Class view=NSClassFromString(@"SDL_uikitview");if(!view)return;
    SEL selectors[4]={@selector(pressesBegan:withEvent:),@selector(pressesEnded:withEvent:),@selector(pressesCancelled:withEvent:),@selector(pressesChanged:withEvent:)};
    IMP filters[4]={(IMP)tb_press_0,(IMP)tb_press_1,(IMP)tb_press_2,(IMP)tb_press_3};
    for(int i=0;i<4;i++){Method m=class_getInstanceMethod(view,selectors[i]);if(m)tb_presses[i]=method_setImplementation(m,filters[i]);}
}
/* OpenAL drives CoreAudio directly and never chooses an audio session, so iOS
   used its default, which the silent switch mutes, and the library's input
   probe could move output to the earpiece. Play game audio through the
   speaker like other games (and HaloPad): Playback, mixed with other audio,
   reactivated after interruptions such as calls. */
static void tb_audio_session(void) {
    /* SDL also manages the session when the game opens an SDL audio device.
       Without this hint it picks PlayAndRecord for a microphone, which routes
       output to the earpiece. */
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY,"playback");
    AVAudioSession *session=AVAudioSession.sharedInstance;NSError *error=nil;
    if(![session setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault
        options:AVAudioSessionCategoryOptionMixWithOthers error:&error]||![session setActive:YES error:&error])
        NSLog(@"TribesPad audio session: %@",error.localizedDescription);
    [NSNotificationCenter.defaultCenter addObserverForName:AVAudioSessionInterruptionNotification object:session
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){
        if([note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue]==AVAudioSessionInterruptionTypeEnded)
            [AVAudioSession.sharedInstance setActive:YES error:nil];}];
    // Report where sound goes once the game has set up its audio.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(45*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
        AVAudioSession *s=AVAudioSession.sharedInstance;NSMutableArray *outs=[NSMutableArray new];
        for(AVAudioSessionPortDescription *p in s.currentRoute.outputs)[outs addObject:p.portType];
        NSLog(@"TribesPad audio route: category=%@ outputs=%@ volume=%.2f other_audio=%d",s.category,
            [outs componentsJoinedByString:@","],s.outputVolume,s.otherAudioPlaying);});
    // And which screens exist: an external display mirrors unless the app gives it its own scene.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(45*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
        for(UIScene *scene in UIApplication.sharedApplication.connectedScenes)if([scene isKindOfClass:UIWindowScene.class]){
            UIScreen *screen=((UIWindowScene *)scene).screen;
            NSLog(@"TribesPad screen: role=%@ bounds=%@ native=%@ scale=%.1f mirrored=%d max_fps=%ld",scene.session.role,
                NSStringFromCGRect(screen.bounds),NSStringFromCGRect(screen.nativeBounds),screen.scale,screen.mirroredScreen!=nil,(long)screen.maximumFramesPerSecond);}
    });
}
/* SDL deactivates the session when the game closes its last SDL audio device,
   which also silences OpenAL. Called after every close (guest_sdl_backend.c). */
void tb_audio_session_restore(void) {
    AVAudioSession *session=AVAudioSession.sharedInstance;NSError *error=nil;
    if(![session setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault
        options:AVAudioSessionCategoryOptionMixWithOthers error:&error]||![session setActive:YES error:&error])
        NSLog(@"TribesPad audio session restore: %@",error.localizedDescription);
}
#endif
int main(int argc,char **argv){
#if TARGET_OS_IOS
    tb_adopt_window_scene();
    tb_filter_controller_presses();
    tb_audio_session();
#endif
    return tribes_sdl_run_application(argc,argv,enter_engine);
}
