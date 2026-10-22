#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreVideo/CoreVideo.h>
#include "dm.h"
#include "front.h"
#include "kernel.h"
#include "input.h"
#include "gamepad.h"
#include "shader_prep.h"
#include "game_timing.h"
#include "settings.h"
#include "camera_controller.h"
#include <unistd.h>

static CAMetalLayer *metal_layer;
#include "performance_overlay.inc"
static void (*guest_entry)(void);
static void (*wait_dump)(void);
static void (*resource_report)(uint32_t, const char *);
static CVDisplayLinkRef vsync_link;
static dispatch_source_t vsync_timer;
static dispatch_queue_t vsync_queue;
static uint32_t vsync_write, vsync_read;
static pthread_mutex_t vsync_lock = PTHREAD_MUTEX_INITIALIZER;

static void stop_vsync(void) {
    if (vsync_timer) {
        dispatch_source_cancel(vsync_timer);
        dispatch_sync(vsync_queue, ^{});
        vsync_timer = nil; vsync_queue = nil;
    }
    if (vsync_link) {
        CVDisplayLinkStop(vsync_link);
        CVDisplayLinkRelease(vsync_link);
        vsync_link = NULL;
    }
}

static CVReturn display_refresh(CVDisplayLinkRef link, const CVTimeStamp *now,
        const CVTimeStamp *output, CVOptionFlags flags, CVOptionFlags *out_flags, void *context) {
    (void)link; (void)now; (void)output; (void)flags; (void)out_flags; (void)context;
    // The guest display runs at most 60 Hz, including on ProMotion hosts.
    static uint64_t previous_slot;
    uint64_t slot = now_ns() / 16666667;
    if (slot != previous_slot) {
        previous_slot = slot;
        cameraFrameSync();
        uint64_t regs[31] = {vsync_write};
        if (!kernel_svc(0x11, regs) || regs[0]) abort();
    }
    return kCVReturnSuccess;
}

uint32_t front_vsync_handle(void) {
    pthread_mutex_lock(&vsync_lock);
    if (!vsync_write) {
        if (!kernel_event_create(&vsync_write, &vsync_read)) die("display event handles exhausted");
        CVReturn result = CVDisplayLinkCreateWithActiveCGDisplays(&vsync_link);
        if (!result) result = CVDisplayLinkSetOutputCallback(vsync_link, display_refresh, NULL);
        if (!result) result = CVDisplayLinkStart(vsync_link);
        if (result) {
            if (vsync_link) { CVDisplayLinkRelease(vsync_link); vsync_link = NULL; }
            // A sleeping/disconnected host display must not stop the emulated console clock.
            vsync_queue = dispatch_queue_create("zelda.display-clock", DISPATCH_QUEUE_SERIAL);
            vsync_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, vsync_queue);
            dispatch_source_set_timer(vsync_timer, DISPATCH_TIME_NOW, 16666667, 100000);
            dispatch_source_set_event_handler(vsync_timer, ^{ display_refresh(NULL, NULL, NULL, 0, NULL, NULL); });
            dispatch_resume(vsync_timer);
            LOG("VI virtual 60 Hz clock started; host display link unavailable code=%d", result);
        } else LOG("VI host refresh event started");
        if (atexit(stop_vsync)) die("display cleanup registration failed");
    }
    uint32_t copy = kernel_event_copy(vsync_read);
    pthread_mutex_unlock(&vsync_lock);
    return copy;
}

void *front_metal_layer(void) { return (__bridge void *)metal_layer; }
void crash_set_wait_dump(void (*fn)(void)) { wait_dump = fn; }
void dm_res_report_register(void (*fn)(uint32_t, const char *)) { resource_report = fn; }

static void *run_guest(void *unused) {
    (void)unused;
    @autoreleasepool { guest_entry(); }
    dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
    return NULL;
}

