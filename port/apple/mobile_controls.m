/* Native touch UI feeds ordinary SDL events through the existing checked ABI.
   Controls are opt-in so the original menus retain direct touch targeting.
   Presentation follows the HaloPad/SunPad overlay style: an analog stick with
   a moving thumb, round icon buttons ringed around a primary fire button, an
   invisible look surface and round icon buttons for the utility actions. */
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#include <math.h>
#include "mobile_controls.h"

typedef NS_ENUM(NSInteger, TBControlKind) { TBKindStick, TBKindLook, TBKindButton };
extern uint64_t tribes_gl4es_presented_frames(void);
static NSString * const TBRepositoryURL=@"https://github.com/chrissotraidis/tribespad";

/* Touch preferences (NSUserDefaults, TribesPad-owned keys). */
static double tb_setting(NSString *key,double fallback){id v=[NSUserDefaults.standardUserDefaults objectForKey:key];return v?[v doubleValue]:fallback;}
static void tb_set(NSString *key,double value){[NSUserDefaults.standardUserDefaults setDouble:value forKey:key];}
#define TB_SIZE tb_setting(@"TribesPad.controlSize",1.0)
#define TB_OPACITY tb_setting(@"TribesPad.controlOpacity",1.0)
#define TB_LOOK tb_setting(@"TribesPad.lookSensitivity",1.0)
#define TB_LEFT_HANDED (tb_setting(@"TribesPad.leftHanded",0)!=0)
#define TB_HIDE_WITH_CONTROLLER (tb_setting(@"TribesPad.hideWithController",1)!=0)
#define TB_SHOW_FPS (tb_setting(@"TribesPad.showFPS",0)!=0)

@interface TBGameControls : UIView
@property(nonatomic) TBMobileControls *controls;
@property(nonatomic,strong) NSArray<UIView *> *pads;
@property(nonatomic,strong) UIButton *toggle;
@property(nonatomic,strong) UIButton *menu;
@property(nonatomic,strong) UIButton *keyboard;
@property(nonatomic) BOOL expanded;
@property(nonatomic,strong) GCController *controller;
@property(nonatomic,strong) CADisplayLink *aimClock;
@property(nonatomic) CGPoint stickAim;
@property(nonatomic) CFTimeInterval lastAimTime;
@property(nonatomic) CGRect laidOutArea;
@property(nonatomic,strong) UILabel *fps;
@property(nonatomic,strong) NSTimer *fpsClock;
@property(nonatomic) uint64_t fpsFrames;
-(void)rebuildMenu;
-(void)releaseControls;
-(void)toggleControls;
@end

@interface TBGamePad : UIView
@property(nonatomic,weak) TBGameControls *owner;
@property(nonatomic,strong) UITouch *finger;
@property(nonatomic) unsigned source;
@property(nonatomic) uint32_t action;
@property(nonatomic) TBControlKind kind;
@property(nonatomic) BOOL primary;
@property(nonatomic) BOOL held;
@property(nonatomic) CGPoint previous;
@property(nonatomic,strong) UIImageView *icon;
@property(nonatomic,strong) UILabel *caption;
@property(nonatomic,strong) UIView *thumb;
@end

static UIColor *tb_rest(BOOL primary){return primary?[UIColor colorWithRed:.12 green:.32 blue:.43 alpha:.55]:[UIColor colorWithWhite:.05 alpha:.42];}
static UIColor *tb_held(BOOL primary){return primary?[UIColor colorWithRed:.38 green:.72 blue:.88 alpha:.75]:[UIColor colorWithWhite:1 alpha:.30];}
static UIColor *tb_accent(void){return [UIColor colorWithRed:.60 green:.85 blue:1 alpha:.9];}

