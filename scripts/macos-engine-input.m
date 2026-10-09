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

// Explicit US physical keys avoid AppleScript's keypad synthesis for digits
// and decimal punctuation. This is test input, not a guest keyboard remap.
static BOOL text_key(unsigned char c, CGKeyCode *code, BOOL *shift) {
    static const char plain[] = "asdfhgzxcvbqwerty1234567890-=[];'\\,./` ";
    static const char upper[] = "ASDFHGZXCVBQWERTY!@#$%^&*()_+{}:\"|<>?~ ";
    static const CGKeyCode codes[] = {0,1,2,3,4,5,6,7,8,9,11,12,13,14,15,17,16,
        18,19,20,21,23,22,26,28,25,29,27,24,33,30,41,39,42,43,47,44,50,49};
    _Static_assert(sizeof plain == sizeof upper, "text modifier rows");
    _Static_assert(sizeof plain-1 == sizeof codes/sizeof *codes, "text key rows");
    // Remaining letter keys.
    static const char extra[] = "uojklimnp";
    static const CGKeyCode extra_codes[] = {32,31,38,40,37,34,46,45,35};
    for (size_t i=0;i<sizeof plain-1;i++) {
        if (c==plain[i] || c==upper[i]) { *code=codes[i]; *shift=c!=plain[i]; return YES; }
    }
    for (size_t i=0;i<sizeof extra-1;i++) {
        if (c==extra[i] || c==extra[i]-'a'+'A') { *code=extra_codes[i]; *shift=c!=extra[i]; return YES; }
    }
    return NO;
}
static void post_key(CGKeyCode code, BOOL down, CGEventFlags flags) {
    CGEventRef event=CGEventCreateKeyboardEvent(NULL,code,down);
    if (!event) exit(6);
    CGEventSetFlags(event,flags);CGEventPost(kCGHIDEventTap,event);CFRelease(event);
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
        if (!strcmp(operation,"text")) {
            if (argc!=4 || strlen(argv[3])>512) return 2;
            CGKeyCode code;BOOL shift;
            for (const unsigned char *p=(const unsigned char *)argv[3];*p;p++)
                if (!text_key(*p,&code,&shift)) return 2;
            if (![app activateWithOptions:0]) return 5;
            usleep(150000);
            for (const unsigned char *p=(const unsigned char *)argv[3];*p;p++) {
                text_key(*p,&code,&shift);
                if (shift) { post_key(56,YES,kCGEventFlagMaskShift); usleep(20000); }
                post_key(code,YES,shift?kCGEventFlagMaskShift:0); usleep(30000);
                post_key(code,NO,shift?kCGEventFlagMaskShift:0); usleep(30000);
                if (shift) { post_key(56,NO,0); usleep(20000); }
            }
            printf("Native HID text pid=%d window=%u characters=%zu\n",pid,
                   [window[(id)kCGWindowNumber] unsignedIntValue],strlen(argv[3]));
            return 0;
        }
        // Drive an actual Simulator touch by dragging or holding its owned window.
        BOOL drag = !strcmp(operation,"drag"), press = !strcmp(operation,"press");
        if (drag || press) {
            if (argc != (drag ? 8 : 6)) return 2;
            double x0=number(argv[3]), y0=number(argv[4]);
            double x1=drag ? number(argv[5]) : x0, y1=drag ? number(argv[6]) : y0;
            double seconds=number(argv[drag ? 7 : 5]);
            if (x0<0 || x0>1 || y0<0 || y0>1 || x1<0 || x1>1 || y1<0 || y1>1 || seconds<.05 || seconds>10) return 2;
            if (![app activateWithOptions:0]) return 5;
            usleep(150000);
            CGPoint point=CGPointMake(bounds.origin.x+bounds.size.width*x0,bounds.origin.y+bounds.size.height*y0);
            CGEventRef event=CGEventCreateMouseEvent(NULL,kCGEventMouseMoved,point,kCGMouseButtonLeft);
            CGEventPost(kCGHIDEventTap,event);CFRelease(event);usleep(150000);
            event=CGEventCreateMouseEvent(NULL,kCGEventLeftMouseDown,point,kCGMouseButtonLeft);
            CGEventPost(kCGHIDEventTap,event);CFRelease(event);
            const int steps=30;
            for (int i=1;i<=steps;++i) {
                usleep((useconds_t)(seconds*1000000/steps));
                point=CGPointMake(bounds.origin.x+bounds.size.width*(x0+(x1-x0)*i/steps),
                                  bounds.origin.y+bounds.size.height*(y0+(y1-y0)*i/steps));
                event=CGEventCreateMouseEvent(NULL,kCGEventLeftMouseDragged,point,kCGMouseButtonLeft);
                CGEventPost(kCGHIDEventTap,event);CFRelease(event);
            }
            event=CGEventCreateMouseEvent(NULL,kCGEventLeftMouseUp,point,kCGMouseButtonLeft);
            CGEventPost(kCGHIDEventTap,event);CFRelease(event);
            printf("Native HID %s pid=%d window=%u duration=%g\n",operation,pid,[window[(id)kCGWindowNumber] unsignedIntValue],seconds);
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
            CGPoint point = click
                ? CGPointMake(bounds.origin.x + bounds.size.width * a, bounds.origin.y + bounds.size.height * b)
                : CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds));
            CGEventRef move = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, point, button);
            // Relative gameplay actions must not inherit a cursor-position delta
            // from the focus move. Keep the event inside the owned window.
            if (!click) {
                CGEventSetIntegerValueField(move, kCGMouseEventDeltaX, 0);
                CGEventSetIntegerValueField(move, kCGMouseEventDeltaY, 0);
            }
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
