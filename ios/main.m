#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AVFAudio/AVFAudio.h>
#import <GameController/GameController.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <mach/mach.h>
#include <os/proc.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>
#include "cloud_save_sync.h"
#include "filesystem.h"
#include "dm.h"
#include "kernel.h"
#include "input.h"
#include "gamepad.h"
#include "audio.h"
#include "settings.h"
#include "performance.h"
#include "performance_metrics.h"
#include "game_timing.h"
#include "shader_prep.h"
#include "dmsh_chain.h"
#include "camera_controller.h"
#include "action_layout.h"
#include "action_layout.inc"
#include "screen_aspect.h"
#include "screen_aspect.inc"
#include "action_file_watcher.h"
#include "action_file_watcher.inc"
#include "native_prompt_overlay.inc"

#ifndef MTLGPUFamilyMetal4
#define MTLGPUFamilyMetal4 ((MTLGPUFamily)5002)
#endif

extern int zelda_main(int, char **);
extern void dm_gfx_suspend(int);
extern void mtl_present_discard(int);
static CAMetalLayer *metal_layer;
static UILabel *status_label, *performance_label;
static UIStackView *performance_panel;
static UIButton *performance_toggle;
static UIButton *cloudCancelButton;
static UIButton *cloudFolderButton;
static BOOL performance_expanded = NO;
static NSLayoutConstraint *performance_width;
static UIStackView *control_toolbar, *fallback_toolbar;
static UIButton *retry_button, *control_mode_button, *confirm_button, *aspect_button;
static _Atomic int selected_screen_aspect = ASPECT_MODE_FILL;
int front_screen_aspect_mode(void) { return atomic_load(&selected_screen_aspect); }
static BOOL full_touch_controller;
static unsigned touch_controller_generation;
static ZeldaCPUUsage cpu_usage;
static BOOL launched, guest_running;
static uint32_t vsync_write, vsync_read;
static pthread_mutex_t vsync_mutex = PTHREAD_MUTEX_INITIALIZER;
static void (*resource_report)(uint32_t, const char *);
static NSString *documents;
static GCVirtualController *touch_controller;

static void disconnect_touch_controller(void) {
    touch_controller_generation++;
    touch_controller.controller.extendedGamepad.valueChangedHandler = nil;
    touch_controller.controller.extendedGamepad.leftThumbstick.valueChangedHandler = nil;
    [touch_controller disconnect];
    touch_controller = nil;
    gamepad_set_virtual_controller(NULL);
    inputTouchControllerClear();
}
static void connect_touch_controller(void) {
    if (touch_controller) return;
    GCVirtualControllerConfiguration *config = [GCVirtualControllerConfiguration new];
    config.elements = full_touch_controller ? [NSSet setWithArray:@[
        GCInputLeftThumbstick, GCInputRightThumbstick,
        GCInputButtonA, GCInputButtonB, GCInputButtonX, GCInputButtonY,
        GCInputLeftShoulder, GCInputRightShoulder, GCInputLeftTrigger, GCInputRightTrigger]] :
        [NSSet setWithObject:GCInputLeftThumbstick];
    GCVirtualController *created = [[GCVirtualController alloc] initWithConfiguration:config];
    touch_controller = created;
    unsigned generation = ++touch_controller_generation;
    [created connectWithReplyHandler:^(NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (generation != touch_controller_generation || created != touch_controller) {
                created.controller.extendedGamepad.valueChangedHandler = nil;
                [created disconnect];
                return;
            }
            if (error) {
                fprintf(stderr, "TOUCH controller failed code=%ld; use persistent menu buttons or retry mode switch\n", (long)error.code);
                touch_controller = nil; return;
            }
            GCController *controller = created.controller;
            gamepad_set_virtual_controller((__bridge void *)controller);
            inputNoteDevice(INPUT_DEVICE_TOUCH);
            controller.extendedGamepad.valueChangedHandler = ^(GCExtendedGamepad *pad, GCControllerElement *element) {
                (void)element;
                if (generation != touch_controller_generation) return;
                if (UIApplication.sharedApplication.applicationState != UIApplicationStateActive) { inputTouchControllerClear(); return; }
                if (!full_touch_controller) {
                    inputTouchStick(pad.leftThumbstick.xAxis.value, pad.leftThumbstick.yAxis.value); return;
                }
                uint64_t buttons = 0;
                if (pad.buttonA.pressed) buttons |= PAD_BTN_A;
                if (pad.buttonB.pressed) buttons |= PAD_BTN_B;
                if (pad.buttonX.pressed) buttons |= PAD_BTN_X;
                if (pad.buttonY.pressed) buttons |= PAD_BTN_Y;
                if (pad.leftShoulder.pressed) buttons |= PAD_BTN_L;
                if (pad.rightShoulder.pressed) buttons |= PAD_BTN_R;
                if (pad.leftTrigger.pressed) buttons |= PAD_BTN_ZL;
                if (pad.rightTrigger.pressed) buttons |= PAD_BTN_ZR;
                inputTouchController(buttons, pad.leftThumbstick.xAxis.value, pad.leftThumbstick.yAxis.value,
                    pad.rightThumbstick.xAxis.value, pad.rightThumbstick.yAxis.value);
            };
        });
    }];
}

// Keep titles textual even when an older user layout still contains emoji.
static NSString *touch_title(NSString *text, NSString *fallback) {
    NSMutableCharacterSet *allowed = [NSCharacterSet.letterCharacterSet mutableCopy];
    [allowed formUnionWithCharacterSet:NSCharacterSet.decimalDigitCharacterSet];
    [allowed addCharactersInString:@" /-()"];
    NSString *plain = [[text componentsSeparatedByCharactersInSet:allowed.invertedSet] componentsJoinedByString:@""];
    plain = [plain stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    return plain.length ? plain : fallback;
}

static double physical_gib(void) {
    task_vm_info_data_t info={0};mach_msg_type_number_t count=TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(),TASK_VM_INFO,(task_info_t)&info,&count)==KERN_SUCCESS?info.phys_footprint/1073741824.0:-1;
}

static NSString *metric(double value, NSString *unit) {
    return isfinite(value) && value >= 0 ? [NSString stringWithFormat:@"%.1f%@", value, unit] : @"N/A";
}

void *front_metal_layer(void) { return (__bridge void *)metal_layer; }
void crash_set_wait_dump(void (*callback)(void)) { (void)callback; }
void dm_res_report_register(void (*callback)(uint32_t, const char *)) { resource_report = callback; }
uint32_t front_vsync_handle(void) {
    pthread_mutex_lock(&vsync_mutex);
    if (!vsync_write && !kernel_event_create(&vsync_write, &vsync_read)) abort();
    uint32_t copy = kernel_event_copy(vsync_read);
    pthread_mutex_unlock(&vsync_mutex);
    return copy;
}

