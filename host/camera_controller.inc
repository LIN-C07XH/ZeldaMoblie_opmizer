#include "camera_controller.h"
#include <pthread.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define DEFAULT_SENSITIVITY_X 3.6f
#define DEFAULT_SENSITIVITY_Y 3.0f
#define DEFAULT_ACCEL_FACTOR 0.045f
#define DEFAULT_DECAY_FACTOR 0.65f
#define VELOCITY_EPSILON 0.05f
#define MAX_STICK_VALUE 32767.0f
#define MAX_BUFFER_CAP 90.0f
#define STICK_DRAIN_RATE 16.0f
#define STICK_SCALE_FACTOR (MAX_STICK_VALUE / STICK_DRAIN_RATE)
#define ZL_BUTTON_MASK (1u << 8)
#define SWITCH_PULSE_DURATION 3
#define SWITCH_DRAG_THRESHOLD 28.0f

typedef struct {
    float pendingDeltaX;
    float pendingDeltaY;
    float bufferX;
    float bufferY;
    float currentVelocityX;
    float currentVelocityY;
    bool mouseCaptured;
    bool isTargetLocked;
    int switchPulseTicks;
    int switchDirection;
    bool isLockDragKeyDown;
    float lockDragAccumX;
    bool lockHasSwitchedInPress;
    bool isGameplayActive;
    CameraConfig config;
} CameraState;

static pthread_mutex_t cameraLock = PTHREAD_MUTEX_INITIALIZER;
static CameraState globalCameraState = {
    .pendingDeltaX = 0.0f,
    .pendingDeltaY = 0.0f,
    .bufferX = 0.0f,
    .bufferY = 0.0f,
    .currentVelocityX = 0.0f,
    .currentVelocityY = 0.0f,
    .mouseCaptured = false,
    .isTargetLocked = false,
    .switchPulseTicks = 0,
    .switchDirection = 0,
    .isLockDragKeyDown = false,
    .lockDragAccumX = 0.0f,
    .lockHasSwitchedInPress = false,
    .isGameplayActive = false,
    .config = {
        .mouseSensitivityX = DEFAULT_SENSITIVITY_X,
        .mouseSensitivityY = DEFAULT_SENSITIVITY_Y,
        .accelerationFactor = DEFAULT_ACCEL_FACTOR,
        .decayFactor = DEFAULT_DECAY_FACTOR,
        .invertY = false,
        .rawMouseMode = true,
    }
};

void cameraInit(void) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.pendingDeltaX = 0.0f;
    globalCameraState.pendingDeltaY = 0.0f;
    globalCameraState.bufferX = 0.0f;
    globalCameraState.bufferY = 0.0f;
    globalCameraState.currentVelocityX = 0.0f;
    globalCameraState.currentVelocityY = 0.0f;
    globalCameraState.mouseCaptured = false;
    globalCameraState.isTargetLocked = false;
    globalCameraState.switchPulseTicks = 0;
    globalCameraState.switchDirection = 0;
    globalCameraState.isLockDragKeyDown = false;
    globalCameraState.lockDragAccumX = 0.0f;
    globalCameraState.lockHasSwitchedInPress = false;
    globalCameraState.isGameplayActive = false;
    pthread_mutex_unlock(&cameraLock);
}

void cameraReset(void) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.pendingDeltaX = 0.0f;
    globalCameraState.pendingDeltaY = 0.0f;
    globalCameraState.bufferX = 0.0f;
    globalCameraState.bufferY = 0.0f;
    globalCameraState.currentVelocityX = 0.0f;
    globalCameraState.currentVelocityY = 0.0f;
    globalCameraState.switchPulseTicks = 0;
    globalCameraState.switchDirection = 0;
    pthread_mutex_unlock(&cameraLock);
}

void cameraAddMouseDelta(float deltaX, float deltaY) {
    if (!isfinite(deltaX) || !isfinite(deltaY)) return;

    pthread_mutex_lock(&cameraLock);
    if (globalCameraState.mouseCaptured) {
        float speed = hypotf(deltaX, deltaY);
        // 无限动态加速度曲线：慢速微调保持精细，高速移动时通过速度因子呈非线性放大
        float accel = 1.0f + globalCameraState.config.accelerationFactor * speed;
        float scaledX = deltaX * globalCameraState.config.mouseSensitivityX * accel;

        // macOS AppKit 中向上推鼠标为负值，校准为标准仰视抬头 (RY > 0)
        float correctedY = -deltaY;
        if (globalCameraState.config.invertY) {
            correctedY = -correctedY;
        }
        float scaledY = correctedY * globalCameraState.config.mouseSensitivityY * accel;

        // 若处于长按锁定键期间，检测横向位移累积是否达到切怪阈值
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

        globalCameraState.pendingDeltaX += scaledX;
        globalCameraState.pendingDeltaY += scaledY;
    }
    pthread_mutex_unlock(&cameraLock);
}