// The drawable follows the window's backing pixels (Retina, resizing, full screen); presentation scales the game
// image into it with the original aspect ratio.
static void fit_drawable(NSWindow *window) {
    NSView *view = window.contentView;
    CGFloat scale = window.backingScaleFactor;
    NSSize size = view.bounds.size;
    CGSize pixels = CGSizeMake(MAX(1, round(size.width * scale)), MAX(1, round(size.height * scale)));
    metal_layer.contentsScale = scale;
    if (!CGSizeEqualToSize(metal_layer.drawableSize, pixels)) metal_layer.drawableSize = pixels;
}

static void store_settings(void (^change)(zelda_settings_t *settings)) {
    zelda_settings_t settings = zelda_settings();
    change(&settings);
    zelda_settings_store(settings);
}

#include "control_panel.inc"
// Shared by the menu bar and the control panel: apply at once, save, then refresh the panel.
static void apply_target_fps(unsigned fps) {
    if (zelda_set_target_fps(fps)) {
        store_settings(^(zelda_settings_t *settings) { settings->target_fps = fps; });
        LOG("SETTINGS target_fps=%u", fps);
    }
    controls_refresh();
}
static void apply_render(int mode) {
    if (zelda_set_render_scale(zelda_render_scale_factor(mode))) {
        store_settings(^(zelda_settings_t *settings) { settings->render = mode; });
        LOG("SETTINGS render_resolution=%d scale=%.2f", mode, zelda_render_scale());
    }
    controls_refresh();
}
static void apply_metalfx(int mode) {
    store_settings(^(zelda_settings_t *settings) { settings->metalfx = mode; });
    zelda_present_metalfx(mode);
    controls_refresh();
}
static void apply_sound(bool on) {
    audio_mute(!on);
    store_settings(^(zelda_settings_t *settings) { settings->muted = !on; });
    controls_refresh();
}
static void apply_performance(bool shown) {
    performance_set_visible(shown);
    store_settings(^(zelda_settings_t *settings) { settings->hide_performance = !shown; });
    controls_refresh();
}
static void apply_vibration(int mode) {
    gamepad_vibration_mode(mode);
    store_settings(^(zelda_settings_t *settings) { settings->vibration = mode; });
    LOG("SETTINGS vibration=%d", mode);
    controls_refresh();
}

@interface ZeldaWindowDelegate : NSObject <NSWindowDelegate>
@end
@implementation ZeldaWindowDelegate
- (void)windowDidResignKey:(NSNotification *)notification {
    (void)notification;
    gamepad_active(false);
    if (cameraIsMouseCaptured()) {
        cameraSetMouseCaptured(false);
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
    }
}
- (void)windowDidBecomeKey:(NSNotification *)notification { (void)notification; gamepad_active(true); }
- (void)windowDidResize:(NSNotification *)notification { fit_drawable(notification.object); }
- (void)windowDidChangeBackingProperties:(NSNotification *)notification { fit_drawable(notification.object); }
- (void)windowDidEnterFullScreen:(NSNotification *)notification {
    fit_drawable(notification.object);
    store_settings(^(zelda_settings_t *settings) { settings->fullscreen = true; });
    controls_refresh();
}
- (void)windowDidExitFullScreen:(NSNotification *)notification {
    fit_drawable(notification.object);
    store_settings(^(zelda_settings_t *settings) { settings->fullscreen = false; });
    controls_refresh();
}
- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    if (cameraIsMouseCaptured()) {
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
    }
    stop_vsync();
    gamepad_stop(); performance_stop();
    if (resource_report) resource_report(0, "window closed");
    // Reclaim live guest threads and host resources together; per-resource exit handlers race ongoing IPC.
    fflush(NULL); _exit(0);
}
@end

@interface ZeldaContainerView : NSView
@end
@implementation ZeldaContainerView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }
- (BOOL)needsPanelToBecomeKey { return YES; }
- (void)keyDown:(NSEvent *)event {
    // 吸收任何可能绕过 Monitor 的按键事件，绝不调 super，彻底消灭 macOS 默认无焦点 beep 提示音
    input_key(event.keyCode, 1);
}
- (void)keyUp:(NSEvent *)event {
    input_key(event.keyCode, 0);
}
- (void)flagsChanged:(NSEvent *)event {
    int down = !!(event.modifierFlags & NSEventModifierFlagShift);
    input_key(event.keyCode, down);
}
@end

