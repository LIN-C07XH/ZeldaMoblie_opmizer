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
    // 方案 A 模式下，手柄摇杆永远为 0
    assert(!cameraPoll(&rx, &ry));
    assert(rx == 0 && ry == 0);
    printf("[PASS] testCameraInit\n");
}

static void testGameplayActiveGate(void) {
    cameraInit();
    cameraSetGameplayActive(false);
    cameraSetMouseCaptured(true);

    double eyeX = 0.0, eyeY = 12.0, eyeZ = 5.0;
    double lookAtX = 0.0, lookAtY = 10.0, lookAtZ = 0.0;

    // 未进入游戏时，鼠标捕获处于未激活态
    cameraAddMouseDelta(50.0f, 50.0f);
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);
    // 坐标未被修改
    assert(eyeX == 0.0 && eyeY == 12.0 && eyeZ == 5.0);

    // 进入游戏并捕获鼠标后，SpringArm 正常响应
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);
    cameraAddMouseDelta(50.0f, 0.0f);
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);
    assert(eyeX != 0.0);

    printf("[PASS] testGameplayActiveGate\n");
}

static void testSpringArmInstantResponse(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);

    double eyeX = 0.0, eyeY = 12.0, eyeZ = 5.0;
    double lookAtX = 0.0, lookAtY = 10.0, lookAtZ = 0.0;

    // 初值对齐
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);

    float yawBefore = 0.0f, pitchBefore = 0.0f, dist = 0.0f;
    cameraGetAngles(&yawBefore, &pitchBefore, &dist);

    // 1. 移动鼠标 10 像素：瞬间产生弧度变化
    cameraAddMouseDelta(10.0f, 0.0f);
    float yawAfter1 = 0.0f, pitchAfter1 = 0.0f;
    cameraGetAngles(&yawAfter1, &pitchAfter1, &dist);
    float diff1 = fabsf(yawAfter1 - yawBefore);
    assert(diff1 > 0.05f);

    // 2. 移动鼠标 50 像素 (5 倍位移)：角度变化严格为 5 倍 (线性 1:1 跟手，无非线性迟滞)
    cameraAddMouseDelta(50.0f, 0.0f);
    float yawAfter2 = 0.0f, pitchAfter2 = 0.0f;
    cameraGetAngles(&yawAfter2, &pitchAfter2, &dist);
    float diff2 = fabsf(yawAfter2 - yawAfter1);
    assert(fabsf(diff2 - diff1 * 5.0f) < 0.001f);

    printf("[PASS] testSpringArmInstantResponse (1:1 绝对零时延无蓄水池响应)\n");
}

static void testSpringArmZeroLatencyStop(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);

    double eyeX = 0.0, eyeY = 12.0, eyeZ = 5.0;
    double lookAtX = 0.0, lookAtY = 10.0, lookAtZ = 0.0;
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);

    // 快速甩动 500 像素
    cameraAddMouseDelta(500.0f, -200.0f);
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);

    float stoppedYaw = 0.0f, stoppedPitch = 0.0f, dist = 0.0f;
    cameraGetAngles(&stoppedYaw, &stoppedPitch, &dist);

    // 鼠标位移停止后，在接下来的 10 帧循环中，角度必须绝对静止，零漂移、零果冻感！
    for (int frame = 0; frame < 10; frame++) {
        cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);
        float curYaw = 0.0f, curPitch = 0.0f;
        cameraGetAngles(&curYaw, &curPitch, &dist);
        assert(curYaw == stoppedYaw);
        assert(curPitch == stoppedPitch);
    }

    printf("[PASS] testSpringArmZeroLatencyStop (停手瞬间绝对咬死静止，0 漂移)\n");
}

static void testSpringArmPitchClamping(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);

    double eyeX = 0.0, eyeY = 12.0, eyeZ = 5.0;
    double lookAtX = 0.0, lookAtY = 10.0, lookAtZ = 0.0;
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);

    // 向上狂推鼠标 5000 像素（抬头到极限）
    cameraAddMouseDelta(0.0f, -5000.0f);
    float yaw = 0.0f, pitch = 0.0f, dist = 0.0f;
    cameraGetAngles(&yaw, &pitch, &dist);
    assert(fabsf(pitch - 1.45f) < 0.001f); // 严格被钳位在 1.45 rad，绝不翻跟斗

    // 向下狂推鼠标 10000 像素（俯视低头到极限）
    cameraAddMouseDelta(0.0f, 10000.0f);
    cameraGetAngles(&yaw, &pitch, &dist);
    assert(fabsf(pitch - (-1.45f)) < 0.001f); // 严格被钳位在 -1.45 rad

    printf("[PASS] testSpringArmPitchClamping (上下俯仰角严格钳位)\n");
}