void cameraAddTouchDelta(float deltaX, float deltaY) {
    if (!isfinite(deltaX) || !isfinite(deltaY)) return;

    pthread_mutex_lock(&cameraLock);
    float speed = hypotf(deltaX, deltaY);
    float accel = 1.0f + (globalCameraState.config.accelerationFactor * 0.5f) * speed;
    float scaledX = deltaX * globalCameraState.config.mouseSensitivityX * accel;

    float correctedY = -deltaY;
    if (globalCameraState.config.invertY) {
        correctedY = -correctedY;
    }
    float scaledY = correctedY * globalCameraState.config.mouseSensitivityY * accel;

    globalCameraState.pendingDeltaX += scaledX;
    globalCameraState.pendingDeltaY += scaledY;
    pthread_mutex_unlock(&cameraLock);
}

bool cameraPoll(int32_t *outRx, int32_t *outRy) {
    if (!outRx || !outRy) return false;

    pthread_mutex_lock(&cameraLock);

    // 未真正进入 3D 游戏游玩世界时（如首页、加载画面、暂停菜单），绝对不输出视角输入，避免锁死摇杆
    if (!globalCameraState.isGameplayActive) {
        globalCameraState.pendingDeltaX = 0.0f;
        globalCameraState.pendingDeltaY = 0.0f;
        globalCameraState.bufferX = 0.0f;
        globalCameraState.bufferY = 0.0f;
        globalCameraState.currentVelocityX = 0.0f;
        globalCameraState.currentVelocityY = 0.0f;
        pthread_mutex_unlock(&cameraLock);
        return false;
    }

    // 优先响应智能切怪微脉冲
    if (globalCameraState.switchPulseTicks > 0) {
        int ticks = globalCameraState.switchPulseTicks--;
        int dir = globalCameraState.switchDirection;
        if (ticks == SWITCH_PULSE_DURATION) {
            *outRx = (int32_t)(dir * MAX_STICK_VALUE);
            *outRy = 0;
        } else if (ticks == 2) {
            *outRx = (int32_t)(dir * 20000.0f);
            *outRy = 0;
        } else {
            *outRx = 0;
            *outRy = 0;
        }
        pthread_mutex_unlock(&cameraLock);
        return true;
    }

    // 将本帧新输入的位移注入 Spring Arm 蓄水池
    globalCameraState.bufferX += globalCameraState.pendingDeltaX;
    globalCameraState.bufferY += globalCameraState.pendingDeltaY;
    globalCameraState.pendingDeltaX = 0.0f;
    globalCameraState.pendingDeltaY = 0.0f;

    // 限制蓄水池上限，防止极大甩动导致过长惯性
    if (globalCameraState.bufferX > MAX_BUFFER_CAP) globalCameraState.bufferX = MAX_BUFFER_CAP;
    if (globalCameraState.bufferX < -MAX_BUFFER_CAP) globalCameraState.bufferX = -MAX_BUFFER_CAP;
    if (globalCameraState.bufferY > MAX_BUFFER_CAP) globalCameraState.bufferY = MAX_BUFFER_CAP;
    if (globalCameraState.bufferY < -MAX_BUFFER_CAP) globalCameraState.bufferY = -MAX_BUFFER_CAP;

    bool hasBuffer = (fabsf(globalCameraState.bufferX) > 0.01f || fabsf(globalCameraState.bufferY) > 0.01f);

    if (!hasBuffer) {
        // 无蓄水时，摇杆输出极速回弹至零，避免任何漂移
        globalCameraState.currentVelocityX *= 0.4f;
        globalCameraState.currentVelocityY *= 0.4f;
        if (fabsf(globalCameraState.currentVelocityX) < 150.0f) globalCameraState.currentVelocityX = 0.0f;
        if (fabsf(globalCameraState.currentVelocityY) < 150.0f) globalCameraState.currentVelocityY = 0.0f;

        *outRx = (int32_t)lroundf(globalCameraState.currentVelocityX);
        *outRy = (int32_t)lroundf(globalCameraState.currentVelocityY);
        bool active = (*outRx != 0 || *outRy != 0);
        pthread_mutex_unlock(&cameraLock);
        return active;
    }

    // 漏桶平滑释放机制：计算当帧输出给 Spring Arm 的摇杆推力
    float targetStickX = globalCameraState.bufferX * STICK_SCALE_FACTOR;
    float targetStickY = globalCameraState.bufferY * STICK_SCALE_FACTOR;

    // 钳位至 Switch 摇杆极限 [-32767, 32767]
    if (targetStickX > MAX_STICK_VALUE) targetStickX = MAX_STICK_VALUE;
    if (targetStickX < -MAX_STICK_VALUE) targetStickX = -MAX_STICK_VALUE;
    if (targetStickY > MAX_STICK_VALUE) targetStickY = MAX_STICK_VALUE;
    if (targetStickY < -MAX_STICK_VALUE) targetStickY = -MAX_STICK_VALUE;

    // 从蓄水池扣除本帧已经转化的位移量
    float consumedX = targetStickX / STICK_SCALE_FACTOR;
    float consumedY = targetStickY / STICK_SCALE_FACTOR;
    globalCameraState.bufferX -= consumedX;
    globalCameraState.bufferY -= consumedY;

    // 剩余余量阻尼衰减
    globalCameraState.bufferX *= globalCameraState.config.decayFactor;
    globalCameraState.bufferY *= globalCameraState.config.decayFactor;
    if (fabsf(globalCameraState.bufferX) < 0.02f) globalCameraState.bufferX = 0.0f;
    if (fabsf(globalCameraState.bufferY) < 0.02f) globalCameraState.bufferY = 0.0f;

    globalCameraState.currentVelocityX = targetStickX;
    globalCameraState.currentVelocityY = targetStickY;

    *outRx = (int32_t)lroundf(targetStickX);
    *outRy = (int32_t)lroundf(targetStickY);

    pthread_mutex_unlock(&cameraLock);
    return true;
}

