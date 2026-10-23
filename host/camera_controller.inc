#include "camera_controller.h"
#include <pthread.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <stddef.h>

#define DEFAULT_SENSITIVITY_X 3.2f
#define DEFAULT_SENSITIVITY_Y 2.8f
#define DEFAULT_ACCEL_FACTOR 0.035f
#define ZL_BUTTON_MASK (1u << 8)
#define SWITCH_PULSE_DURATION 3
#define SWITCH_DRAG_THRESHOLD 28.0f
#define ANGLE_RADIAN_SCALE 0.0022f
#define MAX_PITCH_LIMIT 1.45f
#define PLAYER_CAMERA_TIMEOUT 0.15

typedef struct {
    bool mouseCaptured, isTargetLocked, isLockDragKeyDown, lockHasSwitchedInPress, lockWasActiveInPress;
    int switchPulseTicks, switchDirection;
    float lockDragAccumX;
    bool isGameplayActive;
    bool rightStickOnly, nativeTarget;
    double stickTick, targetTick;
    float stickX, stickY;
    double lastPlayerTick, lastOrbitTick, lastUltrahandTick;
    unsigned orbitMode;
    bool ultrahandAnglesOwned;
    const void *ultrahandExecution;
    float ultrahandPitch, ultrahandYaw;
    const void *playerCamera;
    uintptr_t playerTarget;
    double pendingYaw, pendingPitch;
    float currentYaw, currentPitch, targetDistance;
    CameraConfig config;
} CameraState;

static pthread_mutex_t cameraLock = PTHREAD_MUTEX_INITIALIZER;
static CameraState globalCameraState = {
    .config = {DEFAULT_SENSITIVITY_X, DEFAULT_SENSITIVITY_Y,
               DEFAULT_ACCEL_FACTOR, 0, false, true, true}
};

// CAM3 keeps Chase and the strictly scoped UltraHand callback independent.
// The guest invokes this immutable callback synchronously on its camera thread.
typedef struct {
    char magic[8];
    uint32_t version, reserved;
    uintptr_t callback, ultrahandCallback;
} ZeldaCameraSharedState;
_Static_assert(offsetof(ZeldaCameraSharedState, callback) == 0x10, "camera callback ABI");
_Static_assert(sizeof(ZeldaCameraSharedState) == 0x20, "camera bridge ABI");
static struct {
    void *(*targetType)(void);
    const float *(*minimum)(void *), *(*maximum)(void *);
} cameraTargetApi;

static double cameraNow(void) {
#ifdef UNIT_TEST
    extern double camera_test_time(void);
    return camera_test_time();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
#endif
}

// Bounded session diagnostics: distinguish missing guest hooks from dropped
// host input without enabling a per-event trace or retaining guest pointers.
static struct {
    unsigned long input, nativeInput, stickInput, chase, lifecycle, ultra, applied, resets;
    unsigned long lifecycleDrops, chaseDrops, ultraDrops, chaseReleases;
    unsigned long reportedInput;
    double reportTick;
    unsigned reject;
    float minimum, maximum, beforePitch, beforeYaw, afterPitch, afterYaw;
} cameraDiagnostics;

static void cameraReportRoutingLocked(void) {
#ifndef UNIT_TEST
    double now = cameraNow();
    if (cameraDiagnostics.input == cameraDiagnostics.reportedInput || now - cameraDiagnostics.reportTick < 1) return;
    cameraDiagnostics.reportTick = now;
    cameraDiagnostics.reportedInput = cameraDiagnostics.input;
    fprintf(stderr, "[CAMERA_ROUTE] input=%lu native=%lu stick=%lu callbacks=(%lu,%lu,%lu) ultra_apply=%lu resets=%lu drops=(%lu,%lu,%lu) chase_release=%lu reject=%#x mode=%u direct=%d stick_only=%d captured=%d owned=%d ages=(%.3f,%.3f) pending=(%.5f,%.5f) limits=(%.2f,%.2f) ultra_angles=(%.2f,%.2f)->(%.2f,%.2f)\n",
        cameraDiagnostics.input, cameraDiagnostics.nativeInput, cameraDiagnostics.stickInput,
        cameraDiagnostics.lifecycle, cameraDiagnostics.chase, cameraDiagnostics.ultra,
        cameraDiagnostics.applied, cameraDiagnostics.resets, cameraDiagnostics.lifecycleDrops,
        cameraDiagnostics.chaseDrops, cameraDiagnostics.ultraDrops, cameraDiagnostics.chaseReleases,
        cameraDiagnostics.reject,
        globalCameraState.orbitMode, globalCameraState.config.nativeUltrahand,
        globalCameraState.rightStickOnly, globalCameraState.mouseCaptured,
        globalCameraState.ultrahandAnglesOwned, now - globalCameraState.lastOrbitTick,
        now - globalCameraState.lastUltrahandTick, globalCameraState.pendingYaw,
        globalCameraState.pendingPitch, cameraDiagnostics.minimum, cameraDiagnostics.maximum,
        cameraDiagnostics.beforePitch, cameraDiagnostics.beforeYaw,
        cameraDiagnostics.afterPitch, cameraDiagnostics.afterYaw);
#endif
}

