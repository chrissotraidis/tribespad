#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdlib.h>
#include "../port/apple/mobile_game_paths.h"
static void release_paths(char **paths) { for (int i=0;i<8;++i) free(paths[i]); }
int main(void) {
 @autoreleasepool {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *root=[NSTemporaryDirectory() stringByAppendingPathComponent:[@"TribesPad paths " stringByAppendingString:NSUUID.UUID.UUIDString]];
    NSString *inputs=[root stringByAppendingPathComponent:@"GameData"];
    NSString *storage=[root stringByAppendingPathComponent:@"App Support"];
    NSString *profile=[inputs stringByAppendingPathComponent:@"Profile/base"];
    NSString *loader=[inputs stringByAppendingPathComponent:@"Loader"];
    assert([fm createDirectoryAtPath:profile withIntermediateDirectories:YES attributes:nil error:nil]);
    assert([fm createDirectoryAtPath:loader withIntermediateDirectories:YES attributes:nil error:nil]);
    NSData *seed=[@"original" dataUsingEncoding:NSUTF8StringEncoding];
    assert([seed writeToFile:[profile stringByAppendingPathComponent:@"prefs.cs"] atomically:YES]);
    assert([seed writeToFile:[loader stringByAppendingPathComponent:@"published-code.bin"] atomically:YES]);
    char *paths[9];
    assert(tb_mobile_prepare_paths(inputs.fileSystemRepresentation,storage.fileSystemRepresentation,paths));
    assert(paths[8]==NULL && strcmp(paths[7],"engine")==0);
    assert([fm fileExistsAtPath:@(paths[5])]);
    NSString *saved=[@(paths[4]) stringByAppendingPathComponent:@"base/prefs.cs"];
    NSString *loaded=[@(paths[6]) stringByAppendingPathComponent:@"published-code.bin"];
    assert([[NSData dataWithContentsOfFile:saved] isEqual:seed]);
    assert([[NSData dataWithContentsOfFile:loaded] isEqual:seed]);
    NSData *user=[@"saved controls" dataUsingEncoding:NSUTF8StringEncoding];
    assert([user writeToFile:saved atomically:YES]);
    release_paths(paths);
    NSData *updated=[@"updated loader" dataUsingEncoding:NSUTF8StringEncoding];
    assert([updated writeToFile:[loader stringByAppendingPathComponent:@"published-code.bin"] atomically:YES]);
    assert(tb_mobile_prepare_paths(inputs.fileSystemRepresentation,storage.fileSystemRepresentation,paths));
    assert([[NSData dataWithContentsOfFile:saved] isEqual:user]);
    assert([[NSData dataWithContentsOfFile:loaded] isEqual:updated]);
    release_paths(paths);
    // A failed first install must not masquerade as a complete user profile.
    NSString *broken=[root stringByAppendingPathComponent:@"Broken"];
    assert(!tb_mobile_prepare_paths("/nonexistent/TribesPad",broken.fileSystemRepresentation,paths));
    assert(![fm fileExistsAtPath:[broken stringByAppendingPathComponent:@"Game"]]);
    assert(tb_mobile_prepare_paths(inputs.fileSystemRepresentation,broken.fileSystemRepresentation,paths));
    release_paths(paths);
    assert([fm removeItemAtPath:root error:nil]);
    puts("mobile app paths: first install, preservation, refresh and retry passed");
 }
 return 0;
}
