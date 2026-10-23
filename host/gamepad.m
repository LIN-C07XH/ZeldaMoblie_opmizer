#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#import <TargetConditionals.h>
#include "gamepad.h"
#include "input.h"
#include "vibration.h"
#include "dm_padmap.h"
#include <stdio.h>
#include <time.h>

static GCController *selected;
static __weak GCController *virtual_controller;
static id connect_observer, disconnect_observer;
static bool active;
static int vibration_mode = 1;   // ZELDA_VIBRATION_* from the settings
static CHHapticEngine *haptic_engines[2];
static id<CHHapticPatternPlayer> haptic_players[2];
static bool haptic_running[2];
static ZeldaRumble last_rumble[2];
static unsigned haptic_generation, haptic_failures, haptic_logged, haptic_pulses;
static uint64_t haptic_ticks, haptic_retry_at;
static int haptic_test_ticks;
static NSTimer *haptic_timer;
static bool haptic_trace;

#if TARGET_OS_IOS || defined(ZELDA_TEST_PHONE_HAPTICS)
static CHHapticEngine *phone_haptic_engine;
static id<CHHapticAdvancedPatternPlayer> phone_haptic_player;
static bool phone_haptic_running;
static ZeldaRumble phone_last_rumble = {-1, -1};
static unsigned phone_generation;
static bool haptics_fail(const char *step, NSError *error);
#ifndef ZELDA_PHONE_HAPTICS_SUPPORTED
#define ZELDA_PHONE_HAPTICS_SUPPORTED() ([CHHapticEngine capabilitiesForHardware].supportsHaptics)
#endif

static void phone_haptics_close(void) {
    phone_generation++;
    [phone_haptic_engine stopWithCompletionHandler:nil];
    phone_haptic_player = nil;
    phone_haptic_engine = nil;
    phone_haptic_running = false;
    phone_last_rumble = (ZeldaRumble){-1, -1};
}

static bool phone_haptic_init_player(void) {
    if (phone_haptic_player) return true;
    if (!ZELDA_PHONE_HAPTICS_SUPPORTED()) return false;
    NSError *error = nil;
    CHHapticEngine *engine = [[CHHapticEngine alloc] initAndReturnError:&error];
    if (!engine) return haptics_fail("phone engine creation", error);
    engine.playsHapticsOnly = YES;
    if (![engine startAndReturnError:&error]) {
        [engine stopWithCompletionHandler:nil];
        return haptics_fail("phone engine start", error);
    }
    CHHapticEvent *event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
        parameters:@[[[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1],
                     [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:.5]]
        relativeTime:0 duration:1.0];
    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
    id<CHHapticAdvancedPatternPlayer> player = pattern ? [engine createAdvancedPlayerWithPattern:pattern error:&error] : nil;
    if (!player) { [engine stopWithCompletionHandler:nil]; return haptics_fail("phone player creation", error); }
    player.loopEnabled = YES;
    player.loopEnd = 1.0;
    phone_haptic_engine = engine;
    phone_haptic_player = player;
    unsigned gen = phone_generation;
    engine.resetHandler = ^{
        dispatch_async(dispatch_get_main_queue(), ^{
            if (gen == phone_generation) phone_haptics_close();
        });
    };
    engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
        (void)reason;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (gen == phone_generation) phone_haptics_close();
        });
    };
    return true;
}
#endif