static void status(NSString *text) {
    dispatch_async(dispatch_get_main_queue(), ^{ status_label.text = text; });
}

void front_run(void (*guest)(void)) {
    if (cloudSaveSyncIsConfigured()) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            cloudCancelButton.hidden = NO;
            cloudCancelButton.enabled = YES;
        });
        status(@"Syncing cloud saves…\nTap Cancel to skip.");
        CloudSaveSyncStatus syncStatus = cloudSaveSyncPullBlocking(CLOUD_SAVE_TIMEOUT_SECONDS, ^(NSString *text) {
            status([NSString stringWithFormat:@"%@\nTap Cancel to skip.", text]);
        });
        dispatch_sync(dispatch_get_main_queue(), ^{
            cloudCancelButton.hidden = YES;
        });
        if (syncStatus == CloudSaveSyncStatusConflict)
            NSLog(@"CLOUD_SYNC conflict: keeping local save; cloud remains unchanged");
    }

    NSString *directory = [[documents stringByAppendingPathComponent:@"work/shaders"]
        stringByAppendingPathComponent:@"b-" DMSH_CHAIN];
    NSString *support = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"shader-support/b-" DMSH_CHAIN];
    NSFileManager *fm = NSFileManager.defaultManager;
    NSArray<NSString *> *supportFiles = [fm contentsOfDirectoryAtPath:support error:nil];
    if (supportFiles.count != 4) {
        status(@"缺少视频着色器支持资源，请重新构建 iOS 应用。");
        return;
    }
    [fm createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    for (NSString *file in supportFiles) {
        NSString *destination = [directory stringByAppendingPathComponent:file];
        if (![fm fileExistsAtPath:destination]) {
            NSError *error = nil;
            if (![fm copyItemAtPath:[support stringByAppendingPathComponent:file] toPath:destination error:&error]) {
                fprintf(stderr, "IOS_MOVIE_SHADER copy failed code=%ld\n", (long)error.code);
                status(@"视频着色器资源无法准备，请检查可用空间后重启。");
                return;
            }
            fprintf(stderr, "IOS_MOVIE_SHADER installed %s\n", file.UTF8String);
        }
    }
    CFAbsoluteTime began = CFAbsoluteTimeGetCurrent();
    fprintf(stderr, "SHADER_PREP begin chain=%s scope=needed platform=ios\n", DMSH_CHAIN);
    unsigned failed = shader_prep_run(metal_layer.device, directory, ^(unsigned count, unsigned total) {
        if (!(count % 500) || count == total) fprintf(stderr, "SHADER_PREP progress=%u/%u physical_gib=%.3f available_mib=%.1f\n",
            count,total,physical_gib(),os_proc_available_memory()/1048576.0);
        status([NSString stringWithFormat:@"Preparing shaders\n%u / %u\n\nFirst-time shader compilation can take a long time.\nPlease keep the app open. Later launches are usually faster.", count, total]);
    });
    fprintf(stderr, "SHADER_PREP end failed=%u seconds=%.3f\n", failed, CFAbsoluteTimeGetCurrent() - began);
    dispatch_sync(dispatch_get_main_queue(), ^{
        status_label.hidden = YES; guest_running = YES;
        cloudCancelButton.hidden = YES;
        cloudFolderButton.hidden = YES;
        retry_button.hidden = YES;
        for (UIButton *button in control_toolbar.arrangedSubviews) button.enabled = YES;
        gamepad_start();
        gamepad_active(UIApplication.sharedApplication.applicationState == UIApplicationStateActive);
        gamepad_vibration_mode(zelda_settings().vibration);
        connect_touch_controller();
    });
    guest();
}

static void *boot(void *unused) {
    (void)unused;
    @autoreleasepool {
        NSString *tables = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"patched"];
        BOOL load_only = [NSProcessInfo.processInfo.arguments containsObject:@"--load-only"];
        char *argv[] = {"ZeldaMobile", (char *)tables.fileSystemRepresentation, "--load-only", NULL};
        int result = zelda_main(load_only ? 3 : 2, argv);
        fprintf(stderr, "IOS_HOST returned=%d load_only=%d\n", result, load_only);
        status([NSString stringWithFormat:load_only ? @"Loader check finished (%d)" : @"Game stopped (%d)", result]);
    }
    return NULL;
}

static void start_game(void) {
    if (launched) return;
    BOOL load_only = [NSProcessInfo.processInfo.arguments containsObject:@"--load-only"];
    NSString *romfs = [documents stringByAppendingPathComponent:@"work/romfs-host.bin"];
    NSString *shaders = [documents stringByAppendingPathComponent:@"work/shaders"];
    NSFileManager *fm = NSFileManager.defaultManager;
    if (!load_only) {
        NSData *data=[NSData dataWithContentsOfFile:[shaders stringByAppendingPathComponent:@"ios-ready.json"]];
        id ready=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
        NSDictionary *info=[fm attributesOfItemAtPath:romfs error:nil];
        NSArray *names=[fm contentsOfDirectoryAtPath:[shaders stringByAppendingPathComponent:@"b-" DMSH_CHAIN] error:nil];
        NSUInteger count=0;for(NSString *name in names) if([name hasSuffix:@".metallib"])count++;
        if (![ready isKindOfClass:NSDictionary.class] || ![ready[@"chain"] isEqual:@DMSH_CHAIN] ||
            ![ready[@"romfs_bytes"] isKindOfClass:NSNumber.class] || ![ready[@"libraries"] isKindOfClass:NSNumber.class] ||
            ![ready[@"romfs_bytes"] unsignedLongLongValue] || [ready[@"romfs_bytes"] unsignedLongLongValue]!=[info[NSFileSize] unsignedLongLongValue] ||
            !count || count<[ready[@"libraries"] unsignedIntegerValue]) {
            status(@"Game files are not imported yet.\nFinish the transfer, then tap Retry."); return;
        }
    }
    launched = YES;
    retry_button.hidden = YES;
    // Imported game assets are reproducible; keep only the player's save eligible for device backup.
    for (NSString *path in @[romfs,shaders]) if ([fm fileExistsAtPath:path]) {
        NSError *issue=nil;
        if (![[NSURL fileURLWithPath:path] setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&issue])
            fprintf(stderr,"IOS_ASSET backup exclusion failed code=%ld\n",(long)issue.code);
    }
    for (NSString *key in @[@"work", @"work/save", @"work/logs", @"work/tmp", @"work/shaders/source", @"work/shaders/variants"]) {
        if (![fm createDirectoryAtPath:[documents stringByAppendingPathComponent:key] withIntermediateDirectories:YES attributes:nil error:nil]) abort();
    }
    cloudSaveSyncInit([documents stringByAppendingPathComponent:@"work/save"]);
    cloudSaveSyncSetSnapshotProvider(^BOOL(NSString *destination) {
        return filesystem_snapshot_save(destination.fileSystemRepresentation) == 0;
    });
    setenv("ZELDA_ROMFS", romfs.fileSystemRepresentation, 1);
    setenv("ZELDA_SHARED_FONT", [[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"shared.ttf"] fileSystemRepresentation], 1);
    setenv("ZELDA_SETTINGS", [[documents stringByAppendingPathComponent:@"work/settings.json"] fileSystemRepresentation], 1);
    setenv("DM_MSL_DIR", shaders.fileSystemRepresentation, 1);
    setenv("DM_NVN_SHMAP", [[shaders stringByAppendingPathComponent:@"shader_map.tsv"] fileSystemRepresentation], 1);
    setenv("DM_PSO_USER", [[shaders stringByAppendingPathComponent:@"pso_user.tsv"] fileSystemRepresentation], 1);
    setenv("DM_PSO_MANIFEST", "off", 1);
    setenv("DM_NVN_TBDR", "0", 1);
    setenv("DM_PRESENT_LINK", "0", 1);
    if (!getenv("ZELDA_TIMEOUT_SECONDS")) setenv("ZELDA_TIMEOUT_SECONDS", "0", 1);
    audio_mute(zelda_settings().muted);
    NSError *error = nil;
    AVAudioSession *session = AVAudioSession.sharedInstance;
    if (![session setCategory:AVAudioSessionCategoryPlayback error:&error] || ![session setActive:YES error:&error])
        fprintf(stderr, "AUDIO session activation failed code=%ld\n", (long)error.code);
    status(@"Loading game modules…");
    pthread_attr_t attr; pthread_t thread;
    if (pthread_attr_init(&attr) || pthread_attr_setstacksize(&attr,8<<20) || pthread_create(&thread,&attr,boot,NULL)) abort();
    pthread_attr_destroy(&attr); pthread_detach(thread);
}

