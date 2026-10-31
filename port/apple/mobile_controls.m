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
#include "starsiege_paths.h"

typedef NS_ENUM(NSInteger, TBControlKind) { TBKindStick, TBKindLook, TBKindButton };
extern uint64_t tribes_gl4es_presented_frames(void);
extern uint64_t tribes_gl4es_take_longest_frame_ns(void);
extern void tribes_gl4es_resume_frame_stats(void);
extern void tribes_set_keyboard_wasd(int on);
static NSString * const TBRepositoryURL=@"https://github.com/chrissotraidis/tribespad";
/* Find a Game: the community's own places, checked 22 October 2026. */
static NSString * const TBPlayT2Discord=@"https://discord.gg/EMMVhQJgf9";
static NSString * const TBTheCutDiscord=@"https://discord.gg/yz3AhkJCxg";
static NSString * const TBTribesPadDiscord=@"https://discord.gg/xwHfUD2bxW";
static NSString * const TBTribesNextForum=@"https://www.tribesnext.com/forum/";
/* Starsiege: Tribes: PlayT1 runs the daily pickups (its Discord invite is
   permanent; checked 25 October 2026); tribes1.co lists the community masters'
   servers; the JunkYard keeps 1.41 configs, plugins and mod files. */
static NSString * const TBPlayT1Discord=@"https://discord.gg/bf4RHMq";
static NSString * const TBPlayT1Site=@"https://playt1.com/";
static NSString * const TBTribes1Servers=@"https://tribes1.co/";
static NSString * const TBTribes1JunkYard=@"https://twdemos.iateyourbaby.com/";

/* Touch preferences (NSUserDefaults, TribesPad-owned keys). */
/* Starsiege: Tribes runs these controls too (tb_mobile_controls_install_starsiege):
   its own keys, play state and menu; its controller is read by the engine. */
static BOOL tb_starsiege;
/* Starsiege's engine reads the controller itself; while the quick chat panel
   has it (1), or until the buttons that closed the panel are let go (2), the
   engine steps aside (tb_overlay_owns_controller, called from its thread). */
#include <stdatomic.h>
static _Atomic int tb_panel_owns;
int tb_overlay_owns_controller(void){
    int owns=atomic_load(&tb_panel_owns);if(owns!=2)return owns;
    GCExtendedGamepad *p=GCController.current.extendedGamepad;
    if(!p)for(GCController *c in GCController.controllers)if(c.extendedGamepad){p=c.extendedGamepad;break;}
    if(p&&(p.buttonA.pressed||p.buttonB.pressed||p.dpad.up.pressed||p.dpad.down.pressed||p.dpad.left.pressed||p.dpad.right.pressed))return 1;
    atomic_store(&tb_panel_owns,0);return 0;
}
static NSString *tb_state_path(void){
    NSString *support=NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject;
    return [support stringByAppendingPathComponent:tb_starsiege?@"TribesPad/Starsiege/volume/TribesPad/Game/temp/tribespad-state.cs":@"TribesPad/Game/base/prefs/tribespad-state.txt"];
}
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

@class TBQuickChatView;
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
@property(nonatomic) BOOL stationOpen;
@property(nonatomic) BOOL matchSeen;
@property(nonatomic) BOOL typingDialog;
@property(nonatomic) BOOL zoomed;
@property(nonatomic,strong) TBQuickChatView *quickChat;
-(void)showQuickChat;
-(void)sendChatKeys:(NSString *)keys;
-(void)sendRequest:(NSString *)request;
-(void)sendRequest:(NSString *)request extra:(NSString *)extra;
-(void)sendStarsiegeChat:(NSString *)script;
@property(nonatomic) BOOL holdJumpUntilRelease;
@property(nonatomic,strong) UITouch *menuFinger;
@property(nonatomic) BOOL gameReported;
@property(nonatomic,copy) NSString *pendingRequest;
@property(nonatomic) BOOL connected;
@property(nonatomic) BOOL controllerUsed;
@property(nonatomic) BOOL mouseConnected;
@property(nonatomic) BOOL mouseUsed;
-(void)realMouseUsed;
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
    {"Display","Render Scale draws the game with fewer pixels on iPad and lets the screen scale them up: 100% is the full resolution, 50% a quarter of the pixels. Lower is faster and softer. Applies the next time TribesPad opens. iPhone already draws at a reduced size."},
    {"Keyboard and Trackpad","With a Magic Keyboard or keyboard folio: W A S D move, E zooms and Q picks the next weapon, as on a PC with the WASD layout. Off, the keyboard uses Tribes 2's own keys (E S D F move). The touch controls and controllers work either way. The trackpad moves the game's cursor in menus and aims in play."},
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
    {2,YES,"Render Scale","TribesPad.renderScale",NULL,.5,1.0,NO},
    {2,NO,"Game Picker at Launch","TribesPad.showLauncher","Choose your game when TribesPad opens. Off, it opens Tribes 2 directly.",0,0,NO},
    {3,NO,"WASD Movement","TribesPad.keyboardWASD","Keys from a hardware keyboard only.",0,0,NO},
    {4,NO,"Online Play","TribesPad.onlinePlay","Sign in with TribesNEXT at launch.",0,0,NO}};
enum {TBRowCount=sizeof tb_rows/sizeof tb_rows[0],TBSectionCount=sizeof tb_sections/sizeof tb_sections[0]};
static void tb_register_defaults(void){
    static dispatch_once_t once;dispatch_once(&once,^{
        BOOL phone=UIDevice.currentDevice.userInterfaceIdiom==UIUserInterfaceIdiomPhone;
        [NSUserDefaults.standardUserDefaults registerDefaults:@{@"TribesPad.controlSize":@1.0,@"TribesPad.controlOpacity":@1.0,
            @"TribesPad.lookSensitivity":@1.0,@"TribesPad.leftHanded":@0,@"TribesPad.hideWithController":@1,
            @"TribesPad.trackpad":@(phone?1:0),@"TribesPad.cursorSpeed":@1.2,@"TribesPad.avoidNotch":@1,@"TribesPad.showFPS":@0,@"TribesPad.renderScale":@1.0,@"TribesPad.keyboardWASD":@1,@"TribesPad.showLauncher":@1,
            @"TribesPad.onlinePlay":@1}];
    });
}
/* Starsiege: Tribes draws a fixed 800x600 frame (content_area.m) and has its own
   WASD keys and no TribesNEXT login, so these rows only change Tribes 2. */
static BOOL tb_row_applies(const TBSettingRow *r){
    static const char *const tribes2[]={"TribesPad.avoidNotch","TribesPad.renderScale","TribesPad.keyboardWASD","TribesPad.onlinePlay"};
    if(tb_starsiege)for(unsigned i=0;i<sizeof tribes2/sizeof tribes2[0];i++)if(!strcmp(r->key,tribes2[i]))return NO;
    return YES;
}
static NSInteger tb_section_rows(NSInteger section){
    NSInteger n=0;for(unsigned i=0;i<TBRowCount;i++)n+=tb_rows[i].section==section&&tb_row_applies(&tb_rows[i]);return n;
}
/* The sheet shows the sections that have rows in this game, then Reset to
   Defaults (TBSectionCount). */
