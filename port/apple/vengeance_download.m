#import "vengeance_download.h"
#import <CommonCrypto/CommonDigest.h>
#import <UIKit/UIKit.h>
#include "../vengeance/installer/vg_installer.h"
#include "vengeance_paths.h"

static NSString *tb_storage(void){
    return [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject stringByAppendingPathComponent:@"TribesPad"];}
static NSString *tb_data(void){return [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData/Vengeance"];}
static NSDictionary *tb_inputs(void){
    static NSDictionary *inputs;static dispatch_once_t once;
    dispatch_once(&once,^{NSData *d=[NSData dataWithContentsOfFile:[tb_data() stringByAppendingPathComponent:@"inputs.json"]];
        inputs=d?[NSJSONSerialization JSONObjectWithData:d options:0 error:nil]:nil;});
    return inputs;
}
static NSString *tb_sha256(NSString *path,volatile uint64_t *done){
    NSInputStream *in=[NSInputStream inputStreamWithFileAtPath:path];[in open];if(in.streamStatus!=NSStreamStatusOpen)return nil;
    CC_SHA256_CTX c;CC_SHA256_Init(&c);uint8_t *buf=malloc(1<<22);NSInteger n;
    while((n=[in read:buf maxLength:1<<22])>0){CC_SHA256_Update(&c,buf,(CC_LONG)n);if(done)*done+=(uint64_t)n;}
    free(buf);[in close];if(n<0)return nil;
    unsigned char d[32];CC_SHA256_Final(d,&c);NSMutableString *s=[NSMutableString string];for(int i=0;i<32;i++)[s appendFormat:@"%02x",d[i]];return s;
}
static NSError *tb_error(NSString *text){return [NSError errorWithDomain:@"TribesPad.Vengeance" code:1 userInfo:@{NSLocalizedDescriptionKey:text}];}

@interface TBVengeanceDownload () <NSURLSessionDownloadDelegate>
@property(nonatomic,readwrite) BOOL running;
@property(nonatomic,strong) NSURLSession *session;
@property(nonatomic,strong) NSURLSessionDownloadTask *task;
@property(nonatomic) BOOL cancelled;
@end

@implementation TBVengeanceDownload
+(instancetype)shared{static TBVengeanceDownload *d;static dispatch_once_t once;dispatch_once(&once,^{d=[TBVengeanceDownload new];});return d;}
+(BOOL)engineHere{return [NSFileManager.defaultManager fileExistsAtPath:[tb_data() stringByAppendingPathComponent:@"images"]]&&tb_inputs()!=nil;}
+(uint64_t)downloadSize{return [tb_inputs()[@"archive"][@"bytes"] unsignedLongLongValue];}
+(uint64_t)gameSize{return [tb_inputs()[@"bytes"] unsignedLongLongValue];}
/* The ZIP, then the installer beside it, then the installer and the game: the largest pair, and 1 GB to spare. */
+(uint64_t)spaceNeeded{uint64_t zip=self.downloadSize,game=self.gameSize;return MAX(2*zip,zip+game)+(1ull<<30);}
-(NSString *)work{return [tb_storage() stringByAppendingPathComponent:@"VengeanceDownload"];}
-(void)report:(NSString *)status fraction:(double)fraction{
    dispatch_async(dispatch_get_main_queue(),^{if(self.progress)self.progress(status,fraction);});}
-(void)finish:(NSError *)error{
    dispatch_async(dispatch_get_main_queue(),^{self.running=NO;UIApplication.sharedApplication.idleTimerDisabled=NO;
        fprintf(stderr,"TribesPad: Vengeance download %s\n",error?error.localizedDescription.UTF8String:"installed");
        if(self.finished)self.finished(error);});}
-(void)start{
    if(self.running)return;
    NSDictionary *inputs=tb_inputs();if(!inputs){[self finish:tb_error(@"This copy of TribesPad cannot download Tribes: Vengeance.")];return;}
    NSDictionary *free=[[NSURL fileURLWithPath:NSHomeDirectory()] resourceValuesForKeys:@[NSURLVolumeAvailableCapacityForImportantUsageKey] error:nil];
    uint64_t available=[free[NSURLVolumeAvailableCapacityForImportantUsageKey] unsignedLongLongValue];
    if(available&&available<TBVengeanceDownload.spaceNeeded){
        [self finish:tb_error([NSString stringWithFormat:@"Tribes: Vengeance needs %.0f GB free while it installs (%.0f GB afterwards); this device has %.1f GB.",
            TBVengeanceDownload.spaceNeeded/1e9,TBVengeanceDownload.gameSize/1e9,available/1e9])];return;}
    self.running=YES;self.cancelled=NO;UIApplication.sharedApplication.idleTimerDisabled=YES;
    NSFileManager *fm=NSFileManager.defaultManager;[fm createDirectoryAtPath:self.work withIntermediateDirectories:YES attributes:nil error:nil];
    [[NSURL fileURLWithPath:self.work] setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
    NSString *zip=[self.work stringByAppendingPathComponent:inputs[@"archive"][@"name"]];
    if([fm fileExistsAtPath:zip]){[self unpack:zip];return;}
    if(!self.session){NSURLSessionConfiguration *c=NSURLSessionConfiguration.defaultSessionConfiguration;c.timeoutIntervalForRequest=60;
        self.session=[NSURLSession sessionWithConfiguration:c delegate:self delegateQueue:nil];}
    NSData *resume=[NSData dataWithContentsOfFile:[self.work stringByAppendingPathComponent:@"resume.data"]];
    /* TRIBESPAD_VENGEANCE_URL: tests may name another copy of the same archive (its SHA-256 is still checked). */
    const char *url=getenv("TRIBESPAD_VENGEANCE_URL");
    self.task=resume?[self.session downloadTaskWithResumeData:resume]:
        [self.session downloadTaskWithURL:[NSURL URLWithString:url?@(url):inputs[@"archive"][@"url"]]];
    [self.task resume];[self report:@"Starting the download\u2026" fraction:0];
}
-(void)cancel{
    self.cancelled=YES;
    [self.task cancelByProducingResumeData:^(NSData *data){if(data)[data writeToFile:[self.work stringByAppendingPathComponent:@"resume.data"] atomically:YES];}];
}
+(void)removeGame{
    NSFileManager *fm=NSFileManager.defaultManager;
    for(NSString *n in @[@"VengeanceGame",@"VengeanceDownload",@"Vengeance"])[fm removeItemAtPath:[tb_storage() stringByAppendingPathComponent:n] error:nil];
}
-(void)URLSession:(NSURLSession *)s downloadTask:(NSURLSessionDownloadTask *)t didWriteData:(int64_t)w totalBytesWritten:(int64_t)total totalBytesExpectedToWrite:(int64_t)expected{
    (void)s;(void)t;(void)w;double all=expected>0?(double)expected:(double)TBVengeanceDownload.downloadSize;
    [self report:[NSString stringWithFormat:@"Downloading %.1f of %.1f GB",total/1e9,all/1e9] fraction:.5*total/all];}
-(void)URLSession:(NSURLSession *)s downloadTask:(NSURLSessionDownloadTask *)t didFinishDownloadingToURL:(NSURL *)location{
    (void)s;NSInteger status=[(NSHTTPURLResponse *)t.response statusCode];
    NSString *zip=[self.work stringByAppendingPathComponent:tb_inputs()[@"archive"][@"name"]];
    [NSFileManager.defaultManager removeItemAtPath:zip error:nil];
    [NSFileManager.defaultManager removeItemAtPath:[self.work stringByAppendingPathComponent:@"resume.data"] error:nil];
    if(status!=200&&status!=206){[self finish:tb_error([NSString stringWithFormat:@"The download server answered %ld.",(long)status])];return;}
    NSError *e;if(![NSFileManager.defaultManager moveItemAtPath:location.path toPath:zip error:&e]){[self finish:e];return;}
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{[self unpack:zip];});
}
-(void)URLSession:(NSURLSession *)s task:(NSURLSessionTask *)t didCompleteWithError:(NSError *)error{
    (void)s;(void)t;if(!error)return;
    NSData *resume=error.userInfo[NSURLSessionDownloadTaskResumeData];
    if(resume)[resume writeToFile:[self.work stringByAppendingPathComponent:@"resume.data"] atomically:YES];
    [self finish:self.cancelled?tb_error(@"Download paused. Tap the card to continue."):
        tb_error([NSString stringWithFormat:@"The download stopped (%@). Tap the card to continue where it left off.",error.localizedDescription])];
}
/* The steps after the download, on a background queue. */
-(void)unpack:(NSString *)zip{
    if([NSThread isMainThread]){dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{[self unpack:zip];});return;}
    NSDictionary *inputs=tb_inputs(),*archive=inputs[@"archive"];NSFileManager *fm=NSFileManager.defaultManager;
    uint64_t zipBytes=[archive[@"bytes"] unsignedLongLongValue],gameBytes=TBVengeanceDownload.gameSize;
    __block volatile uint64_t done=0;__block BOOL watching=YES;
    void (^watch)(NSString *,double,double,uint64_t,uint64_t (^)(void))=^(NSString *what,double from,double to,uint64_t total,uint64_t (^count)(void)){
        watching=YES;dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{while(watching){uint64_t c=count();
            [self report:[NSString stringWithFormat:@"%@ %.0f%%",what,MIN(100.0,100.0*c/total)] fraction:from+(to-from)*MIN(1.0,(double)c/total)];
            [NSThread sleepForTimeInterval:.5];}});};
    // 1. The ZIP is the recorded one.
    done=0;watch(@"Checking the download",.5,.53,zipBytes,^uint64_t{return done;});
    NSString *digest=tb_sha256(zip,&done);watching=NO;
    if(![digest isEqualToString:archive[@"sha256"]]){[fm removeItemAtPath:zip error:nil];
        [self finish:tb_error(@"The download did not match the community release. Tap the card to download it again.")];return;}
    // 2. Its three installer files, each checked.
    NSString *installer=[self.work stringByAppendingPathComponent:@"installer"];[fm removeItemAtPath:installer error:nil];
    [fm createDirectoryAtPath:installer withIntermediateDirectories:YES attributes:nil error:nil];
    NSDictionary *members=archive[@"members"];NSArray *names=[members.allKeys sortedArrayUsingSelector:@selector(compare:)];
    const char *cnames[8];for(NSUInteger i=0;i<names.count&&i<8;i++)cnames[i]=[names[i] UTF8String];
    char error[512];done=0;watch(@"Unpacking",.53,.62,zipBytes,^uint64_t{return done;});
    int r=vg_installer_unzip(zip.fileSystemRepresentation,installer.fileSystemRepresentation,cnames,MIN(names.count,8),&done,error,sizeof error);watching=NO;
    if(r){[self finish:tb_error([NSString stringWithFormat:@"Could not unpack the download: %s.",error])];return;}
    for(NSString *n in names)if(![tb_sha256([installer stringByAppendingPathComponent:n],NULL) isEqualToString:members[n]]){
        [self finish:tb_error(@"The installer in the download did not match the community release.")];return;}
    [fm removeItemAtPath:zip error:nil];
    // 3. The installer's files (innoextract; the installer itself never runs).
    NSString *outdir=[self.work stringByAppendingPathComponent:@"out"];[fm removeItemAtPath:outdir error:nil];
    watch(@"Installing",.62,.92,gameBytes,^uint64_t{uint64_t total=0;
        for(NSString *f in [fm enumeratorAtPath:outdir]){NSDictionary *a=[fm attributesOfItemAtPath:[outdir stringByAppendingPathComponent:f] error:nil];
            if([a.fileType isEqualToString:NSFileTypeRegular])total+=a.fileSize;}return total;});
    r=vg_installer_extract([installer stringByAppendingPathComponent:@"setup.exe"].fileSystemRepresentation,outdir.fileSystemRepresentation,error,sizeof error);watching=NO;
    [fm removeItemAtPath:installer error:nil];
    if(r){[self finish:tb_error([NSString stringWithFormat:@"Could not install the game: %s.",error])];return;}
    // 4. Every file the game needs, against its recorded SHA-256; the community's Windows tools stay out.
    NSString *root=[outdir stringByAppendingPathComponent:inputs[@"root"]];NSDictionary *game=inputs[@"game"];
    done=0;watch(@"Checking files",.92,1,gameBytes,^uint64_t{return done;});
    for(NSString *name in game){NSString *d=tb_sha256([root stringByAppendingPathComponent:name],&done);
        if(![d isEqualToString:game[name]]){watching=NO;
            [self finish:tb_error([NSString stringWithFormat:@"%@ did not match the community release.",name])];return;}}
    watching=NO;
    for(NSString *x in inputs[@"excluded"])[fm removeItemAtPath:[root stringByAppendingPathComponent:x] error:nil];
    NSString *target=[tb_storage() stringByAppendingPathComponent:@"VengeanceGame"];[fm removeItemAtPath:target error:nil];
    NSError *e;if(![fm moveItemAtPath:root toPath:target error:&e]){[self finish:e];return;}
    [[NSURL fileURLWithPath:target] setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
    [fm removeItemAtPath:self.work error:nil];
    [self report:@"Installed" fraction:1];[self finish:nil];
}
@end