@interface ZeldaMenu : NSObject <NSMenuItemValidation>
@property (weak) NSWindow *window;
@end
@implementation ZeldaMenu
- (void)targetFPS:(NSMenuItem *)item { apply_target_fps((unsigned)item.tag); }
- (void)fullScreen:(id)sender { (void)sender; [self.window toggleFullScreen:nil]; }
- (void)metalFX:(NSMenuItem *)item { apply_metalfx((int)item.tag); }
- (void)renderResolution:(NSMenuItem *)item { apply_render((int)item.tag); }
- (void)togglePerformance:(id)sender { (void)sender; apply_performance(!performance_visible()); }
- (void)toggleControls:(id)sender { (void)sender; controls_toggle(); }
- (void)quit:(id)sender { (void)sender; [self.window performClose:nil]; }
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if (item.action == @selector(targetFPS:)) {
        item.state = zelda_target_fps() == (unsigned)item.tag ? NSControlStateValueOn : NSControlStateValueOff;
        return zelda_target_fps() != 0;
    }
    if (item.action == @selector(renderResolution:)) {
        item.state = zelda_settings().render == item.tag ? NSControlStateValueOn : NSControlStateValueOff;
        return zelda_render_scale() != 0;
    }
    if (item.action == @selector(metalFX:)) item.state = zelda_settings().metalfx == item.tag ? NSControlStateValueOn : NSControlStateValueOff;
    if (item.action == @selector(togglePerformance:)) item.state = performance_visible() ? NSControlStateValueOn : NSControlStateValueOff;
    if (item.action == @selector(fullScreen:))
        item.title = self.window.styleMask & NSWindowStyleMaskFullScreen ? @"Exit Full Screen" : @"Enter Full Screen";
    return YES;
}
@end

static NSMenuItem *menu_item(NSString *title, SEL action, NSString *key, NSEventModifierFlags modifiers, id target) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
    item.keyEquivalentModifierMask = modifiers; item.target = target;
    return item;
}

static void install_menu(ZeldaMenu *actions) {
    NSMenu *bar = [NSMenu new];
    NSMenu *app = [[NSMenu alloc] initWithTitle:@"Zelda-Mobile"];
    [app addItem:menu_item(@"Controls", @selector(toggleControls:), @",", NSEventModifierFlagCommand, actions)];
    [app addItem:[NSMenuItem separatorItem]];
    [app addItem:menu_item(@"Quit Zelda-Mobile", @selector(quit:), @"q", NSEventModifierFlagCommand, actions)];
    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    NSMenu *rates = [[NSMenu alloc] initWithTitle:@"Target Frame Rate"];
    for (unsigned fps = 30; fps <= 90; fps += 30) {
        NSMenuItem *item = menu_item([NSString stringWithFormat:@"%u FPS", fps], @selector(targetFPS:), @"", 0, actions);
        item.tag = fps; [rates addItem:item];
    }
    NSMenuItem *rate = [[NSMenuItem alloc] initWithTitle:@"Target Frame Rate" action:nil keyEquivalent:@""]; rate.submenu = rates;
    [view addItem:rate];
    NSMenu *renders = [[NSMenu alloc] initWithTitle:@"Render Resolution"];
    NSString *render_titles[] = {@"Original (Game Selected)", @"1080 (1920×1080)", @"720 (1280×720)", @"640 (1136×640)"};
    for (int i = 0; i < 4; i++) {
        NSMenuItem *item = menu_item(render_titles[i], @selector(renderResolution:), @"", 0, actions);
        item.tag = i; [renders addItem:item];
    }
    NSMenuItem *render = [[NSMenuItem alloc] initWithTitle:@"Render Resolution" action:nil keyEquivalent:@""]; render.submenu = renders;
    [view addItem:render];
    [view addItem:menu_item(@"Enter Full Screen", @selector(fullScreen:), @"f", NSEventModifierFlagCommand | NSEventModifierFlagControl, actions)];
    NSMenu *scales = [[NSMenu alloc] initWithTitle:@"MetalFX Upscaling"];
    NSString *scale_titles[] = {@"Off", @"Scale to Window", @"Scale to 4K (3840×2160)"};
    for (int i = 0; i < 3; i++) {
        NSMenuItem *item = menu_item(scale_titles[i], @selector(metalFX:), @"", 0, actions);
        item.tag = i; [scales addItem:item];
    }
    NSMenuItem *scale = [[NSMenuItem alloc] initWithTitle:@"MetalFX Upscaling" action:nil keyEquivalent:@""]; scale.submenu = scales;
    [view addItem:scale];
    [view addItem:menu_item(@"Performance Overlay", @selector(togglePerformance:), @"p", NSEventModifierFlagCommand, actions)];
    for (NSMenu *menu in @[app, view]) {
        NSMenuItem *holder = [[NSMenuItem alloc] initWithTitle:menu.title action:nil keyEquivalent:@""];
        holder.submenu = menu; [bar addItem:holder];
    }
    NSApp.mainMenu = bar;
}

