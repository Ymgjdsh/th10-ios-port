#import "Diagnostics.hpp"
#import <AVFoundation/AVFoundation.h>
#include "NativeHost.hpp"
#include "NativePerformance.hpp"
#include "Renderer.hpp"
#include "Preferences.hpp"
#include "NativeOptions.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <signal.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr off_t maxLogBytes=1024*1024;
int logFD=-1,consoleFD=-1,stderrReadFD=-1;
std::mutex logMutex;
std::mutex stderrMutex;
NSURL* logDirectory;
dispatch_source_t stderrSource;
SDL_LogOutputFunction originalSDLLog=nullptr;
void* originalSDLUserdata=nullptr;
NSUncaughtExceptionHandler* originalExceptionHandler=nullptr;
constexpr int fatalSignals[]={SIGABRT,SIGSEGV,SIGBUS,SIGILL,SIGFPE};
struct sigaction originalSignalActions[5]{};
#if TH10_DIAGNOSTICS
std::uint64_t observedCallbacks=0;
void startSettingsSmoke();
#endif

void writeAll(int fd,const void* bytes,size_t length){
    const char* data=static_cast<const char*>(bytes);
    while(fd>=0&&length){
        const ssize_t written=write(fd,data,length);
        if(written<0&&errno==EINTR)continue;
        if(written<=0)break;
        data+=written;length-=size_t(written);
    }
}
void appendLog(const void* bytes,size_t length){
    std::lock_guard<std::mutex> guard(logMutex);
    if(logFD<0)return;
    struct stat status{};
    if(fstat(logFD,&status)==0&&status.st_size+off_t(length)>maxLogBytes){
        // Retain the startup section and the newest half MiB when bounded.
        constexpr size_t headerBytes=16*1024,tailBytes=512*1024;
        std::vector<char> header(headerBytes),tail(tailBytes);
        const ssize_t head=pread(logFD,header.data(),header.size(),0);
        const ssize_t end=pread(logFD,tail.data(),tail.size(),std::max<off_t>(headerBytes,status.st_size-tailBytes));
        if(head>=0&&end>=0&&ftruncate(logFD,0)==0){
            writeAll(logFD,header.data(),size_t(head));
            constexpr char marker[]="\n--- older middle log entries removed to bound disk usage ---\n";
            writeAll(logFD,marker,sizeof(marker)-1);
            writeAll(logFD,tail.data(),size_t(end));
        }
    }
    writeAll(logFD,bytes,length);
}
void drainStderr(){
    // Flush/export waits for a reader that already consumed pipe bytes to
    // finish appending them before it snapshots the log.
    std::lock_guard<std::mutex> guard(stderrMutex);
    char bytes[4096];
    for(unsigned block=0;block<16;block++){
        const ssize_t count=read(stderrReadFD,bytes,sizeof(bytes));
        if(count<0&&errno==EINTR)continue;
        if(count<=0)break;
        writeAll(consoleFD,bytes,size_t(count));
        appendLog(bytes,size_t(count));
    }
}
void captureStderr(){
    consoleFD=dup(STDERR_FILENO);
    int descriptors[2];
    if(consoleFD<0)return;
    const int consoleFlags=fcntl(consoleFD,F_GETFL);
    if(consoleFlags<0||fcntl(consoleFD,F_SETFL,consoleFlags|O_NONBLOCK)<0){close(consoleFD);consoleFD=-1;return;}
#ifdef F_SETNOSIGPIPE
    fcntl(consoleFD,F_SETNOSIGPIPE,1);
#endif
    if(pipe(descriptors)!=0)return;
    stderrReadFD=descriptors[0];
    const int flags=fcntl(stderrReadFD,F_GETFL);
    const int writeFlags=fcntl(descriptors[1],F_GETFL);
    // A stopped console consumer or stderr flood must never block gameplay.
    // Direct diagnostic records are still persisted if console output drops.
    if(flags<0||writeFlags<0||fcntl(stderrReadFD,F_SETFL,flags|O_NONBLOCK)<0||
       fcntl(descriptors[1],F_SETFL,writeFlags|O_NONBLOCK)<0){close(descriptors[0]);close(descriptors[1]);stderrReadFD=-1;return;}
    if(dup2(descriptors[1],STDERR_FILENO)<0){close(descriptors[0]);close(descriptors[1]);stderrReadFD=-1;return;}
    close(descriptors[1]);setvbuf(stderr,nullptr,_IONBF,0);
    stderrSource=dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,uintptr_t(stderrReadFD),0,
                                       dispatch_get_global_queue(QOS_CLASS_UTILITY,0));
    if(!stderrSource){dup2(consoleFD,STDERR_FILENO);close(stderrReadFD);stderrReadFD=-1;return;}
    dispatch_source_set_event_handler(stderrSource,^{drainStderr();});
    dispatch_resume(stderrSource);
}
void SDLCALL captureSDL(void*,int category,SDL_LogPriority priority,const char* message){
    touhou::ios::diagnostic(@"SDL category=%d priority=%d %s",category,int(priority),message?message:"");
    if(originalSDLLog)originalSDLLog(originalSDLUserdata,category,priority,message);
}
void captureException(NSException* exception){
    touhou::ios::diagnostic(@"Uncaught exception %@: %@\n%@",exception.name,exception.reason,exception.callStackSymbols);
    touhou::ios::diagnosticsFlush();
    if(originalExceptionHandler)originalExceptionHandler(exception);
}
void captureFatalSignal(int number){
    // Only async-signal-safe I/O here: preserve pending stderr before abort.
    char bytes[4096];ssize_t count;
    for(unsigned block=0;block<16&&stderrReadFD>=0&&(count=read(stderrReadFD,bytes,sizeof(bytes)))>0;block++){
        writeAll(consoleFD,bytes,size_t(count));writeAll(logFD,bytes,size_t(count));
    }
    char marker[]="\nTH10 fatal signal 00 (see iOS crash report for stack)\n";
    marker[19]=char('0'+number/10);marker[20]=char('0'+number%10);
    writeAll(consoleFD,marker,sizeof(marker)-1);writeAll(logFD,marker,sizeof(marker)-1);
    if(logFD>=0)fsync(logFD);
    for(size_t i=0;i<5;i++)if(fatalSignals[i]==number){sigaction(number,&originalSignalActions[i],nullptr);break;}
    // Re-raise on the faulting thread, not an arbitrary process thread.
    raise(number);
}
NSURL* exportSnapshot(NSError** error){
    if(!logDirectory||logFD<0){
        if(error)*error=[NSError errorWithDomain:@"TH10Diagnostics" code:1 userInfo:@{NSLocalizedDescriptionKey:@"无法访问本机日志目录。请检查存储空间后重新打开游戏。"}];
        return nil;
    }
    touhou::ios::diagnosticsState(@"export requested");
    touhou::ios::diagnosticsFlush();
    NSMutableData* combined=[NSMutableData data];
    {
        std::lock_guard<std::mutex> guard(logMutex);
        for(NSString* name in @[@"previous.log",@"current.log"]){
            NSData* label=[[NSString stringWithFormat:@"\n========== %@ ==========\n",name] dataUsingEncoding:NSUTF8StringEncoding];
            [combined appendData:label];
            NSError* readError=nil;
            NSData* content=[NSData dataWithContentsOfURL:[logDirectory URLByAppendingPathComponent:name] options:0 error:&readError];
            if(!content&&([name isEqualToString:@"current.log"]||readError.code!=NSFileReadNoSuchFileError)){
                if(error)*error=readError;return nil;
            }
            [combined appendData:content?:[@"(No previous run is available.)\n" dataUsingEncoding:NSUTF8StringEncoding]];
        }
    }
    NSURL* file=[logDirectory URLByAppendingPathComponent:@"TH10-diagnostics.txt"];
    if(![combined writeToURL:file options:NSDataWritingAtomic error:error])return nil;
    return file;
}
}

