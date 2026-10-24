/* Starsiege: Tribes storage, shared by the Mac picker and the iOS app. The
   game folder lives in Application Support, where the game writes its settings,
   recordings and logs; the bundle's copy is the source. Files the game writes
   (config, temp, recordings) are copied only when missing; everything else
   follows the bundle whenever its stamp changes. */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include "starsiege_paths.h"

int tb_starsiege_bundled(const char *gamedata){
    NSString *data=[@(gamedata) stringByAppendingPathComponent:@"Starsiege"];NSFileManager *fm=NSFileManager.defaultManager;
    return [fm fileExistsAtPath:[data stringByAppendingPathComponent:@"image.bin"]]&&[fm fileExistsAtPath:[data stringByAppendingPathComponent:@"Game/Tribes.exe"]];
}
int tb_starsiege_prepare_paths(const char *gamedata,const char *storage,char *arguments[5]){
    @autoreleasepool{
        NSFileManager *fm=NSFileManager.defaultManager;NSString *data=[@(gamedata) stringByAppendingPathComponent:@"Starsiege"];
        NSString *root=[@(storage) stringByAppendingPathComponent:@"Starsiege"];
        NSString *volume=[root stringByAppendingPathComponent:@"volume"],*game=[volume stringByAppendingPathComponent:@"TribesPad/Game"];
        NSString *temp=[volume stringByAppendingPathComponent:@"TribesPad/Temp"];
        if(!tb_starsiege_bundled(gamedata)||![fm createDirectoryAtPath:game withIntermediateDirectories:YES attributes:nil error:nil]||
           ![fm createDirectoryAtPath:temp withIntermediateDirectories:YES attributes:nil error:nil])return 0;
        NSString *stamp=[NSString stringWithContentsOfFile:[data stringByAppendingPathComponent:@"stamp.txt"] encoding:NSUTF8StringEncoding error:nil]?:@"";
        NSString *installed=[game stringByAppendingPathComponent:@".tribespad-stamp"];
        if(!stamp.length||![[NSString stringWithContentsOfFile:installed encoding:NSUTF8StringEncoding error:nil] isEqualToString:stamp]){
            NSString *source=[data stringByAppendingPathComponent:@"Game"];NSUInteger copied=0;
            for(NSString *relative in [fm enumeratorAtPath:source]){
                NSString *from=[source stringByAppendingPathComponent:relative],*to=[game stringByAppendingPathComponent:relative];BOOL dir=NO;
                [fm fileExistsAtPath:from isDirectory:&dir];
                if(dir){[fm createDirectoryAtPath:to withIntermediateDirectories:YES attributes:nil error:nil];continue;}
                NSString *top=relative.pathComponents.firstObject.lowercaseString;
                BOOL players=[top isEqualToString:@"config"]||[top isEqualToString:@"temp"]||[top isEqualToString:@"recordings"];
                if([fm fileExistsAtPath:to]){if(players)continue;[fm removeItemAtPath:to error:nil];}
                if(![fm copyItemAtPath:from toPath:to error:nil]){fprintf(stderr,"TribesPad: could not copy Starsiege file %s\n",relative.UTF8String);return 0;}
                copied++;
            }
            [stamp writeToFile:installed atomically:YES encoding:NSUTF8StringEncoding error:nil];
            fprintf(stderr,"TribesPad: Starsiege game folder updated (%lu files)\n",(unsigned long)copied);
        }
        arguments[0]=strdup([data stringByAppendingPathComponent:@"image.bin"].fileSystemRepresentation);
        arguments[1]=strdup(volume.fileSystemRepresentation);arguments[2]=strdup(game.fileSystemRepresentation);
        arguments[3]=strdup(temp.fileSystemRepresentation);arguments[4]=NULL;
        return arguments[0]&&arguments[1]&&arguments[2]&&arguments[3];
    }
}
