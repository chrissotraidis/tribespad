/* TribesPad's game picker on the Mac (docs/LAUNCHER-PLAN.md), the AppKit twin
 * of launcher.m. It is the app's front executable: it shows the picker, then
 * replaces itself (execv) with the chosen engine, Tribes 2's TribesPadEngine or
 * Starsiege's StarsiegeEngine, both in Contents/MacOS. Each engine therefore
 * starts in a fresh process exactly as it would on its own; a loaded engine
 * can't be unloaded, and the two can't share one process.
 * Starsiege is offered when the app was built with it
 * (build-tribespad.sh --games tribes2,starsiege); otherwise its card stays
 * "In development". "Open Tribes 2 Directly" skips the picker.
 * TRIBESPAD_PICKER_SNAPSHOT=file.png renders the picker offscreen to a PNG and
 * exits (TRIBESPAD_PICKER_SIZE=WxH), so it can be checked without a window.
 * TRIBESPAD_PICK=tribes2|starsiege chooses without showing it. */
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <GameController/GameController.h>
#include <stdio.h>
#include <unistd.h>
#include "starsiege_paths.h"
#include "t1_live.h"

static NSString * const TBRepository=@"https://github.com/chrissotraidis/tribespad";
static NSString * const TBStarsiegePlan=@"https://github.com/chrissotraidis/tribespad/blob/main/docs/STARSIEGE-TRIBES-PLAN.md";
#define TB_CYAN 0x2EC4C9
#define TB_AMBER 0xE8A23A
static CGColorRef tb_rgb(unsigned hex,CGFloat a){return [NSColor colorWithSRGBRed:((hex>>16)&255)/255.0 green:((hex>>8)&255)/255.0 blue:(hex&255)/255.0 alpha:a].CGColor;}
static CGColorRef tb_white(CGFloat a){return [NSColor colorWithWhite:1 alpha:a].CGColor;}
/* A frame with two cut corners (top left, bottom right), like a HUD panel. The
   layer tree is flipped, so y grows downwards as on iOS. */
static CGPathRef tb_chamfer(CGRect r,CGFloat c){
    CGMutablePathRef p=CGPathCreateMutable();
    CGPathMoveToPoint(p,NULL,CGRectGetMinX(r)+c,CGRectGetMinY(r));CGPathAddLineToPoint(p,NULL,CGRectGetMaxX(r),CGRectGetMinY(r));
    CGPathAddLineToPoint(p,NULL,CGRectGetMaxX(r),CGRectGetMaxY(r)-c);CGPathAddLineToPoint(p,NULL,CGRectGetMaxX(r)-c,CGRectGetMaxY(r));
    CGPathAddLineToPoint(p,NULL,CGRectGetMinX(r),CGRectGetMaxY(r));CGPathAddLineToPoint(p,NULL,CGRectGetMinX(r),CGRectGetMinY(r)+c);
    CGPathCloseSubpath(p);return (CGPathRef)CFAutorelease(p);
}
static NSAttributedString *tb_text(NSString *s,CGFloat size,NSFontWeight weight,CGColorRef color,CGFloat kern,BOOL mono){
    NSFont *font=mono?[NSFont monospacedDigitSystemFontOfSize:size weight:weight]:[NSFont systemFontOfSize:size weight:weight];
    return [[NSAttributedString alloc] initWithString:s attributes:@{NSFontAttributeName:font,NSKernAttributeName:@(kern),
        NSForegroundColorAttributeName:[NSColor colorWithCGColor:color]}];
}
static CATextLayer *tb_label(void){CATextLayer *l=[CATextLayer layer];l.wrapped=YES;l.contentsScale=2;return l;}
static CGSize tb_fit(NSAttributedString *s,CGFloat width){
    NSRect r=[s boundingRectWithSize:NSMakeSize(width,1000) options:NSStringDrawingUsesLineFragmentOrigin];
    return CGSizeMake(ceil(r.size.width)+2,ceil(r.size.height)+1);
}