static void cameraDiscardInputLocked(void) {
    globalCameraState.ultrahandAnglesOwned = false;
    globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
    globalCameraState.isTargetLocked = false;
    globalCameraState.switchPulseTicks = 0;
    globalCameraState.isLockDragKeyDown = false;
    globalCameraState.lockDragAccumX = 0;
    globalCameraState.lockHasSwitchedInPress = false;
    globalCameraState.lockWasActiveInPress = false;
    globalCameraState.nativeTarget = false;
}

static bool cameraGameplayActiveLocked(void);
static bool cameraNativeOrbitLocked(void) {
    return !globalCameraState.rightStickOnly && cameraGameplayActiveLocked() &&
        (globalCameraState.orbitMode != 2 || globalCameraState.config.nativeUltrahand) &&
        globalCameraState.lastOrbitTick > 0 &&
        cameraNow() - globalCameraState.lastOrbitTick <= .075 &&
        globalCameraState.lastPlayerTick - globalCameraState.lastOrbitTick <= .050;
}

static bool cameraGameplayActiveLocked(void) {
    extern bool zelda_is_movie(void);
    bool active = globalCameraState.isGameplayActive && !zelda_is_movie() &&
        cameraNow() - globalCameraState.lastPlayerTick <= PLAYER_CAMERA_TIMEOUT;
    if (!active) cameraDiscardInputLocked();
    return active;
}

