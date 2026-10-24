#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include "content_area.h"
#include <math.h>
static float inset=-1,density=0;
static int fixed_width,fixed_height;
void tb_content_fix_game_size(int width,int height){fixed_width=width;fixed_height=height;inset=-1;}
/* Measured once, on the main thread, from the first landscape window. */
static bool measure(void){
    if(inset>=0)return true;
    if(![NSThread isMainThread])return false;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSArray<UIWindow *> *windows=UIApplication.sharedApplication.windows;
#pragma clang diagnostic pop
    for(UIWindow *w in windows){
        CGSize s=w.bounds.size;if(s.width<=s.height)continue;
        UIEdgeInsets e=w.safeAreaInsets;
        /* On by default: the notch and rounded corners would cover the HUD and
           menu edges. The inset margins hold TribesPad's own buttons. */
        id setting=[NSUserDefaults.standardUserDefaults objectForKey:@"TribesPad.avoidNotch"];
        BOOL avoid=setting?[setting boolValue]:YES;
        inset=avoid?(float)ceil(MAX(e.left,e.right)):0;
        /* Phones are under 480 points tall. The original UI needs 480 lines at
           its scale, and QoL only uses whole scales, so render the game 960
           pixels tall: scale 2 then draws the UI over exactly 480 lines, as
           large as it can be. iPad keeps the native resolution unless
           Settings > Render Scale lowers it: the game then draws fewer pixels
           and the system scales them up. The menus keep their size, because
           the UI scale follows the pixel height (guest_sdl_backend.c). */
        if(fixed_width>0&&fixed_height>0){
            /* A fixed game frame: a centred area with its aspect, drawn at its pixel size. */
            CGFloat area=fmin(s.width,s.height*fixed_width/fixed_height);
            inset=(float)floor((s.width-area)/2);density=(float)(fixed_height/s.height);
            NSLog(@"TribesPad content: fixed %dx%d game frame, inset %.0f points, %.3f pixels per point",fixed_width,fixed_height,inset,density);
            return true;
        }
        double scale=[NSUserDefaults.standardUserDefaults doubleForKey:@"TribesPad.renderScale"];
        if(scale<=0||scale>1)scale=1;scale=fmax(scale,.5);
        if(s.height<480)density=(float)fmin(w.screen.scale,960.0/s.height);
        else density=scale<.99?(float)fmax(w.screen.scale*scale,480.0/s.height):0;
        NSLog(@"TribesPad content inset %.0f points on each side of %.0fx%.0f, %.3f pixels per point",inset,s.width,s.height,density);
        return true;
    }
    return false;
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
    [full addSublayer:layer];return (__bridge void *)layer;
}
