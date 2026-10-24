/* Playing Now: who is on TribesNEXT servers right now, people and bots counted
   separately, from the same master list and server queries as the game's JOIN;
   or, for Starsiege: Tribes, on the community masters' servers. */
#import <UIKit/UIKit.h>

@interface TBLiveServersController : UITableViewController
/* Join a server by "a.b.c.d:port"; nil while joining is not possible (on a
   server already, or before the TribesNEXT login). */
@property(nonatomic,copy) void (^join)(NSString *address);
/* Open the game's own server list (PLAY GAME > JOIN). */
@property(nonatomic,copy) void (^serverList)(void);
/* Starsiege: Tribes: servers from the community masters (t1_live.c). Its
   servers report one player count, bots included, as the game's list does. */
@property(nonatomic) BOOL starsiege;
@end