// v1.4.3 ExecutePlayerCameraChase, BEFORE its requested pose reaches collision.
// orbit = {native distance, elevation degrees, azimuth degrees}. This is NOT a
// view matrix and none of these coordinates are copied across host frames.
static void cameraApplyPlayerOrbit(float *orbit, float *pitchState, const void *component) {
    if (!component || (!orbit != !pitchState)) return;
    bool orbitUpdate = orbit != NULL;
    uint32_t flags;
    uintptr_t target;
    memcpy(&flags, (const char *)component + 0x34, sizeof flags);
    memcpy(&target, (const char *)component + 0xc90, sizeof target);
    extern bool zelda_is_movie(void);

    pthread_mutex_lock(&cameraLock);
    double now = cameraNow();
    if (orbitUpdate) cameraDiagnostics.chase++; else cameraDiagnostics.lifecycle++;
    bool valid = target && !(flags & 3) && !zelda_is_movie();
    if (orbitUpdate) valid = valid && isfinite(orbit[0]) && orbit[0] > 0 && orbit[0] < 500 &&
        isfinite(orbit[1]) && isfinite(orbit[2]) && isfinite(*pitchState);
    static int trace = -1;
    if (trace < 0) trace = getenv("ZELDA_CAMERA_TRACE") != NULL;
    bool report = trace && (globalCameraState.playerCamera != component ||
        globalCameraState.isGameplayActive != valid ||
        globalCameraState.pendingYaw != 0 || globalCameraState.pendingPitch != 0);
    bool continuous = globalCameraState.isGameplayActive &&
        globalCameraState.playerCamera == component && globalCameraState.playerTarget == target &&
        now - globalCameraState.lastPlayerTick <= PLAYER_CAMERA_TIMEOUT;
    if (!valid || !continuous) cameraDiscardInputLocked();
    if (!orbitUpdate) {
        // PlayerCamera's native attention update validates the sender's live
        // client before setting bit 0. Other bits remain native camera policy.
        // Read on the guest camera thread only, never retain the actor pointer.
        uint32_t attention;
        memcpy(&attention, (const char *)component + 0x96c, sizeof attention);
        globalCameraState.nativeTarget = valid && (attention & 1);
        if (globalCameraState.nativeTarget) {
            globalCameraState.targetTick = now;
            if (globalCameraState.isLockDragKeyDown && !globalCameraState.lockHasSwitchedInPress)
                globalCameraState.isTargetLocked = true;
        } else if (now - globalCameraState.targetTick > .10) {
            globalCameraState.isTargetLocked = false;
        }
        // PreUpdate also runs in lock-on/aim modes. Keep their native buttons
        // alive, but discard motion if the previous frame never ran Chase.
        if (globalCameraState.lastOrbitTick < globalCameraState.lastPlayerTick || !valid) {
            if (globalCameraState.pendingYaw || globalCameraState.pendingPitch) cameraDiagnostics.lifecycleDrops++;
            globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
        }
        globalCameraState.isGameplayActive = valid;
        globalCameraState.lastPlayerTick = now;
        globalCameraState.playerCamera = component;
        globalCameraState.playerTarget = target;
        pthread_mutex_unlock(&cameraLock);
        return;
    }
    if (now - globalCameraState.lastOrbitTick > PLAYER_CAMERA_TIMEOUT || globalCameraState.orbitMode != 1 || globalCameraState.isTargetLocked || globalCameraState.rightStickOnly) {
        if (globalCameraState.pendingYaw || globalCameraState.pendingPitch) cameraDiagnostics.chaseDrops++;
        globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
    }
    globalCameraState.lastOrbitTick = now;
    globalCameraState.orbitMode = 1;
    if (globalCameraState.ultrahandAnglesOwned) cameraDiagnostics.chaseReleases++;
    globalCameraState.ultrahandAnglesOwned = false;
    globalCameraState.isGameplayActive = valid;
    globalCameraState.lastPlayerTick = now;
    globalCameraState.playerCamera = component;
    globalCameraState.playerTarget = target;
    if (valid) {
        const double degrees = 180.0 / M_PI;
        if (globalCameraState.pendingPitch != 0) {
            float before = orbit[1];
            double pitch = before + globalCameraState.pendingPitch * degrees;
            double limit = MAX_PITCH_LIMIT * degrees;
            orbit[1] = (float)fmax(-limit, fmin(limit, pitch));
            // Keep the native pitch accumulator aligned for the following frame.
            *pitchState += orbit[1] - before;
        }
        if (globalCameraState.pendingYaw != 0)
            orbit[2] = (float)remainder(orbit[2] + globalCameraState.pendingYaw * degrees, 360.0);
        globalCameraState.currentPitch = (float)(orbit[1] / degrees);
        globalCameraState.currentYaw = (float)(orbit[2] / degrees);
        globalCameraState.targetDistance = orbit[0];
    }
    if (report)
        fprintf(stderr, "[CAMERA_HOOK] control=%d flags=%#x target=%d orbit=(%.3f,%.3f,%.3f) delta=(%.5f,%.5f)\n",
                valid, flags, target != 0, orbit[0], orbit[1], orbit[2],
                globalCameraState.pendingYaw, globalCameraState.pendingPitch);
    globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
    pthread_mutex_unlock(&cameraLock);
}

