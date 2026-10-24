/* TribesPad's game picker. A loaded engine can't be unloaded, so the picker
   runs natively before either game starts (docs/LAUNCHER-PLAN.md) and waits
   on the main run loop until a game is chosen. Tribes 2 (TribesNEXT) starts;
   Starsiege: Tribes is shown, marked in development, until its translation
   plays (docs/STARSIEGE-TRIBES-PLAN.md). Art comes from TribesPad's own
   emblem and the player's own Tribes 2 files (copied at build time into
   GameData/Launcher); nothing from the games is in the repository. */
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#include "launcher.h"

static NSString * const TBLaunchStarsiegePlan=@"https://github.com/chrissotraidis/tribespad/blob/main/docs/STARSIEGE-TRIBES-PLAN.md";
static BOOL tb_launch_chosen;

static UIColor *tb_rgb(unsigned hex,CGFloat a){return [UIColor colorWithRed:((hex>>16)&255)/255.0 green:((hex>>8)&255)/255.0 blue:(hex&255)/255.0 alpha:a];}

@interface TBLauncherCard:UIControl
@property(nonatomic,strong) UIImageView *art;
@property(nonatomic,strong) UILabel *title,*subtitle,*status,*badge;
@property(nonatomic,strong) CAGradientLayer *shade;
@property(nonatomic) BOOL available;
@end
@implementation TBLauncherCard
-(instancetype)initWithTitle:(NSString *)title subtitle:(NSString *)subtitle art:(UIImage *)art available:(BOOL)available {
    if(!(self=[super initWithFrame:CGRectZero]))return nil;
    self.available=available;self.clipsToBounds=YES;self.layer.cornerRadius=22;self.layer.cornerCurve=kCACornerCurveContinuous;
    self.layer.borderWidth=2;self.layer.borderColor=tb_rgb(available?0x2EC4C9:0x4A5A6E,available?.9:.6).CGColor;
    self.backgroundColor=tb_rgb(0x0B1E33,1);
    self.art=[[UIImageView alloc] initWithImage:art];self.art.contentMode=UIViewContentModeScaleAspectFill;
    self.art.alpha=available?.85:.35;self.art.userInteractionEnabled=NO;[self addSubview:self.art];
    self.shade=[CAGradientLayer layer];
    self.shade.colors=@[(id)[UIColor colorWithWhite:0 alpha:0].CGColor,(id)tb_rgb(0x06111E,.92).CGColor];
    self.shade.locations=@[@.25,@1];[self.layer addSublayer:self.shade];
    UILabel *(^label)(UIFont *,UIColor *)=^UILabel *(UIFont *font,UIColor *color){
        UILabel *l=[UILabel new];l.font=font;l.textColor=color;l.numberOfLines=0;l.userInteractionEnabled=NO;[self addSubview:l];return l;};
    self.title=label([UIFont systemFontOfSize:34 weight:UIFontWeightHeavy],UIColor.whiteColor);
    self.title.attributedText=[[NSAttributedString alloc] initWithString:title.uppercaseString attributes:@{NSKernAttributeName:@2.5}];
    self.subtitle=label([UIFont systemFontOfSize:15 weight:UIFontWeightSemibold],tb_rgb(0x9FDFE2,1));self.subtitle.text=subtitle;
    self.status=label([UIFont monospacedDigitSystemFontOfSize:15 weight:UIFontWeightMedium],[UIColor colorWithWhite:1 alpha:.85]);
    self.badge=label([UIFont systemFontOfSize:13 weight:UIFontWeightBlack],available?tb_rgb(0x06111E,1):UIColor.whiteColor);
    self.badge.textAlignment=NSTextAlignmentCenter;self.badge.layer.cornerRadius=8;self.badge.layer.masksToBounds=YES;
    self.badge.backgroundColor=available?tb_rgb(0x2EC4C9,1):tb_rgb(0x4A5A6E,1);
    self.badge.attributedText=[[NSAttributedString alloc] initWithString:available?@"PLAY":@"IN DEVELOPMENT" attributes:@{NSKernAttributeName:@1.5}];
    self.isAccessibilityElement=YES;self.accessibilityTraits=UIAccessibilityTraitButton;
    self.accessibilityLabel=[NSString stringWithFormat:@"%@, %@",title,available?@"play":@"in development"];
    return self;
}
-(void)layoutSubviews {
    [super layoutSubviews];CGRect b=self.bounds;CGFloat pad=fmax(18,b.size.width*.06);
    self.art.frame=b;self.shade.frame=b;
    CGSize badge=[self.badge sizeThatFits:CGSizeMake(300,40)];badge.width+=28;badge.height+=12;
    self.badge.frame=CGRectMake(pad,b.size.height-pad-badge.height,badge.width,badge.height);
    CGFloat w=b.size.width-2*pad,y=CGRectGetMinY(self.badge.frame)-14;
    CGSize s=[self.status sizeThatFits:CGSizeMake(w,200)];y-=s.height;self.status.frame=CGRectMake(pad,y,w,s.height);
    s=[self.subtitle sizeThatFits:CGSizeMake(w,200)];y-=s.height+6;self.subtitle.frame=CGRectMake(pad,y,w,s.height);
    s=[self.title sizeThatFits:CGSizeMake(w,200)];y-=s.height+2;self.title.frame=CGRectMake(pad,y,w,s.height);
}
-(void)setHighlighted:(BOOL)highlighted {
    [super setHighlighted:highlighted];
    [UIView animateWithDuration:.15 animations:^{self.transform=highlighted?CGAffineTransformMakeScale(.98,.98):CGAffineTransformIdentity;}];
}
@end

