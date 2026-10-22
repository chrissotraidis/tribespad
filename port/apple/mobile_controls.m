/* Native touch UI feeds ordinary SDL events through the existing checked ABI.
   Controls are opt-in so the original menus retain direct touch targeting.
   Presentation follows the HaloPad/SunPad overlay style: an analog stick with
   a moving thumb, round icon buttons ringed around a primary fire button, an
   invisible look surface and round icon buttons for the utility actions. */
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#import <SafariServices/SafariServices.h>
#include <math.h>
#include "mobile_controls.h"
#import "live_servers.h"

typedef NS_ENUM(NSInteger, TBControlKind) { TBKindStick, TBKindLook, TBKindButton };
extern uint64_t tribes_gl4es_presented_frames(void);
extern uint64_t tribes_gl4es_take_longest_frame_ns(void);
static NSString * const TBRepositoryURL=@"https://github.com/chrissotraidis/tribespad";
/* Find a Game: the community's own places, checked 22 October 2026. */
static NSString * const TBPlayT2Discord=@"https://discord.gg/EMMVhQJgf9";
static NSString * const TBTheCutDiscord=@"https://discord.gg/yz3AhkJCxg";
static NSString * const TBTribesPadDiscord=@"https://discord.gg/xwHfUD2bxW";
static NSString * const TBTribesNextForum=@"https://www.tribesnext.com/forum/";

/* Touch preferences (NSUserDefaults, TribesPad-owned keys). */
static double tb_setting(NSString *key,double fallback){id v=[NSUserDefaults.standardUserDefaults objectForKey:key];return v?[v doubleValue]:fallback;}
/* Account names and passwords: no automatic capitals or corrections. */
static void tb_start_typing(SDL_Window *window){
    static SDL_PropertiesID props;
    if(!props){props=SDL_CreateProperties();SDL_SetNumberProperty(props,SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER,SDL_CAPITALIZE_NONE);
        SDL_SetBooleanProperty(props,SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN,false);}
    SDL_StartTextInputWithProperties(window,props);
}
static void tb_set(NSString *key,double value){[NSUserDefaults.standardUserDefaults setDouble:value forKey:key];}
#define TB_SIZE tb_setting(@"TribesPad.controlSize",1.0)
#define TB_OPACITY tb_setting(@"TribesPad.controlOpacity",1.0)
#define TB_LOOK tb_setting(@"TribesPad.lookSensitivity",1.0)
#define TB_LEFT_HANDED (tb_setting(@"TribesPad.leftHanded",0)!=0)
#define TB_HIDE_WITH_CONTROLLER (tb_setting(@"TribesPad.hideWithController",1)!=0)
#define TB_SHOW_FPS (tb_setting(@"TribesPad.showFPS",0)!=0)
#define TB_TRACKPAD (tb_setting(@"TribesPad.trackpad",0)!=0)

@class TBTrackpad;

@interface TBGameControls : UIView
@property(nonatomic) TBMobileControls *controls;
@property(nonatomic,strong) NSArray<UIView *> *pads;
@property(nonatomic,strong) UIButton *toggle;
@property(nonatomic,strong) UIButton *menu;
@property(nonatomic,strong) UIButton *keyboard;
@property(nonatomic,strong) UIButton *cursor;
@property(nonatomic,strong) TBTrackpad *trackpad;
@property(nonatomic) BOOL expanded;
@property(nonatomic,strong) GCController *controller;
@property(nonatomic,strong) CADisplayLink *aimClock;
@property(nonatomic) CGPoint stickAim;
@property(nonatomic) CGPoint stickMove;
@property(nonatomic) CGFloat scrollCarry;
@property(nonatomic) CFTimeInterval lastAimTime;
@property(nonatomic) CGRect laidOutArea;
@property(nonatomic,strong) UILabel *fps;
@property(nonatomic,strong) NSTimer *fpsClock;
@property(nonatomic) uint64_t fpsFrames;
@property(nonatomic) BOOL restoreControlsAfterKeyboard;
@property(nonatomic) BOOL inMatch;
@property(nonatomic) BOOL matchSeen;
@property(nonatomic) BOOL typingDialog;
@property(nonatomic) BOOL zoomed;
@property(nonatomic) BOOL gameReported;
@property(nonatomic) BOOL connected;
@property(nonatomic) BOOL controllerUsed;
@property(nonatomic) BOOL mouseConnected;
@property(nonatomic,strong) NSTimer *matchClock;
@property(nonatomic,strong) UILabel *hint;
-(void)rebuildMenu;
-(void)releaseControls;
-(void)toggleControls;
-(void)settingsChanged;
-(void)hideForController;
-(void)showControllerHint;
-(void)pointerMoveTo:(CGPoint)point delta:(CGPoint)delta held:(BOOL)held;
-(void)pointerButton:(BOOL)down at:(CGPoint)point;
-(void)pointerClickAt:(CGPoint)point;
-(void)loginTapAt:(CGPoint)point;
-(void)pointerWheel:(int)steps at:(CGPoint)point;
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
    // Jet gets its own warm color so it reads apart from Fire at a glance.
    if(self.action==TBJet)self.backgroundColor=held?[UIColor colorWithRed:.95 green:.55 blue:.2 alpha:.8]:[UIColor colorWithRed:.45 green:.2 blue:.05 alpha:.55];
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

/* Settings sheet: grouped rows that apply live, so the controls behind the
   sheet show each change as it is made. */
typedef struct {int section;BOOL slider;const char *title,*key,*detail;double low,high;BOOL multiplier;} TBSettingRow;
static const char *tb_sections[][2]={
    {"Touch Controls","Show the touch controls first to see size and opacity change as you drag."},
    {"Trackpad Cursor","With the touch controls hidden, drag anywhere to move the cursor and tap to click. Touch and hold, then drag, to hold the button (sliders, scroll bars). Drag with two fingers to scroll."},
    {"Display",NULL},
    {"Online","On, TribesPad opens to the TribesNEXT login: sign in or create a free account, then JOIN lists internet servers. Off, it opens offline with Training, LAN Game and the server list. Applies the next time TribesPad opens."}};
static const TBSettingRow tb_rows[]={
    {0,YES,"Control Size","TribesPad.controlSize",NULL,.75,1.3,NO},
    {0,YES,"Control Opacity","TribesPad.controlOpacity",NULL,.3,1.0,NO},
    {0,YES,"Look Sensitivity","TribesPad.lookSensitivity",NULL,.4,2.5,YES},
    {0,NO,"Left-Handed Layout","TribesPad.leftHanded","Move on the right, fire and aim on the left.",0,0,NO},
    {0,NO,"Hide with a Controller","TribesPad.hideWithController","Touch controls step aside when a gamepad connects.",0,0,NO},
    {1,NO,"Trackpad Cursor","TribesPad.trackpad","Point and click in menus instead of tapping them directly.",0,0,NO},
    {1,YES,"Cursor Speed","TribesPad.cursorSpeed",NULL,.5,2.5,YES},
    {2,NO,"Keep Clear of the Notch","TribesPad.avoidNotch","Keeps the game clear of the notch and rounded corners. Turn off to fill the screen. Applies the next time TribesPad opens.",0,0,NO},
    {2,NO,"Show FPS Counter","TribesPad.showFPS","Frames each second and the longest frame among them. Also in the menu.",0,0,NO},
    {3,NO,"Online Play","TribesPad.onlinePlay","Sign in with TribesNEXT at launch.",0,0,NO}};
