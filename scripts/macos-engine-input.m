// Native input for an owned, visible Mac engine window. No guest-state writes.
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static double number(const char *text) {
    char *end;
    double value = strtod(text, &end);
    if (end == text || *end || !isfinite(value)) exit(2);
    return value;
}

int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc < 3) return 2;
        double requestedPID = number(argv[1]);
        if (requestedPID < 1 || requestedPID > INT_MAX || requestedPID != floor(requestedPID)) return 2;
        pid_t pid = (pid_t)requestedPID;
        NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        if (!app) return 3;
        NSArray *windows = CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID));
        NSDictionary *window = nil;
        CGRect bounds = CGRectZero;
        for (NSDictionary *candidate in windows) {
            if ([candidate[(id)kCGWindowOwnerPID] intValue] != pid) continue;
            CGRect actual;
            if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)candidate[(id)kCGWindowBounds], &actual)) continue;
            // SDL exclusive fullscreen uses layer25; require the actual owner
            // and visible content dimensions instead of assuming layer zero.
            if (actual.size.width < 300 || actual.size.height < 200) continue;
            window = candidate;
            bounds = actual;
            break;
        }
        if (!window) return 4;
        const char *operation = argv[2];
        if (!strcmp(operation, "window")) {
            if (argc != 3) return 2;
            NSDictionary *result = @{@"pid":@(pid), @"id":window[(id)kCGWindowNumber], @"layer":window[(id)kCGWindowLayer],
                @"bounds":@[@(bounds.origin.x), @(bounds.origin.y), @(bounds.size.width), @(bounds.size.height)]};
            NSData *data = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL];
            fwrite(data.bytes, 1, data.length, stdout);
            puts("");
            return 0;
        }
        BOOL key = !strcmp(operation,"key"), click = !strcmp(operation,"click"), aim = !strcmp(operation,"aim");
        BOOL fire = !strcmp(operation,"fire"), jet = !strcmp(operation,"jet");
        if ((!key && !click && !aim && !fire && !jet) || argc != ((fire || jet) ? 4 : 5)) return 2;
        double a = number(argv[3]), b = (fire || jet) ? 0 : number(argv[4]);
        if (key && (a < 0 || a > 127 || a != floor(a) || b < .05 || b > 10)) return 2;
        if (click && (a < 0 || a > 1 || b < 0 || b > 1)) return 2;
        if (aim && (fabs(a) > 300 || fabs(b) > 300)) return 2;
        if ((fire || jet) && (a < .05 || a > 3)) return 2;
        if (![app activateWithOptions:0]) return 5;
        usleep(150000);
        if (key) {
            CGEventRef down = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)a, true);
            CGEventPost(kCGHIDEventTap, down); CFRelease(down);
            usleep((useconds_t)(b * 1000000));
            CGEventRef up = CGEventCreateKeyboardEvent(NULL, (CGKeyCode)a, false);
            CGEventPost(kCGHIDEventTap, up); CFRelease(up);
        } else {
            CGMouseButton button = jet ? kCGMouseButtonRight : kCGMouseButtonLeft;
            CGEventRef current = CGEventCreate(NULL);
            CGPoint point = CGEventGetLocation(current); CFRelease(current);
            if (click) point = CGPointMake(bounds.origin.x + bounds.size.width * a, bounds.origin.y + bounds.size.height * b);
            else if (!CGRectContainsPoint(bounds, point)) point = CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds));
            CGEventRef move = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, point, button);
            CGEventPost(kCGHIDEventTap, move); CFRelease(move);
            usleep(150000);
            if (aim) {
                point.x += a; point.y += b;
                move = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, point, button);
                CGEventSetIntegerValueField(move, kCGMouseEventDeltaX, (int64_t)a);
                CGEventSetIntegerValueField(move, kCGMouseEventDeltaY, (int64_t)b);
                CGEventPost(kCGHIDEventTap, move); CFRelease(move);
            } else {
                CGEventRef down = CGEventCreateMouseEvent(NULL, jet ? kCGEventRightMouseDown : kCGEventLeftMouseDown, point, button);
                CGEventSetIntegerValueField(down, kCGMouseEventClickState, 1);
                CGEventPost(kCGHIDEventTap, down); CFRelease(down);
                usleep((useconds_t)((click ? .1 : a) * 1000000));
                CGEventRef up = CGEventCreateMouseEvent(NULL, jet ? kCGEventRightMouseUp : kCGEventLeftMouseUp, point, button);
                CGEventSetIntegerValueField(up, kCGMouseEventClickState, 1);
                CGEventPost(kCGHIDEventTap, up); CFRelease(up);
            }
        }
        printf("Native HID %s pid=%d window=%u values=%g,%g\n", operation, pid, [window[(id)kCGWindowNumber] unsignedIntValue], a, b);
        return 0;
    }
}
