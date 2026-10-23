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
static void tick(void) { cameraApplyPlayerOrbit(orbit, &pitchState, component); }
static void setup(void) {
    cameraInit(); movie = false; testTime += 1;
    memset(component, 0, sizeof component);
    uintptr_t target = 0x1234;
    memcpy(component + 0xc90, &target, sizeof target);
    orbit[0] = 4.8f; orbit[1] = pitchState = 15; orbit[2] = 30;
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
    tick(); unchanged(4.8f, 15, 30); // no motion accumulated through another mode
}
static void testBoundBridge(void) {
    ZeldaCameraSharedState shared = {.magic = "CAM2", .version = 2};
    cameraBindSharedState(&shared); assert(shared.callback != 0);
    setup(); cameraAddMouseDelta(50, 20);
    ((void (*)(float *, float *, const void *))shared.callback)(orbit, &pitchState, component);
    assert(orbit[0] == 4.8f && orbit[2] < 30 && orbit[1] > 15);
    ZeldaCameraSharedState old = {.magic = "CAM1", .version = 1};
    cameraBindSharedState(&old); assert(old.callback == 0);
}
int main(void) {
    testNativeOrbitAndConsumption(); testLinearAndTouchInput();
    testTitleAndMovieGates(); testTransitionsDropQueuedInput();
    testPitchAndInvalidValues(); testLockKeepsNativeCamera();
    testNativeModesKeepLockAndDropMotion(); testBoundBridge();
    puts("PASS: native player orbit, unchanged arm, mouse/touch, lifecycle, transitions, lock, CAM2 bridge");
}
