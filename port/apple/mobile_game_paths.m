#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mobile_game_paths.h"

static int storage_failed(NSError *error) {
    fprintf(stderr, "TribesPad: cannot prepare app storage: %s\n", error.localizedDescription.UTF8String);
    return 0;
}

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
        /* TribesPad owns base/autoexec.cs (offline mode, preference saving);
           refresh it so updates reach existing profiles. Players' own scripts
           belong in base/scripts/autoexec, which the game also runs. */
        NSString *autoexec = @"base/autoexec.cs";
        NSData *script = [NSData dataWithContentsOfFile:[[inputs stringByAppendingPathComponent:@"Profile"] stringByAppendingPathComponent:autoexec] options:0 error:&error];
        if (!script || ![script writeToFile:[profile stringByAppendingPathComponent:autoexec] options:NSDataWritingAtomic error:&error]) return storage_failed(error);
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