static void testSpringArmCollisionSafeDistance(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);

    double eyeX = 0.0, eyeY = 12.0, eyeZ = 5.0;
    double lookAtX = 0.0, lookAtY = 10.0, lookAtZ = 0.0;
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);

    float yaw = 0.0f, pitch = 0.0f, initialDist = 0.0f;
    cameraGetAngles(&yaw, &pitch, &initialDist);
    assert(fabsf(initialDist - 5.385f) < 0.05f);

    // 当物理碰撞检测到墙体（安全距离缩窄到 1.8m）时，相机必须收缩防止穿墙
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, 1.8f);
    double curDist = sqrt((eyeX - lookAtX) * (eyeX - lookAtX) +
                          (eyeY - lookAtY) * (eyeY - lookAtY) +
                          (eyeZ - lookAtZ) * (eyeZ - lookAtZ));
    assert(fabs(curDist - 1.8) < 0.05);

    // 走出障碍物，安全距离恢复（-1.0f），相机自动拉回初始目标臂长
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);
    curDist = sqrt((eyeX - lookAtX) * (eyeX - lookAtX) +
                   (eyeY - lookAtY) * (eyeY - lookAtY) +
                   (eyeZ - lookAtZ) * (eyeZ - lookAtZ));
    assert(fabs(curDist - 5.385) < 0.05);

    printf("[PASS] testSpringArmCollisionSafeDistance (防穿墙物理碰撞距离收缩与恢复完美通过)\n");
}

static void testSpringArmZoomInOut(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);

    double eyeX = 0.0, eyeY = 12.0, eyeZ = 5.0;
    double lookAtX = 0.0, lookAtY = 10.0, lookAtZ = 0.0;
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);

    float yaw = 0.0f, pitch = 0.0f, baseDist = 0.0f;
    cameraGetAngles(&yaw, &pitch, &baseDist);

    // 滚轮拉远 3 米
    cameraAddZoom(3.0f);
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);
    double curDist = sqrt((eyeX - lookAtX) * (eyeX - lookAtX) +
                          (eyeY - lookAtY) * (eyeY - lookAtY) +
                          (eyeZ - lookAtZ) * (eyeZ - lookAtZ));
    assert(fabs(curDist - (baseDist + 3.0f)) < 0.05);

    // 滚轮拉近 5 米
    cameraAddZoom(-5.0f);
    cameraUpdateSpringArm(&eyeX, &eyeY, &eyeZ, lookAtX, lookAtY, lookAtZ, -1.0f);
    curDist = sqrt((eyeX - lookAtX) * (eyeX - lookAtX) +
                   (eyeY - lookAtY) * (eyeY - lookAtY) +
                   (eyeZ - lookAtZ) * (eyeZ - lookAtZ));
    assert(fabs(curDist - (baseDist - 2.0f)) < 0.05);

    printf("[PASS] testSpringArmZoomInOut (滚轮拉近拉远臂长)\n");
}

static void testLockDragTargetSwitch(void) {
    cameraInit();
    cameraSetGameplayActive(true);
    cameraSetMouseCaptured(true);
    assert(!cameraIsTargetLocked());

    // 单击锁定
    cameraHandleLockDragButton(true);
    cameraHandleLockDragButton(false);
    assert(cameraIsTargetLocked());

    // 再次单击解锁
    cameraHandleLockDragButton(true);
    cameraHandleLockDragButton(false);
    assert(!cameraIsTargetLocked());

    // 长按 + 甩动鼠标向右切怪
    cameraHandleLockDragButton(true);
    cameraFeedMouseMovementForTargetSwitch(35.0f);
    assert(cameraIsTargetLocked());

    int32_t rx = 0, ry = 0;
    assert(cameraPoll(&rx, &ry));
    assert(rx > 0); // 瞬时切怪脉冲

    cameraHandleLockDragButton(false);
    assert(cameraIsTargetLocked());

    printf("[PASS] testLockDragTargetSwitch\n");
}

int main(void) {
    printf("====== 运行纯净方案 A SpringArm 单元测试 ======\n");
    testCameraInit();
    testGameplayActiveGate();
    testSpringArmInstantResponse();
    testSpringArmZeroLatencyStop();
    testSpringArmPitchClamping();
    testSpringArmCollisionSafeDistance();
    testSpringArmZoomInOut();
    testLockDragTargetSwitch();
    printf("====== 所有纯净方案 A 单元测试全部通过！ ======\n");
    return 0;
}