enum {TBRowCount=sizeof tb_rows/sizeof tb_rows[0],TBSectionCount=sizeof tb_sections/sizeof tb_sections[0]};
static void tb_register_defaults(void){
    static dispatch_once_t once;dispatch_once(&once,^{
        BOOL phone=UIDevice.currentDevice.userInterfaceIdiom==UIUserInterfaceIdiomPhone;
        [NSUserDefaults.standardUserDefaults registerDefaults:@{@"TribesPad.controlSize":@1.0,@"TribesPad.controlOpacity":@1.0,
            @"TribesPad.lookSensitivity":@1.0,@"TribesPad.leftHanded":@0,@"TribesPad.hideWithController":@1,
            @"TribesPad.trackpad":@(phone?1:0),@"TribesPad.cursorSpeed":@1.2,@"TribesPad.avoidNotch":@1,@"TribesPad.showFPS":@0,
            @"TribesPad.onlinePlay":@1}];
    });
}
static const TBSettingRow *tb_row(NSIndexPath *path){
    NSInteger n=0;for(unsigned i=0;i<TBRowCount;i++)if(tb_rows[i].section==path.section&&n++==path.row)return &tb_rows[i];return NULL;
}

@interface TBSettingsController : UITableViewController
@property(nonatomic,weak) TBGameControls *owner;
@end
@implementation TBSettingsController
-(void)viewDidLoad {
    [super viewDidLoad];self.title=@"Settings";
    self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(done)];
}
-(void)done{[self dismissViewControllerAnimated:YES completion:nil];}
-(NSInteger)numberOfSectionsInTableView:(UITableView *)tableView{(void)tableView;return TBSectionCount+1;}
-(NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView;if(section==TBSectionCount)return 1;
    NSInteger n=0;for(unsigned i=0;i<TBRowCount;i++)n+=tb_rows[i].section==section;return n;
}
-(NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    (void)tableView;return section<TBSectionCount?@(tb_sections[section][0]):nil;
}
-(NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView;return section<TBSectionCount&&tb_sections[section][1]?@(tb_sections[section][1]):nil;
}
static NSString *tb_slider_text(const TBSettingRow *r,double v){return r->multiplier?[NSString stringWithFormat:@"%.1f×",v]:[NSString stringWithFormat:@"%.0f%%",v*100];}
-(UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView;UITableViewCell *cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.selectionStyle=UITableViewCellSelectionStyleNone;
    if(path.section==TBSectionCount) {
        cell.textLabel.text=@"Reset to Defaults";cell.textLabel.textColor=UIColor.systemRedColor;
        cell.textLabel.textAlignment=NSTextAlignmentCenter;cell.selectionStyle=UITableViewCellSelectionStyleDefault;return cell;
    }
    const TBSettingRow *r=tb_row(path);NSInteger index=r-tb_rows;double v=tb_setting(@(r->key),0);
    if(r->slider) {
        UILabel *title=[UILabel new],*value=[UILabel new];title.text=@(r->title);
        value.text=tb_slider_text(r,v);value.textColor=UIColor.secondaryLabelColor;value.tag=100;
        value.font=[UIFont monospacedDigitSystemFontOfSize:UIFont.labelFontSize weight:UIFontWeightRegular];
        [value setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
        UISlider *slider=[UISlider new];slider.minimumValue=r->low;slider.maximumValue=r->high;
        slider.value=v;slider.tag=index;slider.accessibilityLabel=title.text;
        [slider addTarget:self action:@selector(slid:) forControlEvents:UIControlEventValueChanged];
        UIStackView *row=[[UIStackView alloc] initWithArrangedSubviews:@[title,value]];
        UIStackView *stack=[[UIStackView alloc] initWithArrangedSubviews:@[row,slider]];
        stack.axis=UILayoutConstraintAxisVertical;stack.spacing=6;stack.translatesAutoresizingMaskIntoConstraints=NO;
        [cell.contentView addSubview:stack];UILayoutGuide *m=cell.contentView.layoutMarginsGuide;
        [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:m.leadingAnchor],
            [stack.trailingAnchor constraintEqualToAnchor:m.trailingAnchor],[stack.topAnchor constraintEqualToAnchor:m.topAnchor],
            [stack.bottomAnchor constraintEqualToAnchor:m.bottomAnchor]]];
    } else {
        cell.textLabel.text=@(r->title);cell.detailTextLabel.text=r->detail?@(r->detail):nil;
        cell.detailTextLabel.textColor=UIColor.secondaryLabelColor;cell.detailTextLabel.numberOfLines=0;
        UISwitch *toggle=[UISwitch new];toggle.on=v!=0;toggle.tag=index;
        [toggle addTarget:self action:@selector(switched:) forControlEvents:UIControlEventValueChanged];cell.accessoryView=toggle;
    }
    return cell;
}
-(void)slid:(UISlider *)slider {
    const TBSettingRow *r=&tb_rows[slider.tag];tb_set(@(r->key),slider.value);
    ((UILabel *)[slider.superview viewWithTag:100]).text=tb_slider_text(r,slider.value);[self.owner settingsChanged];
}
-(void)switched:(UISwitch *)toggle {
    const TBSettingRow *r=&tb_rows[toggle.tag];tb_set(@(r->key),toggle.on);[self.owner settingsChanged];
    // Turning this on with a controller already connected steps the controls aside now.
    if(toggle.on&&!strcmp(r->key,"TribesPad.hideWithController"))[self.owner hideForController];
}
-(void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    [tableView deselectRowAtIndexPath:path animated:YES];if(path.section!=TBSectionCount)return;
    for(unsigned i=0;i<TBRowCount;i++)[NSUserDefaults.standardUserDefaults removeObjectForKey:@(tb_rows[i].key)];
    [tableView reloadData];[self.owner settingsChanged];
}
@end

/* Trackpad cursor: shown while the touch controls are hidden. One finger moves
   the cursor relative to where it was, like a laptop trackpad; a tap clicks at
   the cursor; touch and hold, then drag, holds the button down; two fingers
   scroll. Events go through the same SDL queue as a real mouse. */
@interface TBTrackpad : UIView
@property(nonatomic,weak) TBGameControls *owner;
@property(nonatomic) CGPoint cursor;
@property(nonatomic,strong) UIImageView *arrow;
@property(nonatomic,strong) UITouch *finger;
@property(nonatomic) CGPoint last;
@property(nonatomic) CGFloat travel;
@property(nonatomic) CFTimeInterval began;
@property(nonatomic) BOOL dragging;
@property(nonatomic,strong) NSTimer *hold;
@property(nonatomic) CGFloat scrollCarry;
@property(nonatomic) CFTimeInterval lastMove;
-(void)moveBy:(CGPoint)d;
@end

