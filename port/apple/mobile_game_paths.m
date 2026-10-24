#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mobile_game_paths.h"

static int storage_failed(NSError *error) {
    fprintf(stderr, "TribesPad: cannot prepare app storage: %s\n", error.localizedDescription.UTF8String);
    return 0;
}

#if TARGET_OS_IOS
/* Skins: the Files app shows TribesPad's Documents folder (On My iPad >
   TribesPad). Player skins (.png) and skin packs (.vl2) put in its Skins
   folder are copied into the game each launch, before the game scans its
   files. Nothing in the game is deleted; a copy is replaced only when the
   player's file changed. A problem here never stops the game. */
static void tb_copy_skins(NSString *profile) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *skins = [documents stringByAppendingPathComponent:@"Skins"];
    if (!documents || ![fm createDirectoryAtPath:skins withIntermediateDirectories:YES attributes:nil error:nil]) return;
    NSString *about = [skins stringByAppendingPathComponent:@"About Skins.txt"];
    if (![fm fileExistsAtPath:about])
        [@"Put Tribes 2 skins here, then reopen TribesPad.\n\n"
          "Player skins are PNG files named like name.lmale.png, name.lfemale.png and name.lbioderm.png. "
          "Skin packs are .vl2 files. TribesPad copies them into the game when it opens. Choose your skin under "
          "PLAY GAME > WARRIOR SETUP > Custom Skins.\n\n"
          "Other players see your skin only if they have the same file; everyone else sees a standard skin.\n"
          writeToFile:about atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSString *textures = [profile stringByAppendingPathComponent:@"base/textures/skins"];
    NSUInteger copied = 0;
    for (NSString *name in [fm contentsOfDirectoryAtPath:skins error:nil]) {
        NSString *extension = name.pathExtension.lowercaseString;
        NSString *folder = [extension isEqualToString:@"png"] ? textures :
            [extension isEqualToString:@"vl2"] ? [profile stringByAppendingPathComponent:@"base"] : nil;
        if (!folder || [name hasPrefix:@"."]) continue;
        NSString *source = [skins stringByAppendingPathComponent:name], *target = [folder stringByAppendingPathComponent:name];
        NSDictionary *from = [fm attributesOfItemAtPath:source error:nil], *to = [fm attributesOfItemAtPath:target error:nil];
        if (![from.fileType isEqualToString:NSFileTypeRegular]) continue;
        if (to && [to.fileModificationDate isEqualToDate:from.fileModificationDate] && to.fileSize == from.fileSize) continue;
        NSError *error = nil;
        NSString *partial = [target stringByAppendingString:@".copying"];
        [fm createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
        [fm removeItemAtPath:partial error:nil];
        BOOL ok = [fm copyItemAtPath:source toPath:partial error:&error] &&
            [fm setAttributes:@{NSFileModificationDate: from.fileModificationDate} ofItemAtPath:partial error:&error];
        if (ok) ok = to ? [fm replaceItemAtURL:[NSURL fileURLWithPath:target] withItemAtURL:[NSURL fileURLWithPath:partial]
                              backupItemName:nil options:0 resultingItemURL:nil error:&error]
                        : [fm moveItemAtPath:partial toPath:target error:&error];
        if (ok) copied++;
        else {
            fprintf(stderr, "TribesPad: skin %s not copied: %s\n", name.UTF8String, error.localizedDescription.UTF8String);
            [fm removeItemAtPath:partial error:nil];
        }
    }
    if (copied) fprintf(stderr, "TribesPad: copied %lu skin file(s) from Files\n", (unsigned long)copied);
}
#endif

int tb_mobile_prepare_paths(const char *resources, const char *storage, char *arguments[9]) {
    @autoreleasepool {
        memset(arguments, 0, 9 * sizeof(*arguments));
        NSFileManager *fm = NSFileManager.defaultManager;
        NSString *inputs = [NSString stringWithUTF8String:resources];
        NSString *root = [NSString stringWithUTF8String:storage];
        NSString *profile = [root stringByAppendingPathComponent:@"Game"];
        NSString *capture = [root stringByAppendingPathComponent:@"Capture"];
        NSString *volume = [root stringByAppendingPathComponent:@"Volume"];
        NSString *temp = [volume stringByAppendingPathComponent:@"TribesPad/Temp"];
        NSError *error = nil;
        for (NSString *directory in @[root, capture, temp]) {
            if (![fm createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:&error]) return storage_failed(error);
        }
        /* Large, reproducible game assets do not belong in iCloud backups. */
        if (![[NSURL fileURLWithPath:root] setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&error]) return storage_failed(error);
        if (![fm fileExistsAtPath:profile]) {
            NSString *staging = [root stringByAppendingPathComponent:@"Game.installing"];
            if ([fm fileExistsAtPath:staging] && ![fm removeItemAtPath:staging error:&error]) return storage_failed(error);
            if (![fm copyItemAtPath:[inputs stringByAppendingPathComponent:@"Profile"] toPath:staging error:&error]) return storage_failed(error);
            if (![fm moveItemAtPath:staging toPath:profile error:&error]) return storage_failed(error);
        }
        /* TribesPad owns base/autoexec.cs (offline mode, preference saving) and
           base/scripts/GameGui.cs (internet server list); refresh them so
           updates reach existing profiles, and drop the stale compiled copy.
           Players' own scripts belong in base/scripts/autoexec. */
        /* Settings > Online Play, on by default: off, base/autoexec.cs starts
           the game offline. */
        id online = [NSUserDefaults.standardUserDefaults objectForKey:@"TribesPad.onlinePlay"];
        BOOL offline = online && ![online boolValue];
        for (NSString *owned in @[@"base/autoexec.cs", @"base/scripts/GameGui.cs"]) {
            NSData *script = [NSData dataWithContentsOfFile:[[inputs stringByAppendingPathComponent:@"Profile"] stringByAppendingPathComponent:owned] options:0 error:&error];
            if (script && offline && [owned isEqualToString:@"base/autoexec.cs"]) {
                NSMutableData *flagged = [[@"$TribesPadOffline = 1;\n" dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
                [flagged appendData:script];script = flagged;
            }
            if (!script || ![script writeToFile:[profile stringByAppendingPathComponent:owned] options:NSDataWritingAtomic error:&error]) return storage_failed(error);
            [fm removeItemAtPath:[[profile stringByAppendingPathComponent:owned] stringByAppendingString:@".dso"] error:nil];
        }
#if TARGET_OS_IOS
        tb_copy_skins(profile);
#endif
        /* Refresh immutable loader inputs without deleting diagnostic captures
           or an existing user's profile. The app has only one engine instance. */
        NSString *loader = [inputs stringByAppendingPathComponent:@"Loader"];
        NSArray *files = [fm contentsOfDirectoryAtPath:loader error:&error];
        if (!files) return storage_failed(error);
        for (NSString *name in files) {
            NSData *data = [NSData dataWithContentsOfFile:[loader stringByAppendingPathComponent:name] options:0 error:&error];
            if (!data || ![data writeToFile:[capture stringByAppendingPathComponent:name] options:NSDataWritingAtomic error:&error]) return storage_failed(error);
        }
        NSArray *paths = @[@"TribesPad", [inputs stringByAppendingPathComponent:@"engine.bin"],
            [inputs stringByAppendingPathComponent:@"qol.bin"], volume, profile, temp, capture, @"engine"];
        for (NSUInteger i = 0; i < paths.count; ++i) {
            arguments[i] = strdup([paths[i] fileSystemRepresentation]);
            if (!arguments[i]) {
                for (NSUInteger j = 0; j < i; ++j) free(arguments[j]);
                memset(arguments, 0, 9 * sizeof(*arguments));
                fprintf(stderr, "TribesPad: cannot allocate launch paths\n");
                return 0;
            }
        }
        return 1;
    }
}
