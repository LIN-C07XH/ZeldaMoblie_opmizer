#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>

#include "camera_controller.h"
#include "camera_controller.inc"

static void testCameraInit(void) {
    cameraInit();
    assert(!cameraIsMouseCaptured());
    int32_t rx = 0, ry = 0;
    assert(!cameraPoll(&rx, &ry));
    printf("[PASS] testCameraInit\n");
}

static void testGameplayActiveGate(void) {
    cameraInit();
    cameraSetGameplayActive(false);
    cameraSetMouseCaptured(true);

    // 未进入游戏时，即使移动了鼠标，poll 也必须为 false 并自动丢弃位移
    cameraAddMouseDelta(50.0f, 50.0f);
    int32_t rx = 0, ry = 0;
    assert(!cameraPoll(&rx, &ry));

    // 进入游戏并捕获鼠标后，位移应正常响应 (macOS 向上推鼠标 deltaY < 0 为仰视 ry > 0)
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);
    cameraAddMouseDelta(50.0f, -50.0f);
    assert(cameraPoll(&rx, &ry));
    assert(rx > 0 && ry > 0);

    // 再次离开游戏（例如进入菜单），自动释放捕获，位移应再次被屏蔽
    cameraSetGameplayActive(false);
    assert(!cameraIsMouseCaptured());
    cameraAddMouseDelta(30.0f, -30.0f);
    assert(!cameraPoll(&rx, &ry));

    printf("[PASS] testGameplayActiveGate\n");
}

static void testMouseCaptureToggle(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    assert(!cameraIsMouseCaptured());

    // 未捕获状态下，鼠标位移不应累积
    cameraAddMouseDelta(50.0f, -50.0f);
    int32_t rx = 0, ry = 0;
    assert(!cameraPoll(&rx, &ry));

    // 捕获鼠标
    cameraSetMouseCaptured(true);
    assert(cameraIsMouseCaptured());

    // 捕获状态下，鼠标位移正常记录 (向上推鼠标 deltaY < 0 映射为抬头仰视 ry > 0)
    cameraAddMouseDelta(10.0f, -10.0f);
    assert(cameraPoll(&rx, &ry));
    assert(rx > 0 && ry > 0);

    // 释放捕获
    cameraToggleMouseCapture();
    assert(!cameraIsMouseCaptured());

    printf("[PASS] testMouseCaptureToggle\n");
}

static void testUnlimitedAcceleration(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);

    // 1. 慢速微调位移：移动 0.6 像素，测试单帧超线性放大响应
    cameraAddMouseDelta(0.6f, 0.0f);
    int32_t slowRx = 0, slowRy = 0;
    bool slowActive = cameraPoll(&slowRx, &slowRy);
    assert(slowActive);
    assert(slowRx > 0);

    cameraReset();

    // 2. 快速移动 3.0 像素 (位移为 5 倍)
    cameraAddMouseDelta(3.0f, 0.0f);
    int32_t fastRx = 0, fastRy = 0;
    bool fastActive = cameraPoll(&fastRx, &fastRy);
    assert(fastActive);
    assert(fastRx > 0);

    // 验证动态加速度特征：快速移动时的输出比单纯线性 5 倍还要大
    float ratio = (float)fastRx / (float)slowRx;
    assert(ratio > 5.0f);

    cameraReset();

    // 3. 验证 Spring Arm 蓄水池的大甩动总冲量连续释放特性：
    // 快速划动 30 像素时，蓄水池将连续多帧稳定输出高推力，驱动弹簧臂平滑公转
    cameraAddMouseDelta(30.0f, 0.0f);
    int64_t totalImpulse = 0;
    int32_t rx = 0, ry = 0;
    int framesCount = 0;
    while (cameraPoll(&rx, &ry)) {
        totalImpulse += rx;
        framesCount++;
        if (framesCount > 50) break;
    }
    // 应当持续稳定释放多帧 (不少于 4 帧)，驱动 Spring Arm 平滑旋转而不是单帧抽搐
    assert(framesCount >= 4);
    assert(totalImpulse > 80000);

    printf("[PASS] testUnlimitedAcceleration (ratio=%.2f > 5.0, springArmFrames=%d, impulse=%lld)\n",
           ratio, framesCount, (long long)totalImpulse);
}

