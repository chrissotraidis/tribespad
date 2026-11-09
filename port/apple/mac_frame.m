/* The Mac: a game frame of fixed pixel size in a window of any size (Starsiege:
   Tribes, through ANGLE: angle_sdl.c, tribes_angle_fix_frame). The game draws its
   own resolution into a sublayer of the window's Metal layer, centred at the
   game's aspect with black around it; the sublayer's contentsScale keeps its
   bounds times scale at the game's pixel size, so ANGLE's drawable never changes
   size while the window is resized or taken full screen. The iPad's twin is
   content_area.m. */
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <SDL3/SDL.h>
#include <math.h>
void tb_mac_frame_layout(void *frame,SDL_MetalView view,int width,int height){
    CAMetalLayer *layer=(__bridge CAMetalLayer *)frame;CGRect b=((__bridge NSView *)view).layer.bounds;
    if(b.size.width<1||b.size.height<1||width<1||height<1)return;
    CGFloat h=fmin(b.size.height,b.size.width*height/width),w=h*width/height;
    CGRect f=CGRectMake(floor((b.size.width-w)/2),floor((b.size.height-h)/2),w,h);
    if(CGRectEqualToRect(f,layer.frame)&&layer.drawableSize.width==width&&layer.drawableSize.height==height)return;
    [CATransaction begin];[CATransaction setDisableActions:YES];
    layer.frame=f;layer.contentsScale=height/h;layer.drawableSize=CGSizeMake(width,height);
    [CATransaction commit];
}
void *tb_mac_frame_layer(SDL_MetalView view,int width,int height){
    CAMetalLayer *full=(CAMetalLayer *)((__bridge NSView *)view).layer;
    full.backgroundColor=CGColorGetConstantColor(kCGColorBlack);
    CAMetalLayer *layer=[CAMetalLayer layer];
    layer.pixelFormat=full.pixelFormat;layer.device=full.device;layer.opaque=YES;
    layer.magnificationFilter=kCAFilterLinear;
    [full addSublayer:layer];
    tb_mac_frame_layout((__bridge void *)layer,view,width,height);
    return (__bridge void *)layer;
}
