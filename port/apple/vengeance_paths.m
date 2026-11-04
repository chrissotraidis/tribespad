/* Tribes: Vengeance storage on iOS. The game folder lives in Application
   Support, where the game writes its settings (the INI files in Program/Bin),
   saves (Program/Saves) and logs; the bundle's copy is the source. Its Content
   (gigabytes of maps, art and sound, never written) stays where it is: the
   engine reads it in place (VENGEANCE_CONTENT, port/core/guest_filesystem.c).
   The player's files are copied only when missing; everything else follows the
   source whenever its stamp changes.

   The source game folder is GameData/Vengeance/Game in the bundle. A
   development build may leave it out (build-ipad-engine.py --vengeance-sideload)
   and use one copied once into Application Support/TribesPad/VengeanceGame, so
   each new build installs without resending the campaign. */
#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#if !TARGET_OS_OSX
#import <UIKit/UIKit.h>
#endif
#include <stdio.h>
#include <string.h>
#include "vengeance_paths.h"

static NSString *source_game(const char *gamedata,const char *storage){
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *bundled=[@(gamedata) stringByAppendingPathComponent:@"Vengeance/Game"];
    if([fm fileExistsAtPath:[bundled stringByAppendingPathComponent:@"Program/Bin/TV_CD_DVD.exe"]])return bundled;
    NSString *sideloaded=[@(storage) stringByAppendingPathComponent:@"VengeanceGame"];
    if([fm fileExistsAtPath:[sideloaded stringByAppendingPathComponent:@"Program/Bin/TV_CD_DVD.exe"]])return sideloaded;
    return nil;
}
int tb_vengeance_bundled(const char *gamedata,const char *storage){
    return [NSFileManager.defaultManager fileExistsAtPath:[@(gamedata) stringByAppendingPathComponent:@"Vengeance/images"]]&&source_game(gamedata,storage)!=nil;
}
/* The game's drawing size, one of three 4:3 sizes, unless chosen otherwise:
   2048x1536 on iPad and 1600x1200 on iPhone (scaled to the screen); 1024x768 on
   the Mac, where it is the game's window and must fit the screen. The game is
   bound by the CPU: on an M2 iPad Pro 2048x1536 costs about 1% of the frame rate. */
