#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>
#include "NativeHost.hpp"
#include "Lifecycle.hpp"
#include "Diagnostics.hpp"
#include "Preferences.hpp"
#include "Joystick.hpp"
#include <cstdio>
#include <ctime>
#include <cstring>
#include <algorithm>

static uint32_t bombSerial=0, pauseSerial=0;
static bool shooting=false, focusing=false;
static float stickX=0,stickY=0;
static bool stickMode=false;
static touhou::ios::Lifecycle lifecycle;
static uint64_t frames=0;
static bool smoke=false;
static void showSettings(UIButton* button);
static void controls(){
    const bool autoFire=touhou::ios::preference(@"autoFire"),autoSlow=touhou::ios::preference(@"autoSlow"),dragFire=touhou::ios::preference(@"dragFire");
    sdl_native_options(autoFire,autoSlow,dragFire,touhou::ios::preference(@"autoBomb"),touhou::ios::preference(@"hitbox"));
    sdl_touch_options(1,0,float(touhou::ios::preference(@"sensitivity")));
    if(!stickMode)sdl_touch_mode((uint32_t)touhou::ios::preference(@"movement"));
    sdl_touch_controls(shooting||autoFire,focusing||autoSlow,bombSerial,pauseSerial,stickX*32767,stickY*32767);
}
static void audioSession(){
    NSError* error=nil;
    AVAudioSession* session=AVAudioSession.sharedInstance;
    BOOL category=[session setCategory:AVAudioSessionCategoryPlayback error:&error];
    if(!category)touhou::ios::diagnostic(@"Audio category error: %@",error);
    error=nil;BOOL enabled=[session setActive:YES error:&error];
    touhou::ios::diagnostic(@"Audio session: active=%d sampleRate=%.0f buffer=%.4f error=%@",enabled,session.sampleRate,session.IOBufferDuration,error);
}
static void buttonAppearance(UIButton* button,bool held){
    const bool action=button.tag<7,rose=button.tag==4||button.tag==6;
    button.backgroundColor=rose?[UIColor colorWithRed:.20 green:.07 blue:.13 alpha:held?.88:.58]:
                                 [UIColor colorWithRed:.09 green:.13 blue:.21 alpha:held?.88:.58];
    button.layer.borderColor=(rose?[UIColor colorWithRed:.86 green:.32 blue:.49 alpha:held?.95:.68]:
                                   [UIColor colorWithRed:.61 green:.75 blue:.88 alpha:held?.95:.62]).CGColor;
    button.transform=action&&held?CGAffineTransformMakeScale(.90,.90):CGAffineTransformIdentity;
    button.alpha=touhou::ios::preference(@"hidden")&&button.tag<7?0:touhou::ios::preference(@"opacity");
}
@interface TH10Joystick : UIControl
@property(nonatomic,strong) UIView* thumb;
@property(nonatomic,strong) UIView* innerRing;
@property(nonatomic) BOOL engaged;
@property(nonatomic) touhou::ios::JoystickSample sample;
- (void)reset;
@end
@implementation TH10Joystick
- (instancetype)initWithFrame:(CGRect)frame {
    self=[super initWithFrame:frame];if(!self)return nil;
    self.multipleTouchEnabled=NO;self.exclusiveTouch=NO;
    self.backgroundColor=[UIColor colorWithRed:.18 green:.16 blue:.32 alpha:.29];
    self.layer.borderWidth=1.5;self.layer.borderColor=[UIColor colorWithRed:.61 green:.70 blue:.84 alpha:.48].CGColor;
    self.innerRing=[[UIView alloc] initWithFrame:CGRectZero];self.innerRing.userInteractionEnabled=NO;
    self.innerRing.backgroundColor=[UIColor colorWithRed:.28 green:.42 blue:.58 alpha:.23];
    self.innerRing.layer.borderWidth=1;self.innerRing.layer.borderColor=[UIColor colorWithRed:.60 green:.73 blue:.88 alpha:.25].CGColor;
    [self addSubview:self.innerRing];
    self.thumb=[[UIView alloc] initWithFrame:CGRectZero];self.thumb.userInteractionEnabled=NO;
    self.thumb.backgroundColor=[UIColor colorWithRed:.68 green:.78 blue:.96 alpha:.58];
    self.thumb.layer.borderWidth=1;self.thumb.layer.borderColor=[UIColor colorWithRed:.86 green:.91 blue:1 alpha:.70].CGColor;
    [self addSubview:self.thumb];
    self.isAccessibilityElement=YES;self.accessibilityLabel=@"移动摇杆";
    self.accessibilityHint=@"按住并拖动以移动角色或选择菜单";
    self.accessibilityIdentifier=@"th10.joystick";
    self.accessibilityTraits=UIAccessibilityTraitAllowsDirectInteraction;
    return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];CGFloat size=std::min(self.bounds.size.width,self.bounds.size.height);
    self.layer.cornerRadius=size/2;self.thumb.bounds=CGRectMake(0,0,56,56);self.thumb.layer.cornerRadius=28;
    self.innerRing.bounds=CGRectMake(0,0,size*.78,size*.78);self.innerRing.layer.cornerRadius=size*.39;
    self.innerRing.center=CGPointMake(CGRectGetMidX(self.bounds),CGRectGetMidY(self.bounds));
    self.thumb.backgroundColor=self.engaged?[UIColor colorWithRed:.82 green:.90 blue:.96 alpha:.83]:
                                          [UIColor colorWithRed:.68 green:.78 blue:.96 alpha:.58];
    self.thumb.transform=self.engaged?CGAffineTransformMakeScale(1.10,1.10):CGAffineTransformIdentity;
    const CGFloat radius=std::max(CGFloat(1),(size-56)/2);
    self.thumb.center=CGPointMake(CGRectGetMidX(self.bounds)+self.sample.knobX*radius,CGRectGetMidY(self.bounds)+self.sample.knobY*radius);
}
- (void)updateTouch:(UITouch*)touch {
    const CGPoint point=[touch locationInView:self];
    const CGFloat radius=std::max(CGFloat(1),(std::min(self.bounds.size.width,self.bounds.size.height)-56)/2);
    self.sample=touhou::ios::joystickSample(float(point.x-CGRectGetMidX(self.bounds)),float(point.y-CGRectGetMidY(self.bounds)),float(radius),float(touhou::ios::preference(@"deadZone")));
    [self setNeedsLayout];[self sendActionsForControlEvents:UIControlEventValueChanged];
}
- (BOOL)beginTrackingWithTouch:(UITouch*)touch withEvent:(UIEvent*)event {
    if(!lifecycle.running())return NO;
    self.engaged=YES;[self updateTouch:touch];return YES;
}
- (BOOL)continueTrackingWithTouch:(UITouch*)touch withEvent:(UIEvent*)event {
    if(!lifecycle.running()||!CGRectContainsPoint(self.bounds,[touch locationInView:self])){[self reset];return NO;}
    [self updateTouch:touch];return YES;
}
- (void)endTrackingWithTouch:(UITouch*)touch withEvent:(UIEvent*)event {[super endTrackingWithTouch:touch withEvent:event];[self reset];}
- (void)cancelTrackingWithEvent:(UIEvent*)event {[super cancelTrackingWithEvent:event];[self reset];}
- (void)reset {
    self.engaged=NO;self.sample=touhou::ios::JoystickSample{};[self setNeedsLayout];[self sendActionsForControlEvents:UIControlEventValueChanged];
}
@end
@interface TH10Controls : UIView
@property(nonatomic,strong) NSArray<UIButton*>* buttons;
@property(nonatomic,strong) TH10Joystick* joystick;
@property(nonatomic) CGRect layoutBounds;
@property(nonatomic) UIEdgeInsets layoutInsets;
@property(nonatomic) uint32_t pressedButtons;
- (void)releaseInputs;
@end
@implementation TH10Controls
- (instancetype)initWithFrame:(CGRect)frame {
    self=[super initWithFrame:frame]; if(!self)return nil;
    self.multipleTouchEnabled=YES; self.backgroundColor=UIColor.clearColor;
    self.joystick=[[TH10Joystick alloc] initWithFrame:CGRectZero];
    [self.joystick addTarget:self action:@selector(joystickChanged:) forControlEvents:UIControlEventValueChanged];
    [self addSubview:self.joystick];
    NSArray* titles=@[@"Z",@"S",@"X",@"",@""];
    NSArray* labels=@[@"射击／确认",@"低速",@"灵击",@"暂停／返回",@"设置"];
    NSArray* identifiers=@[@"th10.fire",@"th10.slow",@"th10.bomb",@"th10.pause",@"th10.settings"];
    NSMutableArray* all=[NSMutableArray array];
    for(NSInteger i=0;i<titles.count;i++){
        UIButton* button=[UIButton buttonWithType:UIButtonTypeSystem];button.tag=i+4;
        [button setTitle:titles[i] forState:UIControlStateNormal];
        [button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
        button.titleLabel.font=[UIFont systemFontOfSize:24 weight:UIFontWeightSemibold];
        button.accessibilityLabel=labels[i];button.accessibilityIdentifier=identifiers[i];
        if(i>=3){
            UIImageSymbolConfiguration* config=[UIImageSymbolConfiguration configurationWithPointSize:19 weight:UIImageSymbolWeightSemibold];
            [button setImage:[UIImage systemImageNamed:i==3?@"pause.fill":@"gearshape" withConfiguration:config] forState:UIControlStateNormal];
            button.tintColor=UIColor.whiteColor;
        }
        button.layer.borderWidth=i<3?1.5:1;buttonAppearance(button,false);
        button.multipleTouchEnabled=NO;button.exclusiveTouch=NO;
        [button addTarget:self action:@selector(down:) forControlEvents:UIControlEventTouchDown];
        [button addTarget:self action:@selector(reenter:) forControlEvents:UIControlEventTouchDragEnter];
        [button addTarget:self action:@selector(up:) forControlEvents:UIControlEventTouchUpInside|UIControlEventTouchUpOutside|UIControlEventTouchCancel|UIControlEventTouchDragExit];
        [self addSubview:button];[all addObject:button];
    }self.buttons=all;return self;
}
- (UIView*)hitTest:(CGPoint)point withEvent:(UIEvent*)event {UIView* hit=[super hitTest:point withEvent:event];return hit==self?nil:hit;}
- (void)layoutSubviews {
    [super layoutSubviews];UIEdgeInsets insets=self.safeAreaInsets;
    if(!CGRectEqualToRect(self.layoutBounds,self.bounds)||!UIEdgeInsetsEqualToEdgeInsets(self.layoutInsets,insets)){
        // Rotation invalidates both UIKit button tracking and SDL drag origins.
        [self releaseInputs];self.layoutBounds=self.bounds;self.layoutInsets=insets;
    }
    CGFloat left=insets.left+16,right=self.bounds.size.width-insets.right-16,bottom=self.bounds.size.height-insets.bottom-16;
    // Follow the earlier TH07 port: large Z at the right thumb, smaller X to
    // its left and S above, with a concentric joystick for the left thumb.
    const CGFloat scale=touhou::ios::preference(@"size");
    const CGFloat radius=std::clamp(std::min(self.bounds.size.width,self.bounds.size.height)*.064*scale,CGFloat(30),CGFloat(70));
    const CGFloat joystickRadius=std::max(CGFloat(60),radius*1.48);
    self.joystick.frame=CGRectMake(left,bottom-joystickRadius*2,joystickRadius*2,joystickRadius*2);
    const CGFloat actionX=right-radius*1.06,actionY=bottom-radius*1.06;
    const CGPoint centers[]={CGPointMake(actionX,actionY),CGPointMake(actionX-radius*.80,actionY-radius*1.95),
                             CGPointMake(actionX-radius*2.0,actionY),CGPointMake(right-22,insets.top+32),CGPointMake(left+22,insets.top+32)};
    const CGFloat diameters[]={radius*2.12,radius*1.68,radius*1.72,44,44};
    for(NSUInteger i=0;i<self.buttons.count;i++){
        UIButton* button=self.buttons[i];button.bounds=CGRectMake(0,0,diameters[i],diameters[i]);button.center=centers[i];
        button.layer.cornerRadius=diameters[i]/2;
        if(i<3)button.titleLabel.font=[UIFont systemFontOfSize:std::clamp(diameters[i]*.36,CGFloat(22),CGFloat(32)) weight:UIFontWeightSemibold];
    }
    const CGFloat width=self.bounds.size.width,height=self.bounds.size.height;
    const bool portrait=height>=width;
    if(!portrait){
        // Landscape is full-screen; safe areas constrain controls only.
        sdl_set_viewport(0,0,1,1);
        return;
    }
    CGFloat ax=insets.left;
    CGFloat ay=insets.top+56;
    CGFloat aw=width-ax-insets.right;
    CGFloat ah=height-ay-insets.bottom-200;
    if(aw>0&&ah>0){
        CGFloat gw=std::min(aw,ah*4/3),gh=gw*3/4;
        sdl_set_viewport(float((ax+(aw-gw)/2)/width),float((ay+(ah-gh)/2)/height),float(gw/width),float(gh/height));
    }
}
- (void)joystickChanged:(TH10Joystick*)joystick {
    // Shared mode 3 suppresses viewport dragging. Use it while the joystick
    // owns movement, then restore mode 0 so normal area dragging still works.
    const bool engaged=joystick.engaged&&lifecycle.running();
    if(engaged!=stickMode){stickMode=engaged;sdl_touch_mode(engaged?3:0);}
    stickX=engaged?joystick.sample.x:0;stickY=engaged?joystick.sample.y:0;
    controls();
}
- (void)down:(UIButton*)button {
    // Diagnostics remain available after a terminal frame result.
    if(button.tag==8){showSettings(button);return;}
    if(!lifecycle.running()||(self.pressedButtons&(1u<<button.tag)))return;
    self.pressedButtons|=1u<<button.tag;
    if(button.tag==4){shooting=true;sdl_key("KeyZ",1);}
    else if(button.tag==5){focusing=true;}
    else if(button.tag==6){++bombSerial;}
    else {++pauseSerial;}
    controls();buttonAppearance(button,true);
}
- (void)reenter:(UIButton*)button {
    // Continuous controls may be re-entered; a Bomb/pause touch fires once.
    if(button.tag<6)[self down:button];
}
- (void)up:(UIButton*)button {
    self.pressedButtons&=~(1u<<button.tag);
    if(button.tag==4){shooting=false;sdl_key("KeyZ",0);}
    else if(button.tag==5)focusing=false;
    controls();buttonAppearance(button,false);
}
- (void)releaseInputs {
    self.pressedButtons=0;shooting=focusing=false;
    [self.joystick cancelTrackingWithEvent:nil];
    stickX=stickY=0;
    sdl_keys_clear();controls();
    SDL_FlushEvents(SDL_EVENT_FINGER_DOWN,SDL_EVENT_FINGER_CANCELED);
    for(UIButton* button in self.buttons){
        [button cancelTrackingWithEvent:nil];button.highlighted=NO;buttonAppearance(button,false);
    }
}
@end
static TH10Controls* overlay;
static bool settingsShowing=false;
static void pauseFor(touhou::ios::Lifecycle::Reason reason,bool paused){
    const bool changed=lifecycle.set(reason,paused);
    touhou::ios::diagnostic(@"Lifecycle reason=%u paused=%d running=%d changed=%d",unsigned(reason),paused,lifecycle.running(),changed);
    // Clear every pause source, including a second source while already paused.
    if(paused||changed)[overlay releaseInputs];
    if(!changed)return;
    if(lifecycle.running())audioSession();
    sdl_loop_pause(lifecycle.running()?0:1);
    touhou::ios::diagnosticsState(@"lifecycle transition / save");
    touhou::ios::diagnosticsFlush();
}
static void showSettings(UIButton* button){
    if(settingsShowing)return;settingsShowing=true;
    pauseFor(touhou::ios::Lifecycle::settings,true);
    touhou::ios::presentSettings(overlay.window.rootViewController,button,^{
        settingsShowing=false;pauseFor(touhou::ios::Lifecycle::settings,false);
    });
}
static void installControls(SDL_Window* window){
    UIWindow* native=(__bridge UIWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER,nullptr);
    UIView* parent=native.rootViewController.view;
    overlay=[[TH10Controls alloc] initWithFrame:parent.bounds];
    overlay.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    [parent addSubview:overlay];
#if TH10_DIAGNOSTICS
    if([NSProcessInfo.processInfo.arguments containsObject:@"--landscape-smoke"]){
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{
            if(@available(iOS 16.0,*)){
                UIWindowSceneGeometryPreferencesIOS* geometry=[[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeLeft];
                [native.windowScene requestGeometryUpdateWithPreferences:geometry errorHandler:^(NSError* error){
                    touhou::ios::diagnostic(@"Landscape smoke rotation failed: %@",error);
                }];
            }
        });
    }
#endif
    NSNotificationCenter* center=NSNotificationCenter.defaultCenter;
    [center addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*){
        pauseFor(touhou::ios::Lifecycle::inactive,true);
    }];
    [center addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*){
        pauseFor(touhou::ios::Lifecycle::inactive,true);
    }];
    [center addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*){
        pauseFor(touhou::ios::Lifecycle::inactive,false);
    }];
    [center addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* note){
        NSNumber* type=note.userInfo[AVAudioSessionInterruptionTypeKey];
        if(!type)return;
        touhou::ios::diagnostic(@"Audio interruption: type=%@ options=%@",type,note.userInfo[AVAudioSessionInterruptionOptionKey]);
        if(type.unsignedIntegerValue==AVAudioSessionInterruptionTypeBegan)
            pauseFor(touhou::ios::Lifecycle::audioInterruption,true);
        else if(type.unsignedIntegerValue==AVAudioSessionInterruptionTypeEnded)
            pauseFor(touhou::ios::Lifecycle::audioInterruption,false);
    }];
    [center addObserverForName:AVAudioSessionRouteChangeNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* note){
        touhou::ios::diagnostic(@"Audio route changed: reason=%@",note.userInfo[AVAudioSessionRouteChangeReasonKey]);
        if(lifecycle.running())audioSession();
    }];
    [center addObserverForName:UIApplicationDidReceiveMemoryWarningNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*){
        touhou::ios::diagnosticsState(@"memory warning");touhou::ios::diagnosticsFlush();
    }];
    [center addObserverForName:UIApplicationWillTerminateNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*){
        touhou::ios::diagnostic(@"Application will terminate");touhou::ios::diagnosticsFlush();
    }];
    pauseFor(touhou::ios::Lifecycle::inactive,UIApplication.sharedApplication.applicationState!=UIApplicationStateActive);
}
static void frame(void*){
    @autoreleasepool {
        if(!lifecycle.running())return;
#if TH10_DIAGNOSTICS
        if(smoke){
            const bool battle=sdl_game_status()[0]==7;
            const bool confirm=frames>=20&&((frames-20)%45)<3;
            sdl_key("KeyZ",battle||confirm);
        }
#endif
        const double started=double(SDL_GetTicksNS())/1000000000.;
        int result=sdl_loop_frame(started);
        if(result){touhou::ios::diagnostic(@"TH10 frame result %d",result);pauseFor(touhou::ios::Lifecycle::stopped,true);}
        touhou::ios::diagnosticsFrame(started,double(SDL_GetTicksNS())/1000000000.-started,++frames);
    }
}
int main(int argc,char** argv){
    @autoreleasepool {
        touhou::ios::diagnosticsStart();
#if TH10_DIAGNOSTICS
        for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--smoke"))smoke=true;
#endif
        SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight Portrait");
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
        touhou::ios::diagnostic(@"SDL initialization begins; smoke=%d",smoke);
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS|SDL_INIT_JOYSTICK)){touhou::ios::diagnostic(@"SDL initialization failed: %s",SDL_GetError());touhou::ios::diagnosticsFlush();return 1;}
        touhou::ios::diagnostic(@"SDL initialized; opening simplified-Chinese game");
        audioSession();UIApplication.sharedApplication.idleTimerDisabled=YES;
        auto* game=sdl_game_open(1,smoke?12345:uint32_t(std::time(nullptr)));
        if(!game){touhou::ios::diagnostic(@"TH10 initialization failed: %s file=%s",SDL_GetError(),sdl_file_error());touhou::ios::diagnosticsFlush();return 2;}
        auto* window=static_cast<SDL_Window*>(sdl_window());
        if(!window){touhou::ios::diagnostic(@"TH10 window missing");touhou::ios::diagnosticsFlush();return 3;}
        sdl_touch_options(1,0,1);sdl_touch_gestures(0,0);controls();
        sdl_loop_start(game);installControls(window);
        if(!SDL_SetiOSAnimationCallback(window,1,frame,nullptr)){touhou::ios::diagnostic(@"TH10 display callback failed: %s",SDL_GetError());touhou::ios::diagnosticsFlush();return 4;}
        touhou::ios::diagnostic(@"TH10 native started, CHS, logic=60Hz, ARM64 device / simulator diagnostic separated");
        touhou::ios::diagnosticsState(@"startup complete");touhou::ios::diagnosticsFlush();
        return 0;
    }
}