// Target's common update reaches this only for the verified UltraHand vtable.
// Its local polar pose is still upstream of target composition and collision.
static void cameraApplyUltrahandOrbit(float *orbit, void *execution, const void *component) {
    if (!orbit || !execution || !component) return;
    uint32_t flags; uintptr_t target;
    memcpy(&flags, (const char *)component + 0x34, sizeof flags);
    memcpy(&target, (const char *)component + 0xc90, sizeof target);
    extern bool zelda_is_movie(void);
    bool active = target && !(flags & 3) && !zelda_is_movie();
    void *container;
    memcpy(&container, (char *)execution + 0x28, sizeof container);
    float minimum = 0, maximum = 0;
    bool limits = false;
    pthread_mutex_lock(&cameraLock);
    bool direct = globalCameraState.config.nativeUltrahand && !globalCameraState.rightStickOnly;
    pthread_mutex_unlock(&cameraLock);
    // These are the same inherited-parameter getters used by the native Target
    // update. They run on its guest thread, outside the host input mutex.
    if (active && direct && container && cameraTargetApi.targetType) {
        void *(*get)(void *, void *) = (*(void ***)container)[0];
        void *parameter = get(container, cameraTargetApi.targetType());
        if (parameter) {
            const float *low = cameraTargetApi.minimum(parameter), *high = cameraTargetApi.maximum(parameter);
            if (low && high) {
                minimum = *low; maximum = *high;
                limits = isfinite(minimum) && isfinite(maximum) && minimum <= maximum;
            }
        }
    }
    pthread_mutex_lock(&cameraLock);
    double now = cameraNow();
    cameraDiagnostics.ultra++;
    cameraDiagnostics.minimum = minimum; cameraDiagnostics.maximum = maximum;
    cameraDiagnostics.beforePitch = orbit[1]; cameraDiagnostics.beforeYaw = orbit[2];
    bool valid = active && limits && isfinite(orbit[0]) && orbit[0] > 0 && orbit[0] < 500 &&
        isfinite(orbit[1]) && isfinite(orbit[2]);
    if (!active) cameraDiscardInputLocked();
    float *pitch = (float *)((char *)execution + 0xa4);
    float *yaw = (float *)((char *)execution + 0xa8);
    float *yawBaseline = (float *)((char *)execution + 0xb8);
    float *yawOffset = (float *)((char *)execution + 0xbc);
    valid = valid && isfinite(*pitch) && isfinite(*yaw) && isfinite(*yawBaseline) && isfinite(*yawOffset);
    bool continuous = globalCameraState.playerCamera == component && globalCameraState.playerTarget == target &&
        now - globalCameraState.lastOrbitTick <= PLAYER_CAMERA_TIMEOUT && globalCameraState.orbitMode == 2 &&
        globalCameraState.ultrahandExecution == execution;
    cameraDiagnostics.reject = (!active ? 1u : 0) | (!limits ? 2u : 0) | (!valid ? 4u : 0) |
        (!continuous ? 8u : 0) | (!globalCameraState.config.nativeUltrahand ? 16u : 0) |
        (globalCameraState.rightStickOnly ? 32u : 0) | (globalCameraState.isTargetLocked ? 64u : 0);
    if (cameraDiagnostics.reject) {
        cameraDiagnostics.resets++;
        if (globalCameraState.pendingYaw || globalCameraState.pendingPitch) cameraDiagnostics.ultraDrops++;
        globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
        globalCameraState.ultrahandAnglesOwned = false;
    }
    static int trace = -1;
    if (trace < 0) trace = getenv("ZELDA_CAMERA_TRACE") != NULL;
    if (trace && (globalCameraState.orbitMode != 2 || globalCameraState.pendingYaw || globalCameraState.pendingPitch))
        fprintf(stderr, "[CAMERA_ULTRAHAND] control=%d direct=%d stick=%d limits=(%.2f,%.2f) delta=(%.5f,%.5f)\n",
            valid, globalCameraState.config.nativeUltrahand, globalCameraState.rightStickOnly,
            minimum, maximum, globalCameraState.pendingYaw, globalCameraState.pendingPitch);
    globalCameraState.orbitMode = 2;
    globalCameraState.ultrahandExecution = execution;
    globalCameraState.lastUltrahandTick = active ? now : 0;
    // Invalid parameters must leave input on the right-stick fallback path.
    globalCameraState.lastOrbitTick = valid ? now : 0;
    globalCameraState.lastPlayerTick = now;
    globalCameraState.isGameplayActive = active;
    globalCameraState.playerCamera = component;
    globalCameraState.playerTarget = target;
    if (valid && (globalCameraState.pendingYaw || globalCameraState.pendingPitch) &&
        !globalCameraState.ultrahandAnglesOwned) {
        globalCameraState.ultrahandPitch = orbit[1];
        globalCameraState.ultrahandYaw = orbit[2];
        globalCameraState.ultrahandAnglesOwned = true;
    }
    if (valid && globalCameraState.ultrahandAnglesOwned) {
        cameraDiagnostics.applied++;
        // UltraHand's vtable+0x168 auto-framing branch smooths the local polar
        // angles anew each frame (0x23d91d0). It does not use a4/a8/bc like the
        // generic Target input branch. Accumulate user angles independently so
        // native auto-framing cannot undo each swipe on the following frame.
        // Only angles are retained: native target, arm length and collision
        // continue to update. Reset/gamepad/R/mode changes release ownership.
        const double degrees = 180.0 / M_PI;
        float newPitch = (float)fmax(minimum, fmin(maximum,
            globalCameraState.ultrahandPitch + globalCameraState.pendingPitch * degrees));
        float newYaw = (float)remainder(globalCameraState.ultrahandYaw +
            globalCameraState.pendingYaw * degrees, 360);
        orbit[1] = *pitch = globalCameraState.ultrahandPitch = newPitch;
        orbit[2] = *yaw = globalCameraState.ultrahandYaw = newYaw;
        // Keep the generic Target branch's baseline-relative accumulator in
        // step if native policy switches branches during this execution.
        *yawOffset = (float)remainder(newYaw - *yawBaseline, 360);
    }
    if (valid) {
        globalCameraState.currentPitch = orbit[1] * (float)(M_PI / 180);
        globalCameraState.currentYaw = orbit[2] * (float)(M_PI / 180);
        globalCameraState.targetDistance = orbit[0];
    }
    cameraDiagnostics.afterPitch = orbit[1]; cameraDiagnostics.afterYaw = orbit[2];
    cameraReportRoutingLocked();
    globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
    pthread_mutex_unlock(&cameraLock);
}

