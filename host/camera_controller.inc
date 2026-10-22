#include "camera_controller.h"
#include <pthread.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DEFAULT_SENSITIVITY_X 3.2f
#define DEFAULT_SENSITIVITY_Y 2.8f
#define DEFAULT_ACCEL_FACTOR 0.035f
#define ZL_BUTTON_MASK (1u << 8)
#define SWITCH_PULSE_DURATION 3
#define SWITCH_DRAG_THRESHOLD 28.0f

#define DEFAULT_SPRING_ARM_DISTANCE 4.8f
#define MIN_SPRING_ARM_DISTANCE 1.5f
#define MAX_SPRING_ARM_DISTANCE 18.0f
#define MAX_PITCH_LIMIT 1.45f
#define MIN_PITCH_LIMIT -1.45f
#define ANGLE_RADIAN_SCALE 0.0022f

typedef struct {
    bool mouseCaptured;
    bool isTargetLocked;
    int switchPulseTicks;
    int switchDirection;
    bool isLockDragKeyDown;
    float lockDragAccumX;
    bool lockHasSwitchedInPress;
    bool isGameplayActive;

    // 方案 A：SpringArm 极坐标纯净绝对状态 (无蓄水池、无漏桶、无时间延迟)
    float currentYaw;          // 水平旋转角 (弧度，0 ~ 2PI 循环)
    float currentPitch;        // 垂直俯仰角 (弧度，-1.45 ~ +1.45 钳位)
    float targetDistance;      // 目标弹簧臂长 (米，默认 4.8m)
    bool hasInitialSync;       // 是否已与客体相机初始对齐

    CameraConfig config;
} CameraState;

static pthread_mutex_t cameraLock = PTHREAD_MUTEX_INITIALIZER;
static CameraState globalCameraState = {
    .mouseCaptured = false,
    .isTargetLocked = false,
    .switchPulseTicks = 0,
    .switchDirection = 0,
    .isLockDragKeyDown = false,
    .lockDragAccumX = 0.0f,
    .lockHasSwitchedInPress = false,
    .isGameplayActive = false,
    .currentYaw = 0.0f,
    .currentPitch = 0.25f,
    .targetDistance = DEFAULT_SPRING_ARM_DISTANCE,
    .hasInitialSync = false,
    .config = {
        .mouseSensitivityX = DEFAULT_SENSITIVITY_X,
        .mouseSensitivityY = DEFAULT_SENSITIVITY_Y,
        .accelerationFactor = DEFAULT_ACCEL_FACTOR,
        .decayFactor = 0.0f,
        .invertY = false,
        .rawMouseMode = true,
    }
};

void cameraInit(void) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.mouseCaptured = false;
    globalCameraState.isTargetLocked = false;
    globalCameraState.switchPulseTicks = 0;
    globalCameraState.switchDirection = 0;
    globalCameraState.isLockDragKeyDown = false;
    globalCameraState.lockDragAccumX = 0.0f;
    globalCameraState.lockHasSwitchedInPress = false;
    globalCameraState.isGameplayActive = false;
    globalCameraState.currentYaw = 0.0f;
    globalCameraState.currentPitch = 0.25f;
    globalCameraState.targetDistance = DEFAULT_SPRING_ARM_DISTANCE;
    globalCameraState.hasInitialSync = false;
    pthread_mutex_unlock(&cameraLock);
}

void cameraReset(void) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.switchPulseTicks = 0;
    globalCameraState.switchDirection = 0;
    globalCameraState.hasInitialSync = false;
    pthread_mutex_unlock(&cameraLock);
}