static void haptics_close(void) {
    haptic_generation++;
    haptic_test_ticks = 0;
    for (unsigned i = 0; i < 2; i++) {
        // Stopping the engine also stops its players; a disconnected player's stop can throw.
        [haptic_engines[i] stopWithCompletionHandler:nil];
        haptic_players[i] = nil; haptic_engines[i] = nil; haptic_running[i] = false;
        last_rumble[i] = (ZeldaRumble){-1, -1};
    }
#if TARGET_OS_IOS || defined(ZELDA_TEST_PHONE_HAPTICS)
    phone_haptics_close();
#endif
    vibration_active(active);
}
// A failed step (typically a player whose engine the system stopped or reset between pump ticks) closes the engines,
// and output resumes after a short back-off (1, 2, 4, then 8 s) instead of staying off until the window refocuses.
static bool haptics_fail(const char *step, NSError *error) {
    unsigned delay = 60u << (haptic_failures < 3 ? haptic_failures : 3);
    haptic_failures++;
    if (haptic_logged < 8) {
        haptic_logged++;
        fprintf(stderr, "HAPTIC %s failed domain=%s code=%ld; retry in %u s\n", step,
            error.domain.UTF8String ?: "none", (long)error.code, delay / 60);
    }
    haptics_close(); haptic_retry_at = haptic_ticks + delay;
    return false;
}
static bool haptic_player(unsigned channel, GCHapticsLocality locality) {
    if (haptic_players[channel]) return true;
    CHHapticEngine *engine = [selected.haptics createEngineWithLocality:locality];
    NSError *error = nil;
    if (!engine) return haptics_fail("engine creation", nil);
    engine.playsHapticsOnly = YES;
    if (![engine startAndReturnError:&error]) return haptics_fail("engine start", error);
    CHHapticEvent *event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
        parameters:@[[[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1],
                     [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:.5]]
        relativeTime:0 duration:GCHapticDurationInfinite];
    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
    // GameController engines support an infinite continuous event without an advanced looping player.
    id<CHHapticPatternPlayer> player = pattern ? [engine createPlayerWithPattern:pattern error:&error] : nil;
    if (!player) { [engine stopWithCompletionHandler:nil]; return haptics_fail("player creation", error); }
    haptic_engines[channel] = engine; haptic_players[channel] = player;
    unsigned generation = haptic_generation;
    // The system may stop or reset an engine at any time; the next tick recreates it.
    void (^invalidate)(long) = ^(long reason) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (generation != haptic_generation) return;
            if (haptic_logged < 8) { haptic_logged++; fprintf(stderr, "HAPTIC engine stopped reason=%ld; recreating\n", reason); }
            haptics_close();
        });
    };
    engine.resetHandler = ^{ invalidate(-1); };
    engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) { invalidate((long)reason); };
    return true;
}
static void haptics_tick(void) {
    haptic_ticks++;
    if (!active) return;

    bool has_controller_haptics = (selected != nil && selected.haptics != nil);
#if TARGET_OS_IOS || defined(ZELDA_TEST_PHONE_HAPTICS)
    bool has_phone_haptics = ZELDA_PHONE_HAPTICS_SUPPORTED();
#else
    bool has_phone_haptics = false;
#endif
    if (!has_controller_haptics && !has_phone_haptics) return;

    ZeldaVibrationValue values[2]; vibration_take(values);
    InputDevice current_device = inputGetDevice();
    bool route_to_gamepad = has_controller_haptics && (current_device != INPUT_DEVICE_TOUCH || !has_phone_haptics);
    bool xbox = route_to_gamepad && [selected.extendedGamepad isKindOfClass:GCXboxGamepad.class];
    ZeldaRumble rumble[2] = {vibration_scale(vibration_mix(values[0]), vibration_mode, xbox), vibration_scale(vibration_mix(values[1]), vibration_mode, xbox)};
    bool testing = haptic_test_ticks > 0;
    if (testing) { haptic_test_ticks--; rumble[0] = rumble[1] = (ZeldaRumble){.8f, 0}; }

    if (haptic_trace) {
        static unsigned lines; static bool was_nonzero;
        bool nonzero = rumble[0].intensity > 0 || rumble[1].intensity > 0;
        if (lines < 600 && (nonzero || was_nonzero)) {
            lines++;
            fprintf(stderr,"HAPTIC_TRACE time=%.6f source=%s dev=%d target=%s mode=%d xbox=%d left=%.4f/%.0f,%.4f/%.0f right=%.4f/%.0f,%.4f/%.0f out=%.4f,%.4f\n",
                clock_gettime_nsec_np(CLOCK_UPTIME_RAW)/1e9,testing?"test":"game",(int)current_device,
                route_to_gamepad?"gamepad":"phone",vibration_mode,xbox,
                values[0].low_amplitude,values[0].low_frequency,values[0].high_amplitude,values[0].high_frequency,
                values[1].low_amplitude,values[1].low_frequency,values[1].high_amplitude,values[1].high_frequency,
                rumble[0].intensity,rumble[1].intensity);
        }
        was_nonzero=nonzero;
    }

    if (route_to_gamepad) {
#if TARGET_OS_IOS || defined(ZELDA_TEST_PHONE_HAPTICS)
        if (phone_haptic_running) {
            NSError *error = nil;
            if (![phone_haptic_player stopAtTime:CHHapticTimeImmediate error:&error]) { haptics_fail("phone player stop", error); return; }
            phone_haptic_running = false;
            phone_last_rumble = (ZeldaRumble){-1, -1};
        }
#endif
        if (haptic_ticks < haptic_retry_at) return;
        NSSet *localities = selected.haptics.supportedLocalities;
        bool split = [localities containsObject:GCHapticsLocalityLeftHandle] && [localities containsObject:GCHapticsLocalityRightHandle];
        if (!split && rumble[1].intensity > rumble[0].intensity) rumble[0] = rumble[1];
        for (unsigned i = 0; i < (split ? 2u : 1u); i++) {
            NSError *error = nil;
            if (!rumble[i].intensity) {
                if (haptic_running[i] && ![haptic_players[i] stopAtTime:CHHapticTimeImmediate error:&error]) { haptics_fail("player stop", error); return; }
                haptic_running[i] = false; last_rumble[i] = (ZeldaRumble){-1, -1};
            } else {
                GCHapticsLocality locality = split ? (i ? GCHapticsLocalityRightHandle : GCHapticsLocalityLeftHandle) : GCHapticsLocalityDefault;
                if (!haptic_player(i, locality)) return;
                if (rumble[i].intensity != last_rumble[i].intensity || rumble[i].sharpness != last_rumble[i].sharpness) {
                    NSArray *parameters = @[
                        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl value:rumble[i].intensity relativeTime:0],
                        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl value:rumble[i].sharpness relativeTime:0]];
                    if (![haptic_players[i] sendParameters:parameters atTime:CHHapticTimeImmediate error:&error]) { haptics_fail("parameter update", error); return; }
                    last_rumble[i] = rumble[i];
                }
                if (!haptic_running[i]) {
                    if (![haptic_players[i] startAtTime:CHHapticTimeImmediate error:&error]) { haptics_fail("player start", error); return; }
                    haptic_running[i] = true; haptic_failures = 0;
                    if (haptic_pulses < (haptic_trace ? 64u : 4u)) {
                        haptic_pulses++;
                        fprintf(stderr, "HAPTIC output started %s intensity=%.2f sharpness=%.2f\n", locality.UTF8String, rumble[i].intensity, rumble[i].sharpness);
                    }
                }
            }
        }
    } else {
        for (unsigned i = 0; i < 2; i++) {
            if (haptic_running[i]) {
                NSError *error = nil;
                if (![haptic_players[i] stopAtTime:CHHapticTimeImmediate error:&error]) { haptics_fail("route player stop", error); return; }
                haptic_running[i] = false;
                last_rumble[i] = (ZeldaRumble){-1, -1};
            }
        }
#if TARGET_OS_IOS || defined(ZELDA_TEST_PHONE_HAPTICS)
        if (has_phone_haptics && haptic_ticks >= haptic_retry_at) {
            float max_intensity = fmaxf(rumble[0].intensity, rumble[1].intensity);
            float target_sharpness = (rumble[0].intensity >= rumble[1].intensity) ? rumble[0].sharpness : rumble[1].sharpness;
            ZeldaRumble phone_target = {max_intensity, target_sharpness};
            NSError *error = nil;
            if (phone_target.intensity && !phone_haptic_init_player()) return;
            if (!phone_target.intensity) {
                if (phone_haptic_running) {
                    if (![phone_haptic_player stopAtTime:CHHapticTimeImmediate error:&error]) { haptics_fail("phone player stop", error); return; }
                    phone_haptic_running = false;
                    phone_last_rumble = (ZeldaRumble){-1, -1};
                }
            } else {
                if (phone_target.intensity != phone_last_rumble.intensity || phone_target.sharpness != phone_last_rumble.sharpness) {
                    NSArray *parameters = @[
                        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl value:phone_target.intensity relativeTime:0],
                        [[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticSharpnessControl value:phone_target.sharpness relativeTime:0]];
                    if (![phone_haptic_player sendParameters:parameters atTime:CHHapticTimeImmediate error:&error]) { haptics_fail("phone parameter update", error); return; }
                    phone_last_rumble = phone_target;
                }
                if (!phone_haptic_running) {
                    if (![phone_haptic_player startAtTime:CHHapticTimeImmediate error:&error]) { haptics_fail("phone player start", error); return; }
                    phone_haptic_running = true; haptic_failures = 0;
                }
            }
        }
#endif
    }
}
void gamepad_vibration_mode(int mode) {
    if ((mode != 0) != vibration_mode) vibration_active(active);
    vibration_mode = mode != 0;
    if (!vibration_mode) haptics_close(); // Stop a running test as well as game pulses.
}
bool gamepad_test_vibration(void) {
#if TARGET_OS_IOS || defined(ZELDA_TEST_PHONE_HAPTICS)
    bool has_haptics = (selected && selected.haptics) || ZELDA_PHONE_HAPTICS_SUPPORTED();
#else
    bool has_haptics = (selected && selected.haptics);
#endif
    if (!active || !vibration_mode || !has_haptics) return false;
    haptic_test_ticks = 24; haptic_retry_at = haptic_ticks;   // 0.4 s on every motor, retrying a failed engine at once
    return true;
}
void gamepad_describe(char *name, unsigned size, bool *haptics) {
    snprintf(name, size, "%s", selected ? (selected.vendorName.UTF8String ?: "Controller") : "");
    *haptics = selected.haptics != nil;
}

