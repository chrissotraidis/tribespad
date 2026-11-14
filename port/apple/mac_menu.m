/* TribesPad's menus on the Mac: what the iPad's ··· menu offers (mobile_controls.m),
 * in each engine's own menu bar (SDL's NSApp; one engine per process, so this file
 * is compiled into all three: Tribes 2, Starsiege: Tribes and Tribes: Vengeance).
 * Items press the game's own keys (SDL key events, as a keyboard would), type a
 * console command, write a request for Tribes 2's script bridge (base/autoexec.cs,
 * scripts/build-ipad-engine.py), or open a community page. Which game this is
 * comes from the engine's name. Quick Save and Quick Load (Vengeance) are
 * Command-S and Command-L; Choose Game quits the game and shows the picker. */
#import <AppKit/AppKit.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <unistd.h>
#include "vengeance_mac_display.h"

typedef enum { TB_TRIBES2, TB_STARSIEGE, TB_VENGEANCE } TBMacGame;
static TBMacGame game;
static __weak NSWindow *fullscreen_window;

static void toggle_fullscreen(NSWindow *window)
{
    if (!(window.styleMask & NSWindowStyleMaskFullScreen))
        window.collectionBehavior |= NSWindowCollectionBehaviorFullScreenPrimary;
    [window toggleFullScreen:nil];
}

/* Called after the first swap of a visible game window, after the GL probe. */
void tb_mac_restore_fullscreen(SDL_Window *native)
{
    if (game == TB_VENGEANCE || getenv("STARSIEGE_HEADLESS")) return;
    NSWindow *window = (__bridge NSWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(native), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!window || window == fullscreen_window) return;
    fullscreen_window = window;

    NSString *key = game == TB_STARSIEGE ? @"TribesPad.starsiegeFullscreen" : @"TribesPad.tribes2Fullscreen";
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
        [center addObserverForName:NSWindowDidEnterFullScreenNotification object:nil queue:nil
            usingBlock:^(NSNotification *note) {
                if (note.object == fullscreen_window)
                    [NSUserDefaults.standardUserDefaults setBool:YES forKey:key];
            }];
        [center addObserverForName:NSWindowDidExitFullScreenNotification object:nil queue:nil
            usingBlock:^(NSNotification *note) {
                if (note.object == fullscreen_window)
                    [NSUserDefaults.standardUserDefaults setBool:NO forKey:key];
            }];
    });
    if ([NSUserDefaults.standardUserDefaults boolForKey:key]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (window == fullscreen_window && window.isVisible && !(window.styleMask & NSWindowStyleMaskFullScreen))
                toggle_fullscreen(window);
        });
    }
}

/* The engine's play state (vengeance_input.c defines it for Vengeance). */
__attribute__((weak)) int vengeance_in_play(void) { return -1; }

static void key_event(SDL_Scancode code, bool down, SDL_Keymod mod)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    SDL_Window *w = SDL_GetKeyboardFocus();
    e.key.windowID = w ? SDL_GetWindowID(w) : 0;
    e.key.scancode = code;
    e.key.key = SDL_GetKeyFromScancode(code, mod, false);
    e.key.mod = mod;
    e.key.down = down;
    SDL_PushEvent(&e);
}
/* One key press, with Control or Shift held around it; the release comes a frame later. */
static void tap(SDL_Scancode code, SDL_Keymod mod, double after)
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(after * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (mod & SDL_KMOD_CTRL) key_event(SDL_SCANCODE_LCTRL, true, mod);
        if (mod & SDL_KMOD_SHIFT) key_event(SDL_SCANCODE_LSHIFT, true, mod);
        key_event(code, true, mod);
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((after + 0.03) * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        key_event(code, false, mod);
        if (mod & SDL_KMOD_SHIFT) key_event(SDL_SCANCODE_LSHIFT, false, SDL_KMOD_NONE);
        if (mod & SDL_KMOD_CTRL) key_event(SDL_SCANCODE_LCTRL, false, SDL_KMOD_NONE);
    });
}
/* The scancode and Shift for a character on a US keyboard. */
static bool char_key(char c, SDL_Scancode *code, bool *shift)
{
    static const char *plain = "-=;/.' ", *shifted_from = "_+:?>\"";
    static const SDL_Scancode plain_codes[] = { SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_SLASH,
                                                SDL_SCANCODE_PERIOD, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_SPACE };
    *shift = false;
    if (c >= 'a' && c <= 'z') { *code = SDL_SCANCODE_A + (c - 'a'); return true; }
    if (c >= 'A' && c <= 'Z') { *code = SDL_SCANCODE_A + (c - 'A'); *shift = true; return true; }
    if (c >= '1' && c <= '9') { *code = SDL_SCANCODE_1 + (c - '1'); return true; }
    if (c == '0') { *code = SDL_SCANCODE_0; return true; }
    const char *p = strchr(plain, c);
    if (p) { *code = plain_codes[p - plain]; return true; }
    p = strchr(shifted_from, c);
    if (p) { *code = plain_codes[p - shifted_from]; *shift = true; return true; }
    return false;
}
/* The engine's typed-command queue (starsiege_input.c, in the Starsiege and Vengeance
   engines): one key change a frame, so the game's console reads every character.
   Tribes 2's engine has no queue; this default lets it link, and it types below. */