@implementation TBGamePad
-(instancetype)initWithKind:(TBControlKind)kind symbol:(NSString *)symbol fallback:(NSString *)fallback caption:(NSString *)caption {
    if(!(self=[super initWithFrame:CGRectZero]))return nil;
    self.kind=kind;self.multipleTouchEnabled=YES;self.userInteractionEnabled=YES;
    self.accessibilityLabel=caption;self.isAccessibilityElement=YES;
    if(kind==TBKindLook){self.backgroundColor=UIColor.clearColor;return self;}
    self.layer.borderWidth=1.5;self.layer.borderColor=[UIColor colorWithWhite:1 alpha:.32].CGColor;
    if(kind==TBKindStick) {
        self.backgroundColor=[UIColor colorWithWhite:.05 alpha:.30];
        self.thumb=[UIView new];self.thumb.userInteractionEnabled=NO;
        self.thumb.backgroundColor=[UIColor colorWithWhite:1 alpha:.45];
        self.thumb.layer.shadowColor=UIColor.blackColor.CGColor;self.thumb.layer.shadowOpacity=.35;self.thumb.layer.shadowRadius=4;
        self.thumb.layer.shadowOffset=CGSizeZero;[self addSubview:self.thumb];
    } else {
        UIImage *image=[UIImage systemImageNamed:symbol]?:[UIImage systemImageNamed:fallback];
        self.icon=[[UIImageView alloc] initWithImage:image];self.icon.contentMode=UIViewContentModeScaleAspectFit;
        self.icon.tintColor=[UIColor colorWithWhite:1 alpha:.95];
        self.icon.layer.shadowColor=UIColor.blackColor.CGColor;self.icon.layer.shadowOpacity=.5;
        self.icon.layer.shadowRadius=2;self.icon.layer.shadowOffset=CGSizeZero;[self addSubview:self.icon];
    }
    self.caption=[UILabel new];self.caption.text=caption.uppercaseString;self.caption.textAlignment=NSTextAlignmentCenter;
    self.caption.textColor=[UIColor colorWithWhite:1 alpha:.80];self.caption.adjustsFontSizeToFitWidth=YES;self.caption.minimumScaleFactor=.6;
    self.caption.layer.shadowColor=UIColor.blackColor.CGColor;self.caption.layer.shadowOpacity=.6;
    self.caption.layer.shadowRadius=2;self.caption.layer.shadowOffset=CGSizeZero;[self addSubview:self.caption];
    [self setHeld:NO];
    return self;
}
-(void)setHeld:(BOOL)held {
    _held=held;if(self.kind==TBKindLook)return;
    if(self.kind==TBKindStick) {
        self.thumb.backgroundColor=held?tb_accent():[UIColor colorWithWhite:1 alpha:.45];
        self.layer.borderColor=(held?[UIColor colorWithRed:.60 green:.85 blue:1 alpha:.8]:[UIColor colorWithWhite:1 alpha:.30]).CGColor;
        self.layer.borderWidth=held?2:1.5;
        if(!held)[self placeThumb:CGPointMake(self.bounds.size.width/2,self.bounds.size.height/2)];
        return;
    }
    self.backgroundColor=held?tb_held(self.primary):tb_rest(self.primary);
    self.layer.borderColor=[UIColor colorWithWhite:1 alpha:held?.8:.32].CGColor;
}
-(void)placeThumb:(CGPoint)point {
    CGFloat cx=self.bounds.size.width/2,cy=self.bounds.size.height/2,limit=MIN(cx,cy)*.62;
    CGFloat dx=point.x-cx,dy=point.y-cy,d=hypot(dx,dy);
    if(d>limit){dx*=limit/d;dy*=limit/d;}
    self.thumb.center=CGPointMake(cx+dx,cy+dy);
}
-(void)layoutSubviews {
    [super layoutSubviews];CGFloat w=self.bounds.size.width,h=self.bounds.size.height,side=MIN(w,h);
    if(self.kind==TBKindLook)return;
    self.layer.cornerRadius=side/2;
    if(self.kind==TBKindStick) {
        CGFloat t=side*.42;self.thumb.bounds=CGRectMake(0,0,t,t);self.thumb.layer.cornerRadius=t/2;
        if(!self.held)self.thumb.center=CGPointMake(w/2,h/2);
        self.caption.font=[UIFont systemFontOfSize:MAX(9,side*.075) weight:UIFontWeightSemibold];
        self.caption.frame=CGRectMake(0,h-side*.17,w,side*.12);return;
    }
    CGFloat iconSide=side*(self.primary?.40:.42),top=side*(self.caption.hidden?.29:.20);
    self.icon.frame=CGRectMake((w-iconSide)/2,top,iconSide,iconSide);
    self.caption.font=[UIFont systemFontOfSize:MAX(8,side*.135) weight:UIFontWeightBold];
    self.caption.frame=CGRectMake(side*.12,top+iconSide+side*.03,w-side*.24,side*.17);
}
-(void)update:(UITouch *)touch {
    CGPoint point=[touch locationInView:self];TBInput *input=&self.owner.controls->input;
    if(self.kind==TBKindStick) {
        CGFloat radius=MIN(self.bounds.size.width,self.bounds.size.height)*.4;
        tb_input_move(input,self.source,(point.x-self.bounds.size.width/2)/radius,
            (self.bounds.size.height/2-point.y)/radius,.12);
        [self placeThumb:point];
    } else if(!self.action||self.action==TBFire) {
        tb_input_look(input,self.source,(point.x-self.previous.x)*1.5*TB_LOOK,
            (point.y-self.previous.y)*1.5*TB_LOOK);
    }
    self.previous=point;tb_mobile_controls_emit(self.owner.controls);
}
-(void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(self.finger)return;self.finger=touches.anyObject;
    self.previous=[self.finger locationInView:self];
    tb_input_action(&self.owner.controls->input,self.source,self.action,1);
    self.held=YES;[self update:self.finger];
}
-(void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if([touches containsObject:self.finger])[self update:self.finger];
}
-(void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(![touches containsObject:self.finger])return;
    [self update:self.finger];tb_input_release(&self.owner.controls->input,self.source);
    tb_mobile_controls_emit(self.owner.controls);self.finger=nil;self.held=NO;
    // The original inventory needs direct taps: the look surface otherwise covers
    // equipment fields and Done. Retire the key before hiding/releasing pads.
    if(self.action==TBInventory&&self.owner.expanded)[self.owner toggleControls];
}
-(void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(![touches containsObject:self.finger])return;
    tb_input_release(&self.owner.controls->input,self.source);
    tb_mobile_controls_emit(self.owner.controls);self.finger=nil;self.held=NO;
}
@end

