#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include "content_area.h"
#include "content_geometry.h"
#include <math.h>
static float inset=-1,density=0;
static int fixed_width,fixed_height;
/* What the values were measured from: a resize, a safe-area change or a move
   to a display with another scale measures again. */
static __weak UIWindow *measured_window;
static CGSize measured_size;
static UIEdgeInsets measured_insets;
static CGFloat measured_scale;
/* The inset game layer (tb_content_layer) follows each new measurement. */
static __weak CAMetalLayer *content_layer;
static __weak UIView *content_view;
void tb_content_fix_game_size(int width,int height){fixed_width=width;fixed_height=height;inset=-1;measured_window=nil;}
/* The game's own window: SDL's UIKit window once it exists, before that the
   key window of the app's foreground scene (Info.plist declares one). Never
   the main screen or whichever landscape window UIKit happens to list first. */
static UIWindow *game_window(void){
    int count=0;SDL_Window **windows=SDL_GetWindows(&count);UIWindow *found=nil;
    for(int i=0;windows&&i<count&&!found;i++)
        found=(__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(windows[i]),SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER,NULL);
    SDL_free(windows);if(found)return found;
    for(UIScene *scene in UIApplication.sharedApplication.connectedScenes){
        if(![scene isKindOfClass:UIWindowScene.class])continue;
        if(scene.activationState!=UISceneActivationStateForegroundActive&&scene.activationState!=UISceneActivationStateForegroundInactive)continue;
        UIWindowScene *windowScene=(UIWindowScene *)scene;
        if(windowScene.keyWindow)return windowScene.keyWindow;
        if(windowScene.windows.firstObject)return windowScene.windows.firstObject;
    }
    return nil;
}
static void update_layer(void){
    CAMetalLayer *layer=content_layer;UIView *view=content_view;if(!layer||!view)return;
    layer.frame=CGRectInset(view.bounds,inset,0);if(density>0)layer.contentsScale=density;
    layer.drawableSize=fixed_width>0&&fixed_height>0?CGSizeMake(fixed_width,fixed_height):
        CGSizeMake(round(layer.bounds.size.width*layer.contentsScale),round(layer.bounds.size.height*layer.contentsScale));
}
/* Main thread only; other threads see the last measurement. */
static bool measure(void){
    if(![NSThread isMainThread])return inset>=0;
    UIWindow *w=game_window();if(!w)return inset>=0;
    CGSize s=w.bounds.size;
    /* The game is landscape-only: a transient portrait layout keeps the last values. */
    if(s.width<=s.height)return inset>=0;
    UIEdgeInsets e=w.safeAreaInsets;
    CGFloat displayScale=w.traitCollection.displayScale>0?w.traitCollection.displayScale:w.windowScene.screen.scale;
    if(inset>=0&&w==measured_window&&CGSizeEqualToSize(s,measured_size)&&UIEdgeInsetsEqualToEdgeInsets(e,measured_insets)&&displayScale==measured_scale)return true;
    /* On by default: the notch and rounded corners would cover the HUD and
       menu edges. The inset margins hold TribesPad's own buttons. */
    id setting=[NSUserDefaults.standardUserDefaults objectForKey:@"TribesPad.avoidNotch"];
    BOOL avoid=setting?[setting boolValue]:YES;
    double scale=[NSUserDefaults.standardUserDefaults doubleForKey:@"TribesPad.renderScale"];
    TBContentGeometry g=tb_content_geometry(s.width,s.height,e.left,e.right,displayScale,avoid,scale,fixed_width,fixed_height);
    inset=g.inset;density=g.density;
    measured_window=w;measured_size=s;measured_insets=e;measured_scale=displayScale;
    if(fixed_width>0&&fixed_height>0)
        NSLog(@"TribesPad content: fixed %dx%d game frame, inset %.0f points, %.3f pixels per point",fixed_width,fixed_height,inset,density);
    else NSLog(@"TribesPad content inset %.0f points on each side of %.0fx%.0f @%.1f, %.3f pixels per point",inset,s.width,s.height,displayScale,density);
    update_layer();
    return true;
}
float tb_content_inset(void){return measure()?inset:0;}
float tb_content_density(void){return measure()?density:0;}
bool tb_content_pixels(SDL_Window *window,int *width,int *height){
    int w=0,h=0,pw=0,ph=0;float d=tb_content_inset(),scale=tb_content_density();
    if(!SDL_GetWindowSize(window,&w,&h)||!SDL_GetWindowSizeInPixels(window,&pw,&ph)||w<=0||pw<=0)return false;
    if(fixed_width>0&&fixed_height>0&&scale>0){pw=fixed_width;ph=fixed_height;}
    else if(scale>0){pw=(int)lroundf(((float)w-2*d)*scale);ph=(int)lroundf((float)h*scale);}
    else pw-=(int)lroundf(2*d*(float)pw/(float)w);
    if(pw<=0)return SDL_SetError("Game area has no width");
    if(width)*width=pw;if(height)*height=ph;return true;
}
void *tb_content_layer(SDL_MetalView view){
    UIView *v=(__bridge UIView *)view;CAMetalLayer *full=(CAMetalLayer *)v.layer;
    float d=tb_content_inset(),scale=tb_content_density();
    if(d<=0&&scale<=0)return (__bridge void *)full;
    full.backgroundColor=UIColor.blackColor.CGColor;
    CAMetalLayer *layer=[CAMetalLayer layer];
    layer.frame=CGRectInset(v.bounds,d,0);layer.contentsScale=scale>0?scale:full.contentsScale;
    layer.pixelFormat=full.pixelFormat;layer.device=full.device;layer.opaque=YES;
    layer.drawableSize=fixed_width>0&&fixed_height>0?CGSizeMake(fixed_width,fixed_height):
        CGSizeMake(round(layer.bounds.size.width*layer.contentsScale),round(layer.bounds.size.height*layer.contentsScale));
    [full addSublayer:layer];content_layer=layer;content_view=v;return (__bridge void *)layer;
}