bool cameraBindSharedState(void *statePtr) {
    if (!statePtr) return false;
    ZeldaCameraSharedState *state = statePtr;
    if (memcmp(state->magic, "CAM3\0\0\0\0", 8) || state->version != 3) {
        fprintf(stderr, "[CAMERA_HOOK] incompatible bridge; rebuild patched modules\n");
        return false;
    }
    uintptr_t base = (uintptr_t)statePtr - 0x3ae4040;
    cameraTargetApi.targetType = (void *)(base + 0x18a4c0c);
    cameraTargetApi.minimum = (void *)(base + 0x18a9544);
    cameraTargetApi.maximum = (void *)(base + 0x18a9598);
    __atomic_store_n(&state->callback, (uintptr_t)cameraApplyPlayerOrbit, __ATOMIC_RELEASE);
    __atomic_store_n(&state->ultrahandCallback, (uintptr_t)cameraApplyUltrahandOrbit, __ATOMIC_RELEASE);
    fprintf(stderr, "[CAMERA_BRIDGE] CAM3 UltraHand persistent angles; routing diagnostics v1\n");
    return true;
}

void cameraFrameSync(void) {
    pthread_mutex_lock(&cameraLock);
    (void)cameraGameplayActiveLocked();
    pthread_mutex_unlock(&cameraLock);
}

void cameraInit(void) {
    pthread_mutex_lock(&cameraLock);
    CameraConfig config = globalCameraState.config;
    globalCameraState = (CameraState){.config = config};
    pthread_mutex_unlock(&cameraLock);
}

void cameraReset(void) {
    pthread_mutex_lock(&cameraLock);
    cameraDiscardInputLocked();
    globalCameraState.stickX = globalCameraState.stickY = 0;
    pthread_mutex_unlock(&cameraLock);
}

static void cameraAccumulateLocked(float deltaX, float deltaY) {
    cameraDiagnostics.nativeInput++;
    double accel = 1;
    if (!globalCameraState.config.rawMouseMode)
        accel += globalCameraState.config.accelerationFactor * hypot((double)deltaX, deltaY);
    // Positive X looks right (camera orbits left); positive Y looks down.
    double yaw = -deltaX * globalCameraState.config.mouseSensitivityX * accel * ANGLE_RADIAN_SCALE;
    double pitch = deltaY * globalCameraState.config.mouseSensitivityY * accel * ANGLE_RADIAN_SCALE;
    if (globalCameraState.config.invertY) pitch = -pitch;
    if (isfinite(yaw) && isfinite(pitch)) {
        globalCameraState.pendingYaw = remainder(globalCameraState.pendingYaw + yaw, 2 * M_PI);
        globalCameraState.pendingPitch = fmax(-M_PI, fmin(M_PI, globalCameraState.pendingPitch + pitch));
    }
}