@interface ZeldaView : UIView
@end
@implementation ZeldaView
+ (Class)layerClass { return CAMetalLayer.class; }
- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat scale = self.window.screen.scale ?: UIScreen.mainScreen.scale;
    metal_layer.contentsScale = scale;
    metal_layer.drawableSize = CGSizeMake(MAX(1,round(self.bounds.size.width*scale)),MAX(1,round(self.bounds.size.height*scale)));
    static CGSize lastSize;
    static UIEdgeInsets lastSafe;
    static int lastMode = -1;
    int mode = front_screen_aspect_mode();
    UIEdgeInsets safe = self.safeAreaInsets;
    if (!CGSizeEqualToSize(lastSize, metal_layer.drawableSize) || !UIEdgeInsetsEqualToEdgeInsets(lastSafe, safe) || mode != lastMode) {
        lastSize = metal_layer.drawableSize; lastSafe = safe; lastMode = mode;
        fprintf(stderr, "IOS_VIEWPORT points=%.0fx%.0f pixels=%.0fx%.0f safe=%.0f,%.0f,%.0f,%.0f mode=%s\n",
            self.bounds.size.width, self.bounds.size.height, lastSize.width, lastSize.height,
            safe.left, safe.top, safe.right, safe.bottom, mode == ASPECT_MODE_FILL ? "fill-crop" : "fit");
    }
}
@end