void cameraAddMouseDelta(float deltaX, float deltaY) {
    if (!isfinite(deltaX) || !isfinite(deltaY)) return;

    pthread_mutex_lock(&cameraLock);
    if (globalCameraState.mouseCaptured) {
        float speed = hypotf(deltaX, deltaY);
        float accel = 1.0f;
        if (!globalCameraState.config.rawMouseMode) {
            accel = 1.0f + globalCameraState.config.accelerationFactor * speed;
        }

        // 统一物理朝向：macOS 向上推鼠标 deltaY < 0，校准为抬头 (pitch 增加)
        float correctedY = -deltaY;
        if (globalCameraState.config.invertY) {
            correctedY = -correctedY;
        }

        float scaledX = deltaX * globalCameraState.config.mouseSensitivityX * accel;
        float scaledY = correctedY * globalCameraState.config.mouseSensitivityY * accel;

        // 方案 A 直接驱动：鼠标移动瞬间累加弧度，无蓄水池，无排队滞后！
        float dYaw = scaledX * ANGLE_RADIAN_SCALE;
        float dPitch = scaledY * ANGLE_RADIAN_SCALE;

        globalCameraState.currentYaw += dYaw;
        while (globalCameraState.currentYaw >= 2.0f * (float)M_PI) globalCameraState.currentYaw -= 2.0f * (float)M_PI;
        while (globalCameraState.currentYaw < 0.0f) globalCameraState.currentYaw += 2.0f * (float)M_PI;

        globalCameraState.currentPitch += dPitch;
        if (globalCameraState.currentPitch > MAX_PITCH_LIMIT) globalCameraState.currentPitch = MAX_PITCH_LIMIT;
        if (globalCameraState.currentPitch < MIN_PITCH_LIMIT) globalCameraState.currentPitch = MIN_PITCH_LIMIT;

        // 长按锁定键期间横向位移切怪检测
        if (globalCameraState.isLockDragKeyDown) {
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
    }
    pthread_mutex_unlock(&cameraLock);
}

void cameraAddTouchDelta(float deltaX, float deltaY) {
    if (!isfinite(deltaX) || !isfinite(deltaY)) return;

    pthread_mutex_lock(&cameraLock);
    float speed = hypotf(deltaX, deltaY);
    float accel = 1.0f + (globalCameraState.config.accelerationFactor * 0.5f) * speed;
    float correctedY = -deltaY;
    if (globalCameraState.config.invertY) {
        correctedY = -correctedY;
    }

    float scaledX = deltaX * globalCameraState.config.mouseSensitivityX * accel;
    float scaledY = correctedY * globalCameraState.config.mouseSensitivityY * accel;

    // 移动端划屏直接驱动极坐标
    float dYaw = scaledX * ANGLE_RADIAN_SCALE;
    float dPitch = scaledY * ANGLE_RADIAN_SCALE;

    globalCameraState.currentYaw += dYaw;
    while (globalCameraState.currentYaw >= 2.0f * (float)M_PI) globalCameraState.currentYaw -= 2.0f * (float)M_PI;
    while (globalCameraState.currentYaw < 0.0f) globalCameraState.currentYaw += 2.0f * (float)M_PI;

    globalCameraState.currentPitch += dPitch;
    if (globalCameraState.currentPitch > MAX_PITCH_LIMIT) globalCameraState.currentPitch = MAX_PITCH_LIMIT;
    if (globalCameraState.currentPitch < MIN_PITCH_LIMIT) globalCameraState.currentPitch = MIN_PITCH_LIMIT;

    pthread_mutex_unlock(&cameraLock);
}

void cameraUpdateSpringArm(double *ioEyeX, double *ioEyeY, double *ioEyeZ,
                           double lookAtX, double lookAtY, double lookAtZ,
                           float collisionDistance) {
    if (!ioEyeX || !ioEyeY || !ioEyeZ) return;

    pthread_mutex_lock(&cameraLock);

    // 首次调用或重置初值：从客体现有的真实 Eye 与 LookAt 反求当前 Yaw/Pitch
    if (!globalCameraState.hasInitialSync) {
        double dx = *ioEyeX - lookAtX;
        double dy = *ioEyeY - lookAtY;
        double dz = *ioEyeZ - lookAtZ;
        double dist = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 0.1 && isfinite(dist)) {
            globalCameraState.currentYaw = (float)atan2(dx, dz);
            while (globalCameraState.currentYaw < 0.0f) globalCameraState.currentYaw += 2.0f * (float)M_PI;
            double sinPitch = dy / dist;
            if (sinPitch > 0.99) sinPitch = 0.99;
            if (sinPitch < -0.99) sinPitch = -0.99;
            globalCameraState.currentPitch = (float)asin(sinPitch);
            globalCameraState.targetDistance = (float)dist;
            globalCameraState.hasInitialSync = true;
        }
    }

    if (globalCameraState.mouseCaptured && globalCameraState.hasInitialSync) {
        float effectiveDist = globalCameraState.targetDistance;
        // 原生碰撞避障联动：若碰到障碍物自动收缩臂长防穿墙
        if (collisionDistance > 0.1f && collisionDistance < effectiveDist) {
            effectiveDist = collisionDistance;
        }

        float yaw = globalCameraState.currentYaw;
        float pitch = globalCameraState.currentPitch;
        float cosPitch = cosf(pitch);

        double newEyeX = lookAtX + (double)(effectiveDist * cosPitch * sinf(yaw));
        double newEyeY = lookAtY + (double)(effectiveDist * sinf(pitch));
        double newEyeZ = lookAtZ + (double)(effectiveDist * cosPitch * cosf(yaw));

        if (isfinite(newEyeX) && isfinite(newEyeY) && isfinite(newEyeZ)) {
            *ioEyeX = newEyeX;
            *ioEyeY = newEyeY;
            *ioEyeZ = newEyeZ;
        }
    } else {
        // 未捕获光标时持续静默跟踪游戏原生朝向，下一次捕获时绝不发生镜头突兀跳变
        double dx = *ioEyeX - lookAtX;
        double dy = *ioEyeY - lookAtY;
        double dz = *ioEyeZ - lookAtZ;
        double dist = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 0.1 && isfinite(dist)) {
            globalCameraState.currentYaw = (float)atan2(dx, dz);
            while (globalCameraState.currentYaw < 0.0f) globalCameraState.currentYaw += 2.0f * (float)M_PI;
            double sinPitch = dy / dist;
            if (sinPitch > 0.99) sinPitch = 0.99;
            if (sinPitch < -0.99) sinPitch = -0.99;
            globalCameraState.currentPitch = (float)asin(sinPitch);
        }
    }

    pthread_mutex_unlock(&cameraLock);
}