/* Paths inside the app bundle and the player's storage. */
static NSString *tb_macos(NSString *name){return [[NSBundle.mainBundle.executablePath stringByDeletingLastPathComponent] stringByAppendingPathComponent:name];}
static NSString *tb_data(NSString *name){return [[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"] stringByAppendingPathComponent:name];}
static BOOL tb_starsiege_ready(void){
    return [NSFileManager.defaultManager isExecutableFileAtPath:tb_macos(@"StarsiegeEngine")]&&tb_starsiege_bundled(tb_data(@"").fileSystemRepresentation);
}

@interface TBCard:NSObject
@property(nonatomic,strong) CALayer *root,*art,*backdrop,*badge;
@property(nonatomic,strong) CAGradientLayer *shade,*fade;
@property(nonatomic,strong) CAShapeLayer *mask,*frameLine,*badgeMask;
@property(nonatomic,strong) CATextLayer *kicker,*status,*badgeText;
@property(nonatomic) BOOL available,focused;
@property(nonatomic,copy) NSString *statusText;
@end
@implementation TBCard
-(instancetype)initWithKicker:(NSString *)kicker art:(NSImage *)art fallback:(NSImage *)fallback available:(BOOL)available badge:(NSString *)badge {
    if(!(self=[super init]))return nil;
    self.available=available;self.root=[CALayer layer];self.root.backgroundColor=tb_rgb(0x050B14,1);
    self.backdrop=[CALayer layer];self.backdrop.contents=art;self.backdrop.contentsGravity=kCAGravityResizeAspectFill;
    self.backdrop.opacity=.32;self.backdrop.contentsRect=CGRectMake(.1,.45,.8,.55);[self.root addSublayer:self.backdrop];
    self.art=[CALayer layer];self.art.contents=art?:fallback;self.art.contentsGravity=kCAGravityResizeAspect;self.art.opacity=art?1:.25;
    if(art){self.fade=[CAGradientLayer layer];
        self.fade.colors=@[(id)NSColor.clearColor.CGColor,(id)NSColor.blackColor.CGColor,(id)NSColor.blackColor.CGColor,(id)NSColor.clearColor.CGColor];
        self.fade.locations=@[@0,@.12,@.72,@1];self.art.mask=self.fade;}
    [self.root addSublayer:self.art];
    self.shade=[CAGradientLayer layer];
    self.shade.colors=@[(id)[NSColor colorWithWhite:0 alpha:0].CGColor,(id)[NSColor colorWithWhite:0 alpha:0].CGColor,(__bridge id)tb_rgb(0x02060C,.94)];
    self.shade.locations=@[@0,@.55,@1];[self.root addSublayer:self.shade];
    self.kicker=tb_label();self.kicker.string=tb_text(kicker.uppercaseString,13,NSFontWeightBold,tb_rgb(0xBFEFF1,1),2,NO);[self.root addSublayer:self.kicker];
    self.status=tb_label();[self.root addSublayer:self.status];
    self.badge=[CALayer layer];self.badge.backgroundColor=tb_rgb(available?TB_CYAN:TB_AMBER,1);
    self.badgeMask=[CAShapeLayer layer];self.badge.mask=self.badgeMask;
    self.badgeText=tb_label();self.badgeText.alignmentMode=kCAAlignmentCenter;
    self.badgeText.string=tb_text(badge,14,NSFontWeightBlack,tb_rgb(0x02060C,1),2,NO);[self.badge addSublayer:self.badgeText];[self.root addSublayer:self.badge];
    self.mask=[CAShapeLayer layer];self.root.mask=self.mask;
    self.frameLine=[CAShapeLayer layer];self.frameLine.fillColor=NSColor.clearColor.CGColor;self.frameLine.lineWidth=3;[self.root addSublayer:self.frameLine];
    self.focused=NO;return self;
}
-(void)setStatusText:(NSString *)text{_statusText=[text copy];self.status.string=tb_text(text,15,NSFontWeightMedium,tb_white(.9),0,YES);[self layout];}
-(void)setFocused:(BOOL)focused{
    _focused=focused;CGColorRef accent=tb_rgb(self.available?TB_CYAN:TB_AMBER,1);
    self.frameLine.strokeColor=focused?accent:tb_white(.18);self.frameLine.shadowColor=accent;
    self.frameLine.shadowRadius=10;self.frameLine.shadowOpacity=focused?.9:0;self.frameLine.shadowOffset=CGSizeZero;
}
-(void)layout{
    [CATransaction begin];[CATransaction setDisableActions:YES];
    CGRect b=self.root.bounds;CGFloat c=fmin(36,b.size.width*.08),pad=fmax(18,b.size.width*.055);
    self.backdrop.frame=b;
    NSImage *image=self.art.contents;CGSize size=[image isKindOfClass:NSImage.class]?image.size:CGSizeZero;
    if(self.backdrop.contents&&size.width>0){CGFloat h=fmin(b.size.width*size.height/size.width,b.size.height*.7);
        self.art.frame=CGRectMake(0,fmax(0,b.size.height*.42-h/2),b.size.width,h);self.fade.frame=self.art.bounds;}
    else self.art.frame=CGRectInset(b,b.size.width*.2,b.size.height*.2);
    self.shade.frame=b;self.mask.path=tb_chamfer(b,c);self.frameLine.path=tb_chamfer(CGRectInset(b,1.5,1.5),c-1);
    CGSize badge=tb_fit(self.badgeText.string,320);
    self.badge.frame=CGRectMake(pad,b.size.height-pad-badge.height-16,badge.width+40,badge.height+16);
    self.badgeMask.path=tb_chamfer(self.badge.bounds,10);self.badgeText.frame=CGRectMake(0,8,badge.width+40,badge.height);
    CGFloat w=b.size.width-2*pad-c,y=CGRectGetMinY(self.badge.frame)-12;
    if(self.status.string){CGSize s=tb_fit(self.status.string,w);y-=s.height;self.status.frame=CGRectMake(pad,y,w,s.height);}
    CGSize k=tb_fit(self.kicker.string,w);y-=k.height+6;self.kicker.frame=CGRectMake(pad,y,w,k.height);
    [CATransaction commit];
    if(![self.backdrop animationForKey:@"drift"]&&!NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion){
        CABasicAnimation *a=[CABasicAnimation animationWithKeyPath:@"transform.scale"];a.fromValue=@1.0;a.toValue=@1.08;a.duration=16;a.autoreverses=YES;a.repeatCount=HUGE_VALF;
        a.timingFunction=[CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];[self.backdrop addAnimation:a forKey:@"drift"];
        CABasicAnimation *s=[a copy];s.toValue=@1.025;s.duration=11;[self.art addAnimation:s forKey:@"drift"];
    }
}
@end

typedef enum {TBNone,TBTribes2,TBStarsiege} TBChoice;
static TBChoice tb_choice;

@interface TBPickerView:NSView
@property(nonatomic,strong) CAGradientLayer *backdrop;
@property(nonatomic,strong) CALayer *stars,*emblem,*gear;
@property(nonatomic,strong) NSArray<CAShapeLayer *> *brackets;
@property(nonatomic,strong) CATextLayer *wordmark,*tagline,*footer;
@property(nonatomic,strong) NSArray<TBCard *> *cards;
@property(nonatomic) NSUInteger focus;
@property(nonatomic) BOOL starsiege;
@end
@implementation TBPickerView
-(BOOL)isFlipped{return YES;}
-(BOOL)acceptsFirstResponder{return YES;}
-(instancetype)initWithFrame:(NSRect)frame {
    if(!(self=[super initWithFrame:frame]))return nil;
    self.wantsLayer=YES;self.layer=[CALayer layer];self.layer.geometryFlipped=YES;self.layer.backgroundColor=NSColor.blackColor.CGColor;
    self.starsiege=tb_starsiege_ready();
    self.backdrop=[CAGradientLayer layer];self.backdrop.startPoint=CGPointMake(.5,0);self.backdrop.endPoint=CGPointMake(.5,1);
    self.backdrop.colors=@[(__bridge id)tb_rgb(0x0A1A2C,1),(__bridge id)tb_rgb(0x03070D,1),(__bridge id)tb_rgb(0x061A20,1)];[self.layer addSublayer:self.backdrop];
    self.stars=[CALayer layer];[self.layer addSublayer:self.stars];
    NSMutableArray *brackets=[NSMutableArray array];
    for(int i=0;i<4;i++){CAShapeLayer *l=[CAShapeLayer layer];l.strokeColor=tb_rgb(TB_CYAN,.55);l.fillColor=NSColor.clearColor.CGColor;l.lineWidth=2;[self.layer addSublayer:l];[brackets addObject:l];}
    self.brackets=brackets;
    NSImage *emblem=[[NSImage alloc] initWithContentsOfFile:tb_data(@"Launcher/emblem.png")];
    self.emblem=[CALayer layer];self.emblem.contents=emblem;self.emblem.contentsGravity=kCAGravityResizeAspect;[self.layer addSublayer:self.emblem];
    self.wordmark=tb_label();self.wordmark.string=tb_text(@"TRIBESPAD",26,NSFontWeightHeavy,tb_white(1),8,NO);[self.layer addSublayer:self.wordmark];
    self.tagline=tb_label();self.tagline.string=tb_text(@"SELECT YOUR GAME",12,NSFontWeightBold,tb_rgb(TB_CYAN,1),4,NO);[self.layer addSublayer:self.tagline];
    NSImage *gear=[NSImage imageWithSystemSymbolName:@"gearshape.fill" accessibilityDescription:@"Settings"];
    gear=[gear imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:20 weight:NSFontWeightSemibold]];
    NSImage *tinted=[NSImage imageWithSize:gear.size flipped:NO drawingHandler:^BOOL(NSRect r){[gear drawInRect:r];[[NSColor colorWithWhite:1 alpha:.85] set];NSRectFillUsingOperation(r,NSCompositingOperationSourceIn);return YES;}];
    self.gear=[CALayer layer];self.gear.contents=tinted;self.gear.contentsGravity=kCAGravityCenter;[self.layer addSublayer:self.gear];
    NSImage *(^art)(NSString *)=^NSImage *(NSString *n){return [[NSImage alloc] initWithContentsOfFile:tb_data([@"Launcher" stringByAppendingPathComponent:n])];};
    TBCard *starsiege=[[TBCard alloc] initWithKicker:@"Starsiege: Tribes  \u00B7  1998" art:art(@"starsiege.png") fallback:emblem available:self.starsiege
        badge:self.starsiege?@"PLAY  \u25B8":@"IN DEVELOPMENT"];
    starsiege.statusText=self.starsiege?@"Version 1.41, the one the community plays. Bots offline, PlayT1 servers online.":@"The original. Being translated to native code; it opens here once it plays.";
    TBCard *tribes2=[[TBCard alloc] initWithKicker:@"Tribes 2  \u00B7  2001  \u00B7  TribesNEXT online" art:art(@"tribes2.png") fallback:emblem available:YES badge:@"PLAY  \u25B8"];
    tribes2.statusText=@"Checking who is playing\u2026";
    self.cards=@[starsiege,tribes2];for(TBCard *c in self.cards)[self.layer addSublayer:c.root];
    self.focus=1;[self showFocus];
    self.footer=tb_label();self.footer.alignmentMode=kCAAlignmentCenter;
    self.footer.string=tb_text(@"Native ARM64 recompilations of the original games, built from your own copies. Starsiege: Tribes and Tribes 2 belong to their owners; TribesPad is MIT licensed.",11,NSFontWeightMedium,tb_white(.4),0,NO);
    [self.layer addSublayer:self.footer];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseMoved|NSTrackingActiveAlways|NSTrackingInVisibleRect owner:self userInfo:nil]];
    return self;
}
-(void)viewDidChangeBackingProperties{CGFloat s=self.window.backingScaleFactor?:2;for(CALayer *l in @[self.wordmark,self.tagline,self.footer])l.contentsScale=s;
    for(TBCard *c in self.cards)for(CALayer *l in @[c.kicker,c.status,c.badgeText])l.contentsScale=s;}
