#ifndef TB_VENGEANCE_MAC_DISPLAY_H
#define TB_VENGEANCE_MAC_DISPLAY_H

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

static inline NSArray<NSString *> *tb_vengeance_mac_sizes(void)
{
    return @[@"1024x768", @"1600x1200", @"1920x1080", @"2048x1536", @"2560x1440"];
}

static inline NSString *tb_vengeance_mac_display_size(void)
{
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(CGMainDisplayID());
    if (!mode) return @"1024x768";
    size_t width = CGDisplayModeGetPixelWidth(mode);
    size_t height = CGDisplayModeGetPixelHeight(mode);
    CGDisplayModeRelease(mode);
    return [NSString stringWithFormat:@"%zux%zu", width, height];
}

static inline NSString *tb_vengeance_mac_selection(void)
{
    NSString *value = [NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.vengeanceResolution"];
    if ([value isEqualToString:@"display"] || [tb_vengeance_mac_sizes() containsObject:value]) return value;
    return NSScreen.screens.firstObject.backingScaleFactor > 1 ? @"2048x1536" : @"1024x768";
}

#endif