@implementation TBTrackpad
-(instancetype)initWithFrame:(CGRect)frame {
    if(!(self=[super initWithFrame:frame]))return nil;
    self.backgroundColor=UIColor.clearColor;self.multipleTouchEnabled=YES;self.cursor=CGPointMake(-1,-1);
    UIImage *image=[UIImage systemImageNamed:@"cursorarrow" withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:22 weight:UIImageSymbolWeightBold]];
    self.arrow=[[UIImageView alloc] initWithImage:[image imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate]];
    self.arrow.tintColor=UIColor.whiteColor;self.arrow.userInteractionEnabled=NO;
    self.arrow.layer.shadowColor=UIColor.blackColor.CGColor;self.arrow.layer.shadowOpacity=.9;self.arrow.layer.shadowRadius=1.5;self.arrow.layer.shadowOffset=CGSizeZero;
    [self addSubview:self.arrow];
    UIPanGestureRecognizer *scroll=[[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(scrolled:)];
    scroll.minimumNumberOfTouches=2;scroll.maximumNumberOfTouches=2;[self addGestureRecognizer:scroll];
    return self;
}
-(void)layoutSubviews {
    [super layoutSubviews];
    if(self.cursor.x<0||!CGRectContainsPoint(self.bounds,self.cursor))self.cursor=CGPointMake(CGRectGetMidX(self.bounds),CGRectGetMidY(self.bounds));
    [self placeArrow];
}
-(void)moveBy:(CGPoint)d {
    self.hidden=NO;
    CGPoint c=CGPointMake(fmin(fmax(self.cursor.x+d.x,0),self.bounds.size.width-1),fmin(fmax(self.cursor.y+d.y,0),self.bounds.size.height-1));
    CGPoint delta=CGPointMake(c.x-self.cursor.x,c.y-self.cursor.y);if(!delta.x&&!delta.y)return;
    self.cursor=c;[self placeArrow];[self.owner pointerMoveTo:c delta:delta held:NO];
}
-(void)placeArrow{CGSize s=self.arrow.bounds.size;self.arrow.frame=CGRectMake(self.cursor.x-s.width*.28,self.cursor.y-s.height*.12,s.width,s.height);}
-(void)setDragging:(BOOL)dragging{_dragging=dragging;self.arrow.tintColor=dragging?[UIColor colorWithRed:.6 green:.85 blue:1 alpha:1]:UIColor.whiteColor;}
-(void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(self.finger)return;self.finger=touches.anyObject;
    self.last=[self.finger locationInView:self];self.travel=0;self.began=self.lastMove=CACurrentMediaTime();
    __weak TBTrackpad *weak=self;[self.hold invalidate];
    // Hold still for half a second to grab (sliders, scroll bars). A moving
    // finger only ever moves the cursor.
    self.hold=[NSTimer scheduledTimerWithTimeInterval:.5 repeats:NO block:^(NSTimer *t){(void)t;TBTrackpad *pad=weak;
        if(pad.finger&&pad.travel<4){pad.dragging=YES;[pad.owner pointerButton:YES at:pad.cursor];
            [[[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight] impactOccurred];}}];
}
-(void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;if(![touches containsObject:self.finger])return;
    CGPoint p=[self.finger locationInView:self];CGFloat dx=p.x-self.last.x,dy=p.y-self.last.y;self.last=p;
    self.travel+=hypot(dx,dy);if(!self.dragging&&self.travel>=4){[self.hold invalidate];self.hold=nil;}
    // Slow strokes are precise (about 1:1, for small buttons); quick flicks
    // travel up to twice as far. Measured per second so the event rate does
    // not change the feel.
    CFTimeInterval now=CACurrentMediaTime(),dt=fmax(now-self.lastMove,1.0/240);self.lastMove=now;
    CGFloat velocity=hypot(dx,dy)/dt; /* points per second */
    CGFloat speed=tb_setting(@"TribesPad.cursorSpeed",1.2)*(1+fmin(fmax(velocity-150,0)/900,1));
    CGPoint c=CGPointMake(fmin(fmax(self.cursor.x+dx*speed,0),self.bounds.size.width-1),fmin(fmax(self.cursor.y+dy*speed,0),self.bounds.size.height-1));
    CGPoint delta=CGPointMake(c.x-self.cursor.x,c.y-self.cursor.y);self.cursor=c;[self placeArrow];
    [self.owner pointerMoveTo:c delta:delta held:self.dragging];
}
-(void)finish:(BOOL)click {
    [self.hold invalidate];self.hold=nil;
    if(self.dragging){self.dragging=NO;[self.owner pointerButton:NO at:self.cursor];}
    else if(click&&self.travel<8&&CACurrentMediaTime()-self.began<.35)[self.owner pointerClickAt:self.cursor];
    self.finger=nil;
}
-(void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event{(void)event;if([touches containsObject:self.finger])[self finish:YES];}
-(void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event{(void)event;if([touches containsObject:self.finger])[self finish:NO];}
-(void)scrolled:(UIPanGestureRecognizer *)pan {
    if(pan.state==UIGestureRecognizerStateBegan)self.scrollCarry=0;
    CGFloat dy=[pan translationInView:self].y;[pan setTranslation:CGPointZero inView:self];
    self.scrollCarry+=dy;int steps=(int)(self.scrollCarry/18);
    if(steps){self.scrollCarry-=steps*18;[self.owner pointerWheel:steps at:self.cursor];}
}
@end

@implementation TBGameControls
-(instancetype)initWithFrame:(CGRect)frame {
    if(!(self=[super initWithFrame:frame]))return nil;
    tb_register_defaults();
    self.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    self.multipleTouchEnabled=YES;
    // Below everything else, so the buttons stay tappable over it.
    self.trackpad=[[TBTrackpad alloc] initWithFrame:self.bounds];self.trackpad.owner=self;[self addSubview:self.trackpad];
    self.keyboard=tb_round_button(@"keyboard",@"Keyboard",self,@selector(toggleKeyboard));
    self.toggle=tb_round_button(@"gamecontroller.fill",@"Touch controls",self,@selector(toggleControls));
    self.cursor=tb_round_button(@"cursorarrow.rays",@"Trackpad cursor",self,@selector(toggleTrackpad));
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
        {TBKindButton,TBJet,"flame.fill","arrow.up","Jet",YES},
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
    for(UIButton *button in @[self.keyboard,self.toggle,self.cursor,self.menu])[self addSubview:button];
    [self addSubview:self.fps];[self rebuildMenu];[self applyFPSSetting];
    self.hint=[UILabel new];self.hint.numberOfLines=0;self.hint.textAlignment=NSTextAlignmentCenter;
    self.hint.font=[UIFont systemFontOfSize:14 weight:UIFontWeightSemibold];self.hint.textColor=UIColor.whiteColor;
    self.hint.backgroundColor=[UIColor colorWithWhite:0 alpha:.65];self.hint.layer.cornerRadius=12;self.hint.layer.masksToBounds=YES;
    self.hint.userInteractionEnabled=NO;self.hint.alpha=0;[self addSubview:self.hint];
    // The game captures the mouse while you play and frees it in menus,
    // inventory and the score screen. Follow it: touch controls in play, the
    // trackpad cursor everywhere else.
    __weak TBGameControls *weakSelf=self;
    // A previous session may have ended in play; start from the menus. "-"
    // means the game has not reported yet: it reports once past the login.
    [@"-" writeToFile:[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject
        stringByAppendingPathComponent:@"TribesPad/Game/base/prefs/tribespad-state.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    self.matchClock=[NSTimer scheduledTimerWithTimeInterval:.2 repeats:YES block:^(NSTimer *t){(void)t;[weakSelf followMatch];}];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(releaseControls)
        name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(keyboardFrameChanged:)
        name:UIKeyboardDidChangeFrameNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(keyboardHidden:)
        name:UIKeyboardDidHideNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(controllerChanged:)
        name:GCControllerDidConnectNotification object:nil];
    // Throttling is the usual cause of a game slowing after a while; log it.
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(powerChanged:)
        name:NSProcessInfoThermalStateDidChangeNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(powerChanged:)
        name:NSProcessInfoPowerStateDidChangeNotification object:nil];
    [self powerChanged:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(mouseChanged:)
        name:GCMouseDidConnectNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(mouseChanged:)
        name:GCMouseDidDisconnectNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(controllerChanged:)
        name:GCControllerDidDisconnectNotification object:nil];
    return self;
}
-(void)didMoveToSuperview {
    [super didMoveToSuperview];
    if(self.superview){
        [self controllerChanged:nil];[self mouseChanged:nil];
        // Taps on the game itself pass through to SDL; watch them for the login.
        UITapGestureRecognizer *tap=[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(gameTapped:)];
        tap.cancelsTouchesInView=NO;tap.delaysTouchesEnded=NO;[self.superview addGestureRecognizer:tap];
    }
    else [self detachController];
}
-(void)detachController {
    self.controllerUsed=NO;
    GCExtendedGamepad *pad=self.controller.extendedGamepad;
    pad.valueChangedHandler=nil;
    if(pad)for(GCControllerButtonInput *b in @[pad.buttonA,pad.buttonB,pad.buttonX,pad.buttonY,pad.leftShoulder,pad.rightShoulder,pad.buttonMenu,
        pad.dpad.up,pad.dpad.down,pad.dpad.left,pad.dpad.right])b.pressedChangedHandler=nil;
    pad.buttonOptions.pressedChangedHandler=nil;pad.leftThumbstickButton.pressedChangedHandler=nil;pad.rightThumbstickButton.pressedChangedHandler=nil;
    self.stickMove=CGPointZero;
    [self.aimClock invalidate];self.aimClock=nil;self.controller=nil;
    self.stickAim=CGPointZero;self.lastAimTime=0;
    if(self.controls){tb_input_release(&self.controls->input,9);tb_mobile_controls_emit(self.controls);}
}
/* A hardware mouse or trackpad (Magic Keyboard, USB, Bluetooth): the game
   uses it directly through SDL, which locks the pointer in play for mouse
   look. The touch controls step aside, as they do for a controller, and come
   back from the controller button or when it disconnects. */
-(void)mouseChanged:(NSNotification *)notification {
    (void)notification;BOOL connected=GCMouse.mice.count>0;if(connected==self.mouseConnected)return;
    self.mouseConnected=connected;
    if(connected&&self.expanded)[self toggleControls];
    else if(!connected&&self.inMatch&&!self.expanded&&!(self.controllerUsed&&TB_HIDE_WITH_CONTROLLER))[self toggleControls];
    [self setNeedsLayout];
}
-(void)controllerChanged:(NSNotification *)notification {
    (void)notification;if(!self.superview)return;
    if(self.controller&&[GCController.controllers containsObject:self.controller])return;
    [self detachController];
    for(GCController *controller in GCController.controllers)if(controller.extendedGamepad){
        self.controller=controller;controller.handlerQueue=dispatch_get_main_queue();break;
    }
    [self rebuildMenu];
    if(!self.controller)return;
    GCExtendedGamepad *pad=self.controller.extendedGamepad;
    __weak TBGameControls *weakSelf=self;
    /* Playing: sticks, triggers and A are held actions through the input
       layer; the other buttons hold their original keys while pressed.
       Menus: the left stick or D-pad moves the cursor, A clicks (hold to
       drag), B and Menu go back, the right stick scrolls. */
    pad.valueChangedHandler=^(GCExtendedGamepad *p,GCControllerElement *element){
        (void)element;TBGameControls *owner=weakSelf;
        if(!owner.controls||p!=owner.controller.extendedGamepad||UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
        if(!owner.controllerUsed&&(fabsf(p.leftThumbstick.xAxis.value)>.3f||fabsf(p.leftThumbstick.yAxis.value)>.3f||
            fabsf(p.rightThumbstick.xAxis.value)>.3f||fabsf(p.rightThumbstick.yAxis.value)>.3f||p.buttonA.pressed||
            p.rightTrigger.pressed||p.leftTrigger.pressed||p.dpad.xAxis.value||p.dpad.yAxis.value)){
            owner.controllerUsed=YES;[owner hideForController];[owner setNeedsLayout];
            if(owner.inMatch)[owner showControllerHint];}
        owner.stickAim=CGPointMake(p.rightThumbstick.xAxis.value,p.rightThumbstick.yAxis.value);
        owner.stickMove=CGPointMake(p.leftThumbstick.xAxis.value+p.dpad.xAxis.value,p.leftThumbstick.yAxis.value+p.dpad.yAxis.value);
        if(!owner.inMatch)return;
        tb_input_move(&owner.controls->input,9,p.leftThumbstick.xAxis.value,p.leftThumbstick.yAxis.value,.15);
        const uint32_t actions[]={TBFire,TBJet,TBJump};
        NSArray<GCControllerButtonInput *> *held=@[p.rightTrigger,p.leftTrigger,p.buttonA];
        for(unsigned i=0;i<held.count;i++)tb_input_action(&owner.controls->input,9,actions[i],held[i].pressed);
        tb_mobile_controls_emit(owner.controls);
    };
    // Buttons that hold an original key (controlDefaults.cs) while pressed.
    void (^key)(GCControllerButtonInput *,SDL_Scancode,SDL_Keymod)=^(GCControllerButtonInput *button,SDL_Scancode code,SDL_Keymod mod){
        if(!button)return;
        button.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;TBGameControls *owner=weakSelf;
            if(!owner.controls||UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
            if(pressed&&!owner.inMatch)return;
            if(mod&SDL_KMOD_SHIFT)[owner pushKey:SDL_SCANCODE_LSHIFT down:pressed mod:pressed?mod:SDL_KMOD_NONE];
            [owner pushKey:code down:pressed mod:pressed?mod:SDL_KMOD_NONE];};
    };
    key(pad.rightShoulder,SDL_SCANCODE_W,SDL_KMOD_NONE);          // next weapon
    key(pad.leftShoulder,SDL_SCANCODE_W,SDL_KMOD_LSHIFT);         // previous weapon
    key(pad.buttonX,SDL_SCANCODE_R,SDL_KMOD_NONE);                // use pack
    key(pad.buttonY,SDL_SCANCODE_G,SDL_KMOD_NONE);                // grenade
    // Zoom toggles: click once to zoom (holding the game's zoom key), again to
    // return, so a sniper shot does not need the stick held in.
    pad.rightThumbstickButton.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;
        TBGameControls *owner=weakSelf;
        if(!pressed||!owner.controls||!owner.inMatch||UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
        owner.zoomed=!owner.zoomed;[owner pushKey:SDL_SCANCODE_A down:owner.zoomed mod:SDL_KMOD_NONE];};
    key(pad.leftThumbstickButton,SDL_SCANCODE_TAB,SDL_KMOD_NONE); // third person
    key(pad.dpad.up,SDL_SCANCODE_Q,SDL_KMOD_NONE);                // repair kit
    key(pad.dpad.down,SDL_SCANCODE_KP_ENTER,SDL_KMOD_NONE);       // inventory
    key(pad.dpad.left,SDL_SCANCODE_H,SDL_KMOD_NONE);              // beacon
    key(pad.dpad.right,SDL_SCANCODE_V,SDL_KMOD_NONE);             // quick chat
    key(pad.buttonOptions,SDL_SCANCODE_F2,SDL_KMOD_NONE);         // scores
    pad.buttonA.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;TBGameControls *owner=weakSelf;
        if(owner.inMatch||!owner.controls)return;
        [owner pointerButton:pressed at:owner.trackpad.cursor];};
    pad.buttonB.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;TBGameControls *owner=weakSelf;
        if(!pressed||!owner.controls)return;
        if(owner.inMatch)[owner pushKey:SDL_SCANCODE_B down:YES mod:SDL_KMOD_NONE],[owner pushKey:SDL_SCANCODE_B down:NO mod:SDL_KMOD_NONE];
        else [owner tapKey:SDL_SCANCODE_ESCAPE];};
    pad.buttonMenu.pressedChangedHandler=^(GCControllerButtonInput *button,float value,BOOL pressed){
        (void)value;(void)button;TBGameControls *owner=weakSelf;
        if(pressed&&UIApplication.sharedApplication.applicationState==UIApplicationStateActive)[owner openMenu];
    };
    self.aimClock=[CADisplayLink displayLinkWithTarget:self selector:@selector(aimTick:)];
    [self.aimClock addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
-(void)aimTick:(CADisplayLink *)clock {
    CFTimeInterval previous=self.lastAimTime;self.lastAimTime=clock.timestamp;
    if(!previous||UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
    double dt=fmin(clock.timestamp-previous,.05);
    if(self.inMatch){
        tb_mobile_controls_gamepad_look(self.controls,self.stickAim.x,self.stickAim.y,dt,(float)(520*TB_LOOK));
        return;
    }
    // Menu cursor: same dead zone and curve as aiming, up to 900 points a second.
    CGPoint s=self.stickMove;CGFloat m=hypot(s.x,s.y);
    if(m>.14){CGFloat n=fmin(1,(m-.14)/.86),v=(120+780*n*n)*dt/m;[self.trackpad moveBy:CGPointMake(s.x*v,-s.y*v)];}
    CGFloat r=self.stickAim.y;
    if(fabs(r)>.4){self.scrollCarry+=r*dt*12;int steps=(int)self.scrollCarry;if(steps){self.scrollCarry-=steps;[self pointerWheel:steps at:self.trackpad.cursor];}}
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
        if(!self.trackpad.hidden){x=(float)self.trackpad.cursor.x;y=(float)self.trackpad.cursor.y;}
        SDL_Rect caret={(int)x,(int)y,1,24};
        SDL_SetTextInputArea(window,&caret,0);
        SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME,"1");
        tb_start_typing(window);
    }
}
-(void)keyboardFrameChanged:(NSNotification *)notification {
    (void)notification;[self setNeedsLayout];
}
/* Chat hides the touch controls while typing; bring them back once the
   message is sent or the keyboard is dismissed. */
-(void)keyboardHidden:(NSNotification *)notification {
    (void)notification;if(!self.restoreControlsAfterKeyboard)return;
    self.restoreControlsAfterKeyboard=NO;if(!self.expanded)[self toggleControls];
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
-(void)settingsChanged {
    self.laidOutArea=CGRectNull;[self applyFPSSetting];[self rebuildMenu];[self setNeedsLayout];
}
-(void)toggleTrackpad{tb_set(@"TribesPad.trackpad",!TB_TRACKPAD);[self settingsChanged];}
-(void)toggleFPS{tb_set(@"TribesPad.showFPS",!TB_SHOW_FPS);[self settingsChanged];}
-(void)powerChanged:(NSNotification *)notification {
    (void)notification;NSProcessInfo *p=NSProcessInfo.processInfo;
    static const char *states[]={"nominal","fair","serious","critical"};NSInteger s=p.thermalState;
    fprintf(stderr,"TribesPad thermal state: %s, Low Power Mode %s\n",s>=0&&s<4?states[s]:"unknown",p.lowPowerModeEnabled?"on":"off");
}
-(void)followMatch {
    if(!self.controls||UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
    // base/autoexec.cs reports "<playing> <typing> <connected>": playing is 1 in play and
    // 0 in menus, the lobby, inventory and the score screen; typing is 1 while
    // a dialog made for typing (login, server password...) is open; connected
    // is 1 on a server (including one hosted here).
    NSString *support=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject
        stringByAppendingPathComponent:@"TribesPad/Game/base/prefs/tribespad-state.txt"];
    NSString *state=[NSString stringWithContentsOfFile:support encoding:NSUTF8StringEncoding error:nil];
    if(!state.length||[state hasPrefix:@"-"])return;
    if(!self.gameReported){
        // Past the login: a keyboard opened there closes with the next report.
        self.gameReported=YES;SDL_Window *window=SDL_GetWindowFromID(self.controls->window);
        self.typingDialog=window&&SDL_TextInputActive(window);
        [self rebuildMenu];
        // New players rarely know where the games are; point at Find a Game.
        double shown=tb_setting(@"TribesPad.findHintsShown",0);
        if(shown<3){tb_set(@"TribesPad.findHintsShown",shown+1);
            [self flashHint:@"Looking for a game?  ··· > Find a Game has the server list,\nwho is playing now, game nights and the community Discords"];}
    }
    BOOL playing=[state hasPrefix:@"1"],typing=[state hasPrefix:@"0 1"];
    NSArray *words=[state componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    BOOL connected=words.count>2&&[words[2] isEqualToString:@"1"];
    if(connected!=self.connected){self.connected=connected;[self rebuildMenu];}
    if(typing!=self.typingDialog){
        // Open the keyboard with the dialog; close it when the dialog goes.
        self.typingDialog=typing;SDL_Window *window=SDL_GetWindowFromID(self.controls->window);
        if(window&&typing!=SDL_TextInputActive(window))[self toggleKeyboard];
    }
    if(playing==self.inMatch)return;
    self.inMatch=playing;if(playing)self.matchSeen=YES;
    if(self.zoomed){self.zoomed=NO;[self pushKey:SDL_SCANCODE_A down:NO mod:SDL_KMOD_NONE];}
    if(self.controls){tb_input_release(&self.controls->input,9);tb_mobile_controls_emit(self.controls);}
    if(playing){
        if(!self.expanded&&!(self.controllerUsed&&TB_HIDE_WITH_CONTROLLER)&&!self.mouseConnected)[self toggleControls];
        if(self.controllerUsed)[self showControllerHint];
        else if(self.expanded)[self showHint];
    } else if(self.expanded&&!self.restoreControlsAfterKeyboard)[self toggleControls];
    [self rebuildMenu];[self setNeedsLayout];
}
/* The first few times the controls appear, say what the main buttons do. */
-(void)showHint {
    double shown=tb_setting(@"TribesPad.hintsShown",0);if(shown>=3)return;tb_set(@"TribesPad.hintsShown",shown+1);
    [self flashHint:@"Hold JET to fly  ·  Hold SKI / JUMP on slopes to keep speed\nDrag the right side to aim  ·  ··· has chat, scores and the game menu"];
}
-(void)flashHint:(NSString *)text {
    self.hint.text=text;
    [self setNeedsLayout];[self layoutIfNeeded];
    [UIView animateWithDuration:.3 animations:^{self.hint.alpha=1;}];
    [UIView animateWithDuration:.6 delay:7 options:0 animations:^{self.hint.alpha=0;} completion:nil];
}
/* The first few times play starts with a controller, show its layout without
   blocking play. The full guide is in ··· > Help > Controller Guide. */
-(void)showControllerHint {
    double shown=tb_setting(@"TribesPad.controllerHintsShown",0);if(shown>=3)return;tb_set(@"TribesPad.controllerHintsShown",shown+1);
    self.hint.text=@"RT fire  ·  LT jet  ·  A ski / jump  ·  RB / LB next / previous weapon\n"
        "Y grenade  ·  X use pack  ·  B mine  ·  D-pad: up repair kit, down inventory, left beacon, right quick chat\n"
        "Watching as an observer? Press RT to join a team  ·  Menu: game menu  ·  View: scores";
    [self.hint.layer removeAllAnimations];[self setNeedsLayout];[self layoutIfNeeded];
    [UIView animateWithDuration:.3 animations:^{self.hint.alpha=1;}];
    [UIView animateWithDuration:.6 delay:12 options:UIViewAnimationOptionAllowUserInteraction animations:^{self.hint.alpha=0;} completion:nil];
}
/* Pointer events from the trackpad cursor, in window points like SDL's own. */
-(void)pointerMoveTo:(CGPoint)point delta:(CGPoint)delta held:(BOOL)held {
    SDL_Event e={0};e.type=SDL_EVENT_MOUSE_MOTION;e.common.timestamp=SDL_GetTicksNS();e.motion.windowID=self.controls->window;
    e.motion.x=(float)point.x;e.motion.y=(float)point.y;e.motion.xrel=(float)delta.x;e.motion.yrel=(float)delta.y;
    e.motion.state=held?SDL_BUTTON_LMASK:0;SDL_PushEvent(&e);
}
-(void)pointerButton:(BOOL)down at:(CGPoint)point {
    [self pointerMoveTo:point delta:CGPointZero held:!down];
    SDL_Event e={0};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;e.common.timestamp=SDL_GetTicksNS();
    e.button.windowID=self.controls->window;e.button.button=SDL_BUTTON_LEFT;e.button.down=down;e.button.clicks=1;
    e.button.x=(float)point.x;e.button.y=(float)point.y;SDL_PushEvent(&e);
}
/* The original GUI ignores a press and release within one frame, so hold the
   button briefly, as a finger on a real mouse does. */
-(void)gameTapped:(UITapGestureRecognizer *)tap {
    if(tap.state==UIGestureRecognizerStateEnded)[self loginTapAt:[tap locationInView:self]];
}
/* The TribesNEXT login comes before the game reports its state, so a tap
   on its Account or Password field opens the keyboard, keeping that field
   and the one below it in view above it. The original 640x480 layout,
   centered: the fields end 41 units right of center, the buttons start at
   61; the fields sit in the lower half. */
-(void)loginTapAt:(CGPoint)point {
    if(self.gameReported||tb_setting(@"TribesPad.onlinePlay",1)==0||!self.controls)return;
    CGFloat unit=self.bounds.size.height/480;
    CGFloat x=(point.x-CGRectGetMidX(self.bounds))/unit,y=(point.y-CGRectGetMidY(self.bounds))/unit;
    if(x>51||x<-120||y<0)return;
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);if(!window)return;
    SDL_Rect caret={(int)point.x,(int)point.y-12,1,72};SDL_SetTextInputArea(window,&caret,0);
    if(!SDL_TextInputActive(window)){SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME,"1");tb_start_typing(window);}
}
-(void)pointerClickAt:(CGPoint)point {
    [self loginTapAt:point];
    [self pointerButton:YES at:point];__weak TBGameControls *weak=self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(.08*NSEC_PER_SEC)),dispatch_get_main_queue(),^{[weak pointerButton:NO at:point];});
}
-(void)pointerWheel:(int)steps at:(CGPoint)point {
    SDL_Event e={0};e.type=SDL_EVENT_MOUSE_WHEEL;e.common.timestamp=SDL_GetTicksNS();e.wheel.windowID=self.controls->window;
    e.wheel.y=(float)steps;e.wheel.integer_y=steps;e.wheel.direction=SDL_MOUSEWHEEL_NORMAL;
    e.wheel.mouse_x=(float)point.x;e.wheel.mouse_y=(float)point.y;SDL_PushEvent(&e);
}
-(void)hideForController{if(self.controllerUsed&&TB_HIDE_WITH_CONTROLLER&&self.expanded)[self toggleControls];}
/* The menu holds what the original menus cannot reach by touch: chat, scores
   and the in-match keys that have no touch button, then TribesPad's own
   controls, then help. Everything the game's own menus do stays there. */
-(void)rebuildMenu {
    __weak TBGameControls *weak=self;
    UIImage *(^icon)(NSString *)=^UIImage *(NSString *name){return [UIImage systemImageNamed:name];};
    UIAction *(^item)(NSString *,NSString *,void (^)(void))=^UIAction *(NSString *title,NSString *symbol,void (^handler)(void)){
        return [UIAction actionWithTitle:title image:icon(symbol) identifier:nil handler:^(__kindof UIAction *a){(void)a;handler();}];};
    // Keys from the original controlDefaults.cs; each is a tap, as on a keyboard.
    UIAction *(^key)(NSString *,NSString *,SDL_Scancode,SDL_Keymod)=^UIAction *(NSString *title,NSString *symbol,SDL_Scancode code,SDL_Keymod mod){
        return item(title,symbol,^{[weak sendGameKey:code mod:mod];});};
    UIMenu *(^group)(NSArray *)=^UIMenu *(NSArray *children){
        return [UIMenu menuWithTitle:@"" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:children];};
    UIAction *suicide=key(@"Suicide",@"xmark.octagon",SDL_SCANCODE_K,SDL_KMOD_LCTRL);suicide.attributes=UIMenuElementAttributesDestructive;
    NSArray *game=@[
        // Escape opens the game's lobby screen and closes it again.
        self.inMatch?item(@"Game Menu",@"list.bullet",^{[weak openMenu];}):item(@"Back to Game",@"arrow.uturn.backward",^{[weak openMenu];}),
        key(@"Scores",@"trophy",SDL_SCANCODE_F2,SDL_KMOD_NONE),
        [UIMenu menuWithTitle:@"Chat" image:icon(@"bubble.left.and.bubble.right") identifier:nil options:0 children:@[
            item(@"All Chat",@"bubble.left.and.bubble.right",^{[weak openChat:SDL_SCANCODE_U];}),
            item(@"Team Chat",@"bubble.left",^{[weak openChat:SDL_SCANCODE_Y];}),
            item(@"Quick Chat Menu",@"text.bubble",^{[weak openChat:SDL_SCANCODE_V];})]],
        [UIMenu menuWithTitle:@"Actions" image:icon(@"bolt") identifier:nil options:0 children:@[
            group(@[key(@"Use Repair Kit",@"cross.case",SDL_SCANCODE_Q,SDL_KMOD_NONE),
                key(@"Place Mine",@"smallcircle.filled.circle",SDL_SCANCODE_B,SDL_KMOD_NONE),
                key(@"Place Beacon",@"dot.radiowaves.left.and.right",SDL_SCANCODE_H,SDL_KMOD_NONE),
                key(@"Targeting Laser",@"scope",SDL_SCANCODE_L,SDL_KMOD_NONE),
                key(@"Throw Flag",@"flag",SDL_SCANCODE_F,SDL_KMOD_LCTRL)]),
            group(@[key(@"Zoom",@"plus.magnifyingglass",SDL_SCANCODE_A,SDL_KMOD_NONE),
                key(@"Third-Person View",@"person.crop.square",SDL_SCANCODE_TAB,SDL_KMOD_NONE),
                key(@"Command Map",@"map",SDL_SCANCODE_C,SDL_KMOD_NONE)]),
            group(@[suicide])]]];
    UIAction *trackpad=item(@"Trackpad Cursor",@"cursorarrow.rays",^{[weak toggleTrackpad];});
    trackpad.state=TB_TRACKPAD?UIMenuElementStateOn:UIMenuElementStateOff;
    UIAction *fps=item(@"FPS Counter",@"speedometer",^{[weak toggleFPS];});
    fps.state=TB_SHOW_FPS?UIMenuElementStateOn:UIMenuElementStateOff;
    NSArray *controls=@[
        item(self.expanded?@"Hide Touch Controls":@"Show Touch Controls",self.expanded?@"xmark.circle":@"gamecontroller",^{[weak toggleControls];}),
        trackpad,fps,
        item(@"Settings…",@"gearshape",^{[weak showSettings];})];
    NSString *pad=self.controller.vendorName;
    UIMenu *help=[UIMenu menuWithTitle:@"Help" image:icon(@"questionmark.circle") identifier:nil options:0 children:@[
        group(@[item(@"Touch and Trackpad Guide",@"hand.tap",^{[weak showTouchGuide];}),
            item(pad.length?[NSString stringWithFormat:@"Controller: %@",pad]:@"Controller Guide",@"gamecontroller.fill",^{[weak showControllerGuide];})]),
        group(@[item(@"Console",@"terminal",^{[weak openConsole];}),
            item(@"TribesPad on GitHub",@"safari",^{
                [UIApplication.sharedApplication openURL:[NSURL URLWithString:TBRepositoryURL] options:@{} completionHandler:nil];}),
            item(@"About TribesPad",@"info.circle",^{[weak showAbout];})])]];
    /* Find a Game: what new players lack most. The original JOIN list, who is
       on now, when the community plays and where it organizes. It leads the
       menu off a server; on one, the match actions lead and it folds away. */
    NSMutableArray *find=[NSMutableArray array];
    if(!self.connected)[find addObject:item(@"Server List",@"server.rack",^{[weak openServerList];})];
    [find addObjectsFromArray:@[item(@"Who's Playing Now",@"person.3",^{[weak showLiveServers];}),
        item(@"Game Nights",@"calendar",^{[weak showSchedule];}),
        [UIMenu menuWithTitle:@"Community" image:icon(@"bubble.left.and.text.bubble.right") identifier:nil options:0 children:@[
            group(@[item(@"PlayT2 Discord",@"person.2",^{[weak openLink:TBPlayT2Discord];}),
                item(@"The Cut Discord",@"person.2",^{[weak openLink:TBTheCutDiscord];}),
                item(@"TribesPad Discord",@"person.2",^{[weak openLink:TBTribesPadDiscord];})]),
            group(@[item(@"TribesNEXT Forum",@"text.bubble",^{[weak openPage:TBTribesNextForum];})])]]]];
    UIMenu *findMenu=[UIMenu menuWithTitle:@"Find a Game" image:icon(@"globe") identifier:nil
        options:self.connected?0:UIMenuOptionsDisplayInline children:find];
    // Game actions only do something on a server; the original menus have their own buttons.
    NSArray *groups=self.connected||self.inMatch?@[group(game),group(@[findMenu]),group(controls),group(@[help])]:@[findMenu,group(controls),group(@[help])];
    self.menu.menu=[UIMenu menuWithTitle:@"TribesPad" children:groups];
    // The column button shows whether the cursor is on.
    UIButtonConfiguration *c=self.cursor.configuration;
    c.baseBackgroundColor=TB_TRACKPAD?[UIColor colorWithRed:.12 green:.32 blue:.43 alpha:.85]:[UIColor colorWithWhite:.06 alpha:.72];
    self.cursor.configuration=c;self.cursor.accessibilityValue=TB_TRACKPAD?@"On":@"Off";
}
-(void)applyFPSSetting {
    [self.fpsClock invalidate];self.fpsClock=nil;self.fps.hidden=!TB_SHOW_FPS;
    if(!TB_SHOW_FPS)return;
    self.fpsFrames=tribes_gl4es_presented_frames();tribes_gl4es_take_longest_frame_ns();self.fps.text=@"– FPS";
    __weak TBGameControls *weak=self;
    self.fpsClock=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer){(void)timer;
        TBGameControls *owner=weak;if(!owner)return;uint64_t now=tribes_gl4es_presented_frames();
        uint64_t frames=now-owner.fpsFrames;double longest=tribes_gl4es_take_longest_frame_ns()/1e6;owner.fpsFrames=now;
        // Frames per second, then the longest frame: a hitch hides in the average.
        BOOL hot=NSProcessInfo.processInfo.thermalState>=NSProcessInfoThermalStateSerious;
        owner.fps.text=frames?[NSString stringWithFormat:@"%llu FPS · %.0f ms%@",(unsigned long long)frames,longest,hot?@" · Hot":@""]:@"0 FPS";
        owner.fps.textColor=frames>=30&&longest<50&&!hot?UIColor.greenColor:UIColor.orangeColor;}];
}
-(void)tapKey:(SDL_Scancode)scancode {[self tapKey:scancode mod:SDL_KMOD_NONE];}
-(void)tapKey:(SDL_Scancode)scancode mod:(SDL_Keymod)mod {
    SDL_Scancode modifier=mod&SDL_KMOD_CTRL?SDL_SCANCODE_LCTRL:SDL_SCANCODE_UNKNOWN;
    if(modifier)[self pushKey:modifier down:YES mod:mod];
    [self pushKey:scancode down:YES mod:mod];[self pushKey:scancode down:NO mod:mod];
    if(modifier)[self pushKey:modifier down:NO mod:SDL_KMOD_NONE];
}
-(void)pushKey:(SDL_Scancode)scancode down:(BOOL)down mod:(SDL_Keymod)mod {
    SDL_Event event={0};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
    event.key.windowID=self.controls->window;event.key.scancode=scancode;event.key.mod=mod;
    event.key.key=SDL_GetKeyFromScancode(scancode,SDL_KMOD_NONE,false);event.key.down=down;SDL_PushEvent(&event);
}
/* A menu key releases held touches first so a held Fire or Jet does not stick. */
-(void)sendGameKey:(SDL_Scancode)scancode mod:(SDL_Keymod)mod {
    [self releaseControls];[self tapKey:scancode mod:mod];
}
-(void)openChat:(SDL_Scancode)scancode {
    self.restoreControlsAfterKeyboard=self.expanded;
    if(self.expanded)[self toggleControls];else [self releaseControls];
    [self tapKey:scancode];
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);
    if(window&&!SDL_TextInputActive(window)){SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME,"1");tb_start_typing(window);}
}
-(UIViewController *)presenter {
    UIViewController *presenter=self.window.rootViewController;
    while(presenter.presentedViewController)presenter=presenter.presentedViewController;return presenter;
}
-(void)showSettings {
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);if(window)SDL_StopTextInput(window);
    [self releaseControls];
    TBSettingsController *settings=[[TBSettingsController alloc] initWithStyle:UITableViewStyleInsetGrouped];settings.owner=self;
    UINavigationController *sheet=[[UINavigationController alloc] initWithRootViewController:settings];
    sheet.modalPresentationStyle=UIModalPresentationFormSheet;sheet.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;
    [[self presenter] presentViewController:sheet animated:YES completion:nil];
}
-(void)showMessage:(NSString *)title text:(NSString *)text {
    [self releaseControls];
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:title message:text preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    alert.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[[self presenter] presentViewController:alert animated:YES completion:nil];
}
/* Requests to the game (base/autoexec.cs) go through prefs/tribespad-request.txt,
   "<number> <request>"; each new number is acted on once. Server List asks for
   the original JOIN pane. */
