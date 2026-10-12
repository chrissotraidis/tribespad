#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include "content_area.h"
#include <math.h>
static float inset=-1;
float tb_content_inset(void){
    if(inset>=0)return inset;
    if(![NSThread isMainThread])return 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSArray<UIWindow *> *windows=UIApplication.sharedApplication.windows;
#pragma clang diagnostic pop
    for(UIWindow *w in windows){
        CGSize s=w.bounds.size;if(s.width<=s.height)continue;
        UIEdgeInsets e=w.safeAreaInsets;
        inset=(float)ceil(MAX(e.left,e.right));
        NSLog(@"TribesPad content inset %.0f points on each side of %.0fx%.0f",inset,s.width,s.height);
        return inset;
    }
    return 0;
}
bool tb_content_pixels(SDL_Window *window,int *width,int *height){
    int w=0,h=0,pw=0,ph=0;
    if(!SDL_GetWindowSize(window,&w,&h)||!SDL_GetWindowSizeInPixels(window,&pw,&ph)||w<=0||pw<=0)return false;
    pw-=(int)lroundf(2*tb_content_inset()*(float)pw/(float)w);
    if(pw<=0)return SDL_SetError("Game area has no width");
    if(width)*width=pw;if(height)*height=ph;return true;
}
void *tb_content_layer(SDL_MetalView view){
    UIView *v=(__bridge UIView *)view;CAMetalLayer *full=(CAMetalLayer *)v.layer;float d=tb_content_inset();
    if(d<=0)return (__bridge void *)full;
    full.backgroundColor=UIColor.blackColor.CGColor;
    CAMetalLayer *layer=[CAMetalLayer layer];
    layer.frame=CGRectInset(v.bounds,d,0);layer.contentsScale=full.contentsScale;
    layer.pixelFormat=full.pixelFormat;layer.device=full.device;layer.opaque=YES;
    layer.drawableSize=CGSizeMake(layer.bounds.size.width*layer.contentsScale,layer.bounds.size.height*layer.contentsScale);
    [full addSublayer:layer];return (__bridge void *)layer;
}
