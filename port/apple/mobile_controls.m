/* Native touch UI feeds ordinary SDL events through the existing checked ABI.
   Controls are opt-in so the original menus retain direct touch targeting. */
#import <UIKit/UIKit.h>
#include "mobile_controls.h"

@interface TBGameControls : UIView
@property(nonatomic) TBMobileControls *controls;
@property(nonatomic,strong) NSArray<UIView *> *pads;
@property(nonatomic,strong) UIButton *toggle;
@property(nonatomic,strong) UIButton *menu;
@property(nonatomic) BOOL expanded;
-(void)releaseControls;
@end
@interface TBGamePad : UILabel
@property(nonatomic,weak) TBGameControls *owner;
@property(nonatomic,strong) UITouch *finger;
@property(nonatomic) unsigned source;
@property(nonatomic) uint32_t action;
@property(nonatomic) BOOL movement;
@property(nonatomic) CGPoint previous;
@end
@implementation TBGamePad
-(void)update:(UITouch *)touch {
    CGPoint point=[touch locationInView:self];TBInput *input=&self.owner.controls->input;
    if(self.movement) {
        CGFloat radius=MIN(self.bounds.size.width,self.bounds.size.height)*.4;
        tb_input_move(input,self.source,(point.x-self.bounds.size.width/2)/radius,
            (self.bounds.size.height/2-point.y)/radius,.12);
    } else if(!self.action||self.action==TBFire) {
        tb_input_look(input,self.source,(point.x-self.previous.x)*1.5,
            (point.y-self.previous.y)*1.5);
    }
    self.previous=point;tb_mobile_controls_emit(self.owner.controls);
}
-(void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(self.finger)return;self.finger=touches.anyObject;
    self.previous=[self.finger locationInView:self];
    tb_input_action(&self.owner.controls->input,self.source,self.action,1);
    [self update:self.finger];self.backgroundColor=[UIColor colorWithRed:0 green:.55 blue:.6 alpha:.6];
}
-(void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if([touches containsObject:self.finger])[self update:self.finger];
}
-(void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(![touches containsObject:self.finger])return;
    [self update:self.finger];tb_input_release(&self.owner.controls->input,self.source);
    tb_mobile_controls_emit(self.owner.controls);self.finger=nil;
    self.backgroundColor=[UIColor colorWithWhite:0 alpha:.35];
}
-(void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(![touches containsObject:self.finger])return;
    tb_input_release(&self.owner.controls->input,self.source);
    tb_mobile_controls_emit(self.owner.controls);self.finger=nil;
    self.backgroundColor=[UIColor colorWithWhite:0 alpha:.35];
}
@end
@implementation TBGameControls
-(instancetype)initWithFrame:(CGRect)frame {
    if(!(self=[super initWithFrame:frame]))return nil;
    self.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    self.multipleTouchEnabled=YES;
    self.toggle=[UIButton buttonWithType:UIButtonTypeSystem];
    [self.toggle setTitle:@"Touch controls" forState:UIControlStateNormal];
    [self.toggle addTarget:self action:@selector(toggleControls) forControlEvents:UIControlEventTouchUpInside];
    self.menu=[UIButton buttonWithType:UIButtonTypeSystem];
    [self.menu setTitle:@"Menu" forState:UIControlStateNormal];
    [self.menu addTarget:self action:@selector(openMenu) forControlEvents:UIControlEventTouchUpInside];
    for(UIButton *button in @[self.toggle,self.menu]) {
        button.backgroundColor=[UIColor colorWithWhite:0 alpha:.6];
        button.tintColor=UIColor.whiteColor;button.layer.cornerRadius=12;[self addSubview:button];
    }
    NSArray *labels=@[@"MOVE",@"LOOK",@"FIRE + AIM",@"JET",@"SKI / JUMP",@"WEAPON",@"GRENADE",@"PACK",@"INVENTORY"];
    const uint32_t actions[]={0,0,TBFire,TBJet,TBJump,TBWeapon,TBGrenade,TBUse,TBInventory};
    NSMutableArray *pads=[NSMutableArray new];
    for(unsigned i=0;i<labels.count;i++) {
        TBGamePad *pad=[TBGamePad new];pad.owner=self;pad.source=i;pad.action=actions[i];pad.movement=i==0;
        pad.text=labels[i];pad.textAlignment=NSTextAlignmentCenter;pad.textColor=UIColor.whiteColor;
        pad.font=[UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
        pad.backgroundColor=[UIColor colorWithWhite:0 alpha:.35];pad.layer.cornerRadius=18;
        pad.layer.borderWidth=1;pad.layer.borderColor=[UIColor colorWithWhite:1 alpha:.45].CGColor;
        pad.clipsToBounds=YES;pad.userInteractionEnabled=YES;pad.multipleTouchEnabled=YES;
        pad.accessibilityLabel=labels[i];[pads addObject:pad];[self addSubview:pad];
    }
    self.pads=pads;
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(releaseControls)
        name:UIApplicationWillResignActiveNotification object:nil];
    return self;
}
-(UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hit=[super hitTest:point withEvent:event];return hit==self?nil:hit;
}
-(void)releaseControls {
    if(self.controls)tb_mobile_controls_release(self.controls);
    for(TBGamePad *pad in self.pads) {pad.finger=nil;pad.backgroundColor=[UIColor colorWithWhite:0 alpha:.35];}
}
-(void)toggleControls {
    [self releaseControls];self.expanded=!self.expanded;
    [self.toggle setTitle:self.expanded?@"Hide controls":@"Touch controls" forState:UIControlStateNormal];
    [self setNeedsLayout];
}
-(void)openMenu {
    if(self.expanded)[self toggleControls];
    for(int down=1;down>=0;down--) {
        SDL_Event event={0};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
        event.key.windowID=self.controls->window;event.key.scancode=SDL_SCANCODE_ESCAPE;
        event.key.key=SDLK_ESCAPE;event.key.down=down;SDL_PushEvent(&event);
    }
}
-(void)layoutSubviews {
    [super layoutSubviews];[self releaseControls];
    CGRect area=UIEdgeInsetsInsetRect(self.bounds,self.safeAreaInsets);
    CGFloat w=area.size.width,h=area.size.height,x=area.origin.x,y=area.origin.y;
    self.toggle.frame=CGRectMake(x+w/2-70,y+8,140,40);
    self.menu.frame=CGRectMake(x+w/2+78,y+8,70,40);self.menu.hidden=!self.expanded;
    CGFloat size=MIN(180,MIN(w*.20,h*.32));
    const CGRect frames[]={CGRectMake(x+20,y+h-size-24,size,size),
        CGRectMake(x+w*.48,y+h*.39,w*.22,h*.48),
        CGRectMake(x+w-size-20,y+h-size-24,size,size),
        CGRectMake(x+w-size-20,y+h-size-94,size,58),
        CGRectMake(x+20,y+h-size-94,size,58),
        CGRectMake(x+w-130,y+64,110,48),CGRectMake(x+w-130,y+120,110,48),
        CGRectMake(x+20,y+120,110,48),CGRectMake(x+20,y+64,110,48)};
    for(unsigned i=0;i<self.pads.count;i++) {self.pads[i].frame=frames[i];self.pads[i].hidden=!self.expanded;}
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