static UIButton *tb_round_button(NSString *symbol,NSString *label,id target,SEL action) {
    UIButtonConfiguration *c=[UIButtonConfiguration filledButtonConfiguration];
    c.image=[UIImage systemImageNamed:symbol withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:17 weight:UIImageSymbolWeightSemibold]];
    c.baseForegroundColor=UIColor.whiteColor;c.baseBackgroundColor=[UIColor colorWithWhite:.06 alpha:.72];
    c.cornerStyle=UIButtonConfigurationCornerStyleCapsule;
    c.background.strokeColor=[UIColor colorWithWhite:1 alpha:.3];c.background.strokeWidth=1;
    UIButton *button=[UIButton buttonWithConfiguration:c primaryAction:nil];
    [button addTarget:target action:action forControlEvents:UIControlEventTouchUpInside];
    button.accessibilityLabel=label;button.pointerInteractionEnabled=YES;
    return button;
}

@implementation TBGameControls
-(instancetype)initWithFrame:(CGRect)frame {
    if(!(self=[super initWithFrame:frame]))return nil;
    self.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    self.multipleTouchEnabled=YES;
    self.keyboard=tb_round_button(@"keyboard",@"Keyboard",self,@selector(toggleKeyboard));
    self.toggle=tb_round_button(@"gamecontroller.fill",@"Touch controls",self,@selector(toggleControls));
    self.menu=tb_round_button(@"ellipsis",@"Menu",self,nil);
    self.menu.showsMenuAsPrimaryAction=YES;
    self.fps=[UILabel new];self.fps.font=[UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightSemibold];
    self.fps.textColor=UIColor.greenColor;self.fps.backgroundColor=[UIColor colorWithWhite:0 alpha:.5];
    self.fps.textAlignment=NSTextAlignmentCenter;self.fps.layer.cornerRadius=6;self.fps.layer.masksToBounds=YES;
    self.fps.userInteractionEnabled=NO;self.fps.hidden=YES;
    // Order fixes source numbering used by input: 0 move, 1 look, 2 fire...
    struct {TBControlKind kind;uint32_t action;const char *symbol,*fallback,*caption;BOOL primary;} spec[]={
        {TBKindStick,0,"","","Move",NO},
        {TBKindLook,0,"","","Look",NO},
        {TBKindButton,TBFire,"scope","target","Fire",YES},
        {TBKindButton,TBJet,"flame.fill","arrow.up","Jet",NO},
        {TBKindButton,TBJump,"arrow.up.to.line","arrow.up","Ski / Jump",NO},
        {TBKindButton,TBWeapon,"arrow.triangle.2.circlepath","arrow.2.squarepath","Weapon",NO},
        {TBKindButton,TBGrenade,"burst.fill","circle.fill","Grenade",NO},
        {TBKindButton,TBUse,"backpack.fill","shippingbox.fill","Pack",NO},
        {TBKindButton,TBInventory,"square.grid.2x2.fill","square.grid.2x2","Inventory",NO}};
    NSMutableArray *pads=[NSMutableArray new];
    for(unsigned i=0;i<sizeof spec/sizeof spec[0];i++) {
        TBGamePad *pad=[[TBGamePad alloc] initWithKind:spec[i].kind symbol:@(spec[i].symbol) fallback:@(spec[i].fallback) caption:@(spec[i].caption)];
        pad.owner=self;pad.source=i;pad.action=spec[i].action;pad.primary=spec[i].primary;[pad setHeld:NO];
        [pads addObject:pad];[self addSubview:pad];
    }
    self.pads=pads;
    for(UIButton *button in @[self.keyboard,self.toggle,self.menu])[self addSubview:button];
    [self addSubview:self.fps];[self rebuildMenu];[self applyFPSSetting];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(releaseControls)
        name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(keyboardFrameChanged:)
        name:UIKeyboardDidChangeFrameNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(controllerChanged:)
        name:GCControllerDidConnectNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(controllerChanged:)
        name:GCControllerDidDisconnectNotification object:nil];
    return self;
}
-(void)didMoveToSuperview {
    [super didMoveToSuperview];
    if(self.superview)[self controllerChanged:nil];
    else [self detachController];
}
-(void)detachController {
    GCExtendedGamepad *pad=self.controller.extendedGamepad;
    pad.valueChangedHandler=nil;pad.buttonMenu.pressedChangedHandler=nil;
    [self.aimClock invalidate];self.aimClock=nil;self.controller=nil;
    self.stickAim=CGPointZero;self.lastAimTime=0;
    if(self.controls){tb_input_release(&self.controls->input,9);tb_mobile_controls_emit(self.controls);}
}
-(void)controllerChanged:(NSNotification *)notification {
    (void)notification;if(!self.superview)return;
    if(self.controller&&[GCController.controllers containsObject:self.controller])return;
    [self detachController];
    for(GCController *controller in GCController.controllers)if(controller.extendedGamepad){
        self.controller=controller;controller.handlerQueue=dispatch_get_main_queue();break;
    }
    if(!self.controller)return;
    if(TB_HIDE_WITH_CONTROLLER&&self.expanded)[self toggleControls];
    __weak TBGameControls *weakSelf=self;
    self.controller.extendedGamepad.valueChangedHandler=^(GCExtendedGamepad *pad,GCControllerElement *element){
        TBGameControls *owner=weakSelf;
        if(!owner.controls||pad!=owner.controller.extendedGamepad||element==pad.buttonMenu||
            UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
        tb_input_move(&owner.controls->input,9,pad.leftThumbstick.xAxis.value,pad.leftThumbstick.yAxis.value,.15);
        owner.stickAim=CGPointMake(pad.rightThumbstick.xAxis.value,pad.rightThumbstick.yAxis.value);
        const uint32_t actions[]={TBFire,TBJet,TBJump,TBGrenade,TBWeapon,TBUse,TBInventory};
        NSArray<GCControllerButtonInput *> *buttons=@[pad.rightTrigger,pad.leftTrigger,pad.buttonA,
            pad.leftShoulder,pad.rightShoulder,pad.buttonX,pad.buttonY];
        for(unsigned i=0;i<buttons.count;i++)tb_input_action(&owner.controls->input,9,actions[i],buttons[i].pressed);
        tb_mobile_controls_emit(owner.controls);
    };
    self.controller.extendedGamepad.buttonMenu.pressedChangedHandler=^(GCControllerButtonInput *button,float value,BOOL pressed){
        (void)value;TBGameControls *owner=weakSelf;
        if(pressed&&button==owner.controller.extendedGamepad.buttonMenu&&
            UIApplication.sharedApplication.applicationState==UIApplicationStateActive)[owner openMenu];
    };
    self.aimClock=[CADisplayLink displayLinkWithTarget:self selector:@selector(aimTick:)];
    [self.aimClock addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
-(void)aimTick:(CADisplayLink *)clock {
    CFTimeInterval previous=self.lastAimTime;self.lastAimTime=clock.timestamp;
    if(previous&&UIApplication.sharedApplication.applicationState==UIApplicationStateActive)
        tb_mobile_controls_gamepad_look(self.controls,self.stickAim.x,self.stickAim.y,clock.timestamp-previous);
}
-(UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hit=[super hitTest:point withEvent:event];return hit==self?nil:hit;
}
-(void)releaseControls {
    self.stickAim=CGPointZero;self.lastAimTime=0;
    if(self.controls)tb_mobile_controls_release(self.controls);
    for(TBGamePad *pad in self.pads){pad.finger=nil;pad.held=NO;}
}
-(void)toggleKeyboard {
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);if(!window)return;
    if(SDL_TextInputActive(window))SDL_StopTextInput(window);
    else {
        if(self.expanded)[self toggleControls];
        [self releaseControls];
        // Original menus retain their own field focus. The last game touch
        // identifies the field; tell UIKit to keep that point above the IME.
        float x=0,y=0;SDL_GetMouseState(&x,&y);
        SDL_Rect caret={(int)x,(int)y,1,24};
        SDL_SetTextInputArea(window,&caret,0);
        SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME,"1");
        SDL_StartTextInput(window);
    }
}
-(void)keyboardFrameChanged:(NSNotification *)notification {
    (void)notification;[self setNeedsLayout];
}
-(void)toggleControls {
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);
    if(window)SDL_StopTextInput(window);
    [self releaseControls];self.expanded=!self.expanded;
    UIButtonConfiguration *c=self.toggle.configuration;
    c.image=[UIImage systemImageNamed:self.expanded?@"xmark":@"gamecontroller.fill" withConfiguration:
        [UIImageSymbolConfiguration configurationWithPointSize:17 weight:UIImageSymbolWeightSemibold]];
    c.baseBackgroundColor=self.expanded?[UIColor colorWithRed:.12 green:.32 blue:.43 alpha:.8]:[UIColor colorWithWhite:.06 alpha:.72];
    self.toggle.configuration=c;
    self.toggle.accessibilityLabel=self.expanded?@"Hide controls":@"Touch controls";
    [self rebuildMenu];
    [self setNeedsLayout];
}
-(UIAction *)check:(NSString *)title on:(BOOL)on handler:(void (^)(void))handler {
    UIAction *a=[UIAction actionWithTitle:title image:nil identifier:nil handler:^(__kindof UIAction *x){(void)x;handler();}];
    a.state=on?UIMenuElementStateOn:UIMenuElementStateOff;return a;
}
-(UIMenu *)choices:(NSString *)title icon:(NSString *)icon key:(NSString *)key value:(double)current
          options:(NSArray<NSArray *> *)options {
    __weak TBGameControls *weak=self;NSMutableArray *items=[NSMutableArray new];
    for(NSArray *option in options){double v=[option[1] doubleValue];
        [items addObject:[self check:option[0] on:fabs(current-v)<1e-6 handler:^{tb_set(key,v);[weak settingsChanged];}]];}
    return [UIMenu menuWithTitle:title image:[UIImage systemImageNamed:icon] identifier:nil options:0 children:items];
}
-(void)settingsChanged {
    self.laidOutArea=CGRectNull;[self applyFPSSetting];[self rebuildMenu];[self setNeedsLayout];
    if(self.controller&&TB_HIDE_WITH_CONTROLLER&&self.expanded)[self toggleControls];
}
/* The menu holds what the original menus do not: touch-control settings, the
   console, an FPS counter and help. Everything the game's own menus do stays there. */