-(void)openServerList {
    if(!self.gameReported){
        [self showMessage:@"Server List" text:@"Sign in to TribesNEXT first. Once you are past the login, this opens PLAY GAME > JOIN, the list of internet servers."];return;
    }
    [self sendRequest:@"servers"];
}
-(void)sendRequest:(NSString *)request {
    NSString *path=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject
        stringByAppendingPathComponent:@"TribesPad/Game/base/prefs/tribespad-request.txt"];
    [[NSString stringWithFormat:@"%.0f %@\n",NSDate.date.timeIntervalSince1970*1000,request] writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
}
/* Playing Now: people and bots per server, live; tap one to join it. */
-(void)showLiveServers {
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);if(window)SDL_StopTextInput(window);
    [self releaseControls];
    __weak TBGameControls *weak=self;
    TBLiveServersController *live=[[TBLiveServersController alloc] initWithStyle:UITableViewStyleInsetGrouped];
    if(self.gameReported&&!self.connected)live.join=^(NSString *address){[weak sendRequest:[@"join " stringByAppendingString:address]];};
    live.serverList=^{[weak openServerList];};
    UINavigationController *sheet=[[UINavigationController alloc] initWithRootViewController:live];
    sheet.modalPresentationStyle=UIModalPresentationFormSheet;sheet.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;
    [[self presenter] presentViewController:sheet animated:YES completion:nil];
}
/* Web pages open in a sheet over the game, so a match keeps running. */
-(void)openPage:(NSString *)url {
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);if(window)SDL_StopTextInput(window);
    [self releaseControls];
    SFSafariViewController *page=[[SFSafariViewController alloc] initWithURL:[NSURL URLWithString:url]];
    page.modalPresentationStyle=UIModalPresentationPageSheet;page.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;
    [[self presenter] presentViewController:page animated:YES completion:nil];
}
/* Discord links go to the Discord app (or Safari without it). */
-(void)openLink:(NSString *)url {
    [self releaseControls];
    [UIApplication.sharedApplication openURL:[NSURL URLWithString:url] options:@{} completionHandler:nil];
}
/* The community's regular games (tribes2forever.com, 22 October 2026), in
   the player's own time zone, soonest first. */