namespace touhou::ios {
void diagnostic(NSString* format,...){
    @autoreleasepool {
        va_list arguments;va_start(arguments,format);
        NSString* message=[[NSString alloc] initWithFormat:format arguments:arguments];
        va_end(arguments);
        if(message.length>8192)message=[[message substringToIndex:8192] stringByAppendingString:@" [truncated]"];
        NSString* line=[NSString stringWithFormat:@"[%@] %@\n",NSDate.date,message];
        NSData* data=[line dataUsingEncoding:NSUTF8StringEncoding];
        appendLog(data.bytes,data.length);
        writeAll(consoleFD>=0?consoleFD:STDERR_FILENO,data.bytes,data.length);
    }
}
void diagnosticsFlush(){
    fflush(stderr);
    if(stderrReadFD>=0)drainStderr();
    std::lock_guard<std::mutex> guard(logMutex);
    if(logFD>=0)fsync(logFD);
}
void diagnosticsStart(){
    if(logFD>=0)return; // Reinitializing would capture our own SDL callback.
    NSFileManager* manager=NSFileManager.defaultManager;
    NSURL* documents=[manager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    logDirectory=[documents URLByAppendingPathComponent:@"Diagnostics" isDirectory:YES];
    NSError* error=nil;
    if(![manager createDirectoryAtURL:logDirectory withIntermediateDirectories:YES attributes:nil error:&error]){
        NSLog(@"TH10 cannot create diagnostic directory: %@",error);return;
    }
    [logDirectory setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nullptr];
    NSURL* current=[logDirectory URLByAppendingPathComponent:@"current.log"];
    NSURL* previous=[logDirectory URLByAppendingPathComponent:@"previous.log"];
    if([manager fileExistsAtPath:current.path]){
        [manager removeItemAtURL:previous error:nullptr];
        if(![manager moveItemAtURL:current toURL:previous error:&error]){
            NSLog(@"TH10 cannot preserve previous diagnostic log: %@",error);return;
        }
    }
    logFD=open(current.fileSystemRepresentation,O_CREAT|O_RDWR|O_APPEND,0600);
    if(logFD<0){NSLog(@"TH10 cannot open diagnostic log: %s",std::strerror(errno));return;}
    [manager setAttributes:@{NSFileProtectionKey:NSFileProtectionCompleteUntilFirstUserAuthentication}
             ofItemAtPath:current.path error:nullptr];
    captureStderr();
    SDL_GetLogOutputFunction(&originalSDLLog,&originalSDLUserdata);
    SDL_SetLogOutputFunction(captureSDL,nullptr);
    originalExceptionHandler=NSGetUncaughtExceptionHandler();
    NSSetUncaughtExceptionHandler(captureException);
    for(size_t i=0;i<5;i++){
        struct sigaction action{};action.sa_handler=captureFatalSignal;sigemptyset(&action.sa_mask);
        sigaction(fatalSignals[i],&action,&originalSignalActions[i]);
    }
    struct utsname hardware{};uname(&hardware);
    char osBuild[128]{};size_t size=sizeof(osBuild);sysctlbyname("kern.osversion",osBuild,&size,nullptr,0);
    NSDictionary* info=NSBundle.mainBundle.infoDictionary;
    diagnostic(@"TH10 session started; device=%s model=%@ OS=%@ build=%s app=%@ (%@) diagnostics=%@",
               hardware.machine,UIDevice.currentDevice.model,UIDevice.currentDevice.systemVersion,osBuild,
               info[@"CFBundleShortVersionString"],info[@"CFBundleVersion"],info[@"TH10Diagnostics"]);
    diagnostic(@"Logs are local only; current and previous runs retain at most 1 MiB each; game assets/saves are never exported.");
    diagnosticsFlush();
#if TH10_DIAGNOSTICS
    if([NSProcessInfo.processInfo.arguments containsObject:@"--settings-smoke"])startSettingsSmoke();
#endif
}
void diagnosticsState(NSString* reason){
    const int32_t* state=sdl_game_status();
    diagnostic(@"State %@: screen=%d stage=%d error=%d lives=%d power=%d context=%d touch=%d fire=%d focus=%d raw=%d loopPaused=%d fileError=%s SDL=%s",
               reason,state[0],state[1],state[2],state[3],state[4],state[5],state[6],state[7],state[8],state[9],
               sdl_loop_is_paused(),sdl_file_error(),SDL_GetError());
}
void diagnosticsFrame(double timestamp,double workSeconds,std::uint64_t callbacks){
#if TH10_DIAGNOSTICS
    observedCallbacks=callbacks;
#endif
    static double begin=0,previous=0,workSum=0,workMax=0,gapMax=0;
    static unsigned count=0;
    if(!begin)begin=timestamp;
    if(previous)gapMax=std::max(gapMax,timestamp-previous);
    previous=timestamp;workSum+=workSeconds;workMax=std::max(workMax,workSeconds);++count;
    if(callbacks<=3||timestamp-begin>=10){
        diagnostic(@"Frame callbacks=%llu sample=%u wall=%.3fs meanWork=%.2fms maxWork=%.2fms maxGap=%.2fms",
                   static_cast<unsigned long long>(callbacks),count,timestamp-begin,workSum/count*1000,workMax*1000,gapMax*1000);
        const auto& perf=touhou::sdl::nativePerformance;
        const double ticks=std::max(1.,double(perf.ticks)),draws=std::max(1.,double(perf.draws)),presents=std::max(1.,double(perf.callbacks));
        const auto gpu=*sdl_stats();static touhou::sdl::Statistics lastGPU{};
        diagnostic(@"Performance ticks=%llu draws=%llu logicPerTick=%.3fms drawPerFrame=%.3fms flushPerTick=%.3fms audioPerCallback=%.3fms presentPerCallback=%.3fms loadingTotal=%.2fms batches=%u uploadKB=%.1f vertexKB=%.1f readKB=%.1f bufferReplacements=%u subUpdates=%u genericBatches=%u",
                   (unsigned long long)perf.ticks,(unsigned long long)perf.draws,perf.logic/ticks,perf.draw/draws,perf.flush/ticks,
                   perf.audio/presents,perf.present/presents,perf.loading,gpu.batches-lastGPU.batches,
                   (gpu.uploadBytes-lastGPU.uploadBytes)/1024.,(gpu.vertexUploadBytes-lastGPU.vertexUploadBytes)/1024.,
                   (gpu.readBytes-lastGPU.readBytes)/1024.,gpu.bufferReplacements-lastGPU.bufferReplacements,
                   gpu.bufferSubUpdates-lastGPU.bufferSubUpdates,gpu.genericBatches-lastGPU.genericBatches);
        // These are CPU time inside GL calls, including driver waits, not GPU timestamps.
        diagnostic(@"Graphics CPU streamPerFrame=%.3fms submitPerFrame=%.3fms layouts=%u textureBinds=%u framebufferBinds=%u resamples=%u presents=%u",
                   perf.stream/draws,perf.submit/draws,gpu.layoutSetups-lastGPU.layoutSetups,
                   gpu.textureBinds-lastGPU.textureBinds,gpu.framebufferBinds-lastGPU.framebufferBinds,
                   gpu.resamples-lastGPU.resamples,gpu.presentations-lastGPU.presentations);
        touhou::sdl::nativePerformance={};lastGPU=gpu;
        diagnosticsState(@"periodic");diagnosticsFlush();
        begin=timestamp;workSum=workMax=gapMax=0;count=0;
    }
}
}


@interface TH10SettingsController : UITableViewController<UIAdaptivePresentationControllerDelegate,UIPopoverPresentationControllerDelegate>
@property(nonatomic,copy) void (^finished)(void);
@property(nonatomic) BOOL didFinish;
@property(nonatomic) BOOL closing;
- (void)closeSettings;
- (void)finishSharing;
@end
@implementation TH10SettingsController
- (NSArray*)rows {
 return @[
 @[@[@"movement",@"移动模式",@"混合支持摇杆或相对拖动",@"segment"],@[@"hidden",@"No Button",@"隐藏 X / S / Z 与摇杆，保留设置和暂停",@"switch"],@[@"layout",@"自由调整按键与摇杆位置",@"横屏、竖屏分别保存",@"action"],@[@"toggleFire",@"Z 点击保持射击",@"再点一次停止；菜单仍按住确认",@"switch"],@[@"toggleSlow",@"S 点击保持低速",@"再点一次恢复高速",@"switch"],@[@"autoFire",@"自动射击",@"进入战斗后自动按住 Z",@"switch"],@[@"autoSlow",@"自动低速",@"进入战斗后自动按住 S",@"switch"],@[@"autoBomb",@"Auto Bomb / 自动灵击",@"碰撞时在死亡效果前释放，消耗 1.00 火力；火力不足则正常受击",@"switch"],@[@"dragFire",@"拖拽移动时自动射击",@"松手后恢复原有射击状态",@"switch"]],
 @[@[@"opacity",@"按键透明度",@"",@"slider"],@[@"size",@"按键与摇杆大小",@"",@"slider"],@[@"sensitivity",@"拖动灵敏度",@"",@"slider"],@[@"deadZone",@"摇杆中心死区",@"",@"slider"],@[@"leftHand",@"左手按键布局",@"默认位置左右互换，自定义位置优先",@"switch"],@[@"haptics",@"按键触感反馈",@"在支持触感反馈的设备上生效",@"switch"]],
 @[@[@"fps",@"显示帧率",@"",@"segment"],@[@"quality",@"显示清晰度",@"",@"segment"],@[@"smooth",@"平滑缩放",@"关闭时保留清晰的像素边缘",@"switch"],@[@"performance",@"显示性能信息",@"显示帧率、更新频率与内存",@"switch"],@[@"hitbox",@"始终显示自机判定点",@"不按 S 时也显示，不改变速度",@"switch"]],
 @[@[@"export",@"导出诊断日志",@"包含本次与上次运行记录",@"action"],@[@"reset",@"恢复默认设置与按键布局",@"保留游戏成绩及解锁内容",@"action"],@[@"developer",@"开发者模式",@"战斗中显示 DEV：无敌、分数、道具、火力、残机与清弹",@"switch"],@[@"cheat",@"Cheat Code",@"输入 ymgjdsh 解锁 Extra、练习关卡和音乐",@"action"]]
 ];
}
- (void)viewDidLoad {
 [super viewDidLoad];self.title=@"设置";self.navigationItem.rightBarButtonItem=[[UIBarButtonItem alloc] initWithTitle:@"完成" style:UIBarButtonItemStyleDone target:self action:@selector(closeSettings)];self.preferredContentSize=CGSizeMake(720,760);
 UITapGestureRecognizer* back=[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(closeSettings)];back.numberOfTouchesRequired=2;[self.view addGestureRecognizer:back];
}
- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView{return self.rows.count;}
- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section{return [self.rows[section] count];}
- (NSString*)tableView:(UITableView*)tableView titleForHeaderInSection:(NSInteger)section{return @[@"操作方式",@"摇杆与按键",@"画面与性能",@"手势与诊断"][section];}
- (CGFloat)tableView:(UITableView*)tableView heightForRowAtIndexPath:(NSIndexPath*)p{NSString* type=self.rows[p.section][p.row][3];return [type isEqual:@"segment"]||[type isEqual:@"slider"]?116:88;}
- (NSArray*)values:(NSString*)key{return [key isEqual:@"movement"]?@[@0,@1,@2]:[key isEqual:@"fps"]?@[@60,@30]:@[@.5,@.75,@1];}
- (NSString*)valueLabel:(NSString*)key value:(double)value{return [key isEqual:@"sensitivity"]?[NSString stringWithFormat:@"%.2f×",value]:[NSString stringWithFormat:@"%.0f%%",value*100];}
- (void)settingChanged:(UIControl*)control {
 NSArray* row=self.rows[control.tag/100][control.tag%100];NSString* key=row[0];NSNumber* value;
 if([control isKindOfClass:UISwitch.class])value=@(((UISwitch*)control).on);
 else if([control isKindOfClass:UISegmentedControl.class])value=[self values:key][((UISegmentedControl*)control).selectedSegmentIndex];
 else {value=@(((UISlider*)control).value);UILabel* label=(UILabel*)[control.superview viewWithTag:9001];label.text=[self valueLabel:key value:value.doubleValue];}
 touhou::ios::setPreference(key,value);touhou::ios::applyPreferences();
}
- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)p {
 NSArray* row=self.rows[p.section][p.row];NSString* key=row[0],*type=row[3];double value=touhou::ios::preference(key);
 UITableViewCell* cell=[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];cell.selectionStyle=UITableViewCellSelectionStyleNone;cell.accessibilityIdentifier=[@"th10.setting." stringByAppendingString:key];
 if([type isEqual:@"segment"]||[type isEqual:@"slider"]){
  UILabel* title=[[UILabel alloc] init];title.text=row[1];title.font=[UIFont preferredFontForTextStyle:UIFontTextStyleBody];UIControl* control;
  if([type isEqual:@"segment"]){NSArray* labels=[key isEqual:@"movement"]?@[@"混合",@"拖动",@"摇杆"]:[key isEqual:@"fps"]?@[@"60 FPS",@"30 FPS"]:@[@"50%",@"75%",@"100%"];
   UISegmentedControl* segment=[[UISegmentedControl alloc] initWithItems:labels];NSArray* vals=[self values:key];segment.selectedSegmentIndex=0;for(NSUInteger i=0;i<vals.count;i++)if(fabs([vals[i] doubleValue]-value)<.01)segment.selectedSegmentIndex=i;control=segment;
  }else{UISlider* slider=[[UISlider alloc] init];slider.minimumValue=[key isEqual:@"opacity"]?.15:[key isEqual:@"size"]?.65:[key isEqual:@"sensitivity"]?.25:0;slider.maximumValue=[key isEqual:@"opacity"]?1:[key isEqual:@"size"]?1.5:[key isEqual:@"sensitivity"]?2.5:.4;slider.value=value;control=slider;}
  control.tag=p.section*100+p.row;[control addTarget:self action:@selector(settingChanged:) forControlEvents:UIControlEventValueChanged];UIStackView* stack=[[UIStackView alloc] initWithArrangedSubviews:@[title,control]];stack.axis=UILayoutConstraintAxisVertical;stack.spacing=8;
  if([type isEqual:@"slider"]){UILabel* label=[[UILabel alloc] init];label.tag=9001;label.text=[self valueLabel:key value:value];label.font=[UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];label.textAlignment=NSTextAlignmentRight;[stack addArrangedSubview:label];}
  stack.translatesAutoresizingMaskIntoConstraints=NO;[cell.contentView addSubview:stack];[NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:cell.contentView.leadingAnchor constant:20],[stack.trailingAnchor constraintEqualToAnchor:cell.contentView.trailingAnchor constant:-20],[stack.centerYAnchor constraintEqualToAnchor:cell.contentView.centerYAnchor]]];
 }else{cell.textLabel.text=row[1];cell.detailTextLabel.text=row[2];cell.textLabel.numberOfLines=0;cell.detailTextLabel.numberOfLines=0;
  if([type isEqual:@"switch"]){UISwitch* toggle=[[UISwitch alloc] init];toggle.on=value;toggle.tag=p.section*100+p.row;[toggle addTarget:self action:@selector(settingChanged:) forControlEvents:UIControlEventValueChanged];cell.accessoryView=toggle;}
  else{cell.textLabel.textColor=tableView.tintColor;cell.accessoryType=UITableViewCellAccessoryDisclosureIndicator;cell.selectionStyle=UITableViewCellSelectionStyleDefault;}
 }return cell;
}
- (NSString*)tableView:(UITableView*)tableView titleForFooterInSection:(NSInteger)section {
 if(section==0)return @"混合：摇杆或拖动；拖动：相对移动；摇杆：仅使用摇杆。设置、切后台和旋转会释放已锁定的 Z / S。";
 if(section==1)return @"布局编辑可拖动控件，分别保存横屏和竖屏位置。";
 if(section==2)return @"横屏始终铺满。30 FPS 限制显示提交，逻辑仍按 60 Hz 更新；显示清晰度调整屏幕输出分辨率。";
 return @"Z：射击／确认，X：灵击／返回，S：低速。菜单滑动选择、轻点确认；对话轻点继续。战斗双指轻点灵击、双指长按低速、三指长按暂停。设置内双指轻点返回。日志仅保存在本机。";
}
- (void)finishOnce {
    if(self.didFinish)return;self.didFinish=YES;
    if(self.finished)self.finished();self.finished=nil;
}
- (void)closeSettings {
    if(self.closing||self.didFinish)return;self.closing=YES;
    // Keep the completion owner alive until dismissal has released the pause.
    TH10SettingsController* controller=self;
    [self.navigationController dismissViewControllerAnimated:YES completion:^{[controller finishOnce];}];
}
- (void)finishSharing {
    UIViewController* share=self.presentedViewController;
    if(!share){[self closeSettings];return;}
    TH10SettingsController* controller=self;
    // The activity completion may arrive before its dismissal animation ends.
    if(share.isBeingDismissed&&share.transitionCoordinator){
        [share.transitionCoordinator animateAlongsideTransition:nil completion:^(id<UIViewControllerTransitionCoordinatorContext> context){[controller closeSettings];}];
    }else{
        [share dismissViewControllerAnimated:YES completion:^{[controller closeSettings];}];
    }
}
- (void)presentationControllerDidDismiss:(UIPresentationController*)presentationController {
    if([presentationController.presentedViewController isKindOfClass:UIActivityViewController.class])[self closeSettings];
    else [self finishOnce];
}
- (void)popoverPresentationControllerDidDismissPopover:(UIPopoverPresentationController*)popoverPresentationController {[self closeSettings];}
- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    if(indexPath.section==3&&indexPath.row==3){
        UIAlertController* a=[UIAlertController alertControllerWithTitle:@"Cheat Code" message:@"输入 ymgjdsh 解锁 Extra、练习关卡和全部音乐" preferredStyle:UIAlertControllerStyleAlert];
        [a addTextFieldWithConfigurationHandler:^(UITextField* f){f.placeholder=@"输入 Cheat Code";f.autocapitalizationType=UITextAutocapitalizationTypeNone;}];
        [a addAction:[UIAlertAction actionWithTitle:@"取消" style:UIAlertActionStyleCancel handler:nil]];
        [a addAction:[UIAlertAction actionWithTitle:@"应用" style:UIAlertActionStyleDefault handler:^(UIAlertAction*){NSString* code=a.textFields.firstObject.text.lowercaseString;BOOL ok=[code isEqual:@"ymgjdsh"];if(ok){touhou::ios::setPreference(@"unlocked",@YES);sdl_unlock_all();}UIAlertController* r=[UIAlertController alertControllerWithTitle:ok?@"已解锁":@"代码无效" message:ok?@"已保存全部解锁内容。":@"请检查代码。" preferredStyle:UIAlertControllerStyleAlert];[r addAction:[UIAlertAction actionWithTitle:@"确定" style:UIAlertActionStyleDefault handler:nil]];[self presentViewController:r animated:YES completion:nil];}]];[self presentViewController:a animated:YES completion:nil];return;
    }
    if(indexPath.section==3&&indexPath.row==1){touhou::ios::resetPreferences();touhou::ios::applyPreferences();[tableView reloadData];return;}
    if(indexPath.section==0&&indexPath.row==2){TH10SettingsController* controller=self;[self.navigationController dismissViewControllerAnimated:YES completion:^{touhou::ios::editControlLayout(^{[controller finishOnce];});}];return;}
    if(indexPath.section!=3||indexPath.row!=0)return;
    NSError* error=nil;NSURL* file=exportSnapshot(&error);
    if(!file){
        touhou::ios::diagnostic(@"Diagnostic export failed: %@",error);
        UIAlertController* alert=[UIAlertController alertControllerWithTitle:@"无法导出日志" message:error.localizedDescription?:@"请检查可用存储空间后重试。" preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"确定" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];return;
    }
    UIActivityViewController* share=[[UIActivityViewController alloc] initWithActivityItems:@[file] applicationActivities:nil];
    share.popoverPresentationController.sourceView=tableView;
    share.popoverPresentationController.sourceRect=[tableView rectForRowAtIndexPath:indexPath];
    share.popoverPresentationController.permittedArrowDirections=UIPopoverArrowDirectionAny;
    share.popoverPresentationController.delegate=self;
    share.presentationController.delegate=self;
    __weak TH10SettingsController* weakSelf=self;
    share.completionWithItemsHandler=^(UIActivityType activity,BOOL completed,NSArray* returnedItems,NSError* shareError){
        touhou::ios::diagnostic(@"Diagnostic share finished: completed=%d error=%@",completed,shareError);
        dispatch_async(dispatch_get_main_queue(),^{[weakSelf finishSharing];});
    };
    [self presentViewController:share animated:YES completion:nil];
}
@end

