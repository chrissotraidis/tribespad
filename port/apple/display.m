#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#include "display.h"
#include <math.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#else
#import <UIKit/UIKit.h>
#endif
int tb_apple_display_snapshot(TBAppleDisplay *output) {
    if(!output||![NSThread isMainThread])return 0;
    @autoreleasepool {
        TBAppleDisplay value={0};
#if TARGET_OS_OSX
        NSScreen *screen=NSScreen.screens.firstObject;if(!screen)return 0;
        NSRect backing=[screen convertRectToBacking:screen.frame];NSNumber *number=screen.deviceDescription[@"NSScreenNumber"];if(!number)return 0;
        CGDirectDisplayID display=number.unsignedIntValue;value.width=(uint32_t)llround(backing.size.width);value.height=(uint32_t)llround(backing.size.height);
        CGDisplayModeRef mode=CGDisplayCopyDisplayMode(display);if(!mode)return 0;value.native_width=(uint32_t)CGDisplayModeGetPixelWidth(mode);value.native_height=(uint32_t)CGDisplayModeGetPixelHeight(mode);CGDisplayModeRelease(mode);value.scale=screen.backingScaleFactor;value.native_scale=value.scale;value.maximum_refresh_rate=(uint32_t)screen.maximumFramesPerSecond;
        NSRect work=[screen convertRectToBacking:screen.visibleFrame];
        value.work_left=(int32_t)llround(NSMinX(work)-NSMinX(backing));
        value.work_top=(int32_t)llround(NSMaxY(backing)-NSMaxY(work));
        value.work_right=(int32_t)llround(NSMaxX(work)-NSMinX(backing));
        value.work_bottom=(int32_t)llround(NSMaxY(backing)-NSMinY(work));
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        UIScreen *screen=UIScreen.mainScreen;if(!screen)return 0;
#pragma clang diagnostic pop
        CGRect bounds=screen.bounds,native=screen.nativeBounds;value.scale=screen.scale;value.native_scale=screen.nativeScale;value.width=(uint32_t)llround(bounds.size.width*value.scale);value.height=(uint32_t)llround(bounds.size.height*value.scale);value.native_width=(uint32_t)llround(native.size.width);value.native_height=(uint32_t)llround(native.size.height);value.maximum_refresh_rate=(uint32_t)screen.maximumFramesPerSecond;
        /* The mobile guest desktop is a fullscreen surface. Safe-area layout
           belongs to the eventual game window/HUD, not a Windows taskbar. */
        value.work_right=(int32_t)value.width;value.work_bottom=(int32_t)value.height;
#endif
        if(!value.width||!value.height||value.width>32768||value.height>32768||!isfinite(value.scale)||value.scale<=0||value.work_left<0||value.work_top<0||value.work_right<value.work_left||value.work_bottom<value.work_top||(uint32_t)value.work_right>value.width||(uint32_t)value.work_bottom>value.height)return 0;*output=value;return 1;
    }
}
