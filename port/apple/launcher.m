/* TribesPad's game picker. A loaded engine can't be unloaded, so the picker
   runs natively before either game starts (docs/LAUNCHER-PLAN.md) and waits
   on the main run loop until a game is chosen. Tribes 2 (TribesNEXT) starts;
   Starsiege: Tribes is shown, marked in development, until its translation
   plays (docs/STARSIEGE-TRIBES-PLAN.md). Each card shows its game's own title
   screen, taken at build time from the player's own downloads
   (scripts/launcher_art.py) into GameData/Launcher; nothing from the games
   is in the repository. */
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#include "launcher.h"
#include <stdlib.h>
#include <string.h>
#include "starsiege_paths.h"
#include "t1_live.h"

static NSString * const TBLaunchStarsiegePlan=@"https://github.com/chrissotraidis/tribespad/blob/main/docs/STARSIEGE-TRIBES-PLAN.md";
static NSString * const TBLaunchRepository=@"https://github.com/chrissotraidis/tribespad";
static BOOL tb_launch_chosen;
static TBGame tb_launch_game=TBGameTribes2;
static BOOL tb_starsiege_here(void){return tb_starsiege_bundled([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"].fileSystemRepresentation);}

static UIColor *tb_rgb(unsigned hex,CGFloat a){return [UIColor colorWithRed:((hex>>16)&255)/255.0 green:((hex>>8)&255)/255.0 blue:(hex&255)/255.0 alpha:a];}
#define TB_CYAN 0x2EC4C9
#define TB_AMBER 0xE8A23A

/* A frame with two cut corners (top left, bottom right), like a HUD panel. */
static UIBezierPath *tb_chamfer(CGRect r,CGFloat c){
    UIBezierPath *p=[UIBezierPath bezierPath];
    [p moveToPoint:CGPointMake(CGRectGetMinX(r)+c,CGRectGetMinY(r))];[p addLineToPoint:CGPointMake(CGRectGetMaxX(r),CGRectGetMinY(r))];
    [p addLineToPoint:CGPointMake(CGRectGetMaxX(r),CGRectGetMaxY(r)-c)];[p addLineToPoint:CGPointMake(CGRectGetMaxX(r)-c,CGRectGetMaxY(r))];
    [p addLineToPoint:CGPointMake(CGRectGetMinX(r),CGRectGetMaxY(r))];[p addLineToPoint:CGPointMake(CGRectGetMinX(r),CGRectGetMinY(r)+c)];
    [p closePath];return p;
}

@interface TBLauncherCard:UIControl
@property(nonatomic,strong) UIImageView *art,*backdrop;
@property(nonatomic,strong) UILabel *kicker,*status,*badge;
@property(nonatomic,strong) CAGradientLayer *shade;
@property(nonatomic,strong) CAShapeLayer *mask,*frameLine;
@property(nonatomic) BOOL available,selectedCard;
@end
@implementation TBLauncherCard
-(instancetype)initWithKicker:(NSString *)kicker art:(UIImage *)art fallback:(UIImage *)fallback available:(BOOL)available label:(NSString *)label {
    if(!(self=[super initWithFrame:CGRectZero]))return nil;
    self.available=available;self.backgroundColor=tb_rgb(0x050B14,1);
    /* The title screen shows whole across the card; a dimmed, enlarged copy
       fills the rest, so the 4:3 art suits a tall card without cropping. */
    self.backdrop=[[UIImageView alloc] initWithImage:art];self.backdrop.contentMode=UIViewContentModeScaleAspectFill;
    self.backdrop.alpha=.32;self.backdrop.userInteractionEnabled=NO;[self addSubview:self.backdrop];
    // Only the lower part of the title screen, so its logo doesn't repeat.
    self.backdrop.layer.contentsRect=CGRectMake(.1,.45,.8,.55);
    self.art=[[UIImageView alloc] initWithImage:art?:fallback];self.art.userInteractionEnabled=NO;
    self.art.contentMode=UIViewContentModeScaleAspectFit;
    self.art.alpha=art?1:.25;[self addSubview:self.art];
    if(art){CAGradientLayer *fade=[CAGradientLayer layer];
        fade.colors=@[(id)UIColor.clearColor.CGColor,(id)UIColor.blackColor.CGColor,(id)UIColor.blackColor.CGColor,(id)UIColor.clearColor.CGColor];
        fade.locations=@[@0,@.12,@.72,@1];self.art.layer.mask=fade;}
    self.shade=[CAGradientLayer layer];
    self.shade.colors=@[(id)[UIColor colorWithWhite:0 alpha:0].CGColor,(id)[UIColor colorWithWhite:0 alpha:0].CGColor,(id)tb_rgb(0x02060C,.94).CGColor];
    self.shade.locations=@[@0,@.55,@1];[self.layer addSublayer:self.shade];
    UILabel *(^label_)(UIFont *,UIColor *)=^UILabel *(UIFont *font,UIColor *color){
        UILabel *l=[UILabel new];l.font=font;l.textColor=color;l.numberOfLines=0;l.userInteractionEnabled=NO;[self addSubview:l];return l;};
    self.kicker=label_([UIFont systemFontOfSize:13 weight:UIFontWeightBold],tb_rgb(0xBFEFF1,1));
    self.kicker.attributedText=[[NSAttributedString alloc] initWithString:kicker.uppercaseString attributes:@{NSKernAttributeName:@2}];
    self.status=label_([UIFont monospacedDigitSystemFontOfSize:15 weight:UIFontWeightMedium],[UIColor colorWithWhite:1 alpha:.9]);
    self.badge=label_([UIFont systemFontOfSize:14 weight:UIFontWeightBlack],tb_rgb(0x02060C,1));
    self.badge.textAlignment=NSTextAlignmentCenter;self.badge.backgroundColor=tb_rgb(available?TB_CYAN:TB_AMBER,1);
    self.badge.attributedText=[[NSAttributedString alloc] initWithString:!available?@"IN DEVELOPMENT":@"PLAY  \u25B8" attributes:@{NSKernAttributeName:@2}];
    CAShapeLayer *badgeMask=[CAShapeLayer layer];self.badge.layer.mask=badgeMask;
    self.mask=[CAShapeLayer layer];self.layer.mask=self.mask;
    self.frameLine=[CAShapeLayer layer];self.frameLine.fillColor=UIColor.clearColor.CGColor;self.frameLine.lineWidth=3;
    [self.layer addSublayer:self.frameLine];
    self.isAccessibilityElement=YES;self.accessibilityTraits=UIAccessibilityTraitButton;self.accessibilityLabel=label;
    [self setSelectedCard:NO];
    return self;
}
-(void)setSelectedCard:(BOOL)focused {
    _selectedCard=focused;
    self.frameLine.strokeColor=(focused?tb_rgb(self.available?TB_CYAN:TB_AMBER,1):[UIColor colorWithWhite:1 alpha:.18]).CGColor;
    self.frameLine.shadowColor=tb_rgb(self.available?TB_CYAN:TB_AMBER,1).CGColor;
    self.frameLine.shadowRadius=10;self.frameLine.shadowOpacity=focused?.9:0;self.frameLine.shadowOffset=CGSizeZero;
}
-(void)drift {
    if(UIAccessibilityIsReduceMotionEnabled()||[self.backdrop.layer animationForKey:@"drift"])return;
    CABasicAnimation *a=[CABasicAnimation animationWithKeyPath:@"transform.scale"];
    a.fromValue=@1.0;a.toValue=@1.08;a.duration=16;a.autoreverses=YES;a.repeatCount=HUGE_VALF;
    a.timingFunction=[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
    [self.backdrop.layer addAnimation:a forKey:@"drift"];
    CABasicAnimation *b=[CABasicAnimation animationWithKeyPath:@"transform.scale"];
    b.fromValue=@1.0;b.toValue=@1.025;b.duration=11;b.autoreverses=YES;b.repeatCount=HUGE_VALF;
    b.timingFunction=a.timingFunction;[self.art.layer addAnimation:b forKey:@"drift"];
}
-(void)layoutSubviews {
    [super layoutSubviews];CGRect b=self.bounds;CGFloat c=fmin(36,b.size.width*.08),pad=fmax(18,b.size.width*.055);
    self.backdrop.frame=b;
    CGSize image=self.art.image.size;
    if(self.backdrop.image&&image.width>0){
        CGFloat h=fmin(b.size.width*image.height/image.width,b.size.height*.7);
        self.art.frame=CGRectMake(0,fmax(0,b.size.height*.42-h/2),b.size.width,h);
        self.art.layer.mask.frame=self.art.bounds;
    } else self.art.frame=CGRectInset(b,b.size.width*.2,b.size.height*.2);
    self.shade.frame=b;self.mask.path=tb_chamfer(b,c).CGPath;self.frameLine.path=tb_chamfer(CGRectInset(b,1.5,1.5),c-1).CGPath;
    CGSize badge=[self.badge sizeThatFits:CGSizeMake(320,40)];badge.width+=40;badge.height+=16;
    self.badge.frame=CGRectMake(pad,b.size.height-pad-badge.height,badge.width,badge.height);
    ((CAShapeLayer *)self.badge.layer.mask).path=tb_chamfer(self.badge.bounds,10).CGPath;
    CGFloat w=b.size.width-2*pad-c,y=CGRectGetMinY(self.badge.frame)-12;
    CGSize s=[self.status sizeThatFits:CGSizeMake(w,200)];y-=s.height;self.status.frame=CGRectMake(pad,y,w,s.height);
    s=[self.kicker sizeThatFits:CGSizeMake(w,200)];y-=s.height+6;self.kicker.frame=CGRectMake(pad,y,w,s.height);
    [self drift];
}
-(void)setHighlighted:(BOOL)highlighted {
    [super setHighlighted:highlighted];
    [UIView animateWithDuration:.15 animations:^{self.transform=highlighted?CGAffineTransformMakeScale(.98,.98):CGAffineTransformIdentity;}];
}
@end

@interface TBLauncherController:UIViewController
@property(nonatomic,strong) CAGradientLayer *backdrop;
@property(nonatomic,strong) CALayer *stars;
@property(nonatomic,strong) NSArray<CAShapeLayer *> *brackets;
@property(nonatomic,strong) UIImageView *emblem;
@property(nonatomic,strong) UILabel *wordmark,*tagline,*footer;
@property(nonatomic,strong) UIButton *settings;
@property(nonatomic,strong) NSArray<TBLauncherCard *> *cards;
@property(nonatomic) NSUInteger focus;
@end
@implementation TBLauncherController
-(void)viewDidLoad {
    [super viewDidLoad];
    self.backdrop=[CAGradientLayer layer];self.backdrop.startPoint=CGPointMake(.5,0);self.backdrop.endPoint=CGPointMake(.5,1);
    self.backdrop.colors=@[(id)tb_rgb(0x0A1A2C,1).CGColor,(id)tb_rgb(0x03070D,1).CGColor,(id)tb_rgb(0x061A20,1).CGColor];
    [self.view.layer addSublayer:self.backdrop];
    self.stars=[CALayer layer];[self.view.layer addSublayer:self.stars];
    NSMutableArray *brackets=[NSMutableArray array];
    for(int i=0;i<4;i++){CAShapeLayer *l=[CAShapeLayer layer];l.strokeColor=tb_rgb(TB_CYAN,.55).CGColor;l.fillColor=UIColor.clearColor.CGColor;l.lineWidth=2;
        [self.view.layer addSublayer:l];[brackets addObject:l];}
    self.brackets=brackets;
    NSString *art=[[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"] stringByAppendingPathComponent:@"Launcher"];
    UIImage *emblem=[UIImage imageWithContentsOfFile:[art stringByAppendingPathComponent:@"emblem.png"]];
    self.emblem=[[UIImageView alloc] initWithImage:emblem];self.emblem.contentMode=UIViewContentModeScaleAspectFit;[self.view addSubview:self.emblem];
    self.wordmark=[UILabel new];
    self.wordmark.attributedText=[[NSAttributedString alloc] initWithString:@"TRIBESPAD" attributes:@{NSKernAttributeName:@8,
        NSFontAttributeName:[UIFont systemFontOfSize:26 weight:UIFontWeightHeavy],NSForegroundColorAttributeName:UIColor.whiteColor}];
    [self.view addSubview:self.wordmark];
    self.tagline=[UILabel new];
    self.tagline.attributedText=[[NSAttributedString alloc] initWithString:@"SELECT YOUR GAME" attributes:@{NSKernAttributeName:@4,
        NSFontAttributeName:[UIFont systemFontOfSize:12 weight:UIFontWeightBold],NSForegroundColorAttributeName:tb_rgb(TB_CYAN,1)}];
    [self.view addSubview:self.tagline];
    self.settings=[UIButton buttonWithType:UIButtonTypeSystem];
    [self.settings setImage:[UIImage systemImageNamed:@"gearshape.fill" withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:20 weight:UIImageSymbolWeightSemibold]] forState:UIControlStateNormal];
    self.settings.tintColor=[UIColor colorWithWhite:1 alpha:.85];self.settings.accessibilityLabel=@"Settings";
    self.settings.showsMenuAsPrimaryAction=YES;self.settings.pointerInteractionEnabled=YES;[self rebuildSettingsMenu];
    [self.view addSubview:self.settings];
    TBLauncherCard *starsiege=[[TBLauncherCard alloc] initWithKicker:@"Starsiege: Tribes  \u00B7  1998"
        art:[UIImage imageWithContentsOfFile:[art stringByAppendingPathComponent:@"starsiege.png"]] fallback:emblem available:tb_starsiege_here()
        label:tb_starsiege_here()?@"Starsiege: Tribes, play":@"Starsiege: Tribes, in development"];
    starsiege.status.text=tb_starsiege_here()?@"Version 1.41, the one the community plays. Bot matches offline, community servers online.":@"The original. Being translated to native code; it opens here once it plays.";
    TBLauncherCard *tribes2=[[TBLauncherCard alloc] initWithKicker:@"Tribes 2  \u00B7  2001  \u00B7  TribesNEXT online"
        art:[UIImage imageWithContentsOfFile:[art stringByAppendingPathComponent:@"tribes2.png"]] fallback:emblem available:YES
        label:@"Tribes 2, play"];
    tribes2.status.text=@"Checking who is playing\u2026";
    [starsiege addTarget:self action:@selector(starsiegeTapped) forControlEvents:UIControlEventTouchUpInside];
    [tribes2 addTarget:self action:@selector(play) forControlEvents:UIControlEventTouchUpInside];
    self.cards=@[starsiege,tribes2];for(UIView *c in self.cards)[self.view addSubview:c];
    self.focus=1;[self showFocus];
    self.footer=[UILabel new];self.footer.numberOfLines=0;self.footer.textAlignment=NSTextAlignmentCenter;
    self.footer.font=[UIFont systemFontOfSize:11 weight:UIFontWeightMedium];self.footer.textColor=[UIColor colorWithWhite:1 alpha:.4];
    self.footer.text=@"Native ARM64 recompilations of the original games, built from your own copies. Starsiege: Tribes and Tribes 2 belong to their owners; TribesPad is MIT licensed.";
    [self.view addSubview:self.footer];
    [self loadPlayers];
    for(GCController *c in GCController.controllers)[self watch:c];
    [NSNotificationCenter.defaultCenter addObserverForName:GCControllerDidConnectNotification object:nil queue:NSOperationQueue.mainQueue
        usingBlock:^(NSNotification *n){[self watch:n.object];}];
}
-(void)rebuildSettingsMenu {
    __weak TBLauncherController *weak=self;
    id show=[NSUserDefaults.standardUserDefaults objectForKey:@"TribesPad.showLauncher"];
    BOOL direct_on=show&&![show boolValue];
    UIAction *direct=[UIAction actionWithTitle:@"Open Tribes 2 Directly" image:[UIImage systemImageNamed:@"forward.end"] identifier:nil
        handler:^(__kindof UIAction *a){(void)a;
            [NSUserDefaults.standardUserDefaults setBool:direct_on forKey:@"TribesPad.showLauncher"];[weak rebuildSettingsMenu];}];
    direct.subtitle=@"Skip this screen; Settings > Display brings it back";
    direct.state=direct_on?UIMenuElementStateOn:UIMenuElementStateOff;
    UIAction *about=[UIAction actionWithTitle:@"TribesPad on GitHub" image:[UIImage systemImageNamed:@"safari"] identifier:nil
        handler:^(__kindof UIAction *a){(void)a;[UIApplication.sharedApplication openURL:[NSURL URLWithString:TBLaunchRepository] options:@{} completionHandler:nil];}];
    self.settings.menu=[UIMenu menuWithTitle:@"TribesPad" children:@[direct,about]];
}
-(void)watch:(GCController *)controller {
    __weak TBLauncherController *weak=self;GCExtendedGamepad *pad=controller.extendedGamepad;
    pad.buttonA.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;if(pressed)[weak activate];};
    pad.dpad.left.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;if(pressed)[weak moveFocus:-1];};
    pad.dpad.right.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;if(pressed)[weak moveFocus:1];};
}
-(NSArray<UIKeyCommand *> *)keyCommands {
    return @[[UIKeyCommand keyCommandWithInput:@"\r" modifierFlags:0 action:@selector(activate)],
        [UIKeyCommand keyCommandWithInput:UIKeyInputLeftArrow modifierFlags:0 action:@selector(focusLeft)],
        [UIKeyCommand keyCommandWithInput:UIKeyInputRightArrow modifierFlags:0 action:@selector(focusRight)],
        [UIKeyCommand keyCommandWithInput:@"1" modifierFlags:0 action:@selector(starsiegeTapped)],
        [UIKeyCommand keyCommandWithInput:@"2" modifierFlags:0 action:@selector(play)]];
}
-(void)focusLeft{[self moveFocus:-1];}
-(void)focusRight{[self moveFocus:1];}
-(void)moveFocus:(int)step{self.focus=(NSUInteger)MAX(0,MIN((int)self.cards.count-1,(int)self.focus+step));[self showFocus];}
-(void)showFocus{for(NSUInteger i=0;i<self.cards.count;i++)self.cards[i].selectedCard=i==self.focus;}
-(void)activate{if(self.focus==0)[self starsiegeTapped];else [self play];}
-(BOOL)canBecomeFirstResponder{return YES;}
-(void)viewDidAppear:(BOOL)animated{[super viewDidAppear:animated];[self becomeFirstResponder];}
-(BOOL)prefersHomeIndicatorAutoHidden{return YES;}
-(BOOL)prefersStatusBarHidden{return YES;}
-(void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];CGRect b=self.view.bounds;UIEdgeInsets safe=self.view.safeAreaInsets;
    self.backdrop.frame=b;[self layoutStars:b];
    BOOL phone=b.size.height<500;CGFloat m=phone?14:26;
    CGFloat top=fmax(safe.top,m),left=fmax(safe.left,m),right=fmax(safe.right,m),bottom=fmax(safe.bottom,m);
    CGFloat arm=phone?22:38;
    CGPoint corners[4]={{left-8,top-8},{b.size.width-right+8,top-8},{left-8,b.size.height-bottom+8},{b.size.width-right+8,b.size.height-bottom+8}};
    for(int i=0;i<4;i++){CGFloat sx=i%2?-1:1,sy=i>1?-1:1;UIBezierPath *p=[UIBezierPath bezierPath];
        [p moveToPoint:CGPointMake(corners[i].x,corners[i].y+sy*arm)];[p addLineToPoint:corners[i]];[p addLineToPoint:CGPointMake(corners[i].x+sx*arm,corners[i].y)];
        self.brackets[i].path=p.CGPath;}
    CGFloat e=phone?34:52;self.emblem.frame=CGRectMake(left+8,top+4,e,e);
    CGSize w=[self.wordmark sizeThatFits:CGSizeMake(400,60)];
    self.wordmark.frame=CGRectMake(CGRectGetMaxX(self.emblem.frame)+12,CGRectGetMidY(self.emblem.frame)-w.height+4,w.width,w.height);
    CGSize t=[self.tagline sizeThatFits:CGSizeMake(400,30)];
    self.tagline.frame=CGRectMake(CGRectGetMinX(self.wordmark.frame)+2,CGRectGetMaxY(self.wordmark.frame)+2,t.width,t.height);
    self.settings.frame=CGRectMake(b.size.width-right-8-44,CGRectGetMidY(self.emblem.frame)-22,44,44);
    CGSize f=[self.footer sizeThatFits:CGSizeMake(b.size.width-left-right-80,80)];
    self.footer.frame=CGRectMake((b.size.width-f.width)/2,b.size.height-bottom-f.height,f.width,f.height);
    CGFloat cardTop=CGRectGetMaxY(self.emblem.frame)+(phone?12:28),cardBottom=CGRectGetMinY(self.footer.frame)-(phone?10:22),gap=phone?14:30;
    CGFloat cw=(b.size.width-left-right-16-gap)/2;
    self.cards[0].frame=CGRectMake(left+8,cardTop,cw,cardBottom-cardTop);
    self.cards[1].frame=CGRectMake(left+8+cw+gap,cardTop,cw,cardBottom-cardTop);
}
/* A faint, fixed starfield behind everything. */
-(void)layoutStars:(CGRect)b {
    if(CGSizeEqualToSize(self.stars.frame.size,b.size))return;
    self.stars.frame=b;self.stars.sublayers=nil;srand48(1998);
    for(int i=0;i<140;i++){CALayer *s=[CALayer layer];CGFloat r=drand48()<.85?1:2;
        s.frame=CGRectMake(drand48()*b.size.width,drand48()*b.size.height,r,r);s.cornerRadius=r/2;
        s.backgroundColor=[UIColor colorWithWhite:1 alpha:.15+drand48()*.45].CGColor;[self.stars addSublayer:s];}
}
-(void)starsiegeTapped {
    if(self.presentedViewController)return;
    self.focus=0;[self showFocus];
    if(tb_starsiege_here()){[self launch:TBGameStarsiege];return;}
    UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Starsiege: Tribes"
        message:@"Not playable in this build yet. Its 1.41 translation reaches the main menu on the Mac; matches are next. It opens from here once it plays."
        preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:@"Read the Plan" style:UIAlertActionStyleDefault handler:^(UIAlertAction *x){(void)x;
        [UIApplication.sharedApplication openURL:[NSURL URLWithString:TBLaunchStarsiegePlan] options:@{} completionHandler:nil];}]];
    a.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[self presentViewController:a animated:YES completion:nil];
}
-(void)play {
    self.focus=1;[self showFocus];[self launch:TBGameTribes2];
}
-(void)launch:(TBGame)game {
    if(tb_launch_chosen||self.presentedViewController)return;
    TBLauncherCard *card=self.cards[game==TBGameStarsiege?0:1];
    [UIView animateWithDuration:.3 animations:^{self.view.alpha=0;card.transform=CGAffineTransformMakeScale(1.04,1.04);}
        completion:^(BOOL f){(void)f;tb_launch_game=game;tb_launch_chosen=YES;}];
}
/* People playing Starsiege: Tribes now, from the community masters (t1_live.c). */
-(void)loadStarsiegePlayers {
    __weak TBLauncherController *weak=self;NSString *base=self.cards[0].status.text;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        TBT1Live live;int ok=tb_t1_live(&live,4000);
        dispatch_async(dispatch_get_main_queue(),^{
            TBLauncherController *me=weak;if(!me||!ok)return;TBLauncherCard *card=me.cards[0];
            NSString *now=live.people?[NSString stringWithFormat:@"%d %@ playing now on %d %@",live.people,live.people==1?@"person":@"people",live.busy_servers,live.busy_servers==1?@"server":@"servers"]:
                [NSString stringWithFormat:@"Nobody on the %d Tribes 1 servers right now",live.servers_answered];
            card.status.text=[NSString stringWithFormat:@"%@\n%@",now,base];[card setNeedsLayout];});
    });
}
/* People playing Tribes 2 now (players minus bots, TribesNEXT list.json). */
-(void)loadPlayers {
    [self loadStarsiegePlayers];
    NSMutableURLRequest *r=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"https://www.tribesnext.com/list.json"]];
    [r setValue:@"TribesPad" forHTTPHeaderField:@"User-Agent"];r.timeoutInterval=8;
    __weak TBLauncherController *weak=self;
    [[NSURLSession.sharedSession dataTaskWithRequest:r completionHandler:^(NSData *data,NSURLResponse *resp,NSError *error){
        (void)resp;(void)error;NSArray *list=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
        NSInteger people=-1,servers=0;
        if([list isKindOfClass:NSArray.class]){people=0;for(NSDictionary *s in list){if(![s isKindOfClass:NSDictionary.class])continue;
            NSInteger p=[s[@"num_players"] integerValue]-[s[@"num_bots"] integerValue];if(p>0){people+=p;servers++;}}}
        dispatch_async(dispatch_get_main_queue(),^{
            TBLauncherController *me=weak;if(!me)return;TBLauncherCard *card=me.cards[1];
            card.status.text=people<0?@"Online play with TribesNEXT":people==0?@"Nobody online right now \u00B7 see Game Nights in the \u00B7\u00B7\u00B7 menu":
                [NSString stringWithFormat:@"%ld %@ playing now on %ld %@",(long)people,people==1?@"person":@"people",(long)servers,servers==1?@"server":@"servers"];
            [card setNeedsLayout];});
    }] resume];
}
@end