static NSInteger tb_section(NSInteger shown){
    for(NSInteger s=0;s<TBSectionCount;s++)if(tb_section_rows(s)&&shown--==0)return s;return TBSectionCount;
}
static const TBSettingRow *tb_row(NSIndexPath *path){
    NSInteger section=tb_section(path.section),n=0;
    for(unsigned i=0;i<TBRowCount;i++)if(tb_rows[i].section==section&&tb_row_applies(&tb_rows[i])&&n++==path.row)return &tb_rows[i];return NULL;
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
-(NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    (void)tableView;NSInteger n=1;for(NSInteger s=0;s<TBSectionCount;s++)n+=tb_section_rows(s)>0;return n;
}
-(NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView;NSInteger s=tb_section(section);return s==TBSectionCount?1:tb_section_rows(s);
}
-(NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    (void)tableView;NSInteger s=tb_section(section);return s<TBSectionCount?@(tb_sections[s][0]):nil;
}
-(NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView;NSInteger s=tb_section(section);if(s>=TBSectionCount)return nil;
    if(tb_starsiege&&s==2)return @"Starsiege: Tribes draws its original 800x600 frame, which the screen scales up to fit, centred.";
    return tb_sections[s][1]?@(tb_sections[s][1]):nil;
}
static NSString *tb_slider_text(const TBSettingRow *r,double v){return r->multiplier?[NSString stringWithFormat:@"%.1f×",v]:[NSString stringWithFormat:@"%.0f%%",v*100];}
-(UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView;UITableViewCell *cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.selectionStyle=UITableViewCellSelectionStyleNone;
    if(tb_section(path.section)==TBSectionCount) {
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
    [tableView deselectRowAtIndexPath:path animated:YES];if(tb_section(path.section)!=TBSectionCount)return;
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

/* Quick chat for a controller. Tribes 2's chat menu (V) is a tree picked by
   letter keys (voiceBinds.cs: startChatMenu / addChat / endChatMenu), which a
   controller cannot type. This panel shows the same tree: the D-pad moves, A
   (or D-pad right) opens or says, B (or D-pad left) goes back, and at the root
   closes. Saying an item types V and the item's letters, so the game's own
   menu sends it. The tree comes from the player's prefs/customVoiceBinds.cs
   when present, else the copy of the game's voiceBinds.cs bundled at build
   time (GameData/voiceBinds.cs). */
@interface TBChatNode : NSObject
@property(nonatomic,copy) NSString *key,*title,*command,*script;
@property(nonatomic,strong) NSMutableArray<TBChatNode *> *children;
@property(nonatomic,weak) TBChatNode *parent;
@end
@implementation TBChatNode
@end
static TBChatNode *tb_chat_tree(void){
    NSString *support=NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject;
    NSString *custom=[support stringByAppendingPathComponent:@"TribesPad/Game/base/prefs/customVoiceBinds.cs"];
    NSString *text=[NSString stringWithContentsOfFile:custom encoding:NSISOLatin1StringEncoding error:nil];
    if(!text)text=[NSString stringWithContentsOfFile:[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData/voiceBinds.cs"]
        encoding:NSISOLatin1StringEncoding error:nil];
    if(!text)return nil;
    NSRegularExpression *entry=[NSRegularExpression regularExpressionWithPattern:@"(startChatMenu|addChat)\\s*\\(\\s*\"(\\S+)\\s+([^\"]*)\"(?:\\s*,\\s*'([A-Za-z0-9_]+)')?" options:0 error:nil];
    TBChatNode *root=[TBChatNode new];root.title=@"Quick Chat";root.children=[NSMutableArray array];TBChatNode *current=root;
    for(NSString *raw in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]){
        NSString *line=[raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if([line hasPrefix:@"//"])continue;
        if([line hasPrefix:@"endChatMenu"]){if(current.parent)current=current.parent;continue;}
        NSTextCheckingResult *m=[entry firstMatchInString:line options:0 range:NSMakeRange(0,line.length)];if(!m)continue;
        TBChatNode *node=[TBChatNode new];node.key=[[line substringWithRange:[m rangeAtIndex:2]] uppercaseString];
        node.title=[line substringWithRange:[m rangeAtIndex:3]];node.parent=current;[current.children addObject:node];
        if([m rangeAtIndex:4].location!=NSNotFound)node.command=[line substringWithRange:[m rangeAtIndex:4]];
        if([[line substringWithRange:[m rangeAtIndex:1]] isEqualToString:@"startChatMenu"]){node.children=[NSMutableArray array];current=node;}
    }
    return root.children.count?root:nil;
}
/* Starsiege's chat menu (config/Core/ChatMenu.cs in the player's game folder):
   setPlayChatMenu("vOffense") opens a heading (nested with \\), then
   addPlayTeamChat / addPlayChat / addPlayAnim / addLocal / addPlayCMDResponse
   add entries whose first letter is the key. Each entry keeps the command the
   menu itself would run (say, messageAndAnimate, localMessage, remoteEval);
   the commander's menu, after it, is left out. */
static NSArray<NSString *> *tb_script_args(NSString *text){
    NSMutableArray *args=[NSMutableArray array];NSMutableString *now=[NSMutableString string];BOOL quoted=NO;
    for(NSUInteger i=0;i<text.length;i++){unichar c=[text characterAtIndex:i];
        if(c=='"'){quoted=!quoted;continue;}
        if(c==','&&!quoted){[args addObject:[now stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]];now=[NSMutableString string];continue;}
        [now appendFormat:@"%C",c];}
    [args addObject:[now stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]];return args;
}
static NSString *tb_script_string(NSString *s){return [NSString stringWithFormat:@"\"%@\"",[[s stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"]
    stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""]];}
static TBChatNode *tb_starsiege_chat_tree(void){
    NSString *path=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject
        stringByAppendingPathComponent:@"TribesPad/Starsiege/volume/TribesPad/Game/config/Core/ChatMenu.cs"];
    NSString *text=[NSString stringWithContentsOfFile:path encoding:NSISOLatin1StringEncoding error:nil];if(!text)return nil;
    NSRegularExpression *heading=[NSRegularExpression regularExpressionWithPattern:@"^setPlayChatMenu\\(\\s*\"([^\"]*)\"\\s*\\)" options:0 error:nil];
    NSRegularExpression *entry=[NSRegularExpression regularExpressionWithPattern:@"^(addPlayTeamChat|addPlayChat|addPlayAnim|addLocal|addPlayCMDResponse)\\s*\\((.*)\\)\\s*;" options:0 error:nil];
    TBChatNode *root=[TBChatNode new];root.title=@"Quick Chat";root.children=[NSMutableArray array];TBChatNode *current=root;
    for(NSString *raw in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]){
        NSString *line=[raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        if([line hasPrefix:@"//"])continue;
        if([line hasPrefix:@"setCommanderChatMenu"])break;
        NSRange all=NSMakeRange(0,line.length);NSTextCheckingResult *m=[heading firstMatchInString:line options:0 range:all];
        if(m){current=root;
            for(NSString *part in [[line substringWithRange:[m rangeAtIndex:1]] componentsSeparatedByString:@"\\"]){if(part.length<2)continue;
                NSString *key=[[part substringToIndex:1] uppercaseString],*title=[part substringFromIndex:1];TBChatNode *next=nil;
                for(TBChatNode *n in current.children)if(n.children&&[n.key isEqualToString:key])next=n;
                if(!next){next=[TBChatNode new];next.key=key;next.title=title;next.children=[NSMutableArray array];next.parent=current;[current.children addObject:next];}
                current=next;}
            continue;}
        m=[entry firstMatchInString:line options:0 range:all];if(!m)continue;
        NSString *kind=[line substringWithRange:[m rangeAtIndex:1]];NSArray<NSString *> *a=tb_script_args([line substringWithRange:[m rangeAtIndex:2]]);
        if(!a.count||a[0].length<2)continue;
        NSString *(^arg)(NSUInteger)=^NSString *(NSUInteger i){return i<a.count?a[i]:@"";};
        NSString *(^withSound)(NSString *,NSString *)=^NSString *(NSString *msg,NSString *sound){return sound.length?[NSString stringWithFormat:@"%@~w%@",msg,sound]:msg;};
        NSString *script=nil;
        if([kind isEqualToString:@"addPlayTeamChat"])script=[NSString stringWithFormat:@"say(1, %@);",tb_script_string(withSound(arg(1),arg(2)))];
        else if([kind isEqualToString:@"addPlayChat"])script=[NSString stringWithFormat:@"say(0, %@);",tb_script_string(withSound(arg(1),arg(2)))];
        else if([kind isEqualToString:@"addPlayAnim"])script=[NSString stringWithFormat:@"messageAndAnimate(%@, %@);",tb_script_string(arg(1)),tb_script_string(arg(2))];
        else if([kind isEqualToString:@"addLocal"])script=[NSString stringWithFormat:@"localMessage(%@);",tb_script_string(arg(1))];
        else script=[NSString stringWithFormat:@"remoteEval(2048, \"CStatus\", %@, %@);",tb_script_string(arg(1)),tb_script_string(withSound(arg(2),arg(3)))];
        TBChatNode *node=[TBChatNode new];node.key=[[a[0] substringToIndex:1] uppercaseString];node.title=[a[0] substringFromIndex:1];
        node.script=script;node.parent=current;[current.children addObject:node];
    }
    return root.children.count?root:nil;
}
@interface TBQuickChatView : UIView
@property(nonatomic,weak) TBGameControls *owner;
@property(nonatomic,strong) TBChatNode *menu;
@property(nonatomic) NSUInteger selected;
@property(nonatomic,strong) UILabel *title,*help;
@property(nonatomic,strong) NSMutableArray<UILabel *> *rows;
@end
@implementation TBQuickChatView
-(instancetype)initWithTree:(TBChatNode *)tree owner:(TBGameControls *)owner {
    if(!(self=[super initWithFrame:CGRectZero]))return nil;
    self.owner=owner;self.menu=tree;self.rows=[NSMutableArray array];
    self.backgroundColor=[UIColor colorWithWhite:.04 alpha:.86];self.layer.cornerRadius=14;
    self.layer.borderWidth=1;self.layer.borderColor=[UIColor colorWithRed:.18 green:.77 blue:.79 alpha:.7].CGColor;
    self.title=[UILabel new];self.title.font=[UIFont boldSystemFontOfSize:17];self.title.textColor=[UIColor colorWithRed:.18 green:.77 blue:.79 alpha:1];
    self.help=[UILabel new];self.help.font=[UIFont systemFontOfSize:12];self.help.textColor=[UIColor colorWithWhite:1 alpha:.6];self.help.numberOfLines=0;
    self.help.text=@"D-pad: choose  ·  A: open or say  ·  B: back";
    [self addSubview:self.title];[self addSubview:self.help];[self reload];
    return self;
}
-(void)reload {
    for(UILabel *row in self.rows)[row removeFromSuperview];[self.rows removeAllObjects];
    NSMutableArray *path=[NSMutableArray array];for(TBChatNode *n=self.menu;n.parent;n=n.parent)[path insertObject:n.title atIndex:0];
    self.title.text=path.count?[path componentsJoinedByString:@"  ›  "]:@"Quick Chat";
    for(NSUInteger i=0;i<self.menu.children.count;i++){TBChatNode *n=self.menu.children[i];
        UILabel *row=[UILabel new];row.font=[UIFont systemFontOfSize:16 weight:UIFontWeightMedium];row.layer.cornerRadius=6;row.clipsToBounds=YES;
        row.text=[NSString stringWithFormat:@"  %@   %@%@",n.key,n.title,n.children?@"  ›":@""];row.userInteractionEnabled=YES;row.tag=(NSInteger)i;
        [row addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tapped:)]];
        [self addSubview:row];[self.rows addObject:row];}
    if(self.selected>=self.rows.count)self.selected=0;
    [self highlight];[self setNeedsLayout];
}
-(void)highlight {
    for(NSUInteger i=0;i<self.rows.count;i++){BOOL on=i==self.selected;
        self.rows[i].backgroundColor=on?[UIColor colorWithRed:.18 green:.77 blue:.79 alpha:.35]:UIColor.clearColor;
        self.rows[i].textColor=on?UIColor.whiteColor:[UIColor colorWithWhite:1 alpha:.82];}
}
-(CGSize)sizeThatFits:(CGSize)size {(void)size;return CGSizeMake(330,64+30.0*self.rows.count+28);}
-(void)layoutSubviews {
    [super layoutSubviews];CGFloat w=self.bounds.size.width;
    self.title.frame=CGRectMake(16,12,w-32,24);
    for(NSUInteger i=0;i<self.rows.count;i++)self.rows[i].frame=CGRectMake(10,44+30.0*i,w-20,28);
    self.help.frame=CGRectMake(16,44+30.0*self.rows.count+6,w-32,20);
}
-(void)tapped:(UITapGestureRecognizer *)tap {self.selected=(NSUInteger)tap.view.tag;[self choose];}
-(void)moveSelection:(int)step {
    if(!self.rows.count)return;self.selected=(self.selected+self.rows.count+(NSUInteger)(step<0?self.rows.count-1:1))%self.rows.count;[self highlight];
}
-(void)choose {
    if(self.selected>=self.menu.children.count)return;TBChatNode *n=self.menu.children[self.selected];
    if(n.children){self.menu=n;self.selected=0;[self reload];[self.superview setNeedsLayout];return;}
    NSMutableString *keys=[NSMutableString string];for(TBChatNode *k=n;k.parent;k=k.parent)[keys insertString:k.key atIndex:0];
    TBGameControls *owner=self.owner;
    /* The game's own canned-chat command through TribesPad's request bridge,
       so the original menu never opens; typed keys only without one. */
    if(n.script.length){fprintf(stderr,"TribesPad quick chat: %s\n",n.script.UTF8String);[owner sendStarsiegeChat:n.script];}
    else if(n.command.length){fprintf(stderr,"TribesPad quick chat: %s (%s)\n",n.command.UTF8String,n.title.UTF8String);
        [owner sendRequest:[@"chat " stringByAppendingString:n.command]];}
    else {fprintf(stderr,"TribesPad quick chat: V%s (%s)\n",keys.UTF8String,n.title.UTF8String);[owner sendChatKeys:keys];}
    [self close];
}
-(void)back {
    if(self.menu.parent){TBChatNode *was=self.menu;self.menu=self.menu.parent;self.selected=[self.menu.children indexOfObject:was];[self reload];[self.superview setNeedsLayout];}
    else [self close];
}
-(void)close {TBGameControls *owner=self.owner;owner.quickChat=nil;[self removeFromSuperview];
    if(tb_starsiege)atomic_store(&tb_panel_owns,2);}
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
    [self addSubview:self.fps];[self rebuildMenu];[self applyFPSSetting];tribes_set_keyboard_wasd(tb_setting(@"TribesPad.keyboardWASD",1)!=0);
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
    [@"-" writeToFile:tb_state_path() atomically:YES encoding:NSUTF8StringEncoding error:nil];
    self.matchClock=[NSTimer scheduledTimerWithTimeInterval:.2 repeats:YES block:^(NSTimer *t){(void)t;[weakSelf followMatch];}];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(releaseControls)
        name:UIApplicationWillResignActiveNotification object:nil];
    // Time spent in the background is not a slow frame.
    [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidBecomeActiveNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n){(void)n;tribes_gl4es_resume_frame_stats();}];
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
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(pointerLockChanged:)
        name:UIPointerLockStateDidChangeNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(mouseChanged:)
        name:GCMouseDidConnectNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(mouseChanged:)
        name:GCMouseDidDisconnectNotification object:nil];
    /* A keyboard folio's trackpad attached before launch may never announce
       itself; look now, so fingers in menus work beside it from the start. */
    [self mouseChanged:nil];
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
/* A real mouse or trackpad moving or clicking (not touches, which SDL reports
   with its touch mouse id, nor TribesPad's own pointer events, id 0). */