void cameraGetAngles(float *outYaw, float *outPitch, float *outDistance) {
    pthread_mutex_lock(&cameraLock);
    if (outYaw) *outYaw = globalCameraState.currentYaw;
    if (outPitch) *outPitch = globalCameraState.currentPitch;
    if (outDistance) *outDistance = globalCameraState.targetDistance;
    pthread_mutex_unlock(&cameraLock);
}

void cameraSetAngles(float yaw, float pitch, float distance) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.currentYaw = yaw;
    while (globalCameraState.currentYaw >= 2.0f * (float)M_PI) globalCameraState.currentYaw -= 2.0f * (float)M_PI;
    while (globalCameraState.currentYaw < 0.0f) globalCameraState.currentYaw += 2.0f * (float)M_PI;

    globalCameraState.currentPitch = pitch;
    if (globalCameraState.currentPitch > MAX_PITCH_LIMIT) globalCameraState.currentPitch = MAX_PITCH_LIMIT;
    if (globalCameraState.currentPitch < MIN_PITCH_LIMIT) globalCameraState.currentPitch = MIN_PITCH_LIMIT;

    if (distance >= MIN_SPRING_ARM_DISTANCE && distance <= MAX_SPRING_ARM_DISTANCE) {
        globalCameraState.targetDistance = distance;
    }
    globalCameraState.hasInitialSync = true;
    pthread_mutex_unlock(&cameraLock);
}