@interface TBLauncherController:UIViewController
@property(nonatomic,strong) CAGradientLayer *backdrop;
@property(nonatomic,strong) UIImageView *emblem;
@property(nonatomic,strong) UILabel *wordmark,*tagline,*footer,*note;
@property(nonatomic,strong) TBLauncherCard *starsiege,*tribes2;
@property(nonatomic,strong) UISwitch *skip;
@property(nonatomic,strong) UILabel *skipLabel;
@end
@implementation TBLauncherController
-(void)viewDidLoad {
    [super viewDidLoad];
    self.backdrop=[CAGradientLayer layer];self.backdrop.startPoint=CGPointMake(0,0);self.backdrop.endPoint=CGPointMake(1,1);
    self.backdrop.colors=@[(id)tb_rgb(0x0B1E33,1).CGColor,(id)tb_rgb(0x050C16,1).CGColor,(id)tb_rgb(0x0D3A44,1).CGColor];
    [self.view.layer addSublayer:self.backdrop];
    NSString *art=[[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"] stringByAppendingPathComponent:@"Launcher"];
    self.emblem=[[UIImageView alloc] initWithImage:[UIImage imageWithContentsOfFile:[art stringByAppendingPathComponent:@"emblem.png"]]];
    self.emblem.contentMode=UIViewContentModeScaleAspectFit;[self.view addSubview:self.emblem];
    self.wordmark=[UILabel new];self.wordmark.textAlignment=NSTextAlignmentCenter;
    self.wordmark.attributedText=[[NSAttributedString alloc] initWithString:@"TRIBESPAD" attributes:@{NSKernAttributeName:@9,
        NSFontAttributeName:[UIFont systemFontOfSize:30 weight:UIFontWeightHeavy],NSForegroundColorAttributeName:UIColor.whiteColor}];
    [self.view addSubview:self.wordmark];
    self.tagline=[UILabel new];self.tagline.textAlignment=NSTextAlignmentCenter;self.tagline.text=@"Choose your game";
    self.tagline.font=[UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];self.tagline.textColor=tb_rgb(0x9FDFE2,1);[self.view addSubview:self.tagline];
    self.starsiege=[[TBLauncherCard alloc] initWithTitle:@"Starsiege: Tribes" subtitle:@"1998 · the original, freeware 1.11" art:nil available:NO];
    self.starsiege.status.text=@"Being translated to native ARM64. Tap for the plan.";
    [self.starsiege addTarget:self action:@selector(starsiegeTapped) forControlEvents:UIControlEventTouchUpInside];
    self.tribes2=[[TBLauncherCard alloc] initWithTitle:@"Tribes 2" subtitle:@"2001 · TribesNEXT online"
        art:[UIImage imageWithContentsOfFile:[art stringByAppendingPathComponent:@"tribes2.png"]] available:YES];
    self.tribes2.status.text=@"Checking who is playing…";
    [self.tribes2 addTarget:self action:@selector(play) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.starsiege];[self.view addSubview:self.tribes2];
    self.note=[UILabel new];self.note.numberOfLines=0;self.note.textAlignment=NSTextAlignmentCenter;self.note.alpha=0;
    self.note.font=[UIFont systemFontOfSize:14 weight:UIFontWeightMedium];self.note.textColor=UIColor.whiteColor;[self.view addSubview:self.note];
    self.skip=[UISwitch new];self.skip.onTintColor=tb_rgb(0x2EC4C9,1);
    self.skip.on=NO;
    [self.skip addTarget:self action:@selector(skipChanged) forControlEvents:UIControlEventValueChanged];[self.view addSubview:self.skip];
    self.skipLabel=[UILabel new];self.skipLabel.text=@"Open Tribes 2 directly next time (Settings > Display brings this back)";
    self.skipLabel.font=[UIFont systemFontOfSize:13];self.skipLabel.textColor=[UIColor colorWithWhite:1 alpha:.6];self.skipLabel.numberOfLines=0;
    [self.view addSubview:self.skipLabel];
    self.footer=[UILabel new];self.footer.numberOfLines=0;self.footer.textAlignment=NSTextAlignmentCenter;
    self.footer.font=[UIFont systemFontOfSize:12];self.footer.textColor=[UIColor colorWithWhite:1 alpha:.45];
    self.footer.text=@"Native ARM64 recompilation of the original games. Tribes 2 and Starsiege: Tribes belong to their owners; TribesPad is MIT licensed.";
    [self.view addSubview:self.footer];
    [self loadPlayers];
    // Controllers: A plays Tribes 2.
    for(GCController *c in GCController.controllers)[self watch:c];
    [NSNotificationCenter.defaultCenter addObserverForName:GCControllerDidConnectNotification object:nil queue:NSOperationQueue.mainQueue
        usingBlock:^(NSNotification *n){[self watch:n.object];}];
}
-(void)watch:(GCController *)controller {
    __weak TBLauncherController *weak=self;
    controller.extendedGamepad.buttonA.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;if(pressed)[weak play];};
}
-(NSArray<UIKeyCommand *> *)keyCommands {
    UIKeyCommand *ret=[UIKeyCommand keyCommandWithInput:@"\r" modifierFlags:0 action:@selector(play)];
    UIKeyCommand *two=[UIKeyCommand keyCommandWithInput:@"2" modifierFlags:0 action:@selector(play)];
    return @[ret,two];
}
-(BOOL)canBecomeFirstResponder{return YES;}
-(void)viewDidAppear:(BOOL)animated{[super viewDidAppear:animated];[self becomeFirstResponder];}
-(BOOL)prefersHomeIndicatorAutoHidden{return YES;}
-(BOOL)prefersStatusBarHidden{return YES;}
-(void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];CGRect b=self.view.bounds;UIEdgeInsets safe=self.view.safeAreaInsets;
    self.backdrop.frame=b;
    BOOL phone=b.size.height<500;CGFloat top=fmax(safe.top,phone?10:28),side=fmax(fmax(safe.left,safe.right),phone?20:44);
    CGFloat e=phone?44:84;self.emblem.frame=CGRectMake((b.size.width-e)/2,top,e,e);
    self.wordmark.frame=CGRectMake(0,CGRectGetMaxY(self.emblem.frame)+(phone?2:8),b.size.width,36);
    self.tagline.frame=CGRectMake(0,CGRectGetMaxY(self.wordmark.frame),b.size.width,22);
    CGFloat bottom=b.size.height-fmax(safe.bottom,phone?8:20);
    CGSize f=[self.footer sizeThatFits:CGSizeMake(b.size.width-2*side,100)];
    self.footer.frame=CGRectMake(side,bottom-f.height,b.size.width-2*side,f.height);
    CGFloat sy=CGRectGetMinY(self.footer.frame)-(phone?8:14)-31;
    CGSize sl=[self.skipLabel sizeThatFits:CGSizeMake(b.size.width*.6,60)];
    CGFloat sw=51+10+sl.width,sx=(b.size.width-sw)/2;
    self.skip.frame=CGRectMake(sx,sy,51,31);self.skipLabel.frame=CGRectMake(sx+61,sy+(31-sl.height)/2,sl.width,sl.height);
    CGFloat cardTop=CGRectGetMaxY(self.tagline.frame)+(phone?10:24),cardBottom=sy-(phone?10:22),gap=phone?14:28;
    CGFloat cw=(b.size.width-2*side-gap)/2;
    self.starsiege.frame=CGRectMake(side,cardTop,cw,cardBottom-cardTop);
    self.tribes2.frame=CGRectMake(side+cw+gap,cardTop,cw,cardBottom-cardTop);
    CGSize n=[self.note sizeThatFits:CGSizeMake(cw-40,200)];
    self.note.frame=CGRectMake(CGRectGetMidX(self.starsiege.frame)-n.width/2,CGRectGetMinY(self.starsiege.frame)+24,n.width,n.height);
}
-(void)skipChanged{[NSUserDefaults.standardUserDefaults setBool:!self.skip.on forKey:@"TribesPad.showLauncher"];}
-(void)starsiegeTapped {
    self.note.text=@"Starsiege: Tribes isn't playable yet. The free 1.11 release already translates to native code; the Windows layer is next. It opens here once it plays.";
    [self.view setNeedsLayout];[self.view layoutIfNeeded];
    [UIView animateWithDuration:.25 animations:^{self.note.alpha=1;}];
    [UIView animateWithDuration:.4 delay:6 options:0 animations:^{self.note.alpha=0;} completion:nil];
    UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Starsiege: Tribes" message:@"In development. Open the plan on GitHub?" preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Not Now" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:@"Open Plan" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){(void)x;
        [UIApplication.sharedApplication openURL:[NSURL URLWithString:TBLaunchStarsiegePlan] options:@{} completionHandler:nil];}]];
    a.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[self presentViewController:a animated:YES completion:nil];
}
-(void)play {
    if(tb_launch_chosen)return;
    [UIView animateWithDuration:.25 animations:^{self.view.alpha=0;} completion:^(BOOL f){(void)f;tb_launch_chosen=YES;}];
}
/* People playing Tribes 2 now (players minus bots, TribesNEXT list.json). */
-(void)loadPlayers {
    NSMutableURLRequest *r=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"https://www.tribesnext.com/list.json"]];
    [r setValue:@"TribesPad" forHTTPHeaderField:@"User-Agent"];r.timeoutInterval=8;
    __weak TBLauncherController *weak=self;
    [[NSURLSession.sharedSession dataTaskWithRequest:r completionHandler:^(NSData *data,NSURLResponse *resp,NSError *error){
        (void)resp;(void)error;NSArray *list=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
        NSInteger people=-1,servers=0;
        if([list isKindOfClass:NSArray.class]){people=0;for(NSDictionary *s in list){if(![s isKindOfClass:NSDictionary.class])continue;
            NSInteger p=[s[@"num_players"] integerValue]-[s[@"num_bots"] integerValue];if(p>0){people+=p;servers++;}}}
        dispatch_async(dispatch_get_main_queue(),^{
            TBLauncherController *me=weak;if(!me)return;
            me.tribes2.status.text=people<0?@"Online play with TribesNEXT":people==0?@"Nobody online right now · game nights are in ··· > Find a Game":
                [NSString stringWithFormat:@"%ld %@ playing now on %ld %@",(long)people,people==1?@"person":@"people",(long)servers,servers==1?@"server":@"servers"];
            [me.tribes2 setNeedsLayout];});
    }] resume];
}
@end