-(void)rebuildMenu {
    __weak TBGameControls *weak=self;
    UIImage *(^icon)(NSString *)=^UIImage *(NSString *name){return [UIImage systemImageNamed:name];};
    NSArray *game=@[
        [UIAction actionWithTitle:@"Game Menu" image:icon(@"list.bullet") identifier:nil handler:^(__kindof UIAction *a){(void)a;[weak openMenu];}],
        [UIAction actionWithTitle:self.expanded?@"Hide Touch Controls":@"Show Touch Controls" image:icon(@"gamecontroller") identifier:nil
            handler:^(__kindof UIAction *a){(void)a;[weak toggleControls];}],
        [UIAction actionWithTitle:@"Console" image:icon(@"terminal") identifier:nil handler:^(__kindof UIAction *a){(void)a;[weak openConsole];}]];
    UIMenu *controls=[UIMenu menuWithTitle:@"Touch Controls" image:icon(@"slider.horizontal.3") identifier:nil options:0 children:@[
        [self choices:@"Size" icon:@"arrow.up.left.and.arrow.down.right" key:@"TribesPad.controlSize" value:TB_SIZE
            options:@[@[@"Small",@.85],@[@"Medium",@1.0],@[@"Large",@1.18]]],
        [self choices:@"Opacity" icon:@"circle.lefthalf.filled" key:@"TribesPad.controlOpacity" value:TB_OPACITY
            options:@[@[@"Faint",@.45],@[@"Medium",@.7],@[@"Solid",@1.0]]],
        [self choices:@"Look Sensitivity" icon:@"scope" key:@"TribesPad.lookSensitivity" value:TB_LOOK
            options:@[@[@"Low",@.6],@[@"Medium",@1.0],@[@"High",@1.5],@[@"Very High",@2.2]]],
        [self check:@"Left-Handed Layout" on:TB_LEFT_HANDED handler:^{tb_set(@"TribesPad.leftHanded",!TB_LEFT_HANDED);[weak settingsChanged];}],
        [self check:@"Hide with a Controller" on:TB_HIDE_WITH_CONTROLLER handler:^{tb_set(@"TribesPad.hideWithController",!TB_HIDE_WITH_CONTROLLER);[weak settingsChanged];}]]];
    UIMenu *display=[UIMenu menuWithTitle:@"Display" image:icon(@"display") identifier:nil options:0 children:@[
        [self check:@"Show FPS Counter" on:TB_SHOW_FPS handler:^{tb_set(@"TribesPad.showFPS",!TB_SHOW_FPS);[weak settingsChanged];}]]];
    UIMenu *help=[UIMenu menuWithTitle:@"Help" image:icon(@"questionmark.circle") identifier:nil options:0 children:@[
        [UIAction actionWithTitle:@"TribesPad on GitHub" image:icon(@"safari") identifier:nil handler:^(__kindof UIAction *a){(void)a;
            [UIApplication.sharedApplication openURL:[NSURL URLWithString:TBRepositoryURL] options:@{} completionHandler:nil];}],
        [UIAction actionWithTitle:@"About TribesPad" image:icon(@"info.circle") identifier:nil handler:^(__kindof UIAction *a){(void)a;[weak showAbout];}]]];
    self.menu.menu=[UIMenu menuWithTitle:@"TribesPad" children:@[
        [UIMenu menuWithTitle:@"" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:game],
        [UIMenu menuWithTitle:@"" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:@[controls,display]],
        [UIMenu menuWithTitle:@"" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:@[help]]]];
}
-(void)applyFPSSetting {
    [self.fpsClock invalidate];self.fpsClock=nil;self.fps.hidden=!TB_SHOW_FPS;
    if(!TB_SHOW_FPS)return;
    self.fpsFrames=tribes_gl4es_presented_frames();self.fps.text=@"– FPS";
    __weak TBGameControls *weak=self;
    self.fpsClock=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer){(void)timer;
        TBGameControls *owner=weak;if(!owner)return;uint64_t now=tribes_gl4es_presented_frames();
        owner.fps.text=[NSString stringWithFormat:@"%llu FPS",(unsigned long long)(now-owner.fpsFrames)];owner.fpsFrames=now;}];
}
-(void)tapKey:(SDL_Scancode)scancode {
    for(int down=1;down>=0;down--) {
        SDL_Event event={0};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
        event.key.windowID=self.controls->window;event.key.scancode=scancode;
        event.key.key=SDL_GetKeyFromScancode(scancode,SDL_KMOD_NONE,false);event.key.down=down;SDL_PushEvent(&event);
    }
}
/* The original console toggles on the grave key; open the keyboard for typing. */
-(void)openConsole {
    if(self.expanded)[self toggleControls];else [self releaseControls];
    [self tapKey:SDL_SCANCODE_GRAVE];
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);
    if(window&&!SDL_TextInputActive(window)){SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME,"0");SDL_StartTextInput(window);}
}
-(void)showAbout {
    NSDictionary *info=NSBundle.mainBundle.infoDictionary;
    NSString *version=[NSString stringWithFormat:@"%@ (%@)",info[@"CFBundleShortVersionString"]?:@"dev",info[@"CFBundleVersion"]?:@"0"];
    NSString *data=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject stringByAppendingPathComponent:@"TribesPad/Game"];
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"TribesPad"
        message:[NSString stringWithFormat:@"The original Tribes 2 with TribesNEXT/QoL, translated ahead of time to native ARM64.\n\nVersion %@\nGame data: %@",version,data]
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    UIViewController *presenter=self.window.rootViewController;while(presenter.presentedViewController)presenter=presenter.presentedViewController;
    [presenter presentViewController:alert animated:YES completion:nil];
}
-(void)openMenu {
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);
    if(window)SDL_StopTextInput(window);
    if(self.expanded)[self toggleControls];
    else [self releaseControls];
    [self tapKey:SDL_SCANCODE_ESCAPE];
}
-(void)layoutSubviews {
    [super layoutSubviews];
    CGRect area=UIEdgeInsetsInsetRect(self.bounds,self.safeAreaInsets);
    // SDL shifts its root view to reveal the text caret. Keep native controls
    // inside the part of that view still visible in the window.
    if(self.window)area=CGRectIntersection(self.bounds,[self convertRect:
        UIEdgeInsetsInsetRect(self.window.bounds,self.window.safeAreaInsets) fromView:self.window]);
    // Layout also runs when a control's appearance changes. Only a real
    // geometry change may drop held controls; otherwise a held Jet would be
    // released as soon as it is drawn as pressed.
    if(!CGRectEqualToRect(area,self.laidOutArea)){self.laidOutArea=area;[self releaseControls];}
    CGFloat w=area.size.width,h=area.size.height,x=area.origin.x,y=area.origin.y;
    // One scale for iPad and iPhone: 1.0 on an 11-inch iPad in landscape.
    CGFloat k=fmin(1.15,fmax(.62,fmin(w,h)/780))*TB_SIZE,margin=fmax(14,22*k);
    // Utility buttons: compact, near the top centre, clear of the chat (left) and compass (right) HUD.
    CGFloat b=fmax(38,44*k),gap=10*k,top=y+fmax(8,10*k);
    CGFloat mid=x+w*.565;
    self.keyboard.frame=CGRectMake(mid-b*1.5-gap,top,b,b);
    self.toggle.frame=CGRectMake(mid-b/2,top,b,b);
    self.menu.frame=CGRectMake(mid+b/2+gap,top,b,b);
    self.fps.frame=CGRectMake(mid-36,top+b+6,72,22);
    // Movement stick bottom left; Pack and Inventory above it.
    CGFloat stick=170*k,small=58*k;
    CGRect stickFrame=CGRectMake(x+margin,y+h-margin-stick,stick,stick);
    CGRect pack=CGRectMake(CGRectGetMinX(stickFrame)+stick*.05,CGRectGetMinY(stickFrame)-small-14*k,small,small);
    CGRect inventory=CGRectOffset(pack,small+12*k,0);
    // Fire bottom right with Jet, Jump, Grenade and Weapon in an even quarter ring.
    CGFloat fire=112*k,ring=68*k,fireX=x+w-margin-fire/2,fireY=y+h-margin-fire/2;
    CGFloat spacing=ring+12*k,radius=fmax(fire/2+ring/2+12*k,spacing/(2*sin(M_PI/12)));
    CGRect fireFrame=CGRectMake(fireX-fire/2,fireY-fire/2,fire,fire);
    CGRect ringFrames[4];
    for(int i=0;i<4;i++){CGFloat a=M_PI-i*(M_PI/6); /* 180, 150, 120, 90 degrees */
        CGFloat cx=fireX+radius*cos(a),cy=fireY-radius*sin(a);ringFrames[i]=CGRectMake(cx-ring/2,cy-ring/2,ring,ring);}
    // Look anywhere on the right half that is not a button.
    CGRect look=CGRectMake(x+w*.42,top+b+8,w*.58,h-(top+b+8-y));
    const CGRect frames[]={stickFrame,look,fireFrame,ringFrames[0],ringFrames[1],ringFrames[3],ringFrames[2],pack,inventory};
    BOOL mirror=TB_LEFT_HANDED;CGFloat opacity=TB_OPACITY;
    for(unsigned i=0;i<self.pads.count;i++){TBGamePad *pad=(TBGamePad *)self.pads[i];
        // Mirrored controls keep clear of the original weapon column on the right edge.
        CGRect f=frames[i];
        if(mirror){BOOL toRight=CGRectGetMidX(f)<x+w/2;f.origin.x=2*x+w-CGRectGetMaxX(f);
            if(toRight&&pad.kind!=TBKindLook)f.origin.x-=40*k;}
        pad.frame=f;pad.alpha=pad.kind==TBKindLook?1:opacity;pad.hidden=!self.expanded;
        pad.caption.hidden=pad.kind==TBKindButton&&pad.bounds.size.width<52;[pad setNeedsLayout];}
}
@end
void tb_mobile_controls_install(void) {
    static TBMobileControls controls;
    static TBGameControls *overlay;
    [NSTimer scheduledTimerWithTimeInterval:.5 repeats:YES block:^(NSTimer *timer) {
        (void)timer;if(!(SDL_WasInit(SDL_INIT_VIDEO)&SDL_INIT_VIDEO))return;
        SDL_Window *window=SDL_GetKeyboardFocus();if(!window)return;
        UIWindow *native=(__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER,NULL);
        UIView *root=native.rootViewController.view;if(!root)return;
        if(overlay.superview!=root) {
            [overlay releaseControls];[overlay removeFromSuperview];
            controls.window=SDL_GetWindowID(window);
            overlay=[[TBGameControls alloc] initWithFrame:root.bounds];overlay.controls=&controls;
            [root addSubview:overlay];
        }
        [root bringSubviewToFront:overlay];
    }];
}
