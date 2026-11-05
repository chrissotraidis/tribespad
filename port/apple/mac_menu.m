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

typedef enum { TB_TRIBES2, TB_STARSIEGE, TB_VENGEANCE } TBMacGame;
static TBMacGame game;

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
/* A console command, typed (Vengeance opens its console with Tab, as -console allows).
   Returns the seconds the typing takes when typed here (no queue). */
static double type_command(NSString *command, double after)
{
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
/* Starsiege: a request for its script bridge (scripts/starsiege/build_engine.py), as the
   iPad's menu sends it: the request file rewritten, then the flag its report loop checks.
   The number stays below 2^24 so the game's float comparison is exact. */
static void t1_request(NSString *request)
{
    static unsigned counter;
    NSString *number = [NSString stringWithFormat:@"%ld", (long)((long long)NSDate.date.timeIntervalSince1970 % 1000000) * 10 + (long)(counter++ % 10)];
    NSString *folder = support_path(@"Starsiege/volume/TribesPad/Game");
    NSString *text = [NSString stringWithFormat:@"$TribesPad::request = \"%@ %@\";\n", number, request];
    [text writeToFile:[folder stringByAppendingPathComponent:@"config/tribespad-request.cs"] atomically:NO encoding:NSUTF8StringEncoding error:nil];
    NSString *flag = [folder stringByAppendingPathComponent:@"temp/tribespad-request.flag"];
    [NSData.data writeToFile:flag atomically:NO];
    /* The game reads it within a quarter second; the flag then goes. */
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        [NSFileManager.defaultManager removeItemAtPath:flag error:nil]; });
    fprintf(stderr, "TribesPad menu: request %s %s\n", number.UTF8String, request.UTF8String);
}

/* Vengeance: a hosted game on one of TRIBESai's bot maps (scripts/vengeance/bots.py), and
   its bots switched on. The maps switch bots on from a console in each base, which runs
   the map's scripts 01Config01Script to 07 (Blood Eagle) and 02Config01Script to 07
   (Phoenix); the game runs a map's scripts from its console only with its developer
   tools on (GameEngine EnableDevTools, read when a game starts), so that is set for this
   game first. The host joins Blood Eagle. */