static void publish(GCController *controller, GCExtendedGamepad *pad) {
    if (!active || controller != selected) return;
    uint64_t buttons = dm_padmap_face(dm_padmap_kind(controller.productCategory.UTF8String, controller.vendorName.UTF8String),
        pad.buttonA.pressed, pad.buttonB.pressed, pad.buttonX.pressed, pad.buttonY.pressed);
    if (pad.leftShoulder.pressed) buttons |= DM_BTN_L;
    if (pad.rightShoulder.pressed) buttons |= DM_BTN_R;
    if (pad.leftTrigger.pressed) buttons |= DM_BTN_ZL;
    if (pad.rightTrigger.pressed) buttons |= DM_BTN_ZR;
    if (pad.leftThumbstickButton.pressed) buttons |= DM_BTN_LS;
    if (pad.rightThumbstickButton.pressed) buttons |= DM_BTN_RS;
    if (pad.buttonMenu.pressed) buttons |= DM_BTN_PLUS;
    if (pad.buttonOptions.pressed) buttons |= DM_BTN_MINUS;
    if (pad.dpad.left.pressed) buttons |= DM_BTN_LEFT;
    if (pad.dpad.right.pressed) buttons |= DM_BTN_RIGHT;
    if (pad.dpad.up.pressed) buttons |= DM_BTN_UP;
    if (pad.dpad.down.pressed) buttons |= DM_BTN_DOWN;
    input_controller(buttons, pad.leftThumbstick.xAxis.value, pad.leftThumbstick.yAxis.value,
        pad.rightThumbstick.xAxis.value, pad.rightThumbstick.yAxis.value);
}
static void select_controller(void) {
    if (selected) return;
    for (GCController *controller in GCController.controllers) if (controller != virtual_controller && controller.extendedGamepad) {
        selected = controller; controller.handlerQueue = dispatch_get_main_queue();
        haptics_close(); haptic_failures = 0; haptic_retry_at = 0;
        __weak GCController *weak_controller = controller;
        controller.extendedGamepad.valueChangedHandler = ^(GCExtendedGamepad *pad, GCControllerElement *element) {
            (void)element; GCController *current = weak_controller;
            if (current) publish(current, pad);
        };
        fprintf(stderr, "GAMEPAD connected vendor=%s category=%s; face buttons follow printed letters\n",
            controller.vendorName.UTF8String ?: "unknown", controller.productCategory.UTF8String ?: "unknown");
        fprintf(stderr, "HAPTIC controller capability=%s\n", controller.haptics ? "available" : "unavailable");
        publish(controller, controller.extendedGamepad); break;
    }
}
void gamepad_set_virtual_controller(void *controller) {
    virtual_controller = (__bridge GCController *)controller;
    if (selected && selected == virtual_controller) {
        selected.extendedGamepad.valueChangedHandler = nil; selected = nil;
        input_controller_clear(); haptics_close(); select_controller();
    }
}
void gamepad_active(bool value) {
    active = value;
    haptics_close(); haptic_failures = 0; haptic_retry_at = 0;
    if (!value) input_clear();
    else if (selected) publish(selected, selected.extendedGamepad);
}
void gamepad_start(void) {
    const char *trace = getenv("ZELDA_HAPTIC_TRACE"); haptic_trace = trace && atoi(trace);
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    connect_observer = [center addObserverForName:GCControllerDidConnectNotification object:nil queue:NSOperationQueue.mainQueue
        usingBlock:^(NSNotification *notification) { (void)notification; select_controller(); }];
    disconnect_observer = [center addObserverForName:GCControllerDidDisconnectNotification object:nil queue:NSOperationQueue.mainQueue
        usingBlock:^(NSNotification *notification) {
            if (notification.object == selected) {
                selected.extendedGamepad.valueChangedHandler = nil; selected = nil;
                haptics_close();
                input_controller_clear(); fprintf(stderr, "GAMEPAD disconnected\n"); select_controller();
            }
        }];
    active = true; select_controller();
    vibration_active(active);
    haptic_timer = [NSTimer timerWithTimeInterval:1.0 / 60 repeats:YES block:^(NSTimer *timer) { (void)timer; haptics_tick(); }];
    [NSRunLoop.mainRunLoop addTimer:haptic_timer forMode:NSRunLoopCommonModes];
    if (!selected) fprintf(stderr, "GAMEPAD waiting for a connected extended controller\n");
}
void gamepad_stop(void) {
    [haptic_timer invalidate]; haptic_timer = nil;
    selected.extendedGamepad.valueChangedHandler = nil; selected = nil;
    if (connect_observer) [NSNotificationCenter.defaultCenter removeObserver:connect_observer];
    if (disconnect_observer) [NSNotificationCenter.defaultCenter removeObserver:disconnect_observer];
    connect_observer = disconnect_observer = nil; gamepad_active(false);
}
