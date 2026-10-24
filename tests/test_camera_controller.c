#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#define UNIT_TEST 1
static double testTime = 1;
static bool movie;
double camera_test_time(void) { return testTime; }
bool zelda_is_movie(void) { return movie; }
#include "camera_controller.inc"

static unsigned char component[0xd00];
static float orbit[3], pitchState;
static void tick(void) { cameraApplyPlayerOrbit(NULL, NULL, component); cameraApplyPlayerOrbit(orbit, &pitchState, component); }
static void setup(void) {
    cameraInit(); movie = false; testTime += 1;
    memset(component, 0, sizeof component);
    uintptr_t target = 0x1234;
    memcpy(component + 0xc90, &target, sizeof target);
    orbit[0] = 4.8f; orbit[1] = pitchState = 15; orbit[2] = 30;
    component[0x96c] = 1;
    tick(); cameraSetMouseCaptured(true);
}
static void unchanged(float distance, float pitch, float yaw) {
    assert(orbit[0] == distance && orbit[1] == pitch && orbit[2] == yaw);
}
static void testNativeOrbitAndConsumption(void) {
    setup();
    cameraAddMouseDelta(50, 20); tick();
    assert(orbit[0] == 4.8f && orbit[2] < 30 && orbit[1] > 15);
    assert(fabsf(pitchState - orbit[1]) < 0.0001f);
    float pitch = orbit[1], yaw = orbit[2];
    for (int n = 0; n < 10; n++) { tick(); unchanged(4.8f, pitch, yaw); }
    int32_t x = 100, y = 100;
    assert(!cameraPoll(&x, &y) && x == 0 && y == 0);
    // The engine may choose a shorter collision arm or move its target: input
    // cannot put back a cached Eye, target, or radius from a previous frame.
    orbit[0] = 0.05f; cameraAddMouseDelta(80, 0); tick();
    assert(orbit[0] == 0.05f);
}
static void testLinearAndTouchInput(void) {
    setup(); float yaw = orbit[2];
    cameraAddMouseDelta(10, 0); tick(); float first = orbit[2] - yaw;
    yaw = orbit[2]; cameraAddMouseDelta(50, 0); tick();
    assert(fabsf((orbit[2] - yaw) - 5 * first) < 0.0001f);
    cameraSetMouseCaptured(false); yaw = orbit[2];
    cameraAddTouchDelta(10, 0); tick();
    assert(fabsf(orbit[2] - yaw - first) < 0.0001f);
}
static void testTitleAndMovieGates(void) {
    setup(); cameraSetGameplayActive(false);
    cameraAddMouseDelta(100, 100); cameraAddTouchDelta(100, 100);
    tick(); unchanged(4.8f, 15, 30);
    cameraAddMouseDelta(100, 100); movie = true; tick();
    unchanged(4.8f, 15, 30); assert(!cameraIsGameplayActive());
    cameraAddTouchDelta(100, 100); movie = false; tick();
    unchanged(4.8f, 15, 30);
    // No target (title/loading) or native PlayerCamera disable flags.
    for (int mode = 0; mode < 3; mode++) {
        setup(); cameraAddMouseDelta(100, 100);
        if (mode == 0) memset(component + 0xc90, 0, sizeof(uintptr_t));
        else component[0x34] = (unsigned char)mode;
        tick(); unchanged(4.8f, 15, 30); assert(!cameraIsGameplayActive());
    }
}
static void testTransitionsDropQueuedInput(void) {
    setup(); cameraAddMouseDelta(100, 100); testTime += 0.2;
    cameraFrameSync(); assert(!cameraIsGameplayActive()); tick();
    unchanged(4.8f, 15, 30);
    setup(); cameraAddMouseDelta(100, 100);
    uintptr_t target = 0x5678; memcpy(component + 0xc90, &target, sizeof target);
    tick(); unchanged(4.8f, 15, 30);
    setup(); cameraAddMouseDelta(100, 100); cameraSetMouseCaptured(false);
    tick(); unchanged(4.8f, 15, 30);
    setup(); cameraAddTouchDelta(100, 100); cameraNoteGamepadActivity();
    tick(); unchanged(4.8f, 15, 30);
}
static void testPitchAndInvalidValues(void) {
    setup(); cameraAddMouseDelta(0, 5000); tick();
    assert(fabs(orbit[1] - MAX_PITCH_LIMIT * 180 / M_PI) < 0.0001f);
    assert(isfinite(pitchState));
    cameraAddMouseDelta(INFINITY, 0); cameraAddTouchDelta(NAN, 10);
    float pitch = orbit[1], yaw = orbit[2]; tick(); unchanged(4.8f, pitch, yaw);
    CameraConfig valid = cameraGetConfig(), invalid = valid;
    invalid.mouseSensitivityX = NAN; cameraSetConfig(&invalid);
    assert(cameraGetConfig().mouseSensitivityX == valid.mouseSensitivityX);
}
static void testLockKeepsNativeCamera(void) {
    setup(); cameraHandleLockDragButton(true); cameraHandleLockDragButton(false);
    assert(cameraIsTargetLocked()); cameraAddMouseDelta(50, 50); tick();
    unchanged(4.8f, 15, 30);
    cameraHandleLockDragButton(true); cameraAddMouseDelta(35, 0);
    int32_t x, y; assert(cameraPoll(&x, &y) && x > 0 && y == 0);
    tick(); unchanged(4.8f, 15, 30);
    movie = true; assert(cameraGetTargetLockButtons() == 0);
    assert(!cameraIsTargetLocked()); movie = false;
}
static void testNativeModesKeepLockAndDropMotion(void) {
    setup(); cameraToggleTargetLock();
    for (int frame = 0; frame < 20; frame++) {
        testTime += 0.03;
        cameraApplyPlayerOrbit(NULL, NULL, component);
        assert(cameraGetTargetLockButtons() == ZL_BUTTON_MASK);
    }
    cameraToggleTargetLock();
    cameraAddMouseDelta(100, 100);
    int32_t x, y; assert(cameraPoll(&x, &y) && x > 0 && y < 0);
    tick(); unchanged(4.8f, 15, 30); // other modes use the native right stick
}
static void testNativeTargetLossAndEmptyHold(void) {
    setup(); component[0x96c] = 0; testTime += .11; tick();
    cameraHandleLockDragButton(true);
    assert(!cameraIsTargetLocked() && cameraGetTargetLockButtons() == ZL_BUTTON_MASK);
    cameraHandleLockDragButton(false);
    assert(!cameraGetTargetLockButtons());
    cameraHandleLockDragButton(true); component[0x96c] = 1; tick();
    cameraHandleLockDragButton(false); assert(cameraIsTargetLocked());
    component[0x96c] = 0; testTime += .11; tick();
    assert(!cameraIsTargetLocked() && !cameraGetTargetLockButtons());
}
static void testEquipmentAndMenuRouting(void) {
    setup(); cameraSetRightStickMode(true); cameraAddMouseDelta(20, 5);
    int32_t x, y; assert(cameraPoll(&x, &y) && x > 0 && y < 0);
    tick(); unchanged(4.8f, 15, 30);
    assert(!cameraPoll(&x, &y));
    cameraSetRightStickMode(false); cameraAddTouchDelta(20, 0); tick();
    assert(orbit[2] < 30);
    cameraSetGameplayActive(false); assert(cameraIsMouseCaptured());
    cameraAddMouseDelta(-20, 5); assert(cameraPoll(&x, &y) && x < 0 && y < 0);
    cameraAddTouchDelta(20, -5); assert(cameraPoll(&x, &y) && x > 0 && y > 0);
    cameraAddMouseDelta(20, 0); cameraReset(); assert(!cameraPoll(&x, &y));
}
static float ultraMinimum = 10, ultraMaximum = 170;
static unsigned ultraParameterCalls;
static void *ultraType(void) { return (void *)0x123; }
static void *ultraParameter(void *container, void *type) {
    (void)container; assert(type == (void *)0x123); ultraParameterCalls++; return &ultraMinimum;
}
static const float *ultraLow(void *parameter) { (void)parameter; return &ultraMinimum; }
static const float *ultraHigh(void *parameter) { (void)parameter; return &ultraMaximum; }
static void testUltrahandOrbit(void) {
    setup();
    _Alignas(16) unsigned char execution[0x380] = {0};
    void *vtable[] = {(void *)ultraParameter};
    void **container = vtable;
    void *parameterContainer = &container;
    memcpy(execution + 0x28, &parameterContainer, sizeof parameterContainer);
    cameraTargetApi.targetType = ultraType;
    cameraTargetApi.minimum = ultraLow; cameraTargetApi.maximum = ultraHigh;
    float *pitch = (float *)(execution + 0xa4), *yaw = (float *)(execution + 0xa8);
    float *yawOffset = (float *)(execution + 0xbc);
    float *yawBaseline = (float *)(execution + 0xb8);
    *yawBaseline = 12;
    *pitch = orbit[1]; *yaw = orbit[2]; *yawOffset = orbit[2] - *yawBaseline;
    cameraAddMouseDelta(100, 100); // mode entry discards old Chase input
    cameraApplyUltrahandOrbit(orbit, execution, component); unchanged(4.8f,15,30);
    assert(cameraIsUltrahandActive());
    cameraAddMouseDelta(20, 10);
    cameraApplyUltrahandOrbit(orbit, execution, component);
    assert(orbit[0] == 4.8f && orbit[1] > 15 && orbit[2] < 30);
    assert(fabsf(*pitch - orbit[1]) < .0001f && fabsf(*yaw - orbit[2]) < .0001f);
    assert(fabsf(*yawOffset + *yawBaseline - orbit[2]) < .0001f);
    float savedPitch = orbit[1], savedYaw = orbit[2];
    cameraApplyUltrahandOrbit(orbit, execution, component); unchanged(4.8f,savedPitch,savedYaw);
    // Reproduce native UltraHand auto-framing pulling the local angles back
    // every frame, including idle frames. Swipes must still accumulate.
    for (int frame = 0; frame < 30; frame++) {
        testTime += .016;
        cameraApplyPlayerOrbit(NULL, NULL, component);
        orbit[0] = frame & 1 ? 2.0f : 4.8f;
        orbit[1] = 15; orbit[2] = 30;
        cameraAddTouchDelta(10, 0);
        cameraApplyUltrahandOrbit(orbit, execution, component);
        savedYaw = (float)remainder(savedYaw - 10 * DEFAULT_SENSITIVITY_X * ANGLE_RADIAN_SCALE * 180 / M_PI, 360);
        assert(fabsf(orbit[2] - savedYaw) < .0001f);
        assert(fabsf(remainderf(*yawBaseline + *yawOffset - orbit[2], 360)) < .0001f);
        assert(orbit[1] == savedPitch && orbit[0] == (frame & 1 ? 2.0f : 4.8f));
    }
    for (int frame = 0; frame < 10; frame++) {
        orbit[1] = 15; orbit[2] = 30;
        cameraApplyUltrahandOrbit(orbit, execution, component);
        unchanged(2, savedPitch, savedYaw);
    }
    // Real controller activity must immediately return angle authority.
    cameraNoteGamepadActivity(); orbit[1] = 40; orbit[2] = 60;
    cameraApplyUltrahandOrbit(orbit, execution, component); unchanged(2,40,60);
    cameraAddMouseDelta(10, 0);
    cameraApplyUltrahandOrbit(orbit, execution, component);
    assert(orbit[2] < 60 && orbit[2] > 50 && orbit[1] == 40);
    savedYaw = orbit[2]; orbit[0] = 4.8f;
    ultraMaximum = 50; cameraAddTouchDelta(0, 5000);
    cameraApplyUltrahandOrbit(orbit, execution, component);
    assert(orbit[1] == 50 && *pitch == 50); ultraMaximum = 170;
    CameraConfig config = cameraGetConfig(); config.nativeUltrahand = false; cameraSetConfig(&config);
    unsigned calls = ultraParameterCalls;
    cameraAddTouchDelta(25, 0); int32_t x, y;
    assert(cameraPoll(&x, &y) && x > 0 && y == 0);
    cameraApplyUltrahandOrbit(orbit, execution, component); unchanged(4.8f,50,savedYaw);
    assert(ultraParameterCalls == calls); // fallback never invokes the new guest parameter API
    config.nativeUltrahand = true; cameraSetConfig(&config);
    cameraSetRightStickMode(true); cameraAddMouseDelta(25, 0);
    assert(cameraPoll(&x, &y) && x > 0);
    cameraApplyUltrahandOrbit(orbit, execution, component); unchanged(4.8f,50,savedYaw);
    cameraSetRightStickMode(false);
    ultraMinimum = NAN; cameraApplyUltrahandOrbit(orbit, execution, component);
    cameraAddMouseDelta(25, 0); assert(cameraPoll(&x, &y) && x > 0); ultraMinimum = 10;
    cameraAddTouchDelta(10, 10); tick(); unchanged(4.8f,50,savedYaw); // return to Chase drops input
    assert(!cameraIsUltrahandActive());
}
static void testSelectionOrbit(void) {
    setup();
    _Alignas(16) unsigned char execution[0x200] = {0};
    float *pitch = (float *)(execution + 0x50), *yaw = (float *)(execution + 0x54);
    float *desiredPitch = (float *)(execution + 0x160), *desiredYaw = (float *)(execution + 0x164);
    float *minimum = (float *)(execution + 0x1ac), *maximum = (float *)(execution + 0x1b0);
    *pitch = *desiredPitch = orbit[1]; *yaw = *desiredYaw = orbit[2];
    *minimum = -60; *maximum = 70;
    cameraAddMouseDelta(100, 100);
    cameraApplySelectionOrbit(orbit, execution, component); unchanged(4.8f,15,30);
    assert(!cameraIsUltrahandActive()); // phone rotation button is for a held object only
    for (int frame = 0; frame < 40; frame++) {
        testTime += .016;
        cameraApplyPlayerOrbit(NULL, NULL, component);
        // SpecialPower may reframe toward its aiming/gyro state every update.
        orbit[0] = 2 + frame * .01f; orbit[1] = 15; orbit[2] = 30;
        cameraAddTouchDelta(10, 2);
        cameraApplySelectionOrbit(orbit, execution, component);
        float expectedYaw = remainderf(30 - (frame + 1) * 10 * DEFAULT_SENSITIVITY_X * ANGLE_RADIAN_SCALE * 180 / M_PI, 360);
        assert(fabsf(remainderf(orbit[2] - expectedYaw, 360)) < .0002f);
        assert(orbit[0] == 2 + frame * .01f && orbit[1] > 15);
        assert(*pitch == orbit[1] && *desiredPitch == orbit[1]);
        assert(*yaw == orbit[2] && *desiredYaw == orbit[2]);
    }
    float savedPitch = orbit[1], savedYaw = orbit[2], distance = orbit[0];
    orbit[1] = 15; orbit[2] = 30;
    cameraApplySelectionOrbit(orbit, execution, component); unchanged(distance,savedPitch,savedYaw);
    cameraAddTouchDelta(0, 5000); cameraApplySelectionOrbit(orbit, execution, component);
    assert(orbit[1] == 70 && *desiredPitch == 70);
    cameraAddTouchDelta(0, -5000); cameraApplySelectionOrbit(orbit, execution, component);
    assert(orbit[1] == -60 && *desiredPitch == -60);
    // A native fixed-pitch range still permits horizontal aiming.
    *minimum = *maximum = 20; cameraAddMouseDelta(10, 0);
    cameraApplySelectionOrbit(orbit, execution, component);
    assert(orbit[1] == 20 && orbit[2] < savedYaw);
    *minimum = -60; *maximum = 70;
    // Selection's callback must never write the held-object camera's fields.
    assert(*(float *)(execution + 0xa4) == 0 && *(float *)(execution + 0xa8) == 0);
    cameraSetRightStickMode(true); orbit[1] = 20; orbit[2] = 40;
    cameraAddMouseDelta(20, 0); int32_t x, y;
    assert(cameraPoll(&x, &y) && x > 0);
    cameraApplySelectionOrbit(orbit, execution, component); unchanged(distance,20,40);
    cameraSetRightStickMode(false);
    cameraApplySelectionOrbit(orbit, execution, component);
    cameraAddMouseDelta(10, 0); cameraApplySelectionOrbit(orbit, execution, component);
    assert(orbit[2] < 40);
    cameraNoteGamepadActivity(); orbit[1] = 25; orbit[2] = 55;
    cameraApplySelectionOrbit(orbit, execution, component); unchanged(distance,25,55);
    CameraConfig config = cameraGetConfig(); config.nativeUltrahand = false; cameraSetConfig(&config);
    cameraAddTouchDelta(20, 0); assert(cameraPoll(&x, &y) && x > 0);
    cameraApplySelectionOrbit(orbit, execution, component); unchanged(distance,25,55);
    config.nativeUltrahand = true; cameraSetConfig(&config);
    // Invalid limits, movies, instance/target changes and mode exits drop input.
    *minimum = NAN; cameraApplySelectionOrbit(orbit, execution, component);
    cameraAddTouchDelta(20, 0); assert(cameraPoll(&x, &y) && x > 0); *minimum = -60;
    cameraApplySelectionOrbit(orbit, execution, component);
    cameraAddMouseDelta(10, 0); movie = true;
    cameraApplySelectionOrbit(orbit, execution, component); unchanged(distance,25,55); movie = false;
    cameraApplySelectionOrbit(orbit, execution, component);
    cameraAddMouseDelta(10, 0);
    _Alignas(16) unsigned char replacement[sizeof execution]; memcpy(replacement,execution,sizeof execution);
    cameraApplySelectionOrbit(orbit, replacement, component); unchanged(distance,25,55);
    cameraAddMouseDelta(10, 0); tick(); unchanged(distance,25,55);
    assert(!globalCameraState.selectionAnglesOwned);
}
static void testUltrahandDetach(void) {
    setup(); assert(!cameraTriggerUltrahandDetach()); // no shake on title/Chase/selection
    _Alignas(16) unsigned char execution[0x380] = {0};
    void *vtable[] = {(void *)ultraParameter}; void **container = vtable;
    void *parameterContainer = &container;
    memcpy(execution + 0x28, &parameterContainer, sizeof parameterContainer);
    cameraTargetApi.targetType = ultraType;
    cameraTargetApi.minimum = ultraLow; cameraTargetApi.maximum = ultraHigh;
    *(float *)(execution + 0xa4) = 15; *(float *)(execution + 0xa8) = 30;
    cameraApplyUltrahandOrbit(orbit, execution, component);
    assert(cameraTriggerUltrahandDetach());
    assert(!cameraTriggerUltrahandDetach()); // repeated clicks cannot extend a gesture
    int32_t x, y;
    for (int phase = 1; phase <= DETACH_SWING_COUNT + 1; phase++) {
        for (int read = 0; read < 20; read++) {
            assert(cameraPoll(&x, &y));
            assert(x == (phase > DETACH_SWING_COUNT ? 0 : phase & 1 ? 32767 : -32767));
            assert(y == 0); // read frequency cannot change phase duration
        }
        testTime += .101;
        orbit[1] = 18; orbit[2] = 35; // native stick camera would otherwise sway
        cameraApplyUltrahandOrbit(orbit, execution, component);
        unchanged(4.8f,15,30);
    }
    assert(cameraPoll(&x, &y) && x == 0 && y == 0);
    assert(!cameraPoll(&x, &y));
    CameraConfig config = cameraGetConfig(); config.nativeUltrahand = false; cameraSetConfig(&config);
    assert(cameraTriggerUltrahandDetach());
    for (int phase = 1; phase <= 3; phase++) {
        assert(cameraPoll(&x, &y) && x == (phase & 1 ? 32767 : -32767) && y == 0);
        testTime += .101;
        cameraApplyUltrahandOrbit(orbit, execution, component);
    }
    config.nativeUltrahand = true; cameraSetConfig(&config);
    assert(!cameraPoll(&x, &y)); // changing the control setting cancels the gesture
    cameraSetRightStickMode(true); assert(!cameraTriggerUltrahandDetach());
    cameraSetRightStickMode(false);
    assert(cameraTriggerUltrahandDetach()); cameraNoteGamepadActivity();
    assert(!cameraPoll(&x, &y) && !x && !y);
    assert(cameraTriggerUltrahandDetach()); cameraSetMouseCaptured(false);
    assert(!cameraPoll(&x, &y) && !x && !y);
    assert(cameraTriggerUltrahandDetach()); movie = true;
    assert(!cameraPoll(&x, &y) && !x && !y); movie = false;
    cameraApplyUltrahandOrbit(orbit, execution, component);
    assert(cameraTriggerUltrahandDetach()); tick(); // leaving the held-object execution
    assert(!cameraPoll(&x, &y) && !x && !y);
    cameraApplyUltrahandOrbit(orbit, execution, component);
    assert(cameraTriggerUltrahandDetach()); testTime += .2;
    assert(!cameraPoll(&x, &y) && !x && !y); // no stale pulses in menus
}
static void testBoundBridge(void) {
    ZeldaCameraSharedState shared = {.magic = "CAM4", .version = 4};
    cameraBindSharedState(&shared); assert(shared.callback != 0 && shared.ultrahandCallback != 0 && shared.selectionCallback != 0);
    setup(); cameraAddMouseDelta(50, 20);
    ((void (*)(float *, float *, const void *))shared.callback)(orbit, &pitchState, component);
    assert(orbit[0] == 4.8f && orbit[2] < 30 && orbit[1] > 15);
    ZeldaCameraSharedState old = {.magic = "CAM3", .version = 3};
    cameraBindSharedState(&old); assert(old.callback == 0);
}
int main(void) {
    testNativeOrbitAndConsumption(); testLinearAndTouchInput();
    testTitleAndMovieGates(); testTransitionsDropQueuedInput();
    testPitchAndInvalidValues(); testLockKeepsNativeCamera();
    testNativeModesKeepLockAndDropMotion(); testNativeTargetLossAndEmptyHold();
    testEquipmentAndMenuRouting(); testUltrahandOrbit(); testSelectionOrbit(); testUltrahandDetach(); testBoundBridge();
    puts("PASS: native player orbit, unchanged arm, mouse/touch, lifecycle, transitions, lock, UltraHand selection/held states, CAM4 bridge");
}