void cameraAddMouseDelta(float deltaX, float deltaY) {
    if (!isfinite(deltaX) || !isfinite(deltaY)) return;
    pthread_mutex_lock(&cameraLock);
    cameraDiagnostics.input++;
    if (globalCameraState.mouseCaptured) {
        if (!cameraNativeOrbitLocked()) {
            cameraDiagnostics.stickInput++;
            globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
            globalCameraState.stickX += deltaX;
            globalCameraState.stickY -= deltaY;
            globalCameraState.stickTick = cameraNow();
        } else if (globalCameraState.isLockDragKeyDown && globalCameraState.isTargetLocked) {
            globalCameraState.lockDragAccumX += deltaX;
            if (fabsf(globalCameraState.lockDragAccumX) >= SWITCH_DRAG_THRESHOLD) {
                globalCameraState.switchPulseTicks = SWITCH_PULSE_DURATION;
                globalCameraState.switchDirection = globalCameraState.lockDragAccumX > 0 ? 1 : -1;
                globalCameraState.lockHasSwitchedInPress = true;
                globalCameraState.lockDragAccumX = 0;
                globalCameraState.isTargetLocked = true;
            }
        } else if (!globalCameraState.isTargetLocked) {
            cameraAccumulateLocked(deltaX, deltaY);
        }
    }
    cameraReportRoutingLocked();
    pthread_mutex_unlock(&cameraLock);
}

void cameraAddTouchDelta(float deltaX, float deltaY) {
    if (!isfinite(deltaX) || !isfinite(deltaY)) return;
    pthread_mutex_lock(&cameraLock);
    cameraDiagnostics.input++;
    if (!cameraNativeOrbitLocked()) {
        cameraDiagnostics.stickInput++;
        globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
        globalCameraState.stickX += deltaX; globalCameraState.stickY -= deltaY;
        globalCameraState.stickTick = cameraNow();
    } else if (!globalCameraState.isTargetLocked)
        cameraAccumulateLocked(deltaX, deltaY);
    cameraReportRoutingLocked();
    pthread_mutex_unlock(&cameraLock);
}

void cameraGetAngles(float *yaw, float *pitch, float *distance) {
    pthread_mutex_lock(&cameraLock);
    if (yaw) *yaw = globalCameraState.currentYaw;
    if (pitch) *pitch = globalCameraState.currentPitch;
    if (distance) *distance = globalCameraState.targetDistance;
    pthread_mutex_unlock(&cameraLock);
}

bool cameraPoll(int32_t *outRx, int32_t *outRy) {
    if (!outRx || !outRy) return false;
    pthread_mutex_lock(&cameraLock);
    *outRx = *outRy = 0;
    if (!cameraNativeOrbitLocked() && cameraNow() - globalCameraState.stickTick <= .075 &&
        (globalCameraState.stickX || globalCameraState.stickY)) {
        *outRx = (int32_t)(fmaxf(-1, fminf(1, globalCameraState.stickX / 18)) * 32767);
        *outRy = (int32_t)(fmaxf(-1, fminf(1, globalCameraState.stickY / 18)) * 32767);
        globalCameraState.stickX = globalCameraState.stickY = 0;
        pthread_mutex_unlock(&cameraLock);
        return true;
    }
    globalCameraState.stickX = globalCameraState.stickY = 0;
    bool pulse = cameraGameplayActiveLocked() && globalCameraState.switchPulseTicks > 0;
    if (pulse) {
        int ticks = globalCameraState.switchPulseTicks--;
        if (ticks == SWITCH_PULSE_DURATION) *outRx = globalCameraState.switchDirection * 32767;
        else if (ticks == 2) *outRx = globalCameraState.switchDirection * 20000;
    }
    pthread_mutex_unlock(&cameraLock);
    return pulse;
}

void cameraSetMouseCaptured(bool captured) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.mouseCaptured = captured;
    globalCameraState.ultrahandAnglesOwned = false;
    globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
    globalCameraState.stickX = globalCameraState.stickY = 0;
    pthread_mutex_unlock(&cameraLock);
}

bool cameraIsMouseCaptured(void) {
    pthread_mutex_lock(&cameraLock);
    bool result = globalCameraState.mouseCaptured;
    pthread_mutex_unlock(&cameraLock);
    return result;
}

void cameraToggleMouseCapture(void) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.mouseCaptured = !globalCameraState.mouseCaptured;
    globalCameraState.ultrahandAnglesOwned = false;
    globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
    pthread_mutex_unlock(&cameraLock);
}

void cameraToggleTargetLock(void) {
    pthread_mutex_lock(&cameraLock);
    if (!cameraGameplayActiveLocked()) { pthread_mutex_unlock(&cameraLock); return; }
    globalCameraState.isTargetLocked = !globalCameraState.isTargetLocked && globalCameraState.nativeTarget;
    pthread_mutex_unlock(&cameraLock);
}