@interface ZeldaController : UIViewController <UIGestureRecognizerDelegate, UIDocumentPickerDelegate>
@property (nonatomic, strong) CADisplayLink *displayLink;
@property (nonatomic, strong) NSTimer *monitor;
@property (nonatomic, assign) BOOL isPanTracking;
@property (nonatomic, strong) NSMutableArray<UIButton *> *dynamicActionButtons;
@property (nonatomic, strong) NSMutableDictionary<NSNumber *, NSNumber *> *pressedActionButtons;
@property (nonatomic, strong) ZeldaNativePromptView *nativePrompts;
- (void)releaseAllActionButtons;
@end
@implementation ZeldaController
- (void)loadView {
    self.view = [[ZeldaView alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.view.backgroundColor = UIColor.blackColor;
    metal_layer = (CAMetalLayer *)self.view.layer;
    metal_layer.device = MTLCreateSystemDefaultDevice();
    metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    if (!metal_layer.device) abort();
    fprintf(stderr,"IOS_DEVICE gpu=%s memory_gib=%.3f available_mib=%.1f bc=%d metal4=%d\n",
        metal_layer.device.name.UTF8String,NSProcessInfo.processInfo.physicalMemory/1073741824.0,
        os_proc_available_memory()/1048576.0,metal_layer.device.supportsBCTextureCompression,
        [metal_layer.device supportsFamily:MTLGPUFamilyMetal4]);
    fprintf(stderr,"IOS_METAL_COUNTER_SETS count=%lu\n",(unsigned long)metal_layer.device.counterSets.count);
    for (id<MTLCounterSet> set in metal_layer.device.counterSets)
        fprintf(stderr,"IOS_METAL_COUNTER_SET name=%s\n",set.name.UTF8String);
    status_label = [UILabel new]; status_label.textColor = UIColor.whiteColor;
    status_label.numberOfLines = 0; status_label.textAlignment = NSTextAlignmentCenter;
    status_label.font = [UIFont systemFontOfSize:22 weight:UIFontWeightMedium];
    status_label.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:status_label];
    performance_label = [UILabel new]; performance_label.textColor = UIColor.whiteColor;
    performance_label.numberOfLines = 0;
    performance_label.font = [[UIFontMetrics metricsForTextStyle:UIFontTextStyleCaption1]
        scaledFontForFont:[UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular]];
    performance_label.adjustsFontForContentSizeCategory = YES;
    performance_toggle = [UIButton buttonWithType:UIButtonTypeCustom];
    [performance_toggle setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    performance_toggle.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
    performance_toggle.titleLabel.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightSemibold];
    [performance_toggle.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    [performance_toggle addTarget:self action:@selector(togglePerformance:) forControlEvents:UIControlEventTouchUpInside];
    performance_panel = [[UIStackView alloc] initWithArrangedSubviews:@[performance_toggle,performance_label]];
    performance_panel.axis = UILayoutConstraintAxisVertical; performance_panel.spacing = 4;
    performance_panel.layoutMarginsRelativeArrangement = YES;
    performance_panel.layoutMargins = UIEdgeInsetsMake(4,10,10,10);
    performance_panel.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:.78];
    performance_panel.layer.cornerRadius = 8;
    performance_panel.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:performance_panel];
    performance_width = [performance_panel.widthAnchor constraintEqualToConstant:130];
    [NSLayoutConstraint activateConstraints:@[
        [status_label.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [status_label.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [status_label.widthAnchor constraintLessThanOrEqualToAnchor:self.view.safeAreaLayoutGuide.widthAnchor multiplier:.85],
        [performance_panel.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:12],
        [performance_panel.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:10],
        performance_width]];
    full_touch_controller = [NSUserDefaults.standardUserDefaults boolForKey:@"ZELDA_TOUCH_FULL_CONTROLLER"];
    control_toolbar = [[UIStackView alloc] init];
    control_toolbar.axis = UILayoutConstraintAxisHorizontal; control_toolbar.spacing = 6;
    control_toolbar.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:control_toolbar];
    NSNumber *savedAspect = [NSUserDefaults.standardUserDefaults objectForKey:@"ZELDA_SCREEN_ASPECT_MODE"];
    atomic_store(&selected_screen_aspect, savedAspect && savedAspect.intValue == ASPECT_MODE_FIT ? ASPECT_MODE_FIT : ASPECT_MODE_FILL);
    NSArray *titles = @[@"确定", @"返回", @"地图", @"菜单", full_touch_controller ? @"手游模式" : @"虚拟手柄",
        front_screen_aspect_mode() == ASPECT_MODE_FILL ? @"完整画面" : @"铺满屏幕"];
    const uint64_t masks[] = {PAD_BTN_A, PAD_BTN_B, PAD_BTN_MINUS, PAD_BTN_PLUS, 0, 0};
    for (unsigned i = 0; i < titles.count; i++) {
        UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
        [button setTitle:titles[i] forState:UIControlStateNormal];
        button.titleLabel.font = [UIFont systemFontOfSize:14 weight:UIFontWeightSemibold];
        button.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:.60];
        button.layer.cornerRadius = 8; button.tag = (NSInteger)masks[i];
        [button.widthAnchor constraintEqualToConstant:i == 4 ? 88 : (i == 5 ? 72 : 48)].active = YES;
        [button.heightAnchor constraintEqualToConstant:44].active = YES;
        if (i == 0) confirm_button = button;
        if (i == 4) {
            control_mode_button = button;
            [button addTarget:self action:@selector(toggleTouchControls:) forControlEvents:UIControlEventTouchUpInside];
        } else if (i == 5) {
            aspect_button = button;
            [button addTarget:self action:@selector(toggleScreenAspect:) forControlEvents:UIControlEventTouchUpInside];
        } else {
            [button addTarget:self action:@selector(onPersistentButtonDown:) forControlEvents:UIControlEventTouchDown];
            [button addTarget:self action:@selector(onPersistentButtonUp:) forControlEvents:UIControlEventTouchUpInside|UIControlEventTouchUpOutside|UIControlEventTouchCancel];
        }
        [control_toolbar addArrangedSubview:button];
    }
    [NSLayoutConstraint activateConstraints:@[
        [control_toolbar.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-12],
        [control_toolbar.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:8]]];
    fallback_toolbar = [UIStackView new];
    fallback_toolbar.axis = UILayoutConstraintAxisHorizontal; fallback_toolbar.spacing = 6;
    fallback_toolbar.translatesAutoresizingMaskIntoConstraints = NO;
    fallback_toolbar.hidden = YES;
    [self.view addSubview:fallback_toolbar];
    NSArray *fallbackTitles = @[@"盾牌", @"材料", @"口哨", @"武器", @"蹲下", @"望远"];
    const uint64_t fallbackMasks[] = {PAD_BTN_D_LEFT, PAD_BTN_D_UP, PAD_BTN_D_DOWN, PAD_BTN_D_RIGHT, PAD_BTN_L3, PAD_BTN_R3};
    for (unsigned i = 0; i < fallbackTitles.count; i++) {
        UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
        [button setTitle:fallbackTitles[i] forState:UIControlStateNormal];
        button.titleLabel.font = [UIFont systemFontOfSize:14 weight:UIFontWeightSemibold];
        button.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:.65];
        button.layer.cornerRadius = 8; button.tag = (NSInteger)fallbackMasks[i];
        [button.widthAnchor constraintEqualToConstant:i < 4 ? 44 : 52].active = YES;
        [button.heightAnchor constraintEqualToConstant:44].active = YES;
        [button addTarget:self action:@selector(onPersistentButtonDown:) forControlEvents:UIControlEventTouchDown];
        [button addTarget:self action:@selector(onPersistentButtonUp:) forControlEvents:UIControlEventTouchUpInside|UIControlEventTouchUpOutside|UIControlEventTouchCancel];
        if (button.tag & (PAD_BTN_D_LEFT | PAD_BTN_D_RIGHT | PAD_BTN_D_UP)) {
            UIPanGestureRecognizer *selection = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handleEquipmentPan:)];
            selection.maximumNumberOfTouches = 1; selection.cancelsTouchesInView = NO;
            [button addGestureRecognizer:selection];
            button.accessibilityHint = @"按住并左右滑动选择";
        }
        [fallback_toolbar addArrangedSubview:button];
    }
    [NSLayoutConstraint activateConstraints:@[
        [fallback_toolbar.centerXAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerXAnchor],
        [fallback_toolbar.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-8]]];
    retry_button = [UIButton buttonWithType:UIButtonTypeSystem];
    [retry_button setTitle:@"重试" forState:UIControlStateNormal];
    retry_button.translatesAutoresizingMaskIntoConstraints = NO;
    [retry_button addTarget:self action:@selector(retryGame:) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:retry_button];
    [NSLayoutConstraint activateConstraints:@[
        [retry_button.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [retry_button.topAnchor constraintEqualToAnchor:status_label.bottomAnchor constant:8],
        [retry_button.heightAnchor constraintEqualToConstant:44]]];
    cloudCancelButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [cloudCancelButton setTitle:@"Cancel Sync" forState:UIControlStateNormal];
    cloudCancelButton.backgroundColor = [[UIColor redColor] colorWithAlphaComponent:0.75];
    [cloudCancelButton setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    cloudCancelButton.layer.cornerRadius = 8;
    cloudCancelButton.titleLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    cloudCancelButton.translatesAutoresizingMaskIntoConstraints = NO;
    cloudCancelButton.hidden = YES;
    [cloudCancelButton addTarget:self action:@selector(cancelCloudSyncAction:) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:cloudCancelButton];

    cloudFolderButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [cloudFolderButton setTitle:cloudSaveSyncIsConfigured() ? @"Cloud: Linked" : @"Link Cloud Folder" forState:UIControlStateNormal];
    cloudFolderButton.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.65];
    [cloudFolderButton setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    cloudFolderButton.layer.cornerRadius = 8;
    cloudFolderButton.titleLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightMedium];
    cloudFolderButton.translatesAutoresizingMaskIntoConstraints = NO;
    [cloudFolderButton addTarget:self action:@selector(pickCloudFolderAction:) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:cloudFolderButton];

    [NSLayoutConstraint activateConstraints:@[
        [cloudCancelButton.topAnchor constraintEqualToAnchor:status_label.bottomAnchor constant:18],
        [cloudCancelButton.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [cloudCancelButton.widthAnchor constraintEqualToConstant:150],
        [cloudCancelButton.heightAnchor constraintEqualToConstant:40],
        [cloudFolderButton.topAnchor constraintEqualToAnchor:performance_panel.bottomAnchor constant:8],
        [cloudFolderButton.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:12],
        [cloudFolderButton.heightAnchor constraintEqualToConstant:40],
        [cloudFolderButton.widthAnchor constraintGreaterThanOrEqualToConstant:110]
    ]];
    CGSize screenSize = UIScreen.mainScreen.bounds.size;
    DeviceAspectClass dAspect = screenClassifyAspect(screenSize.width, screenSize.height);
    fprintf(stderr, "IOS_DEVICE_ASPECT width=%.0f height=%.0f aspect_class=%d\n", screenSize.width, screenSize.height, (int)dAspect);

    // 初始化 Unity 风格手柄按键与手游级动态布局
    actionLayoutInit();
    inputNoteDevice(INPUT_DEVICE_TOUCH);
    [self setupDynamicActionButtons];
    self.nativePrompts = [[ZeldaNativePromptView alloc] initWithFrame:self.view.bounds];
    [self.view addSubview:self.nativePrompts];

    // 屏幕右半侧触控手势：模拟现代主流 3D 手游平滑划屏转动视角
    UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handleCameraPan:)];
    pan.maximumNumberOfTouches = 1;
    pan.cancelsTouchesInView = NO;
    pan.delaysTouchesBegan = NO;
    pan.delegate = self;
    [self.view addGestureRecognizer:pan];

    self.displayLink=[CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    self.displayLink.preferredFrameRateRange=CAFrameRateRangeMake(60,60,60);
    [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    __weak ZeldaController *weakSelf=self;
    self.monitor=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer){(void)timer;[weakSelf updatePerformance];}];
    [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){
        (void)note; [weakSelf releaseAllActionButtons]; inputTouchControllerClear(); cameraReset(); weakSelf.isPanTracking = NO;
    }];
    [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){
        (void)note;
        if (!guest_running || !cloudSaveSyncIsConfigured()) return;
        __block UIBackgroundTaskIdentifier task = UIBackgroundTaskInvalid;
        task = [UIApplication.sharedApplication beginBackgroundTaskWithExpirationHandler:^{
            cloudSaveSyncCancel();
            if (task != UIBackgroundTaskInvalid) [UIApplication.sharedApplication endBackgroundTask:task];
            task = UIBackgroundTaskInvalid;
        }];
        cloudSaveSyncPushAsync(^(BOOL success) {
            if (!success) NSLog(@"CLOUD_SYNC upload skipped or failed; local save retained");
            if (task != UIBackgroundTaskInvalid) [UIApplication.sharedApplication endBackgroundTask:task];
            task = UIBackgroundTaskInvalid;
        });
    }];
    [NSNotificationCenter.defaultCenter addObserverForName:GCControllerDidDisconnectNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){
        if (note.object != touch_controller.controller) inputNoteDevice(INPUT_DEVICE_TOUCH);
    }];
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gesture shouldReceiveTouch:(UITouch *)touch {
    (void)gesture;
    // Ignore left-hand touches before they enter this recognizer, allowing
    // movement and one-finger camera dragging at the same time.
    if (full_touch_controller || [touch locationInView:self.view].x < self.view.bounds.size.width * 0.40) return NO;
    for (UIView *view = touch.view; view; view = view.superview)
        if ([view isKindOfClass:UIControl.class]) return NO;
    return YES;
}
- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gesture {
    CGPoint point = [gesture locationInView:self.view];
    UIView *hit = [self.view hitTest:point withEvent:nil];
    for (UIView *view = hit; view; view = view.superview)
        if ([view isKindOfClass:UIControl.class]) return NO;
    return guest_running && !full_touch_controller && point.x >= self.view.bounds.size.width * 0.40;
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)first shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)second {
    (void)first; (void)second; return !full_touch_controller;
}
- (void)handleCameraPan:(UIPanGestureRecognizer *)gesture {
    CGPoint loc = [gesture locationInView:self.view];
    if (gesture.state == UIGestureRecognizerStateBegan) {
        // 只允许在屏幕右半侧开始划屏转视角，防止干扰左侧虚拟摇杆移动
        [gesture setTranslation:CGPointZero inView:self.view];
        if (guest_running && loc.x >= self.view.bounds.size.width * 0.40f) {
            inputNoteDevice(INPUT_DEVICE_TOUCH);
            self.isPanTracking = YES;
        } else {
            self.isPanTracking = NO;
        }
    }

    if (full_touch_controller || !guest_running) { self.isPanTracking = NO; return; }
    if (!self.isPanTracking) return;

    if (gesture.state == UIGestureRecognizerStateChanged) {
        CGPoint delta = [gesture translationInView:self.view];
        [gesture setTranslation:CGPointZero inView:self.view];
        if (delta.x != 0 || delta.y != 0) inputNoteDevice(INPUT_DEVICE_TOUCH);
        cameraAddTouchDelta((float)delta.x, (float)delta.y);
    } else if (gesture.state == UIGestureRecognizerStateEnded || gesture.state == UIGestureRecognizerStateCancelled) {
        self.isPanTracking = NO;
    }
}