static void testInvertY(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);

    // 默认设置：向上推鼠标 (-10.0f) 对应抬头仰视 (ry > 0)
    cameraAddMouseDelta(0.0f, -10.0f);
    int32_t rx = 0, ry = 0;
    assert(cameraPoll(&rx, &ry));
    assert(ry > 0);

    cameraReset();

    // 开启反转 Y 轴：向上推鼠标 (-10.0f) 反转为低头俯视 (ry < 0)
    CameraConfig cfg = cameraGetConfig();
    cfg.invertY = true;
    cameraSetConfig(&cfg);

    cameraAddMouseDelta(0.0f, -10.0f);
    assert(cameraPoll(&rx, &ry));
    assert(ry < 0);

    // 恢复默认设置
    cfg.invertY = false;
    cameraSetConfig(&cfg);

    printf("[PASS] testInvertY\n");
}

static void testTouchDelta(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    // 移动端触控向上划屏 (deltaY < 0) 触发抬头仰视 (ry > 0)
    cameraAddTouchDelta(15.0f, -10.0f);
    int32_t rx = 0, ry = 0;
    assert(cameraPoll(&rx, &ry));
    assert(rx > 0);
    assert(ry > 0);

    printf("[PASS] testTouchDelta\n");
}

static void testDecayAndClamping(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);

    // 极大位移：测试极限钳位保护 (向上推 -5000 对应 ry == 32767)
    cameraAddMouseDelta(5000.0f, -5000.0f);
    int32_t rx = 0, ry = 0;
    assert(cameraPoll(&rx, &ry));
    assert(rx == 32767);
    assert(ry == 32767);

    // 连续多次 poll，Spring Arm 蓄水池平滑释放完毕并极速阻尼至零，无幽灵漂移
    for (int i = 0; i < 20; i++) {
        cameraPoll(&rx, &ry);
    }
    // 20 帧后已完全归零
    assert(!cameraPoll(&rx, &ry));

    printf("[PASS] testDecayAndClamping\n");
}

static void testConfigHotReload(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    const char *tmpPath = "/tmp/test_camera_config.json";
    FILE *f = fopen(tmpPath, "wb");
    assert(f != NULL);
    const char *content = "{\n"
                          "  \"mouse_sens_x\": 2.8,\n"
                          "  \"mouse_sens_y\": 3.1,\n"
                          "  \"accel_factor\": 0.088,\n"
                          "  \"decay_factor\": 0.65,\n"
                          "  \"invert_y\": true,\n"
                          "  \"raw_mouse\": false\n"
                          "}";
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    bool reloaded = cameraCheckAndReloadConfig(tmpPath);
    assert(reloaded == true);

    CameraConfig cfg = cameraGetConfig();
    assert(fabsf(cfg.mouseSensitivityX - 2.8f) < 0.001f);
    assert(fabsf(cfg.mouseSensitivityY - 3.1f) < 0.001f);
    assert(fabsf(cfg.accelerationFactor - 0.088f) < 0.001f);
    assert(fabsf(cfg.decayFactor - 0.65f) < 0.001f);
    assert(cfg.invertY == true);
    assert(cfg.rawMouseMode == false);

    // 未做任何改动时再次调用应返回 false
    assert(cameraCheckAndReloadConfig(tmpPath) == false);

    remove(tmpPath);
    printf("[PASS] testConfigHotReload\n");
}

static void testLockDragTargetSwitch(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);
    assert(!cameraIsTargetLocked());

    // 1. 单击（Tap）行为测试：按下后立即松开，无鼠标位移 -> 翻转为锁定状态
    cameraHandleLockDragButton(true);
    cameraHandleLockDragButton(false);
    assert(cameraIsTargetLocked());

    // 再次单击 -> 翻转为未锁定状态
    cameraHandleLockDragButton(true);
    cameraHandleLockDragButton(false);
    assert(!cameraIsTargetLocked());

    // 2. 长按 + 移动鼠标切怪测试：
    cameraHandleLockDragButton(true);
    // 移动鼠标向右超过 28 像素
    cameraAddMouseDelta(35.0f, 0.0f);
    // 应触发向右切怪，且自动进入锁定
    assert(cameraIsTargetLocked());
    int32_t rx = 0, ry = 0;
    assert(cameraPoll(&rx, &ry));
    // 切怪脉冲生效中
    assert(rx > 0);

    // 松开按钮：因为此前发生过切怪，松开时不应退出锁定，而是保持锁敌
    cameraHandleLockDragButton(false);
    assert(cameraIsTargetLocked());

    printf("[PASS] testLockDragTargetSwitch (单击锁定/长按甩鼠标切怪完美通过)\n");
}

int main(void) {
    printf("====== 运行相机控制器单元测试 ======\n");
    testCameraInit();
    testGameplayActiveGate();
    testMouseCaptureToggle();
    testUnlimitedAcceleration();
    testInvertY();
    testTouchDelta();
    testDecayAndClamping();
    testConfigHotReload();
    testLockDragTargetSwitch();
    printf("====== 所有单元测试全部通过！ ======\n");
    return 0;
}