void cameraSetTargetLock(bool locked) {
    pthread_mutex_lock(&cameraLock);
    if (!cameraGameplayActiveLocked()) { pthread_mutex_unlock(&cameraLock); return; }
    globalCameraState.isTargetLocked = locked && globalCameraState.nativeTarget;
    pthread_mutex_unlock(&cameraLock);
}

bool cameraIsTargetLocked(void) {
    pthread_mutex_lock(&cameraLock);
    bool result = globalCameraState.isTargetLocked;
    pthread_mutex_unlock(&cameraLock);
    return result;
}

void cameraSwitchTarget(int direction) {
    pthread_mutex_lock(&cameraLock);
    if (!cameraGameplayActiveLocked() || !globalCameraState.nativeTarget) { pthread_mutex_unlock(&cameraLock); return; }
    globalCameraState.switchPulseTicks = SWITCH_PULSE_DURATION;
    globalCameraState.switchDirection = (direction >= 0) ? 1 : -1;
    pthread_mutex_unlock(&cameraLock);
}

void cameraHandleLockDragButton(bool pressed) {
    pthread_mutex_lock(&cameraLock);
    if (!cameraGameplayActiveLocked()) { pthread_mutex_unlock(&cameraLock); return; }
    if (pressed) {
        globalCameraState.lockWasActiveInPress = globalCameraState.isTargetLocked;
        globalCameraState.isTargetLocked |= globalCameraState.nativeTarget;
        globalCameraState.isLockDragKeyDown = true;
        globalCameraState.lockDragAccumX = 0.0f;
        globalCameraState.lockHasSwitchedInPress = false;
    } else {
        if (globalCameraState.isLockDragKeyDown) {
            globalCameraState.isLockDragKeyDown = false;
            if (globalCameraState.lockWasActiveInPress && !globalCameraState.lockHasSwitchedInPress)
                globalCameraState.isTargetLocked = false;
        }
    }
    pthread_mutex_unlock(&cameraLock);
}

void cameraFeedMouseMovementForTargetSwitch(float deltaX) {
    pthread_mutex_lock(&cameraLock);
    if (!cameraGameplayActiveLocked()) { pthread_mutex_unlock(&cameraLock); return; }
    if (globalCameraState.isLockDragKeyDown && globalCameraState.nativeTarget) {
        globalCameraState.lockDragAccumX += deltaX;
        if (globalCameraState.lockDragAccumX >= SWITCH_DRAG_THRESHOLD) {
            globalCameraState.switchPulseTicks = SWITCH_PULSE_DURATION;
            globalCameraState.switchDirection = 1;
            globalCameraState.lockHasSwitchedInPress = true;
            globalCameraState.lockDragAccumX = 0.0f;
            globalCameraState.isTargetLocked = true;
        } else if (globalCameraState.lockDragAccumX <= -SWITCH_DRAG_THRESHOLD) {
            globalCameraState.switchPulseTicks = SWITCH_PULSE_DURATION;
            globalCameraState.switchDirection = -1;
            globalCameraState.lockHasSwitchedInPress = true;
            globalCameraState.lockDragAccumX = 0.0f;
            globalCameraState.isTargetLocked = true;
        }
    }
    pthread_mutex_unlock(&cameraLock);
}

uint64_t cameraGetTargetLockButtons(void) {
    pthread_mutex_lock(&cameraLock);
    uint64_t btn = 0;
    (void)cameraGameplayActiveLocked();
    if (globalCameraState.isTargetLocked || (globalCameraState.isLockDragKeyDown && !globalCameraState.lockHasSwitchedInPress)) {
        if (globalCameraState.switchPulseTicks == SWITCH_PULSE_DURATION) {
            btn = 0;
        } else {
            btn = ZL_BUTTON_MASK;
        }
    }
    pthread_mutex_unlock(&cameraLock);
    return btn;
}

void cameraSetRightStickMode(bool enabled) {
    pthread_mutex_lock(&cameraLock);
    if (globalCameraState.rightStickOnly != enabled) {
        globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
        globalCameraState.stickX = globalCameraState.stickY = 0;
        globalCameraState.rightStickOnly = enabled;
        globalCameraState.ultrahandAnglesOwned = false;
    }
    pthread_mutex_unlock(&cameraLock);
}

CameraConfig cameraGetConfig(void) {
    pthread_mutex_lock(&cameraLock);
    CameraConfig cfg = globalCameraState.config;
    pthread_mutex_unlock(&cameraLock);
    return cfg;
}