-(void)layout {
    [super layout];[CATransaction begin];[CATransaction setDisableActions:YES];
    CGRect b=self.bounds;self.backdrop.frame=b;[self layoutStars:b];
    BOOL small=b.size.height<500;CGFloat m=small?14:26,top=m+10,left=m,right=m,bottom=m,arm=small?22:38;
    CGPoint corners[4]={{left-8,top-8},{b.size.width-right+8,top-8},{left-8,b.size.height-bottom+8},{b.size.width-right+8,b.size.height-bottom+8}};
    for(int i=0;i<4;i++){CGFloat sx=i%2?-1:1,sy=i>1?-1:1;CGMutablePathRef p=CGPathCreateMutable();
        CGPathMoveToPoint(p,NULL,corners[i].x,corners[i].y+sy*arm);CGPathAddLineToPoint(p,NULL,corners[i].x,corners[i].y);CGPathAddLineToPoint(p,NULL,corners[i].x+sx*arm,corners[i].y);
        self.brackets[i].path=p;CGPathRelease(p);}
    CGFloat e=small?34:52;self.emblem.frame=CGRectMake(left+8,top+4,e,e);
    CGSize w=tb_fit(self.wordmark.string,400);self.wordmark.frame=CGRectMake(CGRectGetMaxX(self.emblem.frame)+12,CGRectGetMidY(self.emblem.frame)-w.height+4,w.width,w.height);
    CGSize t=tb_fit(self.tagline.string,400);self.tagline.frame=CGRectMake(CGRectGetMinX(self.wordmark.frame)+2,CGRectGetMaxY(self.wordmark.frame)+2,t.width,t.height);
    self.gear.frame=CGRectMake(b.size.width-right-8-44,CGRectGetMidY(self.emblem.frame)-22,44,44);
    CGSize f=tb_fit(self.footer.string,b.size.width-left-right-80);self.footer.frame=CGRectMake((b.size.width-f.width)/2,b.size.height-bottom-f.height,f.width,f.height);
    CGFloat cardTop=CGRectGetMaxY(self.emblem.frame)+(small?12:28),cardBottom=CGRectGetMinY(self.footer.frame)-(small?10:22),gap=small?14:30;
    CGFloat cw=(b.size.width-left-right-16-gap)/2;
    self.cards[0].root.frame=CGRectMake(left+8,cardTop,cw,cardBottom-cardTop);
    self.cards[1].root.frame=CGRectMake(left+8+cw+gap,cardTop,cw,cardBottom-cardTop);
    [CATransaction commit];for(TBCard *c in self.cards)[c layout];
}
/* A faint, fixed starfield behind everything. */
-(void)layoutStars:(CGRect)b {
    if(CGSizeEqualToSize(self.stars.frame.size,b.size))return;
    self.stars.frame=b;self.stars.sublayers=nil;srand48(1998);
    for(int i=0;i<140;i++){CALayer *s=[CALayer layer];CGFloat r=drand48()<.85?1:2;
        s.frame=CGRectMake(drand48()*b.size.width,drand48()*b.size.height,r,r);s.cornerRadius=r/2;s.backgroundColor=tb_white(.15+drand48()*.45);[self.stars addSublayer:s];}
}
-(void)showFocus{for(NSUInteger i=0;i<self.cards.count;i++)self.cards[i].focused=i==self.focus;}
-(void)moveFocus:(int)step{self.focus=(NSUInteger)MAX(0,MIN((int)self.cards.count-1,(int)self.focus+step));[self showFocus];}
-(NSInteger)cardAt:(NSPoint)p{for(NSUInteger i=0;i<self.cards.count;i++)if(CGRectContainsPoint(self.cards[i].root.frame,p))return (NSInteger)i;return -1;}
-(void)mouseMoved:(NSEvent *)event{NSInteger i=[self cardAt:[self convertPoint:event.locationInWindow fromView:nil]];if(i>=0&&(NSUInteger)i!=self.focus){self.focus=(NSUInteger)i;[self showFocus];}}
-(void)mouseDown:(NSEvent *)event{
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
    if(CGRectContainsPoint(CGRectInset(self.gear.frame,-6,-6),p)){[self showSettingsAt:p];return;}
    NSInteger i=[self cardAt:p];if(i<0)return;self.focus=(NSUInteger)i;[self showFocus];[self activate];
}
-(void)keyDown:(NSEvent *)event{
    NSString *k=event.charactersIgnoringModifiers;unichar c=k.length?[k characterAtIndex:0]:0;
    if(c==NSLeftArrowFunctionKey)[self moveFocus:-1];else if(c==NSRightArrowFunctionKey)[self moveFocus:1];
    else if(c=='\r'||c==3||c==' ')[self activate];else if(c=='1'){self.focus=0;[self showFocus];[self activate];}
    else if(c=='2'){self.focus=1;[self showFocus];[self activate];}else [super keyDown:event];
}
-(void)activate{
    if(self.focus==1){tb_choice=TBTribes2;return;}
    if(self.starsiege){tb_choice=TBStarsiege;return;}
    NSAlert *a=[NSAlert new];a.messageText=@"Starsiege: Tribes";
    a.informativeText=@"Not playable in this build yet. Its translation reaches the main menu; matches are next. It opens from here once it plays.";
    [a addButtonWithTitle:@"OK"];[a addButtonWithTitle:@"Read the Plan"];
    if([a runModal]==NSAlertSecondButtonReturn)[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:TBStarsiegePlan]];
}
-(void)showSettingsAt:(NSPoint)p{
    NSMenu *menu=[[NSMenu alloc] initWithTitle:@"TribesPad"];
    NSMenuItem *direct=[menu addItemWithTitle:@"Open Tribes 2 Directly" action:@selector(toggleDirect:) keyEquivalent:@""];direct.target=self;
    id show=[NSUserDefaults.standardUserDefaults objectForKey:@"TribesPad.showLauncher"];direct.state=show&&![show boolValue]?NSControlStateValueOn:NSControlStateValueOff;
    direct.toolTip=@"Skip this screen; Settings > Display brings it back";
    NSMenuItem *about=[menu addItemWithTitle:@"TribesPad on GitHub" action:@selector(openRepository:) keyEquivalent:@""];about.target=self;
    [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(CGRectGetMinX(self.gear.frame),CGRectGetMaxY(self.gear.frame)) inView:self];(void)p;
}
-(void)toggleDirect:(NSMenuItem *)item{[NSUserDefaults.standardUserDefaults setBool:item.state==NSControlStateValueOn forKey:@"TribesPad.showLauncher"];}
-(void)openRepository:(id)sender{(void)sender;[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:TBRepository]];}
-(void)watch:(GCController *)controller{
    __weak TBPickerView *weak=self;GCExtendedGamepad *pad=controller.extendedGamepad;
    pad.buttonA.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;if(pressed)[weak activate];};
    pad.dpad.left.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;if(pressed)[weak moveFocus:-1];};
    pad.dpad.right.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;if(pressed)[weak moveFocus:1];};
}
/* People playing Starsiege: Tribes now, from the community masters (t1_live.c). */
-(void)loadStarsiegePlayers{
    __weak TBPickerView *weak=self;NSString *base=self.cards[0].statusText;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        TBT1Live live;int ok=tb_t1_live(&live,4000);
        dispatch_async(dispatch_get_main_queue(),^{TBPickerView *me=weak;if(!me||!ok)return;
            NSString *now=live.people?[NSString stringWithFormat:@"%d %@ playing now on %d %@",live.people,live.people==1?@"person":@"people",live.busy_servers,live.busy_servers==1?@"server":@"servers"]:
                [NSString stringWithFormat:@"Nobody on the %d Tribes 1 servers right now",live.servers_answered];
            me.cards[0].statusText=[NSString stringWithFormat:@"%@\n%@",now,base];});
    });
}
/* People playing Tribes 2 now (players minus bots, TribesNEXT list.json). */
-(void)loadPlayers{
    [self loadStarsiegePlayers];
    NSMutableURLRequest *r=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"https://www.tribesnext.com/list.json"]];
    [r setValue:@"TribesPad" forHTTPHeaderField:@"User-Agent"];r.timeoutInterval=8;__weak TBPickerView *weak=self;
    [[NSURLSession.sharedSession dataTaskWithRequest:r completionHandler:^(NSData *data,NSURLResponse *resp,NSError *error){
        (void)resp;(void)error;NSArray *list=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;NSInteger people=-1,servers=0;
        if([list isKindOfClass:NSArray.class]){people=0;for(NSDictionary *s in list){if(![s isKindOfClass:NSDictionary.class])continue;
            NSInteger n=[s[@"num_players"] integerValue]-[s[@"num_bots"] integerValue];if(n>0){people+=n;servers++;}}}
        dispatch_async(dispatch_get_main_queue(),^{TBPickerView *me=weak;if(!me)return;
            me.cards[1].statusText=people<0?@"Online play with TribesNEXT":people==0?@"Nobody online right now \u00B7 see Game Nights in the \u00B7\u00B7\u00B7 menu":
                [NSString stringWithFormat:@"%ld %@ playing now on %ld %@",(long)people,people==1?@"person":@"people",(long)servers,servers==1?@"server":@"servers"];});
    }] resume];
}
@end