__attribute__((weak)) void ss_type_command(const char *line, int open_with_tab) { (void)line; (void)open_with_tab; }
/* Vengeance's command runner (vengeance_input.c): the game's own command handler,
   called on its thread, with nothing typed. Only the Vengeance engine has it. */
__attribute__((weak)) void vengeance_run_command(const char *line) { (void)line; }
/* A console command, typed (Vengeance opens its console with Tab, as -console allows).
   Returns the seconds the typing takes when typed here (no queue). */
static double type_command(NSString *command, double after)
{
    if (game == TB_VENGEANCE) {
        NSString *copy = [command copy];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(after * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            vengeance_run_command(copy.UTF8String); });
        return 0;
    }
    if (game != TB_TRIBES2) {
        NSString *copy = [command copy];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(after * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            ss_type_command(copy.UTF8String, game == TB_VENGEANCE); });
        return 0;
    }
    double t = after;
    tap(SDL_SCANCODE_TAB, SDL_KMOD_NONE, t); t += 0.08;
    for (NSUInteger i = 0; i < command.length; i++) {
        SDL_Scancode code; bool shift;
        if (!char_key((char)[command characterAtIndex:i], &code, &shift)) continue;
        tap(code, shift ? SDL_KMOD_SHIFT : SDL_KMOD_NONE, t); t += 0.05;
    }
    tap(SDL_SCANCODE_RETURN, SDL_KMOD_NONE, t);
    return t + 0.15 - after;
}
static NSString *support_path(NSString *relative)
{
    NSString *s = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    return [[s stringByAppendingPathComponent:@"TribesPad"] stringByAppendingPathComponent:relative];
}
/* Tribes 2: a request for base/autoexec.cs, as the iPad's menu sends it. */
static void t2_request(NSString *request)
{
    NSString *line = [NSString stringWithFormat:@"%.0f %@\n", NSDate.date.timeIntervalSince1970 * 1000, request];
    [line writeToFile:support_path(@"Game/base/prefs/tribespad-request.txt") atomically:YES encoding:NSUTF8StringEncoding error:nil];
    fprintf(stderr, "TribesPad menu: request %s\n", request.UTF8String);
}
/* Vengeance's bot settings (Game > Bots, and the iPad's Options > Bots): difficulty
   easy, normal or hard; size small, medium or full. The engine reads them from the
   environment when a hosted bot map's bots start (vengeance_input.c). */