static bool SDLCALL tb_mouse_watch(void *context,SDL_Event *e){
    if((e->type==SDL_EVENT_MOUSE_MOTION||e->type==SDL_EVENT_MOUSE_BUTTON_DOWN)&&e->motion.which&&e->motion.which!=SDL_TOUCH_MOUSEID){
        __weak TBGameControls *weak=(__bridge TBGameControls *)context;
        dispatch_async(dispatch_get_main_queue(),^{[weak realMouseUsed];});}
    return true;
}
/* With a mouse or trackpad attached, the touch controls stay until the mouse
   is used in a match; touching the screen brings them back (hitTest). */
/* A finger in menus with a mouse attached presses, drags and releases
   TribesPad's own pointer where it touches (hitTest routes those touches here). */
-(void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for(UITouch *t in touches)if(t.type==UITouchTypeDirect&&!self.menuFinger){
        self.menuFinger=t;[self pointerButton:YES at:[t locationInView:self]];return;}
    [super touchesBegan:touches withEvent:event];
}
-(void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if(self.menuFinger&&[touches containsObject:self.menuFinger]){
        CGPoint now=[self.menuFinger locationInView:self],was=[self.menuFinger previousLocationInView:self];
        [self pointerMoveTo:now delta:CGPointMake(now.x-was.x,now.y-was.y) held:YES];return;}
    [super touchesMoved:touches withEvent:event];
}
-(void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if(self.menuFinger&&[touches containsObject:self.menuFinger]){
        [self pointerButton:NO at:[self.menuFinger locationInView:self]];self.menuFinger=nil;return;}
    [super touchesEnded:touches withEvent:event];
}
-(void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if(self.menuFinger&&[touches containsObject:self.menuFinger]){
        [self pointerButton:NO at:[self.menuFinger locationInView:self]];self.menuFinger=nil;return;}
    [super touchesCancelled:touches withEvent:event];
}
-(void)realMouseUsed {
    if(self.mouseUsed)return;self.mouseUsed=YES;
    if(self.inMatch&&self.expanded&&!self.restoreControlsAfterKeyboard)[self toggleControls];
    [self setNeedsLayout];
}
-(void)mouseChanged:(NSNotification *)notification {
    (void)notification;BOOL connected=GCMouse.mice.count>0;if(connected==self.mouseConnected)return;
    /* TRIBESPAD_TOUCH_CONTROLS=1 (tests): keep the touch controls with a mouse or controller
       attached, such as the Simulator's or a keyboard folio's trackpad. */
    if(getenv("TRIBESPAD_TOUCH_CONTROLS"))connected=NO;
    if(connected==self.mouseConnected)return;
    self.mouseConnected=connected;
    fprintf(stderr,"TribesPad mouse or trackpad %s\n",connected?"attached":"detached");
    static BOOL watching;if(connected&&!watching){watching=YES;SDL_AddEventWatch(tb_mouse_watch,(__bridge void *)self);}
    if(!connected){BOOL wasUsed=self.mouseUsed;self.mouseUsed=NO;
        if(wasUsed&&self.inMatch&&!self.expanded&&!(self.controllerUsed&&TB_HIDE_WITH_CONTROLLER))[self toggleControls];}
    [self setNeedsLayout];
}
-(void)controllerChanged:(NSNotification *)notification {
    (void)notification;if(!self.superview)return;
    if(tb_starsiege){
        /* The engine reads the controller itself (starsiege_controller.m) and
           steps aside for it; Menu opens the game menu (Escape), as in Tribes 2. */
        BOOL any=NO;for(GCController *c in GCController.controllers)if(c.extendedGamepad)any=YES;
        if(getenv("TRIBESPAD_TOUCH_CONTROLS"))any=NO; /* tests: touch controls even with a controller */
        __weak TBGameControls *weakSelf=self;
        for(GCController *c in GCController.controllers)if(c.extendedGamepad){
            c.extendedGamepad.buttonMenu.pressedChangedHandler=^(GCControllerButtonInput *button,float value,BOOL pressed){
                (void)button;(void)value;TBGameControls *owner=weakSelf;
                if(pressed&&UIApplication.sharedApplication.applicationState==UIApplicationStateActive)[owner openMenu];};
            /* Quick chat: D-pad right in a match opens the panel; while it is
               open the D-pad, A and B drive it and the engine steps aside
               (tb_overlay_owns_controller). */
            GCExtendedGamepad *pad=c.extendedGamepad;
            void (^panel)(GCControllerButtonInput *,int)=^(GCControllerButtonInput *button,int what){
                button.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;TBGameControls *owner=weakSelf;
                    if(!pressed||UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
                    if(!owner.quickChat){if(what==0&&owner.inMatch)[owner showQuickChat];return;}
                    if(what==0||what==3)[owner.quickChat choose];else if(what==1)[owner.quickChat moveSelection:-1];
                    else if(what==2)[owner.quickChat moveSelection:1];else [owner.quickChat back];};};
            panel(pad.dpad.right,0);panel(pad.dpad.up,1);panel(pad.dpad.down,2);panel(pad.buttonA,3);panel(pad.dpad.left,4);panel(pad.buttonB,4);
        }
        if(any!=self.controllerUsed)fprintf(stderr,"TribesPad controller %s (Starsiege)\n",any?"connected":"disconnected");
        self.controllerUsed=any;[self rebuildMenu];return;
    }
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
        /* The A that picked in the quick chat panel does not jump: A waits for its release. */
        if(owner.holdJumpUntilRelease&&!p.buttonA.pressed)owner.holdJumpUntilRelease=NO;
        for(unsigned i=0;i<held.count;i++)tb_input_action(&owner.controls->input,9,actions[i],held[i].pressed&&!(i==2&&(owner.quickChat||owner.holdJumpUntilRelease)));
        tb_mobile_controls_emit(owner.controls);
    };
    // Buttons that hold an original key (controlDefaults.cs) while pressed.
    void (^key)(GCControllerButtonInput *,SDL_Scancode,SDL_Keymod)=^(GCControllerButtonInput *button,SDL_Scancode code,SDL_Keymod mod){
        if(!button)return;
        button.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;TBGameControls *owner=weakSelf;
            if(!owner.controls||UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
            if(owner.quickChat){
                if(pressed){GCExtendedGamepad *now=owner.controller.extendedGamepad;
                    if(b==now.dpad.up)[owner.quickChat moveSelection:-1];else if(b==now.dpad.down)[owner.quickChat moveSelection:1];
                    else if(b==now.dpad.left)[owner.quickChat back];}
                return;}
            if(pressed&&!owner.inMatch)return;
            if(mod&SDL_KMOD_SHIFT)[owner pushKey:SDL_SCANCODE_LSHIFT down:pressed mod:pressed?mod:SDL_KMOD_NONE];
            [owner pushKey:code down:pressed mod:pressed?mod:SDL_KMOD_NONE];};
    };
    key(pad.rightShoulder,SDL_SCANCODE_W,SDL_KMOD_NONE);          // next weapon
    key(pad.leftShoulder,SDL_SCANCODE_L,SDL_KMOD_NONE);           // targeting laser (RB cycles weapons)
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
    // Quick chat: a panel the controller can drive (TBQuickChatView).
    pad.dpad.right.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;TBGameControls *owner=weakSelf;
        if(!pressed||!owner.controls||UIApplication.sharedApplication.applicationState!=UIApplicationStateActive)return;
        if(owner.quickChat){[owner.quickChat choose];return;}
        if(owner.inMatch)[owner showQuickChat];};
    key(pad.buttonOptions,SDL_SCANCODE_F2,SDL_KMOD_NONE);         // scores
    pad.buttonA.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;TBGameControls *owner=weakSelf;
        if(owner.quickChat){if(pressed){owner.holdJumpUntilRelease=YES;[owner.quickChat choose];}return;}
        if(owner.inMatch||!owner.controls)return;
        [owner pointerButton:pressed at:owner.trackpad.cursor];};
    pad.buttonB.pressedChangedHandler=^(GCControllerButtonInput *b,float v,BOOL pressed){(void)b;(void)v;TBGameControls *owner=weakSelf;
        if(!pressed||!owner.controls)return;
        if(owner.quickChat){[owner.quickChat back];return;}
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
    UIView *hit=[super hitTest:point withEvent:event];
    /* A finger on the screen after the mouse hid the touch controls brings them back. */
    if(self.mouseUsed&&self.inMatch&&!self.expanded&&event.type==UIEventTypeTouches){
        BOOL finger=NO;for(UITouch *t in event.allTouches)if(t.type==UITouchTypeDirect)finger=YES;
        if(finger){self.mouseUsed=NO;dispatch_async(dispatch_get_main_queue(),^{if(!self.expanded&&self.inMatch)[self toggleControls];});}
    }
    /* Menus with a mouse or trackpad attached: the game takes no touches then,
       so a finger works TribesPad's own pointer (touchesBegan below). */
    if(hit==self&&!tb_starsiege&&!self.inMatch&&self.mouseConnected&&event.type==UIEventTypeTouches)
        for(UITouch *t in event.allTouches)if(t.type==UITouchTypeDirect)return self;
    return hit==self?nil:hit;
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
    tribes_set_keyboard_wasd(tb_setting(@"TribesPad.keyboardWASD",1)!=0);
}
-(void)toggleTrackpad{tb_set(@"TribesPad.trackpad",!TB_TRACKPAD);[self settingsChanged];}
-(void)toggleFPS{tb_set(@"TribesPad.showFPS",!TB_SHOW_FPS);[self settingsChanged];}
/* Whether iPadOS granted pointer lock for mouse look (full screen only). */
-(void)pointerLockChanged:(NSNotification *)notification {
    (void)notification;UIPointerLockState *lock=self.window.windowScene.pointerLockState;
    fprintf(stderr,"TribesPad pointer lock %s\n",lock.locked?"engaged":"released");
}
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
    NSString *state=[NSString stringWithContentsOfFile:tb_state_path() encoding:NSUTF8StringEncoding error:nil];
    if(tb_starsiege&&state.length){
        /* Starsiege's export: $TribesPad::state = "1 0 1"; */
        NSRange q=[state rangeOfString:@"\""];if(q.location==NSNotFound)return;
        NSRange e=[state rangeOfString:@"\"" options:0 range:NSMakeRange(q.location+1,state.length-q.location-1)];
        if(e.location==NSNotFound)return;state=[state substringWithRange:NSMakeRange(q.location+1,e.location-q.location-1)];
    }
    if(!state.length||[state hasPrefix:@"-"])return;
    if(!self.gameReported){
        // Past the login: a keyboard opened there closes with the next report.
        self.gameReported=YES;SDL_Window *window=SDL_GetWindowFromID(self.controls->window);
        self.typingDialog=window&&SDL_TextInputActive(window);
        [self rebuildMenu];
        // New players rarely know where the games are; point at Find a Game.
        double shown=tb_setting(@"TribesPad.findHintsShown",0);
        if(shown<3){tb_set(@"TribesPad.findHintsShown",shown+1);
            [self flashHint:@"Looking for a game?  ··· > Play Online has the server list,\nwho is playing now, game nights and the community Discords"];}
        // The graphics presets are Tribes 2's (base/autoexec.cs).
        else if(!tb_starsiege)[self performSelector:@selector(offerBalancedIfMaximum) withObject:nil afterDelay:3];
    }
    BOOL playing=[state hasPrefix:@"1"],typing=[state hasPrefix:@"0 1"];
    /* Starsiege reports 2 while a station's buy screen is open: say how to buy. */
    BOOL station=tb_starsiege&&[state hasPrefix:@"2"];
    if(station!=self.stationOpen){self.stationOpen=station;
        if(station)[self flashHint:self.controllerUsed?@"Station: point at an item and press A to buy it\nB closes the station":
            @"Station: double-tap an item to buy it\n··· > This Match > Back to Game closes the station"];}
    NSArray *words=[state componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    BOOL connected=words.count>2&&[words[2] isEqualToString:@"1"];
    /* Starsiege reports the last request it handled; then the flag goes. */
    if(tb_starsiege&&self.pendingRequest&&words.count>3&&[words[3] isEqualToString:self.pendingRequest]){
        NSString *game=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject
            stringByAppendingPathComponent:@"TribesPad/Starsiege/volume/TribesPad/Game"];
        [NSFileManager.defaultManager removeItemAtPath:[game stringByAppendingPathComponent:@"temp/tribespad-request.flag"] error:nil];
        self.pendingRequest=nil;
    }
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
        if(!self.expanded&&!(self.controllerUsed&&TB_HIDE_WITH_CONTROLLER)&&!self.mouseUsed)[self toggleControls];
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
-(void)showQuickChat {
    if(self.quickChat)return;
    static TBChatNode *tree;static BOOL loaded;if(!loaded){loaded=YES;tree=tb_starsiege?tb_starsiege_chat_tree():tb_chat_tree();}
    if(!tree){[self sendGameKey:SDL_SCANCODE_V mod:SDL_KMOD_NONE];return;} /* no tree: the game's own menu */
    self.quickChat=[[TBQuickChatView alloc] initWithTree:tree owner:self];
    [self addSubview:self.quickChat];[self setNeedsLayout];[self layoutIfNeeded];
    if(tb_starsiege)atomic_store(&tb_panel_owns,1);
}
/* Starsiege: the request bridge (build_engine.py) runs the entry's command,
   written beside the request as $TribesPad::chat. */