static int tb_exec(TBChoice choice){
    if(choice==TBStarsiege){
        NSURL *support=[NSFileManager.defaultManager URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
        char *paths[5];
        if(!support||!tb_starsiege_prepare_paths(tb_data(@"").fileSystemRepresentation,[support.path stringByAppendingPathComponent:@"TribesPad"].fileSystemRepresentation,paths)){
            fprintf(stderr,"TribesPad: Starsiege storage unavailable\n");return 125;}
        NSString *logs=[NSSearchPathForDirectoriesInDomains(NSLibraryDirectory,NSUserDomainMask,YES).firstObject stringByAppendingPathComponent:@"Logs/TribesPad"];
        [NSFileManager.defaultManager createDirectoryAtPath:logs withIntermediateDirectories:YES attributes:nil error:nil];
        NSString *log=[logs stringByAppendingPathComponent:@"Starsiege.log"];
        if(!getenv("TRIBESPAD_KEEP_STDIO")&&(!freopen(log.fileSystemRepresentation,"w",stdout)||!freopen(log.fileSystemRepresentation,"a",stderr)))return 125;
        if(chdir(paths[2]))return 125;
        char *engine=strdup(tb_macos(@"StarsiegeEngine").fileSystemRepresentation);
        char *args[]={engine,paths[0],paths[1],paths[2],paths[3],NULL};
        fflush(NULL);execv(engine,args);perror("TribesPad: StarsiegeEngine");return 126;
    }
    char *engine=strdup(tb_macos(@"TribesPadEngine").fileSystemRepresentation),*args[]={engine,NULL};
    execv(engine,args);perror("TribesPad: TribesPadEngine");return 126;
}

static void tb_snapshot(NSString *path){
    CGFloat w=1280,h=800;const char *size=getenv("TRIBESPAD_PICKER_SIZE");if(size){double a,b;if(sscanf(size,"%lfx%lf",&a,&b)==2){w=a;h=b;}}
    TBPickerView *view=[[TBPickerView alloc] initWithFrame:NSMakeRect(0,0,w,h)];[view layout];
    view.cards[1].statusText=@"Online play with TribesNEXT";
    if(getenv("TRIBESPAD_PICKER_LIVE")){[view loadPlayers];NSDate *until=[NSDate dateWithTimeIntervalSinceNow:6];
        while(until.timeIntervalSinceNow>0)[NSRunLoop.mainRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.1]];[view layout];}
    NSBitmapImageRep *rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)(w*2) pixelsHigh:(NSInteger)(h*2) bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    rep.size=NSMakeSize(w,h);NSGraphicsContext *g=[NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    CGContextRef cg=g.CGContext;CGContextTranslateCTM(cg,0,h);CGContextScaleCTM(cg,1,-1);
    for(CALayer *l in @[view.wordmark,view.tagline,view.footer])l.contentsScale=2;
    [view.layer layoutIfNeeded];[view.layer renderInContext:cg];
    [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
    fprintf(stderr,"TribesPad picker snapshot: %s (Starsiege %s)\n",path.fileSystemRepresentation,view.starsiege?"bundled":"not bundled");
}

int main(int argc,char **argv){
    (void)argc;(void)argv;
    @autoreleasepool{
        NSString *snapshot=getenv("TRIBESPAD_PICKER_SNAPSHOT")?@(getenv("TRIBESPAD_PICKER_SNAPSHOT")):nil;
        if(snapshot){[NSApplication sharedApplication];tb_snapshot(snapshot);return 0;}
        const char *pick=getenv("TRIBESPAD_PICK");
        if(pick)return tb_exec(!strcmp(pick,"starsiege")&&tb_starsiege_ready()?TBStarsiege:TBTribes2);
        id show=[NSUserDefaults.standardUserDefaults objectForKey:@"TribesPad.showLauncher"];
        if(show&&![show boolValue])return tb_exec(TBTribes2);
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];[NSApp finishLaunching];
        NSRect screen=NSScreen.mainScreen.visibleFrame;NSSize size=NSMakeSize(fmin(1280,screen.size.width*.9),fmin(800,screen.size.height*.9));
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,size.width,size.height)
            styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskFullSizeContentView backing:NSBackingStoreBuffered defer:NO];
        window.titlebarAppearsTransparent=YES;window.titleVisibility=NSWindowTitleHidden;window.title=@"TribesPad";
        window.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];window.minSize=NSMakeSize(720,480);
        window.collectionBehavior=NSWindowCollectionBehaviorFullScreenPrimary;
        TBPickerView *view=[[TBPickerView alloc] initWithFrame:window.contentView.bounds];view.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
        window.contentView=view;[window center];[window makeKeyAndOrderFront:nil];[window makeFirstResponder:view];[NSApp activateIgnoringOtherApps:YES];
        [view loadPlayers];for(GCController *c in GCController.controllers)[view watch:c];
        [NSNotificationCenter.defaultCenter addObserverForName:GCControllerDidConnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n){[view watch:n.object];}];
        fprintf(stderr,"TribesPad picker shown (Starsiege %s)\n",view.starsiege?"bundled":"not bundled");
        while(tb_choice==TBNone&&window.isVisible)@autoreleasepool{
            NSEvent *e=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:.05] inMode:NSDefaultRunLoopMode dequeue:YES];
            if(e)[NSApp sendEvent:e];
        }
        if(tb_choice==TBNone)return 0; /* the picker window was closed */
        [window orderOut:nil];
        fprintf(stderr,"TribesPad picker: starting %s\n",tb_choice==TBStarsiege?"Starsiege: Tribes":"Tribes 2");
        return tb_exec(tb_choice);
    }
}