static NSString *bot_skill(void)
{
    NSString *s = [NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.botSkill"];
    return [@[@"easy", @"normal", @"hard"] containsObject:s] ? s : @"normal";
}
static NSString *bot_size(void)
{
    NSString *s = [NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.vengeanceBotSize"];
    return [@[@"small", @"medium", @"full"] containsObject:s] ? s : @"small";
}
static void pass_bot_settings(int overwrite)
{
    setenv("TRIBESPAD_BOT_SKILL", bot_skill().UTF8String, overwrite);
    setenv("TRIBESPAD_BOT_SIZE", bot_size().UTF8String, overwrite);
}

@interface TBMacMenu : NSObject
@end
@implementation TBMacMenu
- (void)run:(NSMenuItem *)item { void (^action)(void) = item.representedObject; if (action) action(); }
@end
static TBMacMenu *target;

static NSMenuItem *item(NSString *title, NSString *keys, void (^action)(void))
{
    NSMenuItem *i = [[NSMenuItem alloc] initWithTitle:title action:@selector(run:) keyEquivalent:keys ?: @""];
    i.target = target;
    i.representedObject = [action copy];
    return i;
}
static NSMenuItem *key_item(NSString *title, SDL_Scancode code, SDL_Keymod mod)
{
    return item(title, nil, ^{ tap(code, mod, 0.05); });
}
static NSMenuItem *submenu(NSString *title, NSArray<NSMenuItem *> *items)
{
    NSMenuItem *i = [[NSMenuItem alloc] initWithTitle:title action:NULL keyEquivalent:@""];
    NSMenu *m = [[NSMenu alloc] initWithTitle:title];
    for (NSMenuItem *x in items) [m addItem:x];
    i.submenu = m;
    return i;
}
static NSMenuItem *link_item(NSString *title, NSString *url)
{
    return item(title, nil, ^{ [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:url]]; });
}
/* A section title inside a menu (a plain disabled item before macOS 14). */
static NSMenuItem *header(NSString *title)
{
    if (@available(macOS 14, *)) return [NSMenuItem sectionHeaderWithTitle:title];
    NSMenuItem *i = [[NSMenuItem alloc] initWithTitle:title action:NULL keyEquivalent:@""];
    i.enabled = NO;
    return i;
}
static void rebuild_menus(void);
static void open_picker(void)
{
    /* The game quits as from its own Quit (the window's close), then the app starts
       again with the picker showing. */
    NSString *app = NSBundle.mainBundle.bundlePath;
    if (![app hasSuffix:@".app"]) return;
    NSWorkspaceOpenConfiguration *c = [NSWorkspaceOpenConfiguration configuration];
    c.createsNewApplicationInstance = YES;
    c.environment = @{ @"TRIBESPAD_CHOOSE": @"1" };
    SDL_Event e; SDL_zero(e); e.type = SDL_EVENT_QUIT; e.quit.timestamp = SDL_GetTicksNS(); SDL_PushEvent(&e);
    [NSWorkspace.sharedWorkspace openApplicationAtURL:[NSURL fileURLWithPath:app] configuration:c completionHandler:nil];
}
static NSArray<NSMenuItem *> *game_items(void)
{
    NSMutableArray<NSMenuItem *> *items = [NSMutableArray array];
    /* Vengeance's bot difficulty: kept for the next bot game; in a bot game it also
       applies to the bots playing now. */
    NSArray<NSString *> *notes = @[@"aim spread 9°, react in 1.5 s", @"aim spread 5°, react in 0.9 s", @"as TRIBESai made them"];
    NSArray<NSString *> *levels = @[@"easy", @"normal", @"hard"], *names = @[@"Easy", @"Normal", @"Hard"];
    NSMutableArray<NSMenuItem *> *difficulty = [NSMutableArray arrayWithObject:header(@"Difficulty")];
    for (NSUInteger i = 0; i < 3; i++) {
        NSString *level = levels[i];
        NSMenuItem *d = item([NSString stringWithFormat:@"%@ (%@)", names[i], notes[i]], nil, ^{
            [NSUserDefaults.standardUserDefaults setObject:level forKey:@"TribesPad.botSkill"];
            pass_bot_settings(1);
            fprintf(stderr, "TribesPad menu: bot difficulty %s\n", level.UTF8String);
            type_command([@"tribespad botskill " stringByAppendingString:level], 0);
            rebuild_menus(); });
        d.state = [bot_skill() isEqualToString:level] ? NSControlStateValueOn : NSControlStateValueOff;
        [difficulty addObject:d];
    }
    [items addObject:key_item(@"Game Menu", SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE)];
    if (game == TB_TRIBES2) {
        [items addObjectsFromArray:@[key_item(@"Scores", SDL_SCANCODE_F2, SDL_KMOD_NONE),
            submenu(@"Chat", @[key_item(@"All Chat", SDL_SCANCODE_U, SDL_KMOD_NONE), key_item(@"Team Chat", SDL_SCANCODE_Y, SDL_KMOD_NONE),
                key_item(@"Quick Chat Menu", SDL_SCANCODE_V, SDL_KMOD_NONE)]),
            submenu(@"Actions", @[key_item(@"Use Repair Kit", SDL_SCANCODE_Q, SDL_KMOD_NONE), key_item(@"Place Mine", SDL_SCANCODE_B, SDL_KMOD_NONE),
                key_item(@"Place Beacon", SDL_SCANCODE_H, SDL_KMOD_NONE), key_item(@"Targeting Laser", SDL_SCANCODE_L, SDL_KMOD_NONE),
                key_item(@"Throw Flag", SDL_SCANCODE_F, SDL_KMOD_LCTRL), NSMenuItem.separatorItem,
                key_item(@"Third-Person View", SDL_SCANCODE_TAB, SDL_KMOD_NONE), key_item(@"Command Map", SDL_SCANCODE_C, SDL_KMOD_NONE),
                NSMenuItem.separatorItem, key_item(@"Suicide", SDL_SCANCODE_K, SDL_KMOD_LCTRL)]),
            NSMenuItem.separatorItem,
            submenu(@"Graphics Quality", @[item(@"Balanced", nil, ^{ t2_request(@"graphics balanced"); }),
                item(@"Fast", nil, ^{ t2_request(@"graphics fast"); })])]];
    } else if (game == TB_STARSIEGE) {
        [items addObjectsFromArray:@[key_item(@"Scores", SDL_SCANCODE_TAB, SDL_KMOD_NONE),
            submenu(@"Chat", @[key_item(@"All Chat", SDL_SCANCODE_T, SDL_KMOD_NONE), key_item(@"Team Chat", SDL_SCANCODE_Y, SDL_KMOD_NONE),
                key_item(@"Quick Chat Menu", SDL_SCANCODE_V, SDL_KMOD_NONE)]),
            submenu(@"Actions", @[key_item(@"Inventory", SDL_SCANCODE_I, SDL_KMOD_NONE), key_item(@"Objectives", SDL_SCANCODE_O, SDL_KMOD_NONE),
                key_item(@"Command Map", SDL_SCANCODE_C, SDL_KMOD_NONE), NSMenuItem.separatorItem,
                key_item(@"Use Repair Kit", SDL_SCANCODE_H, SDL_KMOD_NONE), key_item(@"Place Beacon", SDL_SCANCODE_B, SDL_KMOD_NONE),
                key_item(@"Drop Flag", SDL_SCANCODE_F, SDL_KMOD_LCTRL), NSMenuItem.separatorItem,
                key_item(@"Zoom", SDL_SCANCODE_Z, SDL_KMOD_NONE), key_item(@"Third-Person View", SDL_SCANCODE_R, SDL_KMOD_NONE),
                NSMenuItem.separatorItem, key_item(@"Suicide", SDL_SCANCODE_K, SDL_KMOD_LCTRL)])]];
    } else {
        [items addObjectsFromArray:@[key_item(@"Scores", SDL_SCANCODE_F2, SDL_KMOD_NONE),
            submenu(@"Chat", @[key_item(@"All Chat", SDL_SCANCODE_T, SDL_KMOD_NONE), key_item(@"Team Chat", SDL_SCANCODE_Y, SDL_KMOD_NONE),
                key_item(@"Quick Chat Menu", SDL_SCANCODE_V, SDL_KMOD_NONE)]),
            submenu(@"Actions", @[key_item(@"Use", SDL_SCANCODE_F, SDL_KMOD_NONE), key_item(@"Third-Person View", SDL_SCANCODE_Z, SDL_KMOD_NONE),
                key_item(@"Zoom Level", SDL_SCANCODE_R, SDL_KMOD_NONE), NSMenuItem.separatorItem, key_item(@"Respawn", SDL_SCANCODE_B, SDL_KMOD_NONE)]),
            NSMenuItem.separatorItem,
            item(@"Quick Save", @"s", ^{ tap(SDL_SCANCODE_F5, SDL_KMOD_NONE, 0.05); }),
            item(@"Quick Load", @"l", ^{ tap(SDL_SCANCODE_F8, SDL_KMOD_NONE, 0.05); }),
            NSMenuItem.separatorItem]];
        /* Bots: TRIBESai's bot maps fill both teams by themselves when you host one in
           MULTIPLAYER > HOST (vengeance_input.c); their difficulty and size are set here. */
        NSString *maps = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData/Vengeance/Game/Content/Maps"];
        if ([NSFileManager.defaultManager fileExistsAtPath:[maps stringByAppendingPathComponent:@"MP-FortBots_V5.tvm"]] || getenv("VENGEANCE_CONTENT")) {
            NSMutableArray<NSMenuItem *> *bots = [NSMutableArray arrayWithArray:difficulty];
            [bots addObjectsFromArray:@[NSMenuItem.separatorItem, header(@"Game Size")]];
            for (NSArray<NSString *> *z in @[@[@"small", @"Small: 6 a Side (defenders and flag grabbers)"], @[@"medium", @"Medium: 10 a Side (adds fighters)"],
                                             @[@"full", @"Full: 15 a Side (adds repair gunners and vehicles)"]]) {
                NSString *value = z[0];
                NSMenuItem *s = item(z[1], nil, ^{ [NSUserDefaults.standardUserDefaults setObject:value forKey:@"TribesPad.vengeanceBotSize"];
                    pass_bot_settings(1); rebuild_menus(); });
                s.state = [bot_size() isEqualToString:value] ? NSControlStateValueOn : NSControlStateValueOff;
                [bots addObject:s];
            }
            [bots addObjectsFromArray:@[NSMenuItem.separatorItem,
                item(@"Log Bot Decisions", nil, ^{ static BOOL on; on = !on; type_command(on ? @"tribespad botlog on" : @"tribespad botlog off", 0); }),
                item(@"How to Play with Bots", nil, ^{
                    NSAlert *a = [NSAlert new];
                    a.messageText = @"Play with Bots";
                    a.informativeText = @"In MULTIPLAYER > HOST, choose Capture the Flag and one of the four bot maps: FortBots, EmeraldBots, Tropics or Feigned Beliefs Bots. "
                        "Start the server: a few seconds after you are in, TribesPad switches on both teams' bots, at the difficulty and size chosen here, and puts you on a team.";
                    [a runModal]; })]];
            [items addObject:submenu(@"Bots", bots)];
        }
        NSString *quality = [NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.vengeanceGraphics"] ?: @"fast";
        NSMenuItem *full = item(@"Full Detail", nil, ^{ [NSUserDefaults.standardUserDefaults setObject:@"full" forKey:@"TribesPad.vengeanceGraphics"];
            type_command(@"set windowsclient projectors true", 0.05); });
        NSMenuItem *fast = item(@"Fast", nil, ^{ [NSUserDefaults.standardUserDefaults setObject:@"fast" forKey:@"TribesPad.vengeanceGraphics"];
            type_command(@"set windowsclient projectors false", 0.05); });
        full.state = [quality isEqualToString:@"full"] ? NSControlStateValueOn : NSControlStateValueOff;
        fast.state = [quality isEqualToString:@"fast"] ? NSControlStateValueOn : NSControlStateValueOff;
        NSMutableArray<NSMenuItem *> *sizes = [NSMutableArray array];
        NSString *size = tb_vengeance_mac_selection();
        NSArray<NSString *> *options = [@[@"display"] arrayByAddingObjectsFromArray:tb_vengeance_mac_sizes()];
        for (NSString *s in options) {
            BOOL display = [s isEqualToString:@"display"];
            NSString *pixels = [(display ? tb_vengeance_mac_display_size() : s)
                stringByReplacingOccurrencesOfString:@"x" withString:@" × "];
            NSString *label = display ? [NSString stringWithFormat:@"Main Display (%@)", pixels] : pixels;
            NSMenuItem *r = item([label stringByAppendingString:@" (next start)"], nil, ^{
                [NSUserDefaults.standardUserDefaults setObject:s forKey:@"TribesPad.vengeanceResolution"];
                rebuild_menus(); });
            r.state = [size isEqualToString:s] ? NSControlStateValueOn : NSControlStateValueOff;
            [sizes addObject:r];
        }
        [items addObject:submenu(@"Graphics Quality", @[full, fast])];
        [items addObject:submenu(@"Resolution", sizes)];
    }
    [items addObject:NSMenuItem.separatorItem];
    NSMenuItem *fullscreen = item(@"Toggle Full Screen", @"f", ^{
        if (game == TB_VENGEANCE) [NSApp.keyWindow toggleFullScreen:nil];
        else toggle_fullscreen(fullscreen_window);
    });
    fullscreen.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    [items addObject:fullscreen];
    [items addObject:key_item(@"Console", game == TB_VENGEANCE ? SDL_SCANCODE_TAB : SDL_SCANCODE_GRAVE, SDL_KMOD_NONE)];
    [items addObject:item(@"Choose Game…", @"g", ^{ open_picker(); })];
    return items;
}
static NSArray<NSMenuItem *> *online_items(void)
{
    if (game == TB_STARSIEGE)
        return @[link_item(@"Server List on the Web (tribes1.co)", @"https://tribes1.co/"), link_item(@"PlayT1", @"https://playt1.com/"),
                 NSMenuItem.separatorItem, link_item(@"PlayT1 Discord", @"https://discord.gg/bf4RHMq"),
                 link_item(@"TribesPad Discord", @"https://discord.gg/xwHfUD2bxW"),
                 link_item(@"Tribes 1 JunkYard: configs, mods, demos", @"https://twdemos.iateyourbaby.com/")];
    if (game == TB_VENGEANCE)
        return @[link_item(@"Server List on the Web (OpenSpy)", @"https://openspy.net/server-list/tribesv"),
                 link_item(@"TribesRevengeance.net", @"https://tribesrevengeance.net/"), NSMenuItem.separatorItem,
                 link_item(@"Tribes Revengeance Discord", @"https://discord.gg/uXHjZ95"), link_item(@"TribesPad Discord", @"https://discord.gg/xwHfUD2bxW")];
    return @[item(@"Server List", nil, ^{ t2_request(@"leave-servers"); }), NSMenuItem.separatorItem,
             link_item(@"PlayT2 Discord", @"https://discord.gg/EMMVhQJgf9"), link_item(@"The Cut Discord", @"https://discord.gg/yz3AhkJCxg"),
             link_item(@"TribesPad Discord", @"https://discord.gg/xwHfUD2bxW"), link_item(@"TribesNEXT Forum", @"https://www.tribesnext.com/forum/")];
}
static void install(void)
{
    NSMenu *bar = NSApp.mainMenu;
    if (!bar || [bar indexOfItemWithTitle:@"Game"] >= 0) return;
    target = [TBMacMenu new];
    NSInteger at = MAX(1, bar.numberOfItems - 1);   /* before SDL's Window menu */
    NSMenuItem *g = submenu(@"Game", game_items());
    NSMenuItem *o = submenu(@"Play Online", online_items());
    NSMenuItem *h = submenu(@"Help", @[link_item(@"TribesPad on GitHub", @"https://github.com/chrissotraidis/tribespad")]);
    [bar insertItem:g atIndex:at];
    [bar insertItem:o atIndex:at + 1];
    [bar addItem:h];
    fprintf(stderr, "TribesPad menu: Game, Play Online and Help menus added\n");
}
/* The Game menu again, so its check marks follow a choice (Difficulty). */
static void rebuild_menus(void)
{
    NSInteger at = [NSApp.mainMenu indexOfItemWithTitle:@"Game"];
    if (at < 0) return;
    NSMenu *m = [[NSMenu alloc] initWithTitle:@"Game"];
    for (NSMenuItem *x in game_items()) [m addItem:x];
    [NSApp.mainMenu itemAtIndex:at].submenu = m;
}
__attribute__((constructor)) static void setup(void)
{
    const char *name = getprogname();
    game = strcasestr(name, "vengeance") ? TB_VENGEANCE : strcasestr(name, "starsiege") ? TB_STARSIEGE : TB_TRIBES2;
    if (game == TB_VENGEANCE) pass_bot_settings(0);   /* a value already given (a test launch) stays */
    if (getenv("STARSIEGE_HEADLESS")) return;   /* scripted test runs have no menu bar */
    /* SDL makes the app and its menus when the game opens its window; add ours once they exist. */
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), NSEC_PER_SEC, NSEC_PER_SEC / 4);
    dispatch_source_set_event_handler(timer, ^{
        if (NSApp && NSApp.mainMenu.numberOfItems) { install(); dispatch_source_cancel(timer); }
    });
    dispatch_resume(timer);
}