TBGame tb_launcher_run(void) {
    /* TRIBESPAD_PICK (devicectl --environment-variables, tests) chooses without the picker. */
    const char *pick=getenv("TRIBESPAD_PICK");
    if(pick)return !strcmp(pick,"starsiege")&&tb_starsiege_here()?TBGameStarsiege:TBGameTribes2;
    /* Switch Game (mobile_controls.m) chose the next game: start it once, without the picker. */
    NSString *next=[NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.nextGame"];
    if(next){
        [NSUserDefaults.standardUserDefaults removeObjectForKey:@"TribesPad.nextGame"];
        TBGame chosen=[next isEqualToString:@"starsiege"]&&tb_starsiege_here()?TBGameStarsiege:TBGameTribes2;
        fprintf(stderr,"TribesPad launcher: Switch Game chose %s\n",chosen==TBGameStarsiege?"Starsiege: Tribes":"Tribes 2");
        return chosen;
    }
    id show=[NSUserDefaults.standardUserDefaults objectForKey:@"TribesPad.showLauncher"];
    if(show&&![show boolValue])return TBGameTribes2;
    // The window scene connects while the run loop turns.
    UIWindowScene *scene=nil;NSDate *give_up=[NSDate dateWithTimeIntervalSinceNow:5];
    while(!scene&&give_up.timeIntervalSinceNow>0){
        for(UIScene *s in UIApplication.sharedApplication.connectedScenes)if([s isKindOfClass:UIWindowScene.class])scene=(UIWindowScene *)s;
        if(!scene)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.05,true);
    }
    if(!scene){fprintf(stderr,"TribesPad launcher: no window scene; starting Tribes 2\n");return TBGameTribes2;}
    UIWindow *window=[[UIWindow alloc] initWithWindowScene:scene];
    window.windowLevel=UIWindowLevelNormal+1;window.backgroundColor=UIColor.blackColor;
    window.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;
    window.rootViewController=[TBLauncherController new];[window makeKeyAndVisible];
    fprintf(stderr,"TribesPad launcher shown\n");
    while(!tb_launch_chosen)@autoreleasepool{CFRunLoopRunInMode(kCFRunLoopDefaultMode,.05,true);}
    fprintf(stderr,"TribesPad launcher: starting %s\n",tb_launch_game==TBGameStarsiege?"Starsiege: Tribes":"Tribes 2");
    window.hidden=YES;window.rootViewController=nil;
    return tb_launch_game;
}