- (void)handleEquipmentPan:(UIPanGestureRecognizer *)gesture {
    UIButton *button = (UIButton *)gesture.view;
    if (full_touch_controller || !guest_running) return;
    if (gesture.state == UIGestureRecognizerStateBegan) [self onPersistentButtonDown:button];
    if (gesture.state == UIGestureRecognizerStateBegan || gesture.state == UIGestureRecognizerStateChanged) {
        CGPoint delta = [gesture translationInView:self.view];
        [gesture setTranslation:CGPointZero inView:self.view];
        cameraAddTouchDelta((float)delta.x, 0);
    } else if (gesture.state == UIGestureRecognizerStateEnded || gesture.state == UIGestureRecognizerStateCancelled || gesture.state == UIGestureRecognizerStateFailed) {
        [self onPersistentButtonUp:button];
    }
}

- (void)setupDynamicActionButtons {
    [self releaseAllActionButtons];
    if (!self.dynamicActionButtons) self.dynamicActionButtons = [NSMutableArray array];
    if (!self.pressedActionButtons) self.pressedActionButtons = [NSMutableDictionary dictionary];
    for (UIButton *b in self.dynamicActionButtons) [b removeFromSuperview];
    [self.dynamicActionButtons removeAllObjects];

    LayoutConfig cfg = actionGetLayoutConfig();
    BOOL hasInteraction = NO;
    for (int i = 0; i < cfg.buttonCount; i++) {
        LayoutButtonDef btnDef = cfg.buttons[i];
        if (!strcmp(btnDef.id, "interact_a")) hasInteraction = YES;
        UIButton *btn = [UIButton buttonWithType:UIButtonTypeCustom];
        btn.tag = i + 100;
        btn.layer.cornerRadius = MIN(btnDef.width, btnDef.height) * 0.5f;
        btn.layer.borderWidth = 1.8f;
        btn.layer.borderColor = [UIColor colorWithWhite:1.0f alpha:0.45f].CGColor;
        btn.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:0.62f];
        [btn setTitle:touch_title(@(btnDef.title), @"操作") forState:UIControlStateNormal];
        [btn setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];

        CGFloat fontSize = (btnDef.width >= 70.0f) ? 17.0f : ((btnDef.width >= 50.0f) ? 14.0f : 12.0f);
        btn.titleLabel.font = [UIFont systemFontOfSize:fontSize weight:UIFontWeightBold];
        if (!strcmp(btnDef.id, "interact_a")) {
            btn.titleLabel.numberOfLines = 2;
            btn.titleLabel.textAlignment = NSTextAlignmentCenter;
            btn.titleLabel.adjustsFontSizeToFitWidth = YES;
            btn.titleLabel.minimumScaleFactor = .7;
            btn.contentEdgeInsets = UIEdgeInsetsMake(4, 6, 4, 6);
        }

        [btn addTarget:self action:@selector(onActionButtonDown:) forControlEvents:UIControlEventTouchDown];
        [btn addTarget:self action:@selector(onActionButtonUp:) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];

        [self.view addSubview:btn];
        [self.dynamicActionButtons addObject:btn];
    }
    inputSetTouchInteractionEnabled(hasInteraction && !full_touch_controller);
}