namespace touhou::ios {
void presentSettings(UIViewController* presenter,UIView* anchor,void (^finished)(void)){
    if(!presenter||!presenter.view.window||presenter.presentedViewController||presenter.isBeingDismissed){if(finished)finished();return;}
    TH10SettingsController* settings=[[TH10SettingsController alloc] initWithStyle:UITableViewStyleInsetGrouped];
    settings.finished=finished;
    UINavigationController* navigation=[[UINavigationController alloc] initWithRootViewController:settings];
    navigation.modalPresentationStyle=UIModalPresentationFormSheet;
    navigation.preferredContentSize=CGSizeMake(720,760);
    navigation.presentationController.delegate=settings;
    [presenter presentViewController:navigation animated:YES completion:nil];
}
}

#if TH10_DIAGNOSTICS
namespace {
UIButton* settingsButtonInView(UIView* view){
    if([view isKindOfClass:UIButton.class]&&[view.accessibilityIdentifier isEqualToString:@"th10.settings"])return (UIButton*)view;
    for(UIView* child in view.subviews){UIButton* found=settingsButtonInView(child);if(found)return found;}
    return nil;
}
void smokeAfter(double seconds,void (^action)(void)){
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,int64_t(seconds*NSEC_PER_SEC)),dispatch_get_main_queue(),action);
}
void smokeFail(NSString* message){
    touhou::ios::diagnostic(@"SETTINGS_SMOKE FAIL: %@",message);touhou::ios::diagnosticsFlush();
}
void invokeSettingsSmoke(unsigned attempts){
    UIButton* button=nil;
    for(UIWindow* window in UIApplication.sharedApplication.windows){
        if(!window.hidden){button=settingsButtonInView(window);if(button)break;}
    }
    if(!button){
        if(attempts<30){smokeAfter(1,^{invokeSettingsSmoke(attempts+1);});return;}
        smokeFail(@"settings button never became available");return;
    }
    if(sdl_loop_is_paused()){smokeFail(@"game was already suspended before opening settings");return;}
    UIViewController* presenter=button.window.rootViewController;
    const std::uint64_t pausedCallbacks=observedCallbacks;
    [button sendActionsForControlEvents:UIControlEventTouchDown];
    touhou::ios::diagnostic(@"SETTINGS_SMOKE invoked actual settings button; callback=%llu",static_cast<unsigned long long>(pausedCallbacks));
    smokeAfter(2,^{
        UIViewController* modal=presenter.presentedViewController;
        TH10SettingsController* settings=[modal isKindOfClass:UINavigationController.class]?(TH10SettingsController*)((UINavigationController*)modal).topViewController:nil;
        if(![settings isKindOfClass:TH10SettingsController.class]||!sdl_loop_is_paused()||observedCallbacks!=pausedCallbacks){
            smokeFail(@"settings did not suspend game callbacks");
            if([settings isKindOfClass:TH10SettingsController.class])[settings closeSettings];return;
        }
        touhou::ios::diagnostic(@"SETTINGS_SMOKE settings visible and suspended; invoking actual export row");
        [settings tableView:settings.tableView didSelectRowAtIndexPath:[NSIndexPath indexPathForRow:0 inSection:3]];
        smokeAfter(2,^{
            UIViewController* modalShare=settings.presentedViewController;
            NSNumber* bytes=[NSFileManager.defaultManager attributesOfItemAtPath:[logDirectory URLByAppendingPathComponent:@"TH10-diagnostics.txt"].path error:nullptr][NSFileSize];
            if(![modalShare isKindOfClass:UIActivityViewController.class]||bytes.unsignedLongLongValue==0||!sdl_loop_is_paused()){
                smokeFail(@"share sheet/snapshot missing or simulation resumed too early");[settings finishSharing];return;
            }
            touhou::ios::diagnostic(@"SETTINGS_SMOKE share visible; exportBytes=%@; dismissing locally in 10 seconds",bytes);
            touhou::ios::diagnosticsFlush();
            smokeAfter(10,^{
                UIActivityViewController* share=(UIActivityViewController*)modalShare;
                // Exercise the same completion used by a user's cancelled
                // share; no activity or external recipient is selected.
                if(share.completionWithItemsHandler)share.completionWithItemsHandler(nil,NO,nil,nil);
                else [settings finishSharing];
                smokeAfter(3,^{
                    if(presenter.presentedViewController||sdl_loop_is_paused()||observedCallbacks<=pausedCallbacks){
                        smokeFail(@"share close did not dismiss settings and resume callbacks");return;
                    }
                    touhou::ios::diagnostic(@"SETTINGS_SMOKE PASS paused=1 exportBytes=%@ resumed=1 callbacksBefore=%llu callbacksAfter=%llu",bytes,
                                           static_cast<unsigned long long>(pausedCallbacks),static_cast<unsigned long long>(observedCallbacks));
                    touhou::ios::diagnosticsFlush();
                });
            });
        });
    });
}
void startSettingsSmoke(){
    touhou::ios::diagnostic(@"SETTINGS_SMOKE scheduled after startup; diagnostic build only");
    smokeAfter(5,^{invokeSettingsSmoke(0);});
}
}
#endif