-(void)sendStarsiegeChat:(NSString *)script {
    [self sendRequest:@"chat" extra:[NSString stringWithFormat:@"$TribesPad::chat = %@;\n",tb_script_string(script)]];
}
/* V opens the game's chat menu; each letter then picks, as typed. Spaced so
   each key lands after the menu it opens. */
-(void)sendChatKeys:(NSString *)keys {
    [self releaseControls];NSMutableArray<NSNumber *> *codes=[NSMutableArray arrayWithObject:@(SDL_SCANCODE_V)];
    for(NSUInteger i=0;i<keys.length;i++){unichar c=[keys characterAtIndex:i];
        if(c>='A'&&c<='Z')[codes addObject:@(SDL_SCANCODE_A+(c-'A'))];
        else if(c>='1'&&c<='9')[codes addObject:@(SDL_SCANCODE_1+(c-'1'))];else if(c=='0')[codes addObject:@(SDL_SCANCODE_0)];}
    __weak TBGameControls *weak=self;
    for(NSUInteger i=0;i<codes.count;i++)dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(i*70*NSEC_PER_MSEC)),dispatch_get_main_queue(),^{
        [weak tapKey:(SDL_Scancode)codes[i].intValue];});
}
-(void)showControllerHint {
    double shown=tb_setting(@"TribesPad.controllerHintsShown",0);if(shown>=3)return;tb_set(@"TribesPad.controllerHintsShown",shown+1);
    /* Each game's own layout: Starsiege's is starsiege_input.c's. */
    self.hint.text=tb_starsiege?@"RT fire  ·  LT jet  ·  A jump / ski  ·  RB / LB next / previous weapon\n"
        "Y grenade  ·  X use pack  ·  B crouch  ·  D-pad: up repair kit, down inventory, left beacon, right quick chat\n"
        "Spawning? Press RT  ·  Menu: game menu  ·  View: scores":
        @"RT fire  ·  LT jet  ·  A ski / jump  ·  RB next weapon  ·  LB targeting laser\n"
        "Y grenade  ·  X use pack  ·  B place mine  ·  D-pad: up repair kit, down inventory, left beacon, right quick chat\n"
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
    fps.subtitle=@"Top right: frames per second and the slowest frame";
    fps.state=TB_SHOW_FPS?UIMenuElementStateOn:UIMenuElementStateOff;
    /* Graphics Quality: what each choice does, and which one is in use. */
    NSString *preset=[NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.graphicsPreset"];
    UIAction *balanced=item(@"Balanced",@"gauge.with.dots.needle.50percent",^{[weak confirmGraphics:@"balanced"];});
    balanced.subtitle=@"Smoother: the game's own High detail";
    balanced.state=[preset isEqualToString:@"balanced"]?UIMenuElementStateOn:UIMenuElementStateOff;
    UIAction *fast=item(@"Fast",@"hare",^{[weak confirmGraphics:@"fast"];});
    fast.subtitle=@"Smoothest: less detail, no clouds";
    fast.state=[preset isEqualToString:@"fast"]?UIMenuElementStateOn:UIMenuElementStateOff;
    UIAction *scale=item(@"Render Scale…",@"square.resize.down",^{[weak showSettings];});
    double rs=tb_setting(@"TribesPad.renderScale",1);
    scale.subtitle=[NSString stringWithFormat:@"%.0f%% of the screen's pixels, in Settings",rs*100];
    if(UIDevice.currentDevice.userInterfaceIdiom!=UIUserInterfaceIdiomPad)scale.attributes=UIMenuElementAttributesHidden;
    NSArray *controls=@[
        item(self.expanded?@"Hide Touch Controls":@"Show Touch Controls",self.expanded?@"xmark.circle":@"gamecontroller",^{[weak toggleControls];}),
        trackpad,fps,
        [UIMenu menuWithTitle:@"Graphics Quality" image:icon(@"slider.horizontal.3") identifier:nil options:0 children:@[
            group(@[balanced,fast]),group(@[scale])]],
        item(@"Settings…",@"gearshape",^{[weak showSettings];})];
    NSString *pad=[self controllerName];
    UIAction *controllerGuide=item(pad.length?[NSString stringWithFormat:@"Controller: %@",pad]:@"Controller Guide",@"gamecontroller.fill",^{[weak showControllerGuide];});
    UIMenu *help=[UIMenu menuWithTitle:@"Help" image:icon(@"questionmark.circle") identifier:nil options:0 children:@[
        group(@[item(@"Touch and Trackpad Guide",@"hand.tap",^{[weak showTouchGuide];}),controllerGuide]),
        group(@[item(@"Console",@"terminal",^{[weak openConsole];}),
            item(@"TribesPad on GitHub",@"safari",^{
                [UIApplication.sharedApplication openURL:[NSURL URLWithString:TBRepositoryURL] options:@{} completionHandler:nil];}),
            item(@"About TribesPad",@"info.circle",^{[weak showAbout];})]),
        ]];
    /* Play Online: what new players lack most. Who is on now, the original JOIN
       list, when the community plays and where it organizes. It leads the menu
       off a server; on one, This Match leads and it folds into a submenu. */
    UIAction *servers=item(@"Server List",@"server.rack",^{[weak openServerList];});
    servers.subtitle=self.connected?@"Leaves this match first":@"PLAY GAME > JOIN";
    UIAction *playing=item(@"Who's Playing Now",@"person.3",^{[weak showLiveServers];});
    playing.subtitle=@"People and bots on each server";
    UIAction *nights=item(@"Game Nights",@"calendar",^{[weak showSchedule];});
    nights.subtitle=@"Regular games, shown in your local time";
    UIMenu *community=[UIMenu menuWithTitle:@"Community" image:icon(@"bubble.left.and.text.bubble.right") identifier:nil options:0 children:@[
        group(@[item(@"PlayT2 Discord",@"person.2",^{[weak openLink:TBPlayT2Discord];}),
            item(@"The Cut Discord",@"person.2",^{[weak openLink:TBTheCutDiscord];}),
            item(@"TribesPad Discord",@"person.2",^{[weak openLink:TBTribesPadDiscord];})]),
        group(@[item(@"TribesNEXT Forum",@"text.bubble",^{[weak openPage:TBTribesNextForum];})])]];
    NSArray *matchItems=game,*onlineItems=@[playing,servers,nights,community],*optionItems=controls;
    if(tb_starsiege){
        /* Starsiege's own keys (config/config.cs): T and Y chat, I inventory,
           O objectives, C command, Tab scores, Ctrl+K kill. */
        UIAction *kill=key(@"Suicide",@"xmark.octagon",SDL_SCANCODE_K,SDL_KMOD_LCTRL);kill.attributes=UIMenuElementAttributesDestructive;
        /* Escape, as in Tribes 2 and the controller's Menu button. */
        NSArray *t1=@[game[0],
            key(@"Scores",@"trophy",SDL_SCANCODE_TAB,SDL_KMOD_NONE),
            [UIMenu menuWithTitle:@"Chat" image:icon(@"bubble.left.and.bubble.right") identifier:nil options:0 children:@[
                item(@"All Chat",@"bubble.left.and.bubble.right",^{[weak openChat:SDL_SCANCODE_T];}),
                item(@"Team Chat",@"bubble.left",^{[weak openChat:SDL_SCANCODE_Y];}),
                item(@"Quick Chat Menu",@"text.bubble",^{[weak openChat:SDL_SCANCODE_V];})]],
            [UIMenu menuWithTitle:@"Actions" image:icon(@"bolt") identifier:nil options:0 children:@[
                group(@[key(@"Inventory",@"square.grid.2x2",SDL_SCANCODE_I,SDL_KMOD_NONE),key(@"Objectives",@"list.number",SDL_SCANCODE_O,SDL_KMOD_NONE),
                    key(@"Command Map",@"map",SDL_SCANCODE_C,SDL_KMOD_NONE)]),
                group(@[key(@"Use Repair Kit",@"cross.case",SDL_SCANCODE_H,SDL_KMOD_NONE),key(@"Place Beacon",@"dot.radiowaves.left.and.right",SDL_SCANCODE_B,SDL_KMOD_NONE),
                    key(@"Drop Flag",@"flag",SDL_SCANCODE_F,SDL_KMOD_LCTRL)]),
                group(@[key(@"Zoom",@"plus.magnifyingglass",SDL_SCANCODE_Z,SDL_KMOD_NONE),key(@"Third-Person View",@"person.crop.square",SDL_SCANCODE_R,SDL_KMOD_NONE)]),
                group(@[kill])]]];
        optionItems=@[controls[0],controls[1],controls[2],controls[4]];
        help=[UIMenu menuWithTitle:@"Help" image:icon(@"questionmark.circle") identifier:nil options:0 children:@[
            group(@[item(@"Touch and Trackpad Guide",@"hand.tap",^{[weak showTouchGuide];}),controllerGuide]),
            group(@[item(@"Console",@"terminal",^{[weak openConsole];}),
                item(@"TribesPad on GitHub",@"safari",^{[UIApplication.sharedApplication openURL:[NSURL URLWithString:TBRepositoryURL] options:@{} completionHandler:nil];}),
                item(@"About TribesPad",@"info.circle",^{[weak showAbout];})])]];
        UIAction *t1servers=item(@"Server List",@"server.rack",^{[weak openServerList];});
        t1servers.subtitle=self.connected?@"Leaves this match first":@"PLAY GAME > JOIN";
        UIAction *t1playing=item(@"Who's Playing Now",@"person.3",^{[weak showLiveServers];});
        t1playing.subtitle=@"Players on each server, from the community masters";
        UIAction *t1nights=item(@"Game Nights",@"calendar",^{[weak showSchedule];});
        t1nights.subtitle=@"PlayT1's daily pickup, in your local time";
        UIMenu *t1community=[UIMenu menuWithTitle:@"Community" image:icon(@"bubble.left.and.text.bubble.right") identifier:nil options:0 children:@[
            group(@[item(@"PlayT1 Discord",@"person.2",^{[weak openLink:TBPlayT1Discord];}),
                item(@"TribesPad Discord",@"person.2",^{[weak openLink:TBTribesPadDiscord];})]),
            group(@[item(@"PlayT1",@"safari",^{[weak openPage:TBPlayT1Site];}),
                item(@"Server List on the Web (tribes1.co)",@"server.rack",^{[weak openPage:TBTribes1Servers];}),
                item(@"Tribes 1 JunkYard: configs, mods, demos",@"archivebox",^{[weak openPage:TBTribes1JunkYard];})])]];
        matchItems=t1;onlineItems=@[t1playing,t1servers,t1nights,t1community];
    }
    /* The same order in both games: This Match (on a server), Play Online,
       Options, Help, then Choose Game. Off a server Play Online leads, open. */
    BOOL onServer=self.connected||self.inMatch;
    UIMenu *online=[UIMenu menuWithTitle:@"Play Online" image:icon(@"globe") identifier:nil
        options:onServer?0:UIMenuOptionsDisplayInline children:onlineItems];
    UIMenu *options=[UIMenu menuWithTitle:@"Options" image:icon(@"slider.horizontal.3") identifier:nil options:0 children:optionItems];
    NSMutableArray *groups=[NSMutableArray array];
    if(onServer)[groups addObject:[UIMenu menuWithTitle:@"This Match" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:matchItems]];
    [groups addObject:onServer?group(@[online]):online];
    [groups addObject:group(@[options,help])];
    if(tb_starsiege_bundled([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"].fileSystemRepresentation)){
        UIAction *choose=item(@"Choose Game\u2026",@"square.grid.2x2",^{[weak chooseGame];});
        choose.subtitle=tb_starsiege?@"Starsiege: Tribes or Tribes 2":@"Tribes 2 or Starsiege: Tribes";
        [groups addObject:group(@[choose])];
    }
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
        owner.fps.text=frames?[NSString stringWithFormat:@"%llu FPS · slowest %.0f ms%@",(unsigned long long)frames,longest,hot?@" · Hot":@""]:@"0 FPS";
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
    if(!self.gameReported&&!tb_starsiege){
        [self showMessage:@"Server List" text:@"Sign in to TribesNEXT first. Once you are past the login, this opens PLAY GAME > JOIN, the list of internet servers."];return;
    }
    if(!self.connected){[self sendRequest:@"servers"];return;}
    // On a server the list is out of reach until you leave, as on a PC.
    [self releaseControls];
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"Leave This Match?"
        message:@"The server list opens once you leave the server you are on. Who's Playing Now shows the servers without leaving."
        preferredStyle:UIAlertControllerStyleAlert];
    __weak TBGameControls *weak=self;
    [alert addAction:[UIAlertAction actionWithTitle:@"Who's Playing Now" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){(void)a;[weak showLiveServers];}]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Leave and Show Servers" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *a){(void)a;
        [weak sendRequest:@"leave-servers"];}]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Stay" style:UIAlertActionStyleCancel handler:nil]];
    alert.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[[self presenter] presentViewController:alert animated:YES completion:nil];
}
-(void)sendRequest:(NSString *)request {[self sendRequest:request extra:nil];}
-(void)sendRequest:(NSString *)request extra:(NSString *)extra {
    if(tb_starsiege){
        /* Starsiege runs only scripts that existed at its start: rewrite the
           shipped request file, then raise the flag its report loop checks. The
           number stays below 2^24 so the game's float comparison is exact. */
        static unsigned counter;NSString *number=[NSString stringWithFormat:@"%ld",
            (long)((long long)NSDate.date.timeIntervalSince1970%1000000)*10+(long)(counter++%10)];
        NSString *game=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject
            stringByAppendingPathComponent:@"TribesPad/Starsiege/volume/TribesPad/Game"];
        NSString *text=[NSString stringWithFormat:@"%@$TribesPad::request = \"%@ %@\";\n",extra?:@"",number,request];
        [text writeToFile:[game stringByAppendingPathComponent:@"config/tribespad-request.cs"] atomically:NO encoding:NSUTF8StringEncoding error:nil];
        [NSData.data writeToFile:[game stringByAppendingPathComponent:@"temp/tribespad-request.flag"] atomically:NO];
        self.pendingRequest=number;fprintf(stderr,"TribesPad: request %s %s\n",number.UTF8String,request.UTF8String);return;
    }
    NSString *path=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject
        stringByAppendingPathComponent:@"TribesPad/Game/base/prefs/tribespad-request.txt"];
    [[NSString stringWithFormat:@"%.0f %@\n",NSDate.date.timeIntervalSince1970*1000,request] writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:nil];
}
/* Graphics presets: the game's own detail settings, set in one step by
   base/autoexec.cs (TribesPadGraphics). Balanced is Dynamix's High profile
   for detail; Fast is its Medium profile. The game's Options still change
   each setting afterwards. */