void front_run(void (*guest)(void)) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSRect frame = NSMakeRect(0, 0, 960, 540);
        const char *points = getenv("ZELDA_WINDOW_POINTS");   // e.g. 1600x900, to exercise upscaling without full screen
        unsigned pw, ph;
        if (points && sscanf(points, "%ux%u", &pw, &ph) == 2 && pw >= 640 && ph >= 360) frame.size = NSMakeSize(pw, ph);
        NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        window.title = @"Zelda-Mobile";
        window.contentAspectRatio = NSMakeSize(16, 9);
        window.contentMinSize = NSMakeSize(640, 360);
        window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
        ZeldaWindowDelegate *delegate = [ZeldaWindowDelegate new];
        window.delegate = delegate;
        ZeldaMenu *actions = [ZeldaMenu new]; actions.window = window;
        // Sound used to be saved in user defaults; carry it into the settings file once.
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        if ([defaults objectForKey:@"ZeldaSoundMuted"]) {
            bool muted = [defaults boolForKey:@"ZeldaSoundMuted"];
            store_settings(^(zelda_settings_t *settings) { settings->muted = muted; });
            [defaults removeObjectForKey:@"ZeldaSoundMuted"];
        }
        zelda_settings_t settings = zelda_settings();
        audio_mute(settings.muted); gamepad_vibration_mode(settings.vibration);
        install_menu(actions);

        static NSVisualEffectView *g_keymap_hud = nil;
        static NSTextField *g_keymap_label = nil;
        static BOOL g_keymap_visible = YES;

        NSEventMask eventMask = NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged |
                               NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp |
                               NSEventMaskRightMouseDown | NSEventMaskRightMouseUp |
                               NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp |
                               NSEventMaskScrollWheel |
                               NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged;

        [NSEvent addLocalMonitorForEventsMatchingMask:eventMask
            handler:^NSEvent *(NSEvent *event) {
                if (event.window != window) return event;

                // -------------------------------------------------------------
                // 1. 键盘事件流：独立拦截与分发，杜绝任何按键回退给系统产生无焦点嘟嘟声
                // -------------------------------------------------------------
                if (event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp || event.type == NSEventTypeFlagsChanged) {
                    // Command 组合快捷键（如 Cmd+Q, Cmd+F, Cmd+P 等）属于系统及应用菜单，必须放行
                    if (event.type == NSEventTypeKeyDown && (event.modifierFlags & NSEventModifierFlagCommand)) {
                        return event;
                    }

                    // F1 键 (keyCode 122)：一键切换 UltraCam 风格 PC 键鼠悬浮 HUD
                    if (event.type == NSEventTypeKeyDown && event.keyCode == 122) {
                        g_keymap_visible = !g_keymap_visible;
                        [NSAnimationContext runAnimationGroup:^(NSAnimationContext *ctx) {
                            ctx.duration = 0.2;
                            g_keymap_hud.animator.alphaValue = g_keymap_visible ? 1.0 : 0.0;
                        }];
                        return nil;
                    }

                    // ESC 键 (keyCode 53)：
                    // 若当前鼠标已被捕获，按下优先释放鼠标捕获唤回系统光标
                    if (event.keyCode == 53 && cameraIsMouseCaptured()) {
                        if (event.type == NSEventTypeKeyDown) {
                            cameraSetMouseCaptured(false);
                            CGAssociateMouseAndMouseCursorPosition(true);
                            [NSCursor unhide];
                        }
                        return nil;
                    }

                    // 游戏常规按键：无论是在标题、菜单还是海拉鲁大陆游玩中，一律直通虚拟手柄
                    int down = event.type == NSEventTypeFlagsChanged ? !!(event.modifierFlags & NSEventModifierFlagShift) : event.type == NSEventTypeKeyDown;
                    input_key(event.keyCode, down);

                    // 绝对必须返回 nil，表示该按键已被游戏完全消费，绝不能回退给系统触发无焦点提示音
                    return nil;
                }

                // -------------------------------------------------------------
                // 2. 鼠标与触控事件流：执行精准相对位移拦截与视角控制
                // -------------------------------------------------------------

                // 当光标被捕获锁定时，相对位移 (deltaX, deltaY) 完全独立于光标绝对位置。
                // 必须在任何绝对矩形命中测试之前优先消费，避免大角度甩动或光标落在标题栏/HUD上被误丢弃。
                BOOL relativeMotion = (event.type == NSEventTypeMouseMoved ||
                                       event.type == NSEventTypeLeftMouseDragged ||
                                       event.type == NSEventTypeRightMouseDragged ||
                                       event.type == NSEventTypeOtherMouseDragged);
                if (relativeMotion && cameraIsMouseCaptured()) {
                    cameraAddMouseDelta((float)event.deltaX, (float)event.deltaY);
                    return nil;
                }

                // 释放注入的鼠标按键
                if (event.type == NSEventTypeLeftMouseUp || event.type == NSEventTypeRightMouseUp) {
                    inputTouchButton(event.type == NSEventTypeLeftMouseUp ? (1u << 3) : (1u << 9), false);
                    if (cameraIsMouseCaptured()) return nil;
                }
                if (event.type == NSEventTypeOtherMouseUp) {
                    cameraHandleLockDragButton(false);
                }

                // 鼠标左键点击时确保窗口成为 Key Window，获得输入焦点
                if (event.type == NSEventTypeLeftMouseDown && !window.isKeyWindow) {
                    [window makeKeyAndOrderFront:nil];
                }

                // 命中测试 (Hit-Testing)：保护设置面板、左下角齿轮、右上角 HUD 面板与顶部标题栏
                NSPoint loc = event.locationInWindow;
                if (controls) {
                    // 若控制面板已展开，放行所有鼠标点击与拖动事件给设置面板
                    if (controls->panel && !controls->panel.hidden) {
                        return event;
                    }
                    // 若点击落在左下角设置图标 handle 内，放行给按钮响应展开
                    if (controls->handle && !controls->handle.hidden) {
                        NSRect hRect = [controls->handle convertRect:controls->handle.bounds toView:nil];
                        if (NSPointInRect(loc, hRect)) return event;
                    }
                }
                // 若点击落在右上角 UltraCam 键位面板上，放行鼠标点击
                if (g_keymap_hud && !g_keymap_hud.hidden && g_keymap_hud.alphaValue > 0.05) {
                    NSRect hudRect = [g_keymap_hud convertRect:g_keymap_hud.bounds toView:nil];
                    if (NSPointInRect(loc, hudRect)) return event;
                }
                // 窗口顶部标题栏与菜单栏区域放行鼠标点击与拖动
                if (loc.y >= window.contentView.bounds.size.height - 28.0) {
                    return event;
                }

                // 游戏世界生命周期门禁：只有真正载入存档进入 3D 世界游玩时才允许捕获视角！
                if (!cameraIsGameplayActive()) {
                    if (cameraIsMouseCaptured()) {
                        cameraSetMouseCaptured(false);
                        CGAssociateMouseAndMouseCursorPosition(true);
                        [NSCursor unhide];
                    }
                    return event;
                }

                // 鼠标中键/侧键：单击锁定/解锁，按住左右甩动鼠标直接切换焦点目标
                if (event.type == NSEventTypeOtherMouseDown) {
                    cameraHandleLockDragButton(true);
                    return nil;
                }

                // 滚轮控制：目标锁定时切换怪物；常规游玩时平滑缩放 SpringArm 弹簧臂距离 (Zoom In / Out)
                if (event.type == NSEventTypeScrollWheel) {
                    if (cameraIsTargetLocked()) {
                        if (fabs(event.scrollingDeltaY) > 2.0) {
                            cameraSwitchTarget(event.scrollingDeltaY > 0 ? 1 : -1);
                        }
                    } else if (cameraIsMouseCaptured()) {
                        float zoomDelta = (float)(-event.scrollingDeltaY * 0.08f);
                        cameraAddZoom(zoomDelta);
                    }
                    return nil;
                }

                // 鼠标左键：未捕获时激活捕获；捕获后映射近战普通攻击 (Y 键)
                if (event.type == NSEventTypeLeftMouseDown) {
                    if (!cameraIsMouseCaptured()) {
                        cameraSetMouseCaptured(true);
                        CGAssociateMouseAndMouseCursorPosition(false);
                        [NSCursor hide];
                        return nil;
                    }
                    inputTouchButton(1u << 3, true);
                    return nil;
                }
                if (event.type == NSEventTypeLeftMouseUp) {
                    if (cameraIsMouseCaptured()) {
                        inputTouchButton(1u << 3, false);
                        return nil;
                    }
                }

                // 鼠标右键：未捕获时激活捕获；捕获后映射拉弓瞄准/松开射出 (ZR 键)
                if (event.type == NSEventTypeRightMouseDown) {
                    if (!cameraIsMouseCaptured()) {
                        cameraSetMouseCaptured(true);
                        CGAssociateMouseAndMouseCursorPosition(false);
                        [NSCursor hide];
                        return nil;
                    }
                    inputTouchButton(1u << 9, true);
                    return nil;
                }
                if (event.type == NSEventTypeRightMouseUp) {
                    if (cameraIsMouseCaptured()) {
                        inputTouchButton(1u << 9, false);
                        return nil;
                    }
                }


                return event;
            }];

        metal_layer = [CAMetalLayer layer];
        metal_layer.device = MTLCreateSystemDefaultDevice();
        if (!metal_layer.device) die("Metal presentation device unavailable");
        metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        ZeldaContainerView *containerView = [[ZeldaContainerView alloc] initWithFrame:frame];
        containerView.wantsLayer = YES;
        containerView.layer = metal_layer;
        window.contentView = containerView;
        [window makeFirstResponder:containerView];
        fit_drawable(window);
        [window center]; [window makeKeyAndOrderFront:nil];

        // 构建 UltraCam 风格 PC 键鼠实时悬浮 HUD 指南 (置于右上角，避免遮挡左下角控制面板)
        CGFloat hudW = 270, hudH = 320;
        NSRect hudFrame = NSMakeRect(window.contentView.bounds.size.width - hudW - 16,
                                     window.contentView.bounds.size.height - hudH - 16,
                                     hudW, hudH);
        g_keymap_hud = [[NSVisualEffectView alloc] initWithFrame:hudFrame];
        g_keymap_hud.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
        g_keymap_hud.material = NSVisualEffectMaterialHUDWindow;
        g_keymap_hud.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        g_keymap_hud.state = NSVisualEffectStateActive;
        g_keymap_hud.wantsLayer = YES;
        g_keymap_hud.layer.cornerRadius = 10.0;
        g_keymap_hud.layer.borderWidth = 1.0;
        g_keymap_hud.layer.borderColor = [NSColor colorWithWhite:1.0 alpha:0.25].CGColor;

        g_keymap_label = [[NSTextField alloc] initWithFrame:NSInsetRect(g_keymap_hud.bounds, 12, 10)];
        g_keymap_label.editable = NO;
        g_keymap_label.selectable = NO;
        g_keymap_label.bordered = NO;
        g_keymap_label.backgroundColor = [NSColor clearColor];
        g_keymap_label.textColor = [NSColor whiteColor];
        g_keymap_label.font = [NSFont monospacedSystemFontOfSize:11.5 weight:NSFontWeightMedium];
        g_keymap_label.stringValue =
            @"🎮 王国之泪 PC 键鼠指南 [F1 隐藏]\n"
            @"─────────────────────────────\n"
            @"🖱️ 鼠标移动  : 360° 无限自由视角\n"
            @"🖱️ 鼠标左键  : 近战普通攻击 (Y)\n"
            @"🖱️ 鼠标右键  : 射箭拉弓/松开射出 (ZR)\n"
            @"🖱️ 鼠标中键  : 单击锁定 / 甩动切怪\n"
            @"🖱️ 鼠标滚轮  : 快速切换注视目标\n"
            @"⌨️ WASD     : 控制角色全向移动\n"
            @"⌨️ Space    : 跳跃 / 滑翔伞 (X)\n"
            @"⌨️ Shift    : 疾跑 / 冲刺闪避 (B)\n"
            @"⌨️ E 键     : 调查/拾取/对话/骑马 (A)\n"
            @"⌨️ Q 键     : 究极手技能轮盘 (L)\n"
            @"⌨️ C / Ctrl : 潜行蹲下 (L3)\n"
            @"⌨️ Tab / M  : 开启地图 (Minus)\n"
            @"⌨️ 1/2/3/4  : 武器/弓盾材料轮盘\n"
            @"⌨️ ESC      : 释放光标 / 暂停菜单\n"
            @"─────────────────────────────\n"
            @"💡 左下角可呼出图形与性能设置";
        [g_keymap_hud addSubview:g_keymap_label];
        [window.contentView addSubview:g_keymap_hud positioned:NSWindowAbove relativeTo:nil];

        // 启动生命周期监测定时器：精准识别真正进入 3D 游戏游玩世界 vs 首页菜单/加载界面
        NSTimer *lifecycleTimer = [NSTimer scheduledTimerWithTimeInterval:0.2 repeats:YES block:^(NSTimer *timer) {
            (void)timer;
            double gameSec = zelda_game_seconds();
            // 世界时钟大于 0.2 说明已载入存档并处于 3D 游玩状态
            BOOL inOverworld = (gameSec > 0.2);
            if (inOverworld != cameraIsGameplayActive()) {
                cameraSetGameplayActive(inOverworld);
                if (!inOverworld && cameraIsMouseCaptured()) {
                    cameraSetMouseCaptured(false);
                    CGAssociateMouseAndMouseCursorPosition(true);
                    [NSCursor unhide];
                }
            }
        }];
        [[NSRunLoop currentRunLoop] addTimer:lifecycleTimer forMode:NSRunLoopCommonModes];
        if (settings.fullscreen) dispatch_async(dispatch_get_main_queue(), ^{ [window toggleFullScreen:nil]; });
        [NSApp activateIgnoringOtherApps:YES];
        gamepad_start(); gamepad_active(window.keyWindow);
        guest_entry = guest;
        shader_prep_begin(window.contentView, metal_layer.device, ^{
            performance_start(window, !zelda_settings().hide_performance);
            controls_start(window);
            pthread_attr_t attr;
            if (pthread_attr_init(&attr) || pthread_attr_setstacksize(&attr, 8 << 20)) abort();
            pthread_t thread;
            if (pthread_create(&thread, &attr, run_guest, NULL)) abort();
            pthread_attr_destroy(&attr); pthread_detach(thread);
        });
        [NSApp run];
    }
}
