/* Playing Now. The TribesNEXT master lists servers over HTTPS; each server
   answers the original Tribes 2 ping (14 -> 16, ends with its name) and info
   (18 -> 20: rules, game type, mission, status, players, max, bots) queries
   over UDP, as the game's browser asks them. Players include bots, so people
   are players minus bots. Status bit 0x2 marks a password. Read-only. */
#import "live_servers.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static NSString * const TBMasterList=@"https://master.tribesnext.com/list";

@interface TBLiveServer : NSObject
@property(nonatomic,copy) NSString *address,*name,*mission,*type;
@property(nonatomic) NSInteger people,bots,slots,ping;
@property(nonatomic) BOOL password,answered;
@end
@implementation TBLiveServer
@end

static NSString *tb_pascal(const uint8_t *d,size_t n,size_t *i){
    if(*i>=n)return nil;size_t len=d[*i];if(*i+1+len>n)return nil;
    NSString *s=[[NSString alloc] initWithBytes:d+*i+1 length:len encoding:NSWindowsCP1252StringEncoding];*i+=1+len;return s?:@"";
}

/* Query every listed server at once and wait up to two seconds for answers. */
static NSArray<TBLiveServer *> *tb_query(NSArray<NSString *> *addresses){
    int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return @[];
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
    NSMutableDictionary<NSString *,TBLiveServer *> *servers=[NSMutableDictionary dictionary];
    NSMutableDictionary<NSString *,NSDate *> *sent=[NSMutableDictionary dictionary];
    for(NSString *address in addresses){
        NSArray *parts=[address componentsSeparatedByString:@":"];if(parts.count!=2)continue;
        struct sockaddr_in to={0};to.sin_len=sizeof to;to.sin_family=AF_INET;to.sin_port=htons((uint16_t)[parts[1] intValue]);
        if(inet_pton(AF_INET,[parts[0] UTF8String],&to.sin_addr)!=1||!to.sin_port)continue;
        NSString *key=[NSString stringWithFormat:@"%s:%u",inet_ntoa(to.sin_addr),ntohs(to.sin_port)];
        TBLiveServer *s=[TBLiveServer new];s.address=key;s.name=key;s.ping=-1;servers[key]=s;sent[key]=NSDate.date;
        for(uint8_t type=14;type<=18;type+=4){
            const uint8_t query[]={type,2,1,2,3,4};sendto(fd,query,sizeof query,0,(struct sockaddr *)&to,sizeof to);
        }
    }
    NSDate *end=[NSDate dateWithTimeIntervalSinceNow:2];uint8_t d[4096];
    while(end.timeIntervalSinceNow>0){
        struct pollfd p={fd,POLLIN,0};if(poll(&p,1,(int)fmax(1,end.timeIntervalSinceNow*1000))<=0)continue;
        struct sockaddr_in from;socklen_t length=sizeof from;
        ssize_t n=recvfrom(fd,d,sizeof d,0,(struct sockaddr *)&from,&length);if(n<7)continue;
        TBLiveServer *s=servers[[NSString stringWithFormat:@"%s:%u",inet_ntoa(from.sin_addr),ntohs(from.sin_port)]];if(!s)continue;
        if(d[0]==16){
            s.ping=(NSInteger)(-[sent[s.address] timeIntervalSinceNow]*1000);
            // The name is the last length-prefixed string.
            for(ssize_t i=n-2;i>=6;i--)if(d[i]==n-i-1){s.name=[[NSString alloc] initWithBytes:d+i+1 length:(NSUInteger)(n-i-1) encoding:NSWindowsCP1252StringEncoding]?:s.name;break;}
        } else if(d[0]==20){
            size_t i=6;tb_pascal(d,(size_t)n,&i);NSString *type=tb_pascal(d,(size_t)n,&i),*mission=tb_pascal(d,(size_t)n,&i);
            if(!mission||i+4>(size_t)n)continue;
            s.type=type;s.mission=mission;s.password=(d[i]&2)!=0;s.slots=d[i+2];s.bots=d[i+3];
            s.people=MAX(0,(NSInteger)d[i+1]-(NSInteger)d[i+3]);s.answered=YES;
        }
    }
    close(fd);
    NSArray *answered=[servers.allValues filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"answered == YES"]];
    return [answered sortedArrayUsingComparator:^NSComparisonResult(TBLiveServer *a,TBLiveServer *b){
        if(a.people!=b.people)return a.people>b.people?NSOrderedAscending:NSOrderedDescending;
        if(a.bots!=b.bots)return a.bots>b.bots?NSOrderedAscending:NSOrderedDescending;
        return [a.name localizedCaseInsensitiveCompare:b.name];}];
}

@interface TBLiveServersController ()
@property(nonatomic,copy) NSArray<TBLiveServer *> *busy,*quiet;
@property(nonatomic,copy) NSString *summary;
@end