-(void)confirmGraphics:(NSString *)preset {
    if(!self.gameReported){
        [self showMessage:@"Graphics" text:@"Sign in to TribesNEXT first; the presets apply once the game is past the login."];return;
    }
    [self releaseControls];
    BOOL fast=[preset isEqualToString:@"fast"];
    NSString *text=fast?
        @"Coarser terrain and shapes, no clouds, decals, reflections or building lights, light shadows and no weather. The smoothest choice on busy maps.":
        @"Terrain, buildings and sky at the game's own High detail, one cloud layer, light shadows and no weather. Much less work than the maximum detail sliders, with little visible difference.";
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:fast?@"Fast Graphics":@"Balanced Graphics"
        message:[text stringByAppendingString:@"\n\nIt sets the detail sliders in the game's Options > Graphics, where you can still change each one. Turn on ··· > FPS Counter to see the difference."]
        preferredStyle:UIAlertControllerStyleAlert];
    __weak TBGameControls *weak=self;
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Apply" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){(void)a;
        [weak sendRequest:[@"graphics " stringByAppendingString:preset]];
        [NSUserDefaults.standardUserDefaults setObject:preset forKey:@"TribesPad.graphicsPreset"];[weak rebuildMenu];}]];
    alert.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[[self presenter] presentViewController:alert animated:YES completion:nil];
}
/* Profiles made before TribesPad set detail defaults (and every profile
   whose Options were saved with empty detail prefs) sit at the most
   expensive settings: terrain screenError 0, shape detail near 2. Offer
   Balanced once, outside a match. */