void cameraAddZoom(float deltaZoom) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.targetDistance += deltaZoom;
    if (globalCameraState.targetDistance < MIN_SPRING_ARM_DISTANCE) {
        globalCameraState.targetDistance = MIN_SPRING_ARM_DISTANCE;
    }
    if (globalCameraState.targetDistance > MAX_SPRING_ARM_DISTANCE) {
        globalCameraState.targetDistance = MAX_SPRING_ARM_DISTANCE;
    }
    pthread_mutex_unlock(&cameraLock);
}

void cameraRequestInitialSync(void) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.hasInitialSync = false;
    pthread_mutex_unlock(&cameraLock);
}

bool cameraPoll(int32_t *outRx, int32_t *outRy) {
    if (!outRx || !outRy) return false;

    pthread_mutex_lock(&cameraLock);

    // 方案 A 模式：手柄右摇杆彻底断开，保持绝对为 0，杜绝摇杆与 SpringArm 竞争！
    *outRx = 0;
    *outRy = 0;

    // 仅在切怪脉冲触发时输出切怪瞬时脉冲
    if (globalCameraState.switchPulseTicks > 0) {
        int ticks = globalCameraState.switchPulseTicks--;
        int dir = globalCameraState.switchDirection;
        if (ticks == SWITCH_PULSE_DURATION) {
            *outRx = dir * 32767;
        } else if (ticks == 2) {
            *outRx = dir * 20000;
        }
        pthread_mutex_unlock(&cameraLock);
        return true;
    }

    pthread_mutex_unlock(&cameraLock);
    return false;
}

void cameraSetMouseCaptured(bool captured) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.mouseCaptured = captured;
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
    pthread_mutex_unlock(&cameraLock);
}

void cameraToggleTargetLock(void) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.isTargetLocked = !globalCameraState.isTargetLocked;
    pthread_mutex_unlock(&cameraLock);
}

void cameraSetTargetLock(bool locked) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.isTargetLocked = locked;
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
    globalCameraState.switchPulseTicks = SWITCH_PULSE_DURATION;
    globalCameraState.switchDirection = (direction >= 0) ? 1 : -1;
    pthread_mutex_unlock(&cameraLock);
}

void cameraHandleLockDragButton(bool pressed) {
    pthread_mutex_lock(&cameraLock);
    if (pressed) {
        globalCameraState.isLockDragKeyDown = true;
        globalCameraState.lockDragAccumX = 0.0f;
        globalCameraState.lockHasSwitchedInPress = false;
    } else {
        if (globalCameraState.isLockDragKeyDown) {
            globalCameraState.isLockDragKeyDown = false;
            if (!globalCameraState.lockHasSwitchedInPress) {
                globalCameraState.isTargetLocked = !globalCameraState.isTargetLocked;
            }
        }
    }
    pthread_mutex_unlock(&cameraLock);
}

void cameraFeedMouseMovementForTargetSwitch(float deltaX) {
    pthread_mutex_lock(&cameraLock);
    if (globalCameraState.isLockDragKeyDown) {
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
    if (globalCameraState.isTargetLocked) {
        if (globalCameraState.switchPulseTicks == SWITCH_PULSE_DURATION) {
            btn = 0;
        } else {
            btn = ZL_BUTTON_MASK;
        }
    }
    pthread_mutex_unlock(&cameraLock);
    return btn;
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
    globalCameraState.config = *newConfig;
    pthread_mutex_unlock(&cameraLock);
}

void cameraSetGameplayActive(bool active) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.isGameplayActive = active;
    if (!active) {
        globalCameraState.mouseCaptured = false;
        globalCameraState.isLockDragKeyDown = false;
        globalCameraState.hasInitialSync = false;
    }
    pthread_mutex_unlock(&cameraLock);
}

bool cameraIsGameplayActive(void) {
    pthread_mutex_lock(&cameraLock);
    bool active = globalCameraState.isGameplayActive;
    pthread_mutex_unlock(&cameraLock);
    return active;
}

void cameraNoteGamepadActivity(void) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.hasInitialSync = false;
    pthread_mutex_unlock(&cameraLock);
}

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

    free(buf);

    cameraSetConfig(&currentCfg);
    return true;
}
