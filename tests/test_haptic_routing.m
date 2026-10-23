// Run the phone output path with fake hardware on a Mac.
#define ZELDA_TEST_PHONE_HAPTICS 1
#define ZELDA_PHONE_HAPTICS_SUPPORTED() true
#include "../host/gamepad.m"
#include <assert.h>

static InputDevice device = INPUT_DEVICE_TOUCH;
void input_clear(void) {}
void input_controller(uint64_t b,float x,float y,float rx,float ry) {(void)b;(void)x;(void)y;(void)rx;(void)ry;}
void input_controller_clear(void) {}
InputDevice inputGetDevice(void) { return device; }

@interface FakePlayer : NSObject
@property unsigned starts, stops, updates;
@property BOOL fail;
@property float intensity;
@end
@implementation FakePlayer
- (BOOL)result:(NSError **)error {
    assert(error);
    if (self.fail) *error = [NSError errorWithDomain:CoreHapticsErrorDomain code:CHHapticErrorCodeEngineNotRunning userInfo:nil];
    return !self.fail;
}
- (BOOL)startAtTime:(NSTimeInterval)t error:(NSError **)e { (void)t; self.starts++; return [self result:e]; }
- (BOOL)stopAtTime:(NSTimeInterval)t error:(NSError **)e { (void)t; self.stops++; return [self result:e]; }
- (BOOL)sendParameters:(NSArray<CHHapticDynamicParameter *> *)p atTime:(NSTimeInterval)t error:(NSError **)e {
    (void)t; self.updates++; self.intensity=p.firstObject.value; return [self result:e];
}
@end
@interface FakeEngine : NSObject
@property unsigned stops;
@end
@implementation FakeEngine
- (void)stopWithCompletionHandler:(CHHapticCompletionHandler)c { self.stops++; if(c)c(nil); }
@end
@interface FakeHaptics : NSObject
@property NSSet *supportedLocalities;
@end
@implementation FakeHaptics @end
@interface FakeXbox : NSObject @end
@implementation FakeXbox
- (BOOL)isKindOfClass:(Class)c { return c==GCXboxGamepad.class || [super isKindOfClass:c]; }
@end
@interface FakeController : NSObject
@property FakeHaptics *haptics;
@property FakeXbox *extendedGamepad;
@end
@implementation FakeController @end

int main(void) { @autoreleasepool {
    FakeController *controller=[FakeController new]; controller.haptics=[FakeHaptics new];
    controller.extendedGamepad=[FakeXbox new];
    controller.haptics.supportedLocalities=[NSSet setWithObject:GCHapticsLocalityDefault];
    selected=(GCController *)(id)controller; active=true; vibration_active(true); vibration_permit(true);
    uint32_t handles[]={3,0x10003};
    ZeldaVibrationValue values[]={{.1f,160,0,320},{.05f,160,0,320}}, silent[]={{0,160,0,320},{0,160,0,320}};
    // Idle touch input must not construct a phone engine.
    haptics_tick(); assert(!phone_haptic_engine && !phone_haptic_player);
    FakePlayer *phone=[FakePlayer new], *pad=[FakePlayer new];
    FakeEngine *phoneEngine=[FakeEngine new], *padEngine=[FakeEngine new];
    phone_haptic_engine=(id)phoneEngine; phone_haptic_player=(id)phone;
    haptic_engines[0]=(id)padEngine; haptic_players[0]=(id)pad;
    assert(vibration_send(handles,values,2,false)); haptics_tick();
    assert(phone.starts==1 && pad.starts==0 && fabsf(phone.intensity-.1f)<1e-6f);
    // Switching to a controller stops the phone and applies Xbox gain there.
    device=INPUT_DEVICE_GAMEPAD; haptics_tick();
    assert(phone.stops==1 && pad.starts==1 && pad.intensity>.9f);
    device=INPUT_DEVICE_TOUCH; haptics_tick();
    assert(pad.stops==1 && phone.starts==2);
    assert(vibration_send(handles,silent,2,false)); haptics_tick();
    assert(phone.stops==2 && !phone_haptic_running);
    // Failure is observed with a non-null error pointer, then both sinks close.
    assert(vibration_send(handles,values,2,false)); haptics_tick();
    phone.fail=YES; unsigned generation=phone_generation;
    assert(vibration_send(handles,silent,2,false)); haptics_tick();
    assert(phone_generation>generation && !phone_haptic_player && !haptic_players[0]);
    assert(haptic_retry_at>haptic_ticks && haptic_failures==1);
    assert(vibration_send(handles,values,2,false)); haptics_tick();
    assert(!phone_haptic_engine); // Backoff prevents a failing 60 Hz retry loop.
    selected=nil; gamepad_active(false);
    assert(!gamepad_test_vibration() && !phone_haptic_running);
    gamepad_active(true); gamepad_vibration_mode(0); haptics_tick();
    assert(!phone_haptic_engine && !gamepad_test_vibration());
    ZeldaVibrationValue output[2]; gamepad_vibration_mode(1); vibration_take(output);
    assert(!output[0].low_amplitude && !output[1].low_amplitude);
    assert(gamepad_test_vibration()); gamepad_active(false);
    assert(!haptic_test_ticks && !phone_haptic_running);
    puts("Phone/controller haptics: routing, gain isolation, silence, failures, backoff and lifecycle passed");
} }