- (void)refreshDynamicButtonLayout {
    CGSize size = self.view.bounds.size;
    if (size.width <= 0 || size.height <= 0) return;

    UIEdgeInsets safe = self.view.safeAreaInsets;
    UiSafeAreaInsets insets = { (float)safe.top, (float)safe.left, (float)safe.bottom, (float)safe.right };

    LayoutConfig cfg = actionGetLayoutConfig();
    BOOL isLocked = cameraIsTargetLocked();
    for (UIButton *button in control_toolbar.arrangedSubviews) button.enabled = guest_running || button == control_mode_button || button == aspect_button;
    fallback_toolbar.hidden = !guest_running;
    for (UIButton *button in fallback_toolbar.arrangedSubviews) {
        button.enabled = guest_running;
        button.hidden = !full_touch_controller && !(button.tag & (PAD_BTN_D_LEFT | PAD_BTN_D_RIGHT | PAD_BTN_D_UP));
    }
    performance_width.constant = performance_expanded ? MIN(260, MAX(130, size.width - safe.left - safe.right - 420)) : 130;
    InputInteractionPrompt prompt = {0};
    BOOL hasPrompt = inputGetInteractionPrompt(&prompt);
    BOOL gameplay = cameraIsGameplayActive();
    BOOL replacementAvailable = NO;
    for (int i = 0; i < cfg.buttonCount; i++)
        if (!strcmp(cfg.buttons[i].id, "interact_a")) replacementAvailable = YES;

    // A real native action gets the middle-right slot; otherwise A remains in
    // the menu toolbar, including title screens with no camera/UI bridge yet.
    BOOL contextual = guest_running && !full_touch_controller && replacementAvailable && hasPrompt && inputGetDevice() == INPUT_DEVICE_TOUCH;
    if (contextual && !confirm_button.hidden) [self onPersistentButtonUp:confirm_button];
    confirm_button.hidden = contextual;

    for (NSUInteger i = 0; i < self.dynamicActionButtons.count && (int)i < cfg.buttonCount; i++) {
        UIButton *btn = self.dynamicActionButtons[i];
        LayoutButtonDef btnDef = cfg.buttons[i];
        if (!strcmp(btnDef.id, "interact_a")) btnDef.visibleEvents = EVENT_MASK_ALWAYS;

        ComputedButtonRect rect = actionComputeButtonRect(&btnDef, (float)size.width, (float)size.height, &insets);
        // Bounds/center remain well-defined during the pressed scale animation.
        btn.bounds = CGRectMake(0, 0, rect.width, rect.height);
        btn.center = CGPointMake(rect.x + rect.width * .5, rect.y + rect.height * .5);
        btn.layer.cornerRadius = MIN(rect.width, rect.height) * 0.5f;

        // 仅在游戏已载入且事件激活时显示
        BOOL hidden = !guest_running || full_touch_controller || !rect.isVisible || inputGetDevice() == INPUT_DEVICE_GAMEPAD;
        if (!gameplay && strcmp(btnDef.id, "interact_a")) hidden = YES;
        if (!strcmp(btnDef.id, "interact_a")) {
            hidden |= !hasPrompt;
            NSString *title = hasPrompt ? touch_title([[NSString alloc] initWithCharacters:prompt.text length:prompt.length], @"操作") : @"确定";
            BOOL titleChanged = ![btn.currentTitle isEqualToString:title];
            if (titleChanged) [btn setTitle:title forState:UIControlStateNormal];
            NSNumber *held = self.pressedActionButtons[@(btn.tag)];
            if (held && (hidden || titleChanged || held.unsignedLongLongValue != (hasPrompt ? prompt.button : PAD_BTN_A)))
                [self onActionButtonUp:btn];
        }
        // The primary native action now owns glider availability; an in-air
        // guide alone is insufficient to know whether the glider can open.
        if (!strcmp(btnDef.id, "glider_x") && gameplay && replacementAvailable) hidden = YES;
        if (hidden && !btn.hidden) [self onActionButtonUp:btn];
        btn.hidden = hidden;

        if (btnDef.padButton == PAD_BTN_ZL && isLocked) {
            [btn setTitle:@"已锁定" forState:UIControlStateNormal];
            // 锁敌激活时高亮
            btn.backgroundColor = [UIColor colorWithRed:0.95 green:0.75 blue:0.15 alpha:0.88];
            btn.layer.borderColor = [UIColor colorWithRed:1.0 green:0.9 blue:0.3 alpha:1.0].CGColor;
        } else if (rect.isHighlighted || (gameplay && hasPrompt && !strcmp(btnDef.id, "interact_a"))) {
            btn.backgroundColor = [UIColor colorWithRed:0.2 green:0.75 blue:0.95 alpha:0.88];
            btn.layer.borderColor = [UIColor colorWithRed:0.4 green:0.9 blue:1.0 alpha:1.0].CGColor;
        } else {
            if (btnDef.padButton == PAD_BTN_ZL) [btn setTitle:touch_title(@(btnDef.title), @"注视") forState:UIControlStateNormal];
            btn.backgroundColor = [UIColor.blackColor colorWithAlphaComponent:0.62f];
            btn.layer.borderColor = [UIColor colorWithWhite:1.0f alpha:0.45f].CGColor;
        }
    }
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    [self refreshDynamicButtonLayout];
}

