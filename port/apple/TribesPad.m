#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#import <GameController/GameController.h>
#include "input.h"
#include "server_query.h"
#include "slice_runtime.h"
static TBInput input;
static void tb_log_input(NSString*phase,unsigned source){
#if !TARGET_OS_OSX
 NSString*dir=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
 NSString*path=[dir stringByAppendingPathComponent:@"input-events.jsonl"];
 NSDictionary*event=@{@"phase":phase,@"source":@(source),@"actions":@(input.sources[source].actions),@"moveX":@(input.sources[source].x),@"moveY":@(input.sources[source].y),@"lookX":@(input.sources[source].lookX),@"lookY":@(input.sources[source].lookY)};
 NSMutableData*data=[[NSJSONSerialization dataWithJSONObject:event options:0 error:nil] mutableCopy];[data appendData:[@"\n" dataUsingEncoding:NSUTF8StringEncoding]];
 if(![NSFileManager.defaultManager fileExistsAtPath:path])[NSFileManager.defaultManager createFileAtPath:path contents:nil attributes:nil];
 NSFileHandle*f=[NSFileHandle fileHandleForWritingAtPath:path];[f seekToEndOfFile];[f writeData:data];[f closeFile];
#endif
}
static uint32_t gameVersion(void){uint8_t stack[16]={0};TBState s={0};s.r[4]=0x3000000;s.regions[0]=(TBRegion){0x3000000,sizeof(stack),stack};tb_game_version(&s);return s.r[0];}
static uint32_t crcCheck(void){uint32_t table[256];for(unsigned n=0;n<256;n++){uint32_t v=n;for(int k=0;k<8;k++)v=(v>>1)^((v&1)?0xedb88320:0);table[n]=v;}uint8_t data[]="123456789";uint32_t stack[64]={0};TBState s={0};s.r[4]=0x3000080;stack[32]=0x4000000;stack[33]=0;stack[34]=0x2000000;stack[35]=9;s.regions[0]=(TBRegion){0x7c4568,sizeof(table),(uint8_t*)table};s.regions[1]=(TBRegion){0x2000000,sizeof(data),data};s.regions[2]=(TBRegion){0x3000000,sizeof(stack),(uint8_t*)stack};tb_crc32(&s);return s.r[0];}
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
@interface TBMacView:NSView
@property NSTextField *status;
@end
@implementation TBMacView
-(BOOL)acceptsFirstResponder{return YES;}
-(void)flagsChanged:(NSEvent*)e{tb_input_action(&input,10,TBJet,!!(e.modifierFlags&NSEventModifierFlagShift));[self refresh];}
-(void)keyDown:(NSEvent*)e {unsigned move=e.keyCode==13?1:e.keyCode==1?2:e.keyCode==0?4:e.keyCode==2?8:0;if(move)tb_input_key_move(&input,10,move,1);uint32_t action=e.keyCode==49?TBJump:e.keyCode==56?TBJet:0;if(action)tb_input_action(&input,10,action,1);[self refresh];}
-(void)keyUp:(NSEvent*)e {unsigned move=e.keyCode==13?1:e.keyCode==1?2:e.keyCode==0?4:e.keyCode==2?8:0;if(move)tb_input_key_move(&input,10,move,0);uint32_t action=e.keyCode==49?TBJump:e.keyCode==56?TBJet:0;if(action)tb_input_action(&input,10,action,0);[self refresh];}
-(void)mouseDown:(NSEvent*)e{tb_input_action(&input,11,TBFire,1);[self refresh];}
-(void)mouseUp:(NSEvent*)e{tb_input_action(&input,11,TBFire,0);[self refresh];}
-(void)refresh{TBInputFrame f=tb_input_frame(&input);self.status.stringValue=[NSString stringWithFormat:@"TribesPad · engine integration diagnostic\nOriginal build: %u · CRC32: %08x\n\nNative ARM64 slices verified. Whole game is not integrated yet.\n\nWASD: move · Space: ski/jump · Shift: jet · Mouse: fire\nMove: %+.2f %+.2f · Action mask: %02x",gameVersion(),crcCheck(),f.x,f.y,f.actions];}
@end
@interface TBDelegate:NSObject<NSApplicationDelegate>
@property NSWindow*window;
@end
@implementation TBDelegate
-(void)applicationDidFinishLaunching:(NSNotification*)n{self.window=[[NSWindow alloc]initWithContentRect:NSMakeRect(0,0,900,520) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];self.window.title=@"TribesPad";TBMacView*v=[[TBMacView alloc]initWithFrame:self.window.contentView.bounds];v.status=[NSTextField labelWithString:@""];v.status.frame=NSMakeRect(40,80,820,380);v.status.font=[NSFont monospacedSystemFontOfSize:18 weight:NSFontWeightRegular];v.status.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;[v addSubview:v.status];self.window.contentView=v;[v refresh];[self.window makeFirstResponder:v];[self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];}
-(void)applicationDidResignActive:(NSNotification*)n{tb_input_reset(&input);[(TBMacView*)self.window.contentView refresh];}
-(BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)a{return YES;}
@end
int main(int argc,char**argv){@autoreleasepool{if(argc>1&&!strcmp(argv[1],"--self-test")){uint32_t v=gameVersion(),crc=crcCheck();printf("build=%u crc=%08x\n",v,crc);return v==25034&&crc==0xcbf43926?0:1;}NSApplication*a=NSApplication.sharedApplication;a.activationPolicy=NSApplicationActivationPolicyRegular;TBDelegate*d=[TBDelegate new];a.delegate=d;[a run];}}
#else
#import <UIKit/UIKit.h>
@interface TBPad:UIView
@property unsigned source;
@property uint32_t action;
@property BOOL movement;
@property UILabel*label;
@property UITouch*active;
@property CGPoint previous;
@end
@implementation TBPad
-(instancetype)initWithLabel:(NSString*)text source:(unsigned)s action:(uint32_t)a movement:(BOOL)m{if((self=[super initWithFrame:CGRectZero])){self.source=s;self.action=a;self.movement=m;self.multipleTouchEnabled=YES;self.backgroundColor=[UIColor colorWithWhite:0.12 alpha:1];self.layer.cornerRadius=22;self.layer.borderWidth=1;self.layer.borderColor=[UIColor colorWithWhite:0.4 alpha:1].CGColor;self.label=[UILabel new];self.label.text=text;self.label.textColor=UIColor.whiteColor;self.label.font=[UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];self.label.textAlignment=NSTextAlignmentCenter;[self addSubview:self.label];self.accessibilityLabel=text;self.isAccessibilityElement=YES;}return self;}
-(void)layoutSubviews{[super layoutSubviews];self.label.frame=self.bounds;}
-(void)update:(UITouch*)t{CGPoint p=[t locationInView:self];if(self.movement){CGFloat radius=MIN(self.bounds.size.width,self.bounds.size.height)*0.4;tb_input_move(&input,self.source,(p.x-self.bounds.size.width/2)/radius,(self.bounds.size.height/2-p.y)/radius,0.10);}else if(self.action==0||self.action==TBFire){CGFloat sensitivity=[NSUserDefaults.standardUserDefaults floatForKey:@"lookSensitivity"];if(sensitivity<=0)sensitivity=0.18;tb_input_look(&input,self.source,(p.x-self.previous.x)*sensitivity,(p.y-self.previous.y)*sensitivity);}self.previous=p;tb_log_input(@"move",self.source);}
-(void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e{if(self.active)return;self.active=touches.anyObject;self.previous=[self.active locationInView:self];tb_input_action(&input,self.source,self.action,1);[self update:self.active];tb_log_input(@"down",self.source);self.backgroundColor=[UIColor colorWithRed:0.1 green:0.3 blue:0.38 alpha:1];}
-(void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e{if([touches containsObject:self.active])[self update:self.active];}
-(void)finish:(NSSet<UITouch*>*)touches{if([touches containsObject:self.active]){tb_input_release(&input,self.source);tb_log_input(@"up-or-cancel",self.source);self.active=nil;self.backgroundColor=[UIColor colorWithWhite:0.12 alpha:1];}}
-(void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e{if([touches containsObject:self.active]){[self update:self.active];tb_input_end(&input,self.source);tb_log_input(@"up",self.source);self.active=nil;self.backgroundColor=[UIColor colorWithWhite:0.12 alpha:1];}}
-(void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e{[self finish:touches];}
@end
@interface TBViewController:UIViewController
@property UILabel*status;
@property UILabel*instructions;
@property NSArray<TBPad*>*pads;
@property CADisplayLink*clock;
@property GCController*controller;
@property CGPoint controllerLook;
@property NSString*lastResult;
@property UIButton*reset;
@property UIButton*network;
@property UILabel*networkStatus;
@end
@implementation TBViewController
-(void)viewDidLoad{[super viewDidLoad];self.view.backgroundColor=[UIColor colorWithWhite:0.04 alpha:1];self.status=[UILabel new];self.status.textColor=UIColor.whiteColor;self.status.font=[UIFont monospacedSystemFontOfSize:16 weight:UIFontWeightMedium];self.status.numberOfLines=0;self.status.textAlignment=NSTextAlignmentCenter;[self.view addSubview:self.status];self.instructions=[UILabel new];self.instructions.text=@"ENGINE INTEGRATION DIAGNOSTIC\nHold SKI + JET + FIRE together. Drag FIRE to aim.\nWhole game rendering and online play are still pending.";self.instructions.numberOfLines=0;self.instructions.textAlignment=NSTextAlignmentCenter;self.instructions.textColor=[UIColor colorWithWhite:0.65 alpha:1];self.instructions.font=[UIFont systemFontOfSize:15];[self.view addSubview:self.instructions];self.pads=@[[[TBPad alloc]initWithLabel:@"MOVE" source:0 action:0 movement:YES],[[TBPad alloc]initWithLabel:@"LOOK" source:1 action:0 movement:NO],[[TBPad alloc]initWithLabel:@"FIRE + AIM" source:2 action:TBFire movement:NO],[[TBPad alloc]initWithLabel:@"JET" source:3 action:TBJet movement:NO],[[TBPad alloc]initWithLabel:@"SKI / JUMP" source:4 action:TBJump movement:NO],[[TBPad alloc]initWithLabel:@"GRENADE" source:5 action:TBGrenade movement:NO],[[TBPad alloc]initWithLabel:@"WEAPON" source:6 action:TBWeapon movement:NO],[[TBPad alloc]initWithLabel:@"USE" source:7 action:TBUse movement:NO],[[TBPad alloc]initWithLabel:@"INVENTORY" source:8 action:TBInventory movement:NO]];for(UIView*p in self.pads)[self.view addSubview:p];self.reset=[UIButton buttonWithType:UIButtonTypeSystem];[self.reset setTitle:@"Reset input" forState:UIControlStateNormal];[self.reset addTarget:self action:@selector(resetInput) forControlEvents:UIControlEventTouchUpInside];[self.view addSubview:self.reset];self.network=[UIButton buttonWithType:UIButtonTypeSystem];[self.network setTitle:@"Probe servers" forState:UIControlStateNormal];[self.network addTarget:self action:@selector(probeServers) forControlEvents:UIControlEventTouchUpInside];[self.view addSubview:self.network];self.networkStatus=[UILabel new];self.networkStatus.text=@"Server probe checks reachability; joining a match is pending.";self.networkStatus.numberOfLines=0;self.networkStatus.textAlignment=NSTextAlignmentCenter;self.networkStatus.font=[UIFont systemFontOfSize:13];self.networkStatus.textColor=UIColor.systemTealColor;[self.view addSubview:self.networkStatus];self.clock=[CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];[self.clock addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];NSNotificationCenter*n=NSNotificationCenter.defaultCenter;[n addObserver:self selector:@selector(connect:) name:GCControllerDidConnectNotification object:nil];[n addObserver:self selector:@selector(disconnect:) name:GCControllerDidDisconnectNotification object:nil];[n addObserver:self selector:@selector(resetInput) name:UIApplicationWillResignActiveNotification object:nil];for(GCController*c in GCController.controllers)[self attach:c];[self configureMouse];tb_log_input(@"launch",0);uint32_t v=gameVersion(),crc=crcCheck();self.lastResult=[NSString stringWithFormat:@"AOT SELF TEST %@ · build %u · CRC %08x",v==25034&&crc==0xcbf43926?@"PASS":@"FAIL",v,crc];NSLog(@"%@",self.lastResult);}
-(void)viewDidLayoutSubviews{[super viewDidLayoutSubviews];CGRect b=UIEdgeInsetsInsetRect(self.view.bounds,self.view.safeAreaInsets);BOOL compact=b.size.width<800;CGFloat pad=compact?100:150,gap=12,h=compact?48:58;CGFloat left=b.origin.x+16,right=CGRectGetMaxX(b)-16;CGFloat bottom=CGRectGetMaxY(b)-16;self.pads[0].frame=CGRectMake(left,bottom-pad,pad,pad);self.pads[1].frame=CGRectMake(right-pad,bottom-pad,pad,pad);CGFloat buttonWidth=compact?104:126;self.pads[2].frame=CGRectMake(right-pad-buttonWidth-gap,bottom-h,buttonWidth,h);self.pads[3].frame=CGRectMake(right-pad-buttonWidth-gap,bottom-2*h-gap,buttonWidth,h);self.pads[4].frame=CGRectMake(left+pad+gap,bottom-h,buttonWidth,h);self.pads[5].frame=CGRectMake(left+pad+gap,bottom-2*h-gap,buttonWidth,h);CGFloat topWidth=MAX(44,MIN(buttonWidth,(b.size.width-32-230-3*gap)/3));for(int n=6;n<9;n++)self.pads[n].frame=CGRectMake(left+(n-6)*(topWidth+gap),b.origin.y+16,topWidth,h);self.instructions.font=[UIFont systemFontOfSize:compact?13:15];self.instructions.text=compact?@"Hold SKI + JET + FIRE. Drag FIRE to aim.\nGame rendering and joining matches are pending.":@"ENGINE INTEGRATION DIAGNOSTIC\nHold SKI + JET + FIRE together. Drag FIRE to aim.\nWhole game rendering and online play are still pending.";self.reset.frame=CGRectMake(right-110,b.origin.y+16,110,h);self.network.frame=CGRectMake(right-225,b.origin.y+16,110,h);CGFloat centerTop=b.origin.y+h+28;CGFloat centerBottom=bottom-pad-10;self.status.frame=CGRectMake(left,centerTop,b.size.width-32,MAX(65,(centerBottom-centerTop)*0.55));self.instructions.frame=CGRectMake(left,CGRectGetMaxY(self.status.frame),b.size.width-32,MAX(45,(centerBottom-centerTop)*0.45)-35);self.networkStatus.frame=CGRectMake(left,CGRectGetMaxY(self.instructions.frame),b.size.width-32,35);}
-(UIInterfaceOrientationMask)supportedInterfaceOrientations{return UIInterfaceOrientationMaskLandscape;}
-(BOOL)prefersStatusBarHidden{return YES;}
-(void)probeServers{
 self.network.enabled=NO;self.networkStatus.text=@"Discovering community servers…";
 NSMutableURLRequest*request=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"https://www.tribesnext.com/list.json"] cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:12];
 [[[NSURLSession sharedSession] dataTaskWithRequest:request completionHandler:^(NSData*data,NSURLResponse*response,NSError*error){
  NSMutableArray<NSString*>*lines=[NSMutableArray new];id servers=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
  if(![servers isKindOfClass:NSArray.class]){[lines addObject:@"Server discovery failed. Try again."];}
  else {for(id server in servers){if(![server isKindOfClass:NSDictionary.class])continue;NSString*name=server[@"info_hostname"],*address=server[@"s_ipa"];if(![name isKindOfClass:NSString.class]||![address isKindOfClass:NSString.class])continue;if([name rangeOfString:@"DAD TRIBES" options:NSCaseInsensitiveSearch].location==NSNotFound&&[name rangeOfString:@"Legacy CTF" options:NSCaseInsensitiveSearch].location==NSNotFound)continue;NSArray*parts=[address componentsSeparatedByString:@":"];if(parts.count!=2)continue;NSInteger port=[parts[1] integerValue];if(port<=0||port>65535)continue;TBServerPing r={0};char failure[256]={0};BOOL ok=tb_server_ping([parts[0] UTF8String],(uint16_t)port,3000,&r,failure,sizeof(failure));[lines addObject:ok?[NSString stringWithFormat:@"%@ · %.0f ms · protocol %u · build %u",name,r.pingMS,r.protocol,r.build]:[NSString stringWithFormat:@"%@ · no valid UDP reply",name]];if(lines.count==2)break;}}
  if(!lines.count)[lines addObject:@"Selected community servers are absent from the current list."];
  NSString*result=[lines componentsJoinedByString:@"\n"];dispatch_async(dispatch_get_main_queue(),^{self.networkStatus.text=result;self.network.enabled=YES;NSLog(@"TRIBESPAD SERVER PROBE %@",result);NSString*dir=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;[result writeToFile:[dir stringByAppendingPathComponent:@"server-probe.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil];});
 }] resume];
}
-(void)resetInput{tb_input_reset(&input);self.controllerLook=CGPointZero;for(TBPad*p in self.pads){p.active=nil;p.backgroundColor=[UIColor colorWithWhite:0.12 alpha:1];}}
-(void)connect:(NSNotification*)n{[self attach:n.object];}
-(void)disconnect:(NSNotification*)n{if(n.object==self.controller){tb_input_release(&input,9);self.controller=nil;self.controllerLook=CGPointZero;}}
-(void)attach:(GCController*)c{if(!c.extendedGamepad)return;self.controller=c;c.handlerQueue=dispatch_get_main_queue();__weak TBViewController*w=self;GCExtendedGamepad*g=c.extendedGamepad;g.leftThumbstick.valueChangedHandler=^(GCControllerDirectionPad*d,float x,float y){tb_input_move(&input,9,x,y,0.15);};g.rightThumbstick.valueChangedHandler=^(GCControllerDirectionPad*d,float x,float y){w.controllerLook=CGPointMake(fabsf(x)>0.15?x:0,fabsf(y)>0.15?-y:0);};NSArray*buttons=@[g.rightTrigger,g.leftTrigger,g.buttonA,g.leftShoulder,g.rightShoulder,g.buttonX,g.buttonY,g.rightThumbstickButton?:g.buttonB];uint32_t actions[]={TBFire,TBJet,TBJump,TBGrenade,TBWeapon,TBUse,TBInventory,TBZoom};for(NSUInteger n=0;n<buttons.count;n++){GCControllerButtonInput*b=buttons[n];uint32_t a=actions[n];b.pressedChangedHandler=^(GCControllerButtonInput*b,float v,BOOL pressed){tb_input_action(&input,9,a,pressed);};}}
-(void)configureMouse{NSNotificationCenter*n=NSNotificationCenter.defaultCenter;[n addObserverForName:GCMouseDidConnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*note){[self mouse:note.object];}];[n addObserverForName:GCMouseDidDisconnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*note){tb_input_release(&input,11);}];for(GCMouse*m in GCMouse.mice)[self mouse:m];}
-(void)mouse:(GCMouse*)m{m.handlerQueue=dispatch_get_main_queue();m.mouseInput.mouseMovedHandler=^(GCMouseInput*i,float x,float y){tb_input_look(&input,11,x*0.18,-y*0.18);};m.mouseInput.leftButton.pressedChangedHandler=^(GCControllerButtonInput*b,float v,BOOL p){tb_input_action(&input,11,TBFire,p);};m.mouseInput.rightButton.pressedChangedHandler=^(GCControllerButtonInput*b,float v,BOOL p){tb_input_action(&input,11,TBJet,p);};}
-(void)key:(UIPress*)p down:(BOOL)down{UIKeyboardHIDUsage k=p.key.keyCode;uint32_t a=k==UIKeyboardHIDUsageKeyboardSpacebar?TBJump:k==UIKeyboardHIDUsageKeyboardLeftShift?TBJet:k==UIKeyboardHIDUsageKeyboardG?TBGrenade:k==UIKeyboardHIDUsageKeyboardE?TBUse:k==UIKeyboardHIDUsageKeyboardQ?TBWeapon:k==UIKeyboardHIDUsageKeyboardI?TBInventory:0;if(a)tb_input_action(&input,10,a,down);unsigned move=k==UIKeyboardHIDUsageKeyboardW?1:k==UIKeyboardHIDUsageKeyboardS?2:k==UIKeyboardHIDUsageKeyboardA?4:k==UIKeyboardHIDUsageKeyboardD?8:0;if(move)tb_input_key_move(&input,10,move,down);}
-(void)pressesBegan:(NSSet<UIPress*>*)p withEvent:(UIPressesEvent*)e{for(UIPress*k in p)[self key:k down:YES];}
-(void)pressesEnded:(NSSet<UIPress*>*)p withEvent:(UIPressesEvent*)e{for(UIPress*k in p)[self key:k down:NO];}
-(void)pressesCancelled:(NSSet<UIPress*>*)p withEvent:(UIPressesEvent*)e{for(UIPress*k in p)[self key:k down:NO];}
-(void)tick:(CADisplayLink*)c{CGFloat dt=MIN(c.targetTimestamp-c.timestamp,0.05);tb_input_look(&input,9,self.controllerLook.x*180*dt,self.controllerLook.y*180*dt);TBInputFrame f=tb_input_frame(&input);self.status.text=[NSString stringWithFormat:@"TribesPad\n%@\nMOVE %+.2f %+.2f · LOOK %+.2f %+.2f\nSKI %@ · JET %@ · FIRE %@",self.lastResult,f.x,f.y,f.lookX,f.lookY,f.actions&TBJump?@"ON":@"—",f.actions&TBJet?@"ON":@"—",f.actions&TBFire?@"ON":@"—"];}
@end
@interface TBDelegate:UIResponder<UIApplicationDelegate>
@property UIWindow*window;
@end
@implementation TBDelegate
-(BOOL)application:(UIApplication*)a didFinishLaunchingWithOptions:(NSDictionary*)o{self.window=[[UIWindow alloc]initWithFrame:UIScreen.mainScreen.bounds];self.window.rootViewController=[TBViewController new];[self.window makeKeyAndVisible];return YES;}
@end
int main(int argc,char**argv){@autoreleasepool{return UIApplicationMain(argc,argv,nil,NSStringFromClass(TBDelegate.class));}}
#endif