void cameraSetConfig(const CameraConfig *newConfig) {
    if (!newConfig) return;
    pthread_mutex_lock(&cameraLock);
    if (isfinite(newConfig->mouseSensitivityX) && newConfig->mouseSensitivityX >= 0 && newConfig->mouseSensitivityX <= 100 &&
        isfinite(newConfig->mouseSensitivityY) && newConfig->mouseSensitivityY >= 0 && newConfig->mouseSensitivityY <= 100 &&
        isfinite(newConfig->accelerationFactor) && newConfig->accelerationFactor >= 0 && newConfig->accelerationFactor <= 1)
    {
        if (globalCameraState.config.nativeUltrahand != newConfig->nativeUltrahand) {
            globalCameraState.ultrahandAnglesOwned = false;
            globalCameraState.pendingYaw = globalCameraState.pendingPitch = 0;
            globalCameraState.stickX = globalCameraState.stickY = 0;
        }
        globalCameraState.config = *newConfig;
    }
    pthread_mutex_unlock(&cameraLock);
}

void cameraSetGameplayActive(bool active) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.isGameplayActive = active;
    globalCameraState.lastPlayerTick = cameraNow();
    if (!active) cameraDiscardInputLocked();
    pthread_mutex_unlock(&cameraLock);
}

bool cameraIsGameplayActive(void) {
    pthread_mutex_lock(&cameraLock);
    bool active = cameraGameplayActiveLocked();
    pthread_mutex_unlock(&cameraLock);
    return active;
}

bool cameraIsUltrahandActive(void) {
    pthread_mutex_lock(&cameraLock);
    bool active = cameraGameplayActiveLocked() && globalCameraState.orbitMode == 2 &&
        cameraNow() - globalCameraState.lastUltrahandTick <= PLAYER_CAMERA_TIMEOUT;
    pthread_mutex_unlock(&cameraLock);
    return active;
}

void cameraNoteGamepadActivity(void) { cameraReset(); }

static time_t lastConfigMtime = 0;

static float parseJsonNumber(const char *json, const char *key, float defaultVal) {
    const char *pos = strstr(json, key);
    if (!pos) return defaultVal;
    pos = strchr(pos, ':');
    if (!pos) return defaultVal;
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == '"') pos++;
    char *end = NULL;
    float val = strtof(pos, &end);
    if (end == pos) return defaultVal;
    return val;
}

static bool parseJsonBool(const char *json, const char *key, bool defaultVal) {
    const char *pos = strstr(json, key);
    if (!pos) return defaultVal;
    pos = strchr(pos, ':');
    if (!pos) return defaultVal;
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == '"') pos++;
    if (strncmp(pos, "true", 4) == 0 || strncmp(pos, "1", 1) == 0) return true;
    if (strncmp(pos, "false", 5) == 0 || strncmp(pos, "0", 1) == 0) return false;
    return defaultVal;
}

bool cameraCheckAndReloadConfig(const char *configPath) {
    if (!configPath || !configPath[0]) return false;
    struct stat st;
    if (stat(configPath, &st) != 0) return false;
    if (st.st_mtime == lastConfigMtime) return false;

    FILE *f = fopen(configPath, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len > 65536) {
        fclose(f);
        return false;
    }

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t readBytes = fread(buf, 1, len, f);
    fclose(f);
    buf[readBytes] = '\0';

    lastConfigMtime = st.st_mtime;

    CameraConfig currentCfg = cameraGetConfig();
    currentCfg.mouseSensitivityX = parseJsonNumber(buf, "mouse_sens_x", currentCfg.mouseSensitivityX);
    currentCfg.mouseSensitivityY = parseJsonNumber(buf, "mouse_sens_y", currentCfg.mouseSensitivityY);
    currentCfg.accelerationFactor = parseJsonNumber(buf, "accel_factor", currentCfg.accelerationFactor);
    currentCfg.decayFactor = parseJsonNumber(buf, "decay_factor", currentCfg.decayFactor);
    currentCfg.invertY = parseJsonBool(buf, "invert_y", currentCfg.invertY);
    currentCfg.rawMouseMode = parseJsonBool(buf, "raw_mouse", currentCfg.rawMouseMode);
    currentCfg.nativeUltrahand = parseJsonBool(buf, "native_ultrahand", currentCfg.nativeUltrahand);

    free(buf);

    cameraSetConfig(&currentCfg);
    return true;
}