- (void)onActionButtonDown:(UIButton *)sender {
    int index = (int)sender.tag - 100;
    LayoutConfig cfg = actionGetLayoutConfig();
    if (index < 0 || index >= cfg.buttonCount) return;
    LayoutButtonDef btn = cfg.buttons[index];
    if (!strcmp(btn.id, "interact_a")) {
        btn.isSpecialAction = false;
        InputInteractionPrompt prompt = {0};
        if (!inputGetInteractionPrompt(&prompt)) return;
        btn.padButton = prompt.button;
    }
    inputNoteDevice(INPUT_DEVICE_TOUCH);

    [UIView animateWithDuration:0.08 animations:^{
        sender.transform = CGAffineTransformMakeScale(0.92, 0.92);
    }];

    if (btn.isSpecialAction) {
        if (btn.padButton == PAD_BTN_SPECIAL_SWITCH) {
            cameraSwitchTarget(1);
        }
    } else {
        if (btn.padButton == PAD_BTN_ZL) {
            self.pressedActionButtons[@(sender.tag)] = @(btn.padButton);
            cameraHandleLockDragButton(true);
            [self refreshDynamicButtonLayout];
        } else {
            self.pressedActionButtons[@(sender.tag)] = @(btn.padButton);
            inputTouchButton(btn.padButton, true);
        }
    }
}

- (void)onActionButtonUp:(UIButton *)sender {
    [UIView animateWithDuration:0.08 animations:^{
        sender.transform = CGAffineTransformIdentity;
    }];

    NSNumber *mask = self.pressedActionButtons[@(sender.tag)];
    if (mask.unsignedLongLongValue == PAD_BTN_ZL) cameraHandleLockDragButton(false);
    [self.pressedActionButtons removeObjectForKey:@(sender.tag)];
    if (mask && ![self.pressedActionButtons.allValues containsObject:mask]) {
        inputTouchButton(mask.unsignedLongLongValue, false);
    }
}
- (void)releaseAllActionButtons {
    for (NSNumber *mask in self.pressedActionButtons.allValues) inputTouchButton(mask.unsignedLongLongValue, false);
    [self.pressedActionButtons removeAllObjects];
    cameraHandleLockDragButton(false);
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    start_game();

    __weak ZeldaController *weakSelf = self;
    if (documents) {
        [NSFileManager.defaultManager createDirectoryAtPath:[documents stringByAppendingPathComponent:@"work"]
            withIntermediateDirectories:YES attributes:nil error:nil];
        NSString *camCfg = [documents stringByAppendingPathComponent:@"work/camera_config.json"];
        fileWatcherStart(camCfg.fileSystemRepresentation, ^(const char *path) {
            cameraCheckAndReloadConfig(path);
        });
        NSString *layoutCfg = [documents stringByAppendingPathComponent:@"work/layout_config.json"];
        fileWatcherStart(layoutCfg.fileSystemRepresentation, ^(const char *path) {
            FILE *f = fopen(path, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long len = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (len > 0 && len < 65536) {
                    char *buf = (char *)malloc(len + 1);
                    if (!buf) { fclose(f); return; }
                    size_t rb = fread(buf, 1, len, f);
                    buf[rb] = '\0';
                    NSData *data = [NSData dataWithBytes:buf length:rb];
                    id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
                    if ([json isKindOfClass:NSDictionary.class] && [json[@"buttons"] isKindOfClass:NSArray.class] && actionLayoutLoadFromJson(buf)) {
                        dispatch_async(dispatch_get_main_queue(), ^{
                            [weakSelf setupDynamicActionButtons];
                            [weakSelf refreshDynamicButtonLayout];
                        });
                    }
                    free(buf);
                }
                fclose(f);
            }
        });
    }
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (void)cancelCloudSyncAction:(UIButton *)button {
    (void)button;
    cloudSaveSyncCancel();
    cloudCancelButton.hidden = YES;
}
- (void)pickCloudFolderAction:(UIButton *)button {
    (void)button;
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *folderURL = urls.firstObject;
    if (folderURL && cloudSaveSyncSetFolderURL(folderURL)) {
        [cloudFolderButton setTitle:@"Cloud: Linked" forState:UIControlStateNormal];
        status(@"Cloud folder linked.\nNext launch will sync automatically.");
    }
}
- (void)retryGame:(UIButton *)button { (void)button; start_game(); }
- (void)onPersistentButtonDown:(UIButton *)button {
    if (!guest_running) return;
    self.pressedActionButtons[@(button.tag + 10000)] = @((uint64_t)button.tag);
    inputTouchButton((uint64_t)button.tag, true);
}
- (void)onPersistentButtonUp:(UIButton *)button {
    NSNumber *mask = self.pressedActionButtons[@(button.tag + 10000)];
    [self.pressedActionButtons removeObjectForKey:@(button.tag + 10000)];
    if (mask && ![self.pressedActionButtons.allValues containsObject:mask]) inputTouchButton(mask.unsignedLongLongValue, false);
}
- (void)toggleScreenAspect:(UIButton *)button {
    int mode = front_screen_aspect_mode() == ASPECT_MODE_FILL ? ASPECT_MODE_FIT : ASPECT_MODE_FILL;
    atomic_store(&selected_screen_aspect, mode);
    [NSUserDefaults.standardUserDefaults setInteger:mode forKey:@"ZELDA_SCREEN_ASPECT_MODE"];
    [button setTitle:mode == ASPECT_MODE_FILL ? @"完整画面" : @"铺满屏幕" forState:UIControlStateNormal];
    [self.view setNeedsLayout];
    [self.nativePrompts setNeedsDisplay];
}
- (void)toggleTouchControls:(UIButton *)button {
    [self releaseAllActionButtons];
    self.isPanTracking = NO; input_clear();
    disconnect_touch_controller();
    full_touch_controller = !full_touch_controller;
    [NSUserDefaults.standardUserDefaults setBool:full_touch_controller forKey:@"ZELDA_TOUCH_FULL_CONTROLLER"];
    [button setTitle:full_touch_controller ? @"手游模式" : @"虚拟手柄" forState:UIControlStateNormal];
    inputNoteDevice(INPUT_DEVICE_TOUCH);
    [self setupDynamicActionButtons];
    [self refreshDynamicButtonLayout];
    if (guest_running) connect_touch_controller();
}
- (void)togglePerformance:(UIButton *)button {
    (void)button; performance_expanded = !performance_expanded; [self updatePerformance];
}
- (void)tick:(CADisplayLink *)link {
    cameraFrameSync();
    [self.nativePrompts refreshNativePrompts];
    actionDispatchEvents(inputGetActionEvents());
    [self refreshDynamicButtonLayout];
    (void)link;pthread_mutex_lock(&vsync_mutex);uint32_t write=vsync_write;pthread_mutex_unlock(&vsync_mutex);
    if (write) { uint64_t regs[31]={write};if (!kernel_svc(0x11,regs)||regs[0]) abort(); }
}
- (void)updatePerformance {
    ZeldaPerformance p; BOOL ready=performance_snapshot(&p);
    NSProcessInfo *process=NSProcessInfo.processInfo;
    unsigned cores=(unsigned)process.activeProcessorCount;
    double now=CACurrentMediaTime(), physical=physical_gib(), available=os_proc_available_memory()/1048576.0;
    struct rusage usage;
    double cpu=getrusage(RUSAGE_SELF,&usage)==0 ? usage.ru_utime.tv_sec+usage.ru_stime.tv_sec+
        (usage.ru_utime.tv_usec+usage.ru_stime.tv_usec)/1e6 : NAN;
    double cpu_percent=zelda_cpu_percent(&cpu_usage,now,cpu,cores);
    BOOL active=UIApplication.sharedApplication.applicationState==UIApplicationStateActive;
    BOOL fresh=ready && active && p.last_presented>0 && now-p.last_presented<2;
    double fps=fresh?p.fps:-1, gpu=fresh?p.gpu_ms:-1, target=zelda_game_frame_rate();
    // shortcut: sensor temperature and hardware GPU load have no verified sampler here; omit them until available.
    NSInteger heat=MIN(MAX(process.thermalState,0),3);
    performance_panel.hidden=zelda_settings().hide_performance;
    performance_label.hidden=!performance_expanded;
    NSString *rate=fps>=0?[NSString stringWithFormat:@"FPS %@ / %.0f",metric(fps,@""),target]:active?@"Preparing":@"Paused";
    // System buttons crossfade changing titles, making a once-per-second counter blink.
    [UIView performWithoutAnimation:^{
        [performance_toggle setTitle:[NSString stringWithFormat:@"%@  %@",rate,
            performance_expanded?@"▴":@"▾"] forState:UIControlStateNormal];
        [performance_toggle layoutIfNeeded];
    }];
    performance_toggle.accessibilityLabel=performance_expanded?@"Collapse performance details":@"Expand performance details";
    performance_toggle.accessibilityValue=performance_toggle.currentTitle;
    NSString *render=p.render_width?[NSString stringWithFormat:@"%u × %u",p.render_width,p.render_height]:@"N/A";
    NSMutableArray<NSString *> *details=[NSMutableArray array];
    if (p.render_width) [details addObject:[@"Render  " stringByAppendingString:render]];
    NSMutableArray<NSString *> *load=[NSMutableArray array];
    if (isfinite(cpu_percent)) [load addObject:[@"CPU app " stringByAppendingString:metric(cpu_percent,@"%")]];
    if (gpu>=0) [load addObject:[@"GPU " stringByAppendingString:metric(gpu,@" ms")]];
    if (load.count) [details addObject:[load componentsJoinedByString:@"  |  "]];
    if (physical>=0) [details addObject:[NSString stringWithFormat:@"Memory  %.2f GiB",physical]];
    if (process.lowPowerModeEnabled) [details addObject:@"Low Power Mode"];
    performance_label.text=[details componentsJoinedByString:@"\n"];

    fprintf(stderr,"IOS_PERF time_s=%.3f frame=%llu fps=%.3f render_fps=%.3f p95_ms=%.3f target=%.0f width=%u height=%u "
        "gpu_ms=%.3f cpu_ms=%.3f cpu_percent=%.3f cpu_cores=%u physical_gib=%.3f available_mib=%.1f "
        "thermal=%ld active=%d low_power=%d game_s=%.6f\n",
        now,(unsigned long long)p.frame,fps,fresh?p.render_fps:-1,fresh?p.frame_p95_ms:-1,target,p.render_width,p.render_height,
        gpu,fresh?p.submit_cpu_ms:-1,cpu_percent,cores,physical,available,(long)heat,active,process.lowPowerModeEnabled,zelda_game_seconds());
}
@end

@interface ZeldaSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation ZeldaSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session;(void)options;
    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController=[ZeldaController new];[self.window makeKeyAndVisible];
}
- (void)sceneWillResignActive:(UIScene *)scene { (void)scene;gamepad_active(false);audio_mute(true); }
- (void)sceneDidEnterBackground:(UIScene *)scene {
    UIApplication.sharedApplication.idleTimerDisabled=NO;
    (void)scene;if (guest_running) { mtl_present_discard(1);dm_gfx_suspend(1); }
    [AVAudioSession.sharedInstance setActive:NO error:nil];
    fprintf(stderr,"IOS_LIFECYCLE background\n");fflush(NULL);
}
- (void)sceneWillEnterForeground:(UIScene *)scene {
    (void)scene;if (guest_running) { dm_gfx_suspend(0);mtl_present_discard(0); }
}
- (void)sceneDidBecomeActive:(UIScene *)scene {
    UIApplication.sharedApplication.idleTimerDisabled=YES;
    cpu_usage=(ZeldaCPUUsage){0}; performance_reset_window();
    (void)scene;[AVAudioSession.sharedInstance setActive:YES error:nil];
    gamepad_active(true);audio_mute(zelda_settings().muted);fprintf(stderr,"IOS_LIFECYCLE active\n");
}
@end