NSString *tb_vengeance_resolution(void){
    NSString *size=[NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.vengeanceResolution"];
#if TARGET_OS_OSX
    NSString *fallback=@"1024x768";
#else
    NSString *fallback=UIDevice.currentDevice.userInterfaceIdiom==UIUserInterfaceIdiomPad?@"2048x1536":@"1600x1200";
#endif
    return [@[@"1024x768",@"1600x1200",@"2048x1536"] containsObject:size]?size:fallback;
}
static BOOL players(NSString *relative){
    NSString *lower=relative.lowercaseString;
    return [lower hasPrefix:@"program/saves/"]||([lower hasPrefix:@"program/bin/"]&&[lower hasSuffix:@".ini"]);
}
int tb_vengeance_prepare_paths(const char *gamedata,const char *storage,char *arguments[6]){
    @autoreleasepool{
        NSFileManager *fm=NSFileManager.defaultManager;NSString *data=[@(gamedata) stringByAppendingPathComponent:@"Vengeance"];
        NSString *volume=[[@(storage) stringByAppendingPathComponent:@"Vengeance"] stringByAppendingPathComponent:@"volume"];
        NSString *game=[volume stringByAppendingPathComponent:@"TribesPad/Game"],*temp=[volume stringByAppendingPathComponent:@"TribesPad/Temp"];
        NSString *source=source_game(gamedata,storage),*content=[game stringByAppendingPathComponent:@"Content"];
        if(!tb_vengeance_bundled(gamedata,storage)||![fm createDirectoryAtPath:game withIntermediateDirectories:YES attributes:nil error:nil]||
           ![fm createDirectoryAtPath:temp withIntermediateDirectories:YES attributes:nil error:nil])return 0;
        /* An empty Content folder, so listings of the game folder show it; the engine reads the source's. */
        if([fm contentsOfDirectoryAtPath:content error:nil].count)[fm removeItemAtPath:content error:nil];
        [fm createDirectoryAtPath:content withIntermediateDirectories:YES attributes:nil error:nil];
        NSString *stamp=[NSString stringWithContentsOfFile:[data stringByAppendingPathComponent:@"stamp.txt"] encoding:NSUTF8StringEncoding error:nil]?:@"";
        stamp=[stamp stringByAppendingString:source];   /* a different source folder is a change too */
        NSString *installed=[game stringByAppendingPathComponent:@".tribespad-stamp"];
        if(![[NSString stringWithContentsOfFile:installed encoding:NSUTF8StringEncoding error:nil] isEqualToString:stamp]){
            NSUInteger copied=0;
            for(NSString *relative in [fm enumeratorAtPath:source]){
                if([relative.pathComponents.firstObject.lowercaseString isEqualToString:@"content"])continue;
                NSString *from=[source stringByAppendingPathComponent:relative],*to=[game stringByAppendingPathComponent:relative];BOOL dir=NO;
                [fm fileExistsAtPath:from isDirectory:&dir];
                if(dir){[fm createDirectoryAtPath:to withIntermediateDirectories:YES attributes:nil error:nil];continue;}
                if([fm fileExistsAtPath:to]){if(players(relative))continue;[fm removeItemAtPath:to error:nil];}
                NSError *error=nil;
                if(![fm copyItemAtPath:from toPath:to error:&error]){
                    fprintf(stderr,"TribesPad: could not copy Vengeance file %s: %s\n",relative.UTF8String,error.description.UTF8String);return 0;}
                copied++;
            }
            [stamp writeToFile:installed atomically:YES encoding:NSUTF8StringEncoding error:nil];
            fprintf(stderr,"TribesPad: Vengeance game folder updated from %s (%lu files)\n",source.UTF8String,(unsigned long)copied);
        }
        /* TribesPad's Graphics Quality (mobile_controls.m): Fast, the default, turns off
           projectors (projected lights and shadows), about a third of each frame on
           iPad; Full Detail keeps the game's own setting. Applied at each launch to
           the game's INI, which the game reads at start. */
        NSString *ini=[game stringByAppendingPathComponent:@"Program/Bin/TV_CD_DVD.ini"];
        NSString *text=[NSString stringWithContentsOfFile:ini encoding:NSISOLatin1StringEncoding error:nil];
        if(text){NSString *quality=[NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.vengeanceGraphics"]?:@"fast";
            NSString *want=[quality isEqualToString:@"full"]?@"Projectors=True":@"Projectors=False";
            NSString *changed=[[text stringByReplacingOccurrencesOfString:@"Projectors=True" withString:want]
                stringByReplacingOccurrencesOfString:@"Projectors=False" withString:want];
            /* 8x anisotropic filtering on both settings (the game ships it off): floors
               and walls stay sharp into the distance, at no measurable cost on iPad. */
            changed=[changed stringByReplacingOccurrencesOfString:@"LevelOfAnisotropy=1\r" withString:@"LevelOfAnisotropy=8\r"];
            changed=[changed stringByReplacingOccurrencesOfString:@"LevelOfAnisotropy=1\n" withString:@"LevelOfAnisotropy=8\n"];
            /* TribesPad's Resolution (the same menu): the game draws at this 4:3 size and
               the screen scales it to fit. The engine's desktop is the same size
               (VENGEANCE_DISPLAY), so it is the one mode the game offers. */
            NSString *size=tb_vengeance_resolution();
            NSArray<NSString *> *wh=[size componentsSeparatedByString:@"x"];
            for(NSString *key in @[@"FullscreenViewport",@"WindowedViewport"])for(NSUInteger axis=0;axis<2;axis++){
                NSRegularExpression *re=[NSRegularExpression regularExpressionWithPattern:[NSString stringWithFormat:@"(?m)^%@%@=[0-9]+",key,axis?@"Y":@"X"] options:0 error:nil];
                changed=[re stringByReplacingMatchesInString:changed options:0 range:NSMakeRange(0,changed.length)
                    withTemplate:[NSString stringWithFormat:@"%@%@=%@",key,axis?@"Y":@"X",wh[axis]]];}
            setenv("VENGEANCE_DISPLAY",size.UTF8String,1);
            if(![changed isEqualToString:text])[changed writeToFile:ini atomically:YES encoding:NSISOLatin1StringEncoding error:nil];}
        arguments[0]=strdup([data stringByAppendingPathComponent:@"images"].fileSystemRepresentation);
        arguments[1]=strdup(volume.fileSystemRepresentation);arguments[2]=strdup(game.fileSystemRepresentation);
        arguments[3]=strdup(temp.fileSystemRepresentation);
        arguments[4]=strdup([source stringByAppendingPathComponent:@"Content"].fileSystemRepresentation);arguments[5]=NULL;
        return arguments[0]&&arguments[1]&&arguments[2]&&arguments[3]&&arguments[4];
    }
}
