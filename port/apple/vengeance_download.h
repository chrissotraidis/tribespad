/* Tribes: Vengeance as an optional download (iPhone and iPad). The app carries the
   engine; the game comes from the community release, downloaded and unpacked on
   the device the way scripts/vengeance/prepare.py does on a Mac, every file checked
   against the bundled GameData/Vengeance/inputs.json. The result is the game folder
   vengeance_paths.m reads: Application Support/TribesPad/VengeanceGame. */
#import <Foundation/Foundation.h>

@interface TBVengeanceDownload : NSObject
+(instancetype)shared;
/* The engine is in this app (it can download and play the game). */
+(BOOL)engineHere;
/* Bytes the download needs free while it runs, and the game's size afterwards. */
+(uint64_t)spaceNeeded;
+(uint64_t)gameSize;
+(uint64_t)downloadSize;
@property(nonatomic,readonly) BOOL running;
/* Called on the main thread: a line for the picker card and 0...1. done is set at the end
   (error nil when the game is installed). */
@property(nonatomic,copy) void (^progress)(NSString *status,double fraction);
@property(nonatomic,copy) void (^finished)(NSError *error);
-(void)start;
-(void)cancel;
/* Deletes the installed game (settings and saves included) and any partial download. */
+(void)removeGame;
@end