@interface ZeldaAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation ZeldaAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app;(void)options;
    documents=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
    if (chdir(documents.fileSystemRepresentation)) abort();
    rename("boot.log","boot.previous.log");
    if (!freopen("boot.log","w",stderr)) abort();setvbuf(stderr,NULL,_IOLBF,0);
    dup2(STDERR_FILENO,STDOUT_FILENO);setvbuf(stdout,NULL,_IOLBF,0);
    struct rlimit files;if (!getrlimit(RLIMIT_NOFILE,&files)) { files.rlim_cur=MIN(files.rlim_max,4096);setrlimit(RLIMIT_NOFILE,&files); }
    fprintf(stderr,"IOS_BOOT version=0.1 build=1 chain=%s\n",DMSH_CHAIN);
    NSDictionary *disk=[NSFileManager.defaultManager attributesOfFileSystemForPath:documents error:nil];
    fprintf(stderr,"IOS_STORAGE free_gib=%.3f\n",[disk[NSFileSystemFreeSize] unsignedLongLongValue]/1073741824.0);
    setenv("TZ",NSTimeZone.localTimeZone.name.UTF8String,1);tzset();
    return YES;
}
- (void)applicationDidReceiveMemoryWarning:(UIApplication *)app {
    (void)app;fprintf(stderr,"IOS_MEMORY_WARNING available_mib=%.1f\n",os_proc_available_memory()/1048576.0);
    if (resource_report) resource_report(0,"iOS memory warning");
}
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(ZeldaAppDelegate.class)); } }