static void vengeance_bots_on(void)
{
    type_command(@"changeteam 0", 0);
    for (NSString *team in @[@"01", @"02"])
        for (int i = 1; i <= 7; i++) type_command([NSString stringWithFormat:@"runscript %@Config0%dScript", team, i], 0);
    /* Developer tools back off: they were only needed to run the map's scripts. */
    type_command(@"set GameEngine EnableDevTools False", 0);
    fprintf(stderr, "TribesPad menu: bots switched on\n");
}
static void vengeance_play_with_bots(NSString *map)
{
    /* No tournament ready-up for a game against bots. */
    type_command(@"set MultiplayerGameInfo bTournamentMode False", 0);
    type_command(@"set GameEngine EnableDevTools True", 0);
    type_command([NSString stringWithFormat:@"open %@?game=GameClasses.ModeCTF?listen", map], 0);
    fprintf(stderr, "TribesPad menu: hosting %s with bots\n", map.UTF8String);
    /* Once the map is up: play leaves (the load), then returns; a few seconds later the bots. */
    __block int phase = 0, ticks = 0;
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), NSEC_PER_SEC / 2, NSEC_PER_SEC / 10);
    dispatch_source_set_event_handler(timer, ^{
        int playing = vengeance_in_play();
        ticks++;
        if (phase == 0 && playing == 0) phase = 1;
        else if (phase == 1 && playing == 1) { phase = 2; ticks = 0; }
        else if (phase == 2 && ticks >= 8) { dispatch_source_cancel(timer); vengeance_bots_on(); }
        if (ticks > 360 && phase < 2) { dispatch_source_cancel(timer); fprintf(stderr, "TribesPad menu: the bot map did not start\n"); }
    });
    dispatch_resume(timer);
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
            submenu(@"Play with Bots", @[item(@"Katabatic, 4 Bots", nil, ^{ t2_request(@"bots 4"); }),
                item(@"Katabatic, 8 Bots", nil, ^{ t2_request(@"bots 8"); }), item(@"Katabatic, 14 Bots", nil, ^{ t2_request(@"bots 14"); })]),
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
                NSMenuItem.separatorItem, key_item(@"Suicide", SDL_SCANCODE_K, SDL_KMOD_LCTRL)]),
            NSMenuItem.separatorItem,
            /* The game's own AI on both teams, in a game hosted here (build_engine.py). */
            submenu(@"Play with Bots", @[item(@"Raindance, 4 Bots", nil, ^{ t1_request(@"bots 4 Raindance"); }),
                item(@"Raindance, 8 Bots", nil, ^{ t1_request(@"bots 8 Raindance"); }),
                item(@"Dangerous Crossing, 8 Bots", nil, ^{ t1_request(@"bots 8 DangerousCrossing"); }),
                item(@"Snowblind, 8 Bots", nil, ^{ t1_request(@"bots 8 Snowblind"); })])]];
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
        NSString *maps = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData/Vengeance/Game/Content/Maps"];
        NSMutableArray<NSMenuItem *> *bots = [NSMutableArray array];
        for (NSArray<NSString *> *m in @[@[@"MP-FortBots_V5", @"Fort"], @[@"MP-EmeraldBots_V71", @"Emerald"], @[@"MP-TropicsBots_V1", @"Tropics"],
                                         @[@"MP-FeignedBeliefsBots_V1", @"Feigned Beliefs"]]) {
            NSString *file = m[0];
            if (![NSFileManager.defaultManager fileExistsAtPath:[maps stringByAppendingPathComponent:[file stringByAppendingString:@".tvm"]]] && !getenv("VENGEANCE_CONTENT")) continue;
            [bots addObject:item([m[1] stringByAppendingString:@", Capture the Flag"], nil, ^{ vengeance_play_with_bots(file); })];
        }
        if (bots.count) {
            [bots addObject:NSMenuItem.separatorItem];
            [bots addObject:item(@"Switch Bots On Here", nil, ^{ vengeance_bots_on(); })];
            [items addObject:submenu(@"Play with Bots", bots)];
        }
        NSString *quality = [NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.vengeanceGraphics"] ?: @"fast";
        NSMenuItem *full = item(@"Full Detail", nil, ^{ [NSUserDefaults.standardUserDefaults setObject:@"full" forKey:@"TribesPad.vengeanceGraphics"];
            type_command(@"set windowsclient projectors true", 0.05); });
        NSMenuItem *fast = item(@"Fast", nil, ^{ [NSUserDefaults.standardUserDefaults setObject:@"fast" forKey:@"TribesPad.vengeanceGraphics"];
            type_command(@"set windowsclient projectors false", 0.05); });
        full.state = [quality isEqualToString:@"full"] ? NSControlStateValueOn : NSControlStateValueOff;
        fast.state = [quality isEqualToString:@"fast"] ? NSControlStateValueOn : NSControlStateValueOff;
        NSMutableArray<NSMenuItem *> *sizes = [NSMutableArray array];
        NSString *size = [NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.vengeanceResolution"];
        for (NSString *s in @[@"1024x768", @"1600x1200", @"2048x1536"]) {
            NSMenuItem *r = item([[s stringByReplacingOccurrencesOfString:@"x" withString:@" × "] stringByAppendingString:@" (next start)"], nil, ^{
                [NSUserDefaults.standardUserDefaults setObject:s forKey:@"TribesPad.vengeanceResolution"]; });
            r.state = [size isEqualToString:s] ? NSControlStateValueOn : NSControlStateValueOff;
            [sizes addObject:r];
        }
        [items addObject:submenu(@"Graphics Quality", @[full, fast, NSMenuItem.separatorItem, submenu(@"Resolution", sizes)])];
    }
    [items addObject:NSMenuItem.separatorItem];
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
__attribute__((constructor)) static void setup(void)
{
    const char *name = getprogname();
    game = strcasestr(name, "vengeance") ? TB_VENGEANCE : strcasestr(name, "starsiege") ? TB_STARSIEGE : TB_TRIBES2;
    if (getenv("STARSIEGE_HEADLESS")) return;   /* scripted test runs have no menu bar */
    /* SDL makes the app and its menus when the game opens its window; add ours once they exist. */
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), NSEC_PER_SEC, NSEC_PER_SEC / 4);
    dispatch_source_set_event_handler(timer, ^{
        if (NSApp && NSApp.mainMenu.numberOfItems) { install(); dispatch_source_cancel(timer); }
    });
    dispatch_resume(timer);
}