@implementation TBLiveServersController
-(void)viewDidLoad {
    [super viewDidLoad];self.title=@"Playing Now";
    self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(done)];
    self.navigationItem.leftBarButtonItem=[[UIBarButtonItem alloc] initWithTitle:@"Server List" style:UIBarButtonItemStylePlain target:self action:@selector(openList)];
    self.refreshControl=[UIRefreshControl new];[self.refreshControl addTarget:self action:@selector(refresh) forControlEvents:UIControlEventValueChanged];
    self.summary=@"Asking the servers…";[self refresh];
}
-(void)done{[self dismissViewControllerAnimated:YES completion:nil];}
-(void)openList{void (^list)(void)=self.serverList;[self dismissViewControllerAnimated:YES completion:^{if(list)list();}];}
-(void)refresh {
    NSMutableURLRequest *request=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:TBMasterList]];
    [request setValue:@"TribesPad" forHTTPHeaderField:@"User-Agent"];request.timeoutInterval=10;
    __weak TBLiveServersController *weak=self;
    [[NSURLSession.sharedSession dataTaskWithRequest:request completionHandler:^(NSData *data,NSURLResponse *response,NSError *error){
        NSString *text=data&&((NSHTTPURLResponse *)response).statusCode==200?[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]:nil;
        NSMutableArray *addresses=[NSMutableArray array];
        for(NSString *line in [text componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet])
            if([line containsString:@":"])[addresses addObject:line];
        NSArray<TBLiveServer *> *servers=addresses.count?tb_query(addresses):@[];
        dispatch_async(dispatch_get_main_queue(),^{
            TBLiveServersController *me=weak;if(!me)return;[me.refreshControl endRefreshing];
            me.busy=[servers filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"people > 0"]];
            me.quiet=[servers filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"people == 0"]];
            NSInteger people=[[me.busy valueForKeyPath:@"@sum.people"] integerValue];
            me.summary=!text?[NSString stringWithFormat:@"The TribesNEXT master did not answer (%@). Pull down to try again.",error.localizedDescription?:@"no list"]:
                people?[NSString stringWithFormat:@"%ld %@ on %lu %@ right now. Bots are not counted.",(long)people,people==1?@"person":@"people",
                    (unsigned long)me.busy.count,me.busy.count==1?@"server":@"servers"]:
                @"Nobody is playing right now, only bots. Most games run in North American evenings: see Game Nights.";
            [me.tableView reloadData];
        });
    }] resume];
}
-(NSInteger)numberOfSectionsInTableView:(UITableView *)tableView{(void)tableView;return 2;}
-(NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section{(void)tableView;return section?self.quiet.count:self.busy.count;}
-(NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    (void)tableView;return section?(self.quiet.count?@"Bots only or empty":nil):(self.busy.count?@"People playing":nil);
}
-(NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView;return section?@"The same servers as Server List (PLAY GAME > JOIN), read from the TribesNEXT master server and asked directly, as the game asks them. "
        "The game's list counts bots as players: 40 (38) there means 2 people and 38 bots. Tap a server to join it, or pull down to refresh.":self.summary;
}
-(UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView;TBLiveServer *s=(path.section?self.quiet:self.busy)[path.row];
    UITableViewCell *cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.textLabel.text=s.name;cell.textLabel.textColor=path.section?UIColor.secondaryLabelColor:UIColor.labelColor;
    NSMutableArray *details=[NSMutableArray array];
    if(s.people)[details addObject:[NSString stringWithFormat:@"%ld %@",(long)s.people,s.people==1?@"person":@"people"]];
    if(s.bots)[details addObject:[NSString stringWithFormat:@"%ld bots",(long)s.bots]];
    if(!s.people&&!s.bots)[details addObject:@"empty"];
    if(s.mission.length)[details addObject:s.mission];
    if(s.type.length)[details addObject:s.type];
    if(s.ping>=0)[details addObject:[NSString stringWithFormat:@"%ld ms",(long)s.ping]];
    // The game's Server List counts bots as players: "# Players (Bots)".
    [details addObject:[NSString stringWithFormat:@"Server List: %ld (%ld)",(long)(s.people+s.bots),(long)s.bots]];
    cell.detailTextLabel.text=[details componentsJoinedByString:@"  ·  "];cell.detailTextLabel.textColor=UIColor.secondaryLabelColor;
    if(s.password)cell.accessoryView=[[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@"lock.fill"]];
    return cell;
}
-(void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    [tableView deselectRowAtIndexPath:path animated:YES];TBLiveServer *s=(path.section?self.quiet:self.busy)[path.row];
    void (^join)(NSString *)=self.join;
    NSString *why=s.password?@"This server needs a password: pick it in the Server List to enter one.":
        !join?@"Join from the Server List once you are signed in and off a server.":nil;
    UIAlertController *sheet=[UIAlertController alertControllerWithTitle:s.name message:why preferredStyle:UIAlertControllerStyleActionSheet];
    if(!why)[sheet addAction:[UIAlertAction actionWithTitle:@"Join" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){(void)a;
        [self dismissViewControllerAnimated:YES completion:^{join(s.address);}];}]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Open Server List" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){(void)a;[self openList];}]];
    [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    UITableViewCell *cell=[tableView cellForRowAtIndexPath:path];
    sheet.popoverPresentationController.sourceView=cell?:tableView;sheet.popoverPresentationController.sourceRect=cell?cell.bounds:CGRectZero;
    [self presentViewController:sheet animated:YES completion:nil];
}
@end