-(void)showSchedule {
    [self releaseControls];
    NSCalendar *eastern=[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    eastern.timeZone=[NSTimeZone timeZoneWithName:@"America/New_York"];
    NSDateFormatter *format=[NSDateFormatter new];[format setLocalizedDateFormatFromTemplate:@"EEEEjmm"];
    struct {NSInteger weekday,hour,minute;NSString *what;} games[]={
        {4,21,0,@"Weekly pickup game"},{7,21,30,@"Weekly pickup game, the big one"},{1,21,30,@"CTF public game (every other week)"}};
    NSMutableArray<NSArray *> *next=[NSMutableArray array];
    for(unsigned i=0;i<sizeof games/sizeof games[0];i++){
        NSDateComponents *when=[NSDateComponents new];when.weekday=games[i].weekday;when.hour=games[i].hour;when.minute=games[i].minute;
        NSDate *date=[eastern nextDateAfterDate:NSDate.date matchingComponents:when options:NSCalendarMatchNextTime];
        if(date)[next addObject:@[date,games[i].what]];
    }
    [next sortUsingComparator:^NSComparisonResult(NSArray *a,NSArray *b){return [a[0] compare:b[0]];}];
    NSMutableString *text=[NSMutableString string];
    for(NSArray *game in next)[text appendFormat:@"%@\n%@\n\n",[format stringFromDate:game[0]],game[1]];
    [text appendString:@"Times are in your time zone (9:00 and 9:30 PM US Eastern). At other hours most public servers are empty or hold only bots. "
        "Sign-ups and pickup games are posted on the PlayT2 and The Cut Discords."];
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"Game Nights" message:text preferredStyle:UIAlertControllerStyleAlert];
    __weak TBGameControls *weak=self;
    [alert addAction:[UIAlertAction actionWithTitle:@"PlayT2 Discord" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){(void)a;[weak openLink:TBPlayT2Discord];}]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Who's Playing Now" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){(void)a;[weak showLiveServers];}]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    alert.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[[self presenter] presentViewController:alert animated:YES completion:nil];
}
-(void)showTouchGuide {
    [self showMessage:@"Touch and Trackpad" text:@"Menus: with the cursor button on, drag anywhere to move the cursor and tap to click. "
        "Touch and hold, then drag, to move sliders. Drag with two fingers to scroll. Turn the cursor off to tap menus directly.\n\n"
        "Playing: the touch controls appear by themselves when play starts and step aside for menus.\n"
        "Move: stick, bottom left\nAim: drag on the right side\n"
        "Fire: hold; drag while firing to aim\nJet (orange): hold to fly. Ski / Jump: hold on downhill slopes to keep speed\nGrenade and Weapon: beside Jet and above Fire\n"
        "Pack and Inventory: above the stick\n\n"
        "Chat, Scores and other keys are in the ··· menu."];
}
-(void)showControllerGuide {
    NSString *name=self.controller.vendorName;
    [self showMessage:name.length?name:@"Controller Guide" text:[NSString stringWithFormat:@"%@\n\n"
        "PLAYING\nLeft stick: move · Right stick: aim\nRT: fire · LT: jet · A: ski / jump\n"
        "RB / LB: next / previous weapon\nY: grenade · X: use pack · B: mine\n"
        "D-pad: up repair kit · down inventory · left beacon · right quick chat\n"
        "Click right stick: zoom on / off · click left stick: third person\nMenu: game menu · View: scores\n"
        "Observing after you join? Press RT to join a team.\n"
        "Loadout: D-pad down opens inventory; pick armor and weapons there.\n\n"
        "MENUS\nLeft stick or D-pad: move the cursor · A: click (hold to drag)\nB or Menu: back · Right stick: scroll",
        name.length?@"Connected. Touch controls step aside once you use it.":@"Connect an Xbox, PlayStation or MFi controller in Settings > Bluetooth."]];
}
/* The original console toggles on the grave key; open the keyboard for typing. */
-(void)openConsole {
    if(self.expanded)[self toggleControls];else [self releaseControls];
    [self tapKey:SDL_SCANCODE_GRAVE];
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);
    if(window&&!SDL_TextInputActive(window)){SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME,"0");tb_start_typing(window);}
}
-(void)showAbout {
    NSDictionary *info=NSBundle.mainBundle.infoDictionary;
    NSString *version=[NSString stringWithFormat:@"%@ (%@)",info[@"CFBundleShortVersionString"]?:@"dev",info[@"CFBundleVersion"]?:@"0"];
    NSString *data=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject stringByAppendingPathComponent:@"TribesPad/Game"];
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"TribesPad"
        message:[NSString stringWithFormat:@"The original Tribes 2 with TribesNEXT/QoL, translated ahead of time to native ARM64.\n\nVersion %@\nGame data: %@",version,data]
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    alert.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[[self presenter] presentViewController:alert animated:YES completion:nil];
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
    // 1.0 on an 11-inch iPad in landscape. Phones keep thumb-sized controls
    // with their labels (0.8 instead of the 0.5 the screen height suggests).
    BOOL phone=self.traitCollection.verticalSizeClass==UIUserInterfaceSizeClassCompact;
    CGFloat k=(phone?.8:fmin(1.15,fmax(.62,fmin(w,h)/780)))*TB_SIZE,margin=fmax(14,22*k);
    // Utility buttons: a column on the right edge, between the original
    // compass (top right) and weapon column (bottom right). The game fills the
    // screen, so on phones the column sits in the outer margin, beside the
    // notch's safe area, and stays small and faint so the menus underneath
    // remain readable.
    CGFloat edge=fmax(self.window.safeAreaInsets.right,0);
    CGFloat b=phone?30:40,gap=phone?6:9,top=y+h*(phone?.24:.17);
    CGFloat right=phone?CGRectGetMaxX(self.bounds)-edge*.5-b/2-2:x+w-b-10;
    if(phone)right=fmin(right,CGRectGetMaxX(self.bounds)-b-2);
    NSArray<UIButton *> *utility=@[self.menu,self.toggle,self.cursor,self.keyboard];
    // While playing with touch controls only Menu and the controls toggle are
    // needed; chat has its own keyboard in the menu.
    if(self.expanded)utility=@[self.menu,self.toggle];
    self.cursor.hidden=self.keyboard.hidden=self.expanded;
    for(NSUInteger i=0;i<utility.count;i++){utility[i].frame=CGRectMake(right,top+i*(b+gap),b,b);utility[i].alpha=phone?.8:1;}
    CGSize hintSize=[self.hint sizeThatFits:CGSizeMake(fmin(w-120,720),200)];
    self.hint.frame=CGRectMake(x+(w-hintSize.width-28)/2,y+h*.16,hintSize.width+28,hintSize.height+18);
    // Beside the menu button.
    self.fps.frame=CGRectMake(right-160,top+(b-22)/2,152,22);
    /* A connected mouse or trackpad points at the game itself (SDL), so the
       trackpad-cursor overlay must not take its clicks. */
    self.trackpad.frame=self.bounds;self.trackpad.hidden=self.expanded||self.inMatch||!(TB_TRACKPAD||self.controllerUsed)||(self.mouseConnected&&!self.controllerUsed);
    // Movement stick bottom left; Pack and Inventory above it.
    CGFloat stick=170*k,small=58*k;
    CGRect stickFrame=CGRectMake(x+margin,y+h-margin-stick,stick,stick);
    CGRect pack=CGRectMake(CGRectGetMinX(stickFrame)+stick*.05,CGRectGetMinY(stickFrame)-small-14*k,small,small);
    CGRect inventory=CGRectOffset(pack,small+12*k,0);
    // Fire bottom right; Jet, the next most used, the same row to its left and
    // nearly as large; Grenade beyond it; Ski / Jump above Jet and Weapon above
    // Fire. Phones show the game's weapon column at the right edge; keep the
    // cluster clear of it.
    CGFloat column=(!TB_LEFT_HANDED&&(self.window.safeAreaInsets.left>0||self.window.safeAreaInsets.right>0))?26:0;
    CGFloat fire=112*k,jet=96*k,ring=68*k,space=8*k;
    CGFloat fireX=x+w-margin-column-fire/2,base=y+h-margin;
    CGRect fireFrame=CGRectMake(fireX-fire/2,base-fire,fire,fire);
    CGRect jetFrame=CGRectMake(CGRectGetMinX(fireFrame)-space-jet,base-jet,jet,jet);
    CGRect grenadeFrame=CGRectMake(CGRectGetMinX(jetFrame)-space-ring,base-ring,ring,ring);
    CGRect jumpFrame=CGRectMake(CGRectGetMidX(jetFrame)-ring/2,CGRectGetMinY(jetFrame)-space-ring,ring,ring);
    CGRect weaponFrame=CGRectMake(fireX-ring/2,CGRectGetMinY(fireFrame)-space-ring,ring,ring);
    // Look anywhere on the right half that is not a button, left of the column.
    CGRect look=CGRectMake(x+w*.42,y,right-4-(x+w*.42),h);
    const CGRect frames[]={stickFrame,look,fireFrame,jetFrame,jumpFrame,weaponFrame,grenadeFrame,pack,inventory};
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