void tb_launcher_run(void) {
    id show=[NSUserDefaults.standardUserDefaults objectForKey:@"TribesPad.showLauncher"];
    if(show&&![show boolValue])return;
    // The window scene connects while the run loop turns.
    UIWindowScene *scene=nil;NSDate *give_up=[NSDate dateWithTimeIntervalSinceNow:5];
    while(!scene&&give_up.timeIntervalSinceNow>0){
        for(UIScene *s in UIApplication.sharedApplication.connectedScenes)if([s isKindOfClass:UIWindowScene.class])scene=(UIWindowScene *)s;
        if(!scene)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.05,true);
    }
    if(!scene){fprintf(stderr,"TribesPad launcher: no window scene; starting Tribes 2\n");return;}
    UIWindow *window=[[UIWindow alloc] initWithWindowScene:scene];
    window.windowLevel=UIWindowLevelNormal+1;window.backgroundColor=UIColor.blackColor;
    window.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;
    window.rootViewController=[TBLauncherController new];[window makeKeyAndVisible];
    fprintf(stderr,"TribesPad launcher shown\n");
    while(!tb_launch_chosen)@autoreleasepool{CFRunLoopRunInMode(kCFRunLoopDefaultMode,.05,true);}
    fprintf(stderr,"TribesPad launcher: starting Tribes 2\n");
    window.hidden=YES;window.rootViewController=nil;
}