-(void)offerBalancedIfMaximum {
    if(self.inMatch||tb_setting(@"TribesPad.detailOfferShown",0)||[NSUserDefaults.standardUserDefaults stringForKey:@"TribesPad.graphicsPreset"])return;
    NSString *prefs=[NSString stringWithContentsOfFile:[[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject
        stringByAppendingPathComponent:@"TribesPad/Game/base/prefs"] stringByAppendingPathComponent:@"ClientPrefs.cs"] encoding:NSUTF8StringEncoding error:nil];
    double (^pref)(NSString *)=^double(NSString *name){
        NSRange r=[prefs rangeOfString:[NSString stringWithFormat:@"$pref::%@ = \"",name]];
        return r.location==NSNotFound?-1:[[prefs substringFromIndex:NSMaxRange(r)] doubleValue];};
    double terrain=pref(@"Terrain::screenError"),shapes=pref(@"TS::detailAdjust");
    if(!prefs||terrain<0||terrain>2||shapes<1.5)return;
    tb_set(@"TribesPad.detailOfferShown",1);[self releaseControls];
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"Graphics at Maximum Detail"
        message:@"Terrain and shape detail are set above the game's own High preset, which costs a lot of frames in busy matches. "
            "Balanced keeps the game's High detail and plays much smoother. You can switch any time in ··· > Graphics Quality."
        preferredStyle:UIAlertControllerStyleAlert];
    __weak TBGameControls *weak=self;
    [alert addAction:[UIAlertAction actionWithTitle:@"Keep Maximum" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Use Balanced" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){(void)a;
        [weak sendRequest:@"graphics balanced"];
        [NSUserDefaults.standardUserDefaults setObject:@"balanced" forKey:@"TribesPad.graphicsPreset"];[weak rebuildMenu];}]];
    alert.preferredAction=alert.actions.lastObject;
    alert.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[[self presenter] presentViewController:alert animated:YES completion:nil];
}
/* Playing Now: people and bots per server, live; tap one to join it. */
-(void)showLiveServers {
    SDL_Window *window=SDL_GetWindowFromID(self.controls->window);if(window)SDL_StopTextInput(window);
    [self releaseControls];
    __weak TBGameControls *weak=self;
    TBLiveServersController *live=[[TBLiveServersController alloc] initWithStyle:UITableViewStyleInsetGrouped];
    if(self.gameReported&&!self.connected)live.join=^(NSString *address){[weak sendRequest:[@"join " stringByAppendingString:address]];};
    live.serverList=^{[weak openServerList];};
    live.starsiege=tb_starsiege;
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
/* Starsiege: Tribes: PlayT1 organizes a pickup game every day, on Rosco's
   Mixtape from about 8:00 PM US Eastern (playt1.com, checked 25 October 2026). */
-(void)showStarsiegeSchedule {
    NSCalendar *eastern=[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    eastern.timeZone=[NSTimeZone timeZoneWithName:@"America/New_York"];
    NSDateFormatter *format=[NSDateFormatter new];[format setLocalizedDateFormatFromTemplate:@"EEEEjmm"];format.timeZone=NSTimeZone.localTimeZone;
    NSDateComponents *when=[NSDateComponents new];when.hour=20;when.minute=0;
    NSDate *date=[eastern nextDateAfterDate:[NSDate dateWithTimeIntervalSinceNow:-7200] matchingComponents:when options:NSCalendarMatchNextTime];
    NSDateComponentsFormatter *until=[NSDateComponentsFormatter new];until.unitsStyle=NSDateComponentsFormatterUnitsStyleAbbreviated;
    until.allowedUnits=NSCalendarUnitDay|NSCalendarUnitHour|NSCalendarUnitMinute;until.maximumUnitCount=2;
    NSMutableString *text=[NSMutableString string];
    for(int i=0;i<3&&date;i++){NSTimeInterval wait=[date timeIntervalSinceNow];
        [text appendFormat:@"%@ (%@)\nDaily pickup game\n\n",[format stringFromDate:date],wait<=0?@"on now":[@"in " stringByAppendingString:[until stringFromTimeInterval:wait]?:@""]];
        date=[eastern dateByAddingUnit:NSCalendarUnitDay value:1 toDate:date options:0];}
    NSTimeZone *zone=NSTimeZone.localTimeZone;
    [text appendFormat:@"Shown in your time zone, %@. PlayT1 organizes a pickup every day, on Rosco's Mixtape from about 8:00 PM US Eastern. "
        "Teams are picked in the PlayT1 Discord; check there for the day's game.",[zone localizedName:NSTimeZoneNameStyleGeneric locale:NSLocale.currentLocale]?:zone.name];
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"Game Nights" message:text preferredStyle:UIAlertControllerStyleAlert];
    __weak TBGameControls *weak=self;
    [alert addAction:[UIAlertAction actionWithTitle:@"PlayT1 Discord" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){(void)a;[weak openLink:TBPlayT1Discord];}]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Who's Playing Now" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a){(void)a;[weak showLiveServers];}]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    alert.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[[self presenter] presentViewController:alert animated:YES completion:nil];
}
-(void)showSchedule {
    [self releaseControls];
    if(tb_starsiege){[self showStarsiegeSchedule];return;}
    NSCalendar *eastern=[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
    eastern.timeZone=[NSTimeZone timeZoneWithName:@"America/New_York"];
    NSDateFormatter *format=[NSDateFormatter new];[format setLocalizedDateFormatFromTemplate:@"EEEEjmm"];
    format.timeZone=NSTimeZone.localTimeZone;
    struct {NSInteger weekday,hour,minute;BOOL biweekly;NSString *what;} games[]={
        {4,21,0,NO,@"Weekly pickup game"},{7,21,30,NO,@"Weekly pickup game, the big one"},{1,21,30,YES,@"CTF public game (every other Sunday)"}};
    // The every-other-week CTF game ran on 24 May 2026 (tribes2forever.com).
    NSDateComponents *anchorParts=[NSDateComponents new];anchorParts.year=2026;anchorParts.month=5;anchorParts.day=24;anchorParts.hour=21;anchorParts.minute=30;
    NSDate *anchor=[eastern dateFromComponents:anchorParts];
    NSMutableArray<NSArray *> *next=[NSMutableArray array];
    for(unsigned i=0;i<sizeof games/sizeof games[0];i++){
        NSDateComponents *when=[NSDateComponents new];when.weekday=games[i].weekday;when.hour=games[i].hour;when.minute=games[i].minute;
        // Still on now if it started in the last two hours.
        NSDate *date=[eastern nextDateAfterDate:[NSDate dateWithTimeIntervalSinceNow:-7200] matchingComponents:when options:NSCalendarMatchNextTime];
        if(date&&games[i].biweekly&&anchor){
            NSInteger days=[eastern components:NSCalendarUnitDay fromDate:anchor toDate:date options:0].day;
            if((days/7)%2)date=[eastern dateByAddingUnit:NSCalendarUnitDay value:7 toDate:date options:0];
        }
        if(date)[next addObject:@[date,games[i].what]];
    }
    [next sortUsingComparator:^NSComparisonResult(NSArray *a,NSArray *b){return [a[0] compare:b[0]];}];
    NSMutableString *text=[NSMutableString string];
    NSDateComponentsFormatter *until=[NSDateComponentsFormatter new];until.unitsStyle=NSDateComponentsFormatterUnitsStyleAbbreviated;
    until.allowedUnits=NSCalendarUnitDay|NSCalendarUnitHour|NSCalendarUnitMinute;until.maximumUnitCount=2;
    for(NSArray *game in next){
        NSTimeInterval wait=[game[0] timeIntervalSinceNow];
        NSString *when=wait<=0?@"on now":[@"in " stringByAppendingString:[until stringFromTimeInterval:wait]?:@""];
        [text appendFormat:@"%@ (%@)\n%@\n\n",[format stringFromDate:game[0]],when,game[1]];
    }
    NSTimeZone *zone=NSTimeZone.localTimeZone;
    [text appendFormat:@"Shown in your time zone, %@. The community sets them in US Eastern time: 9:00 PM Wednesdays, 9:30 PM Saturdays and Sundays. "
        "At other hours most public servers are empty or hold only bots. Sign-ups and pickups are posted on the PlayT2 and The Cut Discords; check there for changes.",
        [zone localizedName:NSTimeZoneNameStyleGeneric locale:NSLocale.currentLocale]?:zone.name];
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
    NSString *name=[self controllerName];
    NSString *status=name.length?@"Connected. Touch controls step aside once you use it.":@"Connect an Xbox, PlayStation or MFi controller in Settings > Bluetooth.";
    if(tb_starsiege){
        [self showMessage:name.length?name:@"Controller Guide" text:[NSString stringWithFormat:@"%@\n\n"
            "PLAYING\nLeft stick: move · Right stick: aim\nRT: fire · LT: jet · A: jump / ski\n"
            "RB / LB: next / previous weapon\nY: grenade · X: use pack · B: crouch\n"
            "D-pad: up repair kit · down inventory · left beacon · right quick chat\n"
            "Quick chat: D-pad to choose, A to say or open, B to go back\n"
            "Click right stick: zoom on / off · click left stick: third person\nMenu: game menu · View: scores\n"
            "Spawning: press RT (fire).\n\n"
            "MENUS AND INVENTORY\nLeft stick or D-pad: move the cursor · A: click\nX: double-click (buy from the inventory station's lists)\n"
            "B: back · Right stick: scroll\n\n"
            "Recorded demos play as a menu: B leaves them.",status]];
        return;
    }
    [self showMessage:name.length?name:@"Controller Guide" text:[NSString stringWithFormat:@"%@\n\n"
        "PLAYING\nLeft stick: move · Right stick: aim\nRT: fire · LT: jet · A: ski / jump\n"
        "RB: next weapon · LB: targeting laser\nY: grenade · X: use pack · B: place mine\n"
        "D-pad: up repair kit · down inventory · left beacon · right quick chat\n"
        "Quick chat: D-pad to choose, A to open or say, B to go back\n"
        "Click right stick: zoom on / off · click left stick: third person\nMenu: game menu · View: scores\n"
        "Observing after you join? Press RT to join a team.\n"
        "Loadout: D-pad down opens inventory; pick armor and weapons there.\n\n"
        "MENUS\nLeft stick or D-pad: move the cursor · A: click (hold to drag)\nB or Menu: back · Right stick: scroll",
        status]];
}
/* The connected gamepad's name. Tribes 2 holds the one it reads (self.controller);
   Starsiege's engine reads the first gamepad itself (starsiege_controller.m). */
-(NSString *)controllerName {
    GCController *pad=self.controller;
    if(!pad&&GCController.current.extendedGamepad)pad=GCController.current;
    if(!pad)for(GCController *c in GCController.controllers)if(c.extendedGamepad){pad=c;break;}
    return pad.vendorName;
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
    if(tb_starsiege)data=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject
        stringByAppendingPathComponent:@"TribesPad/Starsiege/volume/TribesPad/Game"];
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"TribesPad"
        message:[NSString stringWithFormat:@"%@\n\nVersion %@\nGame data: %@",tb_starsiege?
            @"Starsiege: Tribes 1.41, the version the community plays, translated ahead of time to native ARM64. Online through the community master servers (tribes1.co).":
            @"The original Tribes 2 with TribesNEXT/QoL, translated ahead of time to native ARM64.",version,data]
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    alert.overrideUserInterfaceStyle=UIUserInterfaceStyleDark;[[self presenter] presentViewController:alert animated:YES completion:nil];
}
/* Choose Game (docs/LAUNCHER-PLAN.md): a game engine can't be unloaded and iOS
   doesn't let an app reopen itself, so TribesPad closes, as when it is swiped
   away, and the next launch shows the game picker (TribesPad.nextGame "picker",
   consumed by the picker), even when Open Tribes 2 Directly is on. */
-(void)chooseGame {
    [self releaseControls];
    UIAlertController *alert=[UIAlertController alertControllerWithTitle:@"Choose Another Game?"
        message:@"TribesPad closes, because iOS doesn't let an app restart itself. Tap TribesPad on the Home Screen and the game picker opens."
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Close TribesPad" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *a){(void)a;
        [NSUserDefaults.standardUserDefaults setObject:@"picker" forKey:@"TribesPad.nextGame"];
        [NSUserDefaults.standardUserDefaults synchronize];
        fprintf(stderr,"TribesPad: Choose Game; the picker opens at the next launch; closing\n");
        exit(0);}]];
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
    // Top right corner, inside the safe area.
    UIEdgeInsets safe=self.window.safeAreaInsets;
    self.fps.frame=CGRectMake(CGRectGetMaxX(self.bounds)-fmax(safe.right,8)-168,fmax(safe.top,6),168,22);
    if(self.quickChat){CGSize q=[self.quickChat sizeThatFits:CGSizeZero];q.height=fmin(q.height,h-20);
        self.quickChat.frame=CGRectMake(x+fmax(margin,24),y+(h-q.height)/2,q.width,q.height);[self bringSubviewToFront:self.quickChat];}
    /* A connected mouse or trackpad points at the game itself (SDL), so the
       trackpad-cursor overlay must not take its clicks. */
    /* Starsiege moves the game's own cursor from the controller (starsiege_input.c). */
    self.trackpad.frame=self.bounds;self.trackpad.hidden=self.expanded||self.inMatch||!(TB_TRACKPAD||(self.controllerUsed&&!tb_starsiege))||(self.mouseConnected&&!self.controllerUsed);
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
void tb_mobile_controls_install_starsiege(void) {
    /* Starsiege's config.cs: W S A D move, Space jets up (jump), G grenade,
       Q next weapon, P pack, I inventory; fire and jet are the mouse buttons. */
    static const SDL_Scancode keys[9]={SDL_SCANCODE_W,SDL_SCANCODE_S,SDL_SCANCODE_A,SDL_SCANCODE_D,SDL_SCANCODE_SPACE,
        SDL_SCANCODE_G,SDL_SCANCODE_Q,SDL_SCANCODE_P,SDL_SCANCODE_I};
    tb_starsiege=YES;tb_mobile_controls_keys(keys);tb_mobile_controls_install();
}
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