void cameraSetMouseCaptured(bool captured) {
    pthread_mutex_lock(&cameraLock);
    globalCameraState.mouseCaptured = captured;
    if (!captured) {
        globalCameraState.pendingDeltaX = 0.0f;
        globalCameraState.pendingDeltaY = 0.0f;
        globalCameraState.currentVelocityX = 0.0f;
        globalCameraState.currentVelocityY = 0.0f;
    }
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
    if (!globalCameraState.mouseCaptured) {
        globalCameraState.pendingDeltaX = 0.0f;
        globalCameraState.pendingDeltaY = 0.0f;
        globalCameraState.currentVelocityX = 0.0f;
        globalCameraState.currentVelocityY = 0.0f;
    }
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
            // 若按住期间未曾甩动鼠标切怪，则视为单击切换锁定状态
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
        // 在切怪脉冲的第一帧释放 ZL，配合摇杆偏转让游戏重新选定相邻目标
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
        // 离开真正游戏世界游玩状态（如退回首页、进加载、打开暂停菜单），重置动力学并安全释放捕获
        globalCameraState.pendingDeltaX = 0.0f;
        globalCameraState.pendingDeltaY = 0.0f;
        globalCameraState.bufferX = 0.0f;
        globalCameraState.bufferY = 0.0f;
        globalCameraState.currentVelocityX = 0.0f;
        globalCameraState.currentVelocityY = 0.0f;
        globalCameraState.mouseCaptured = false;
        globalCameraState.isLockDragKeyDown = false;
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
    // 物理实体手柄产生操作：重置鼠标残留累积速度，避免两者冲突
    globalCameraState.pendingDeltaX = 0.0f;
    globalCameraState.pendingDeltaY = 0.0f;
    globalCameraState.bufferX = 0.0f;
    globalCameraState.bufferY = 0.0f;
    globalCameraState.currentVelocityX = 0.0f;
    globalCameraState.currentVelocityY = 0.0f;
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
    fprintf(stderr, "[CAMERA_HOT_RELOAD] Loaded config from %s: sensX=%.2f sensY=%.2f accel=%.4f decay=%.2f invertY=%d raw=%d\n",
            configPath, currentCfg.mouseSensitivityX, currentCfg.mouseSensitivityY,
            currentCfg.accelerationFactor, currentCfg.decayFactor, currentCfg.invertY, currentCfg.rawMouseMode);
    return true;
}
