#pragma once
#include <stdint.h>
#include <stdbool.h>

/// 相机灵敏度与加速度配置结构体
typedef struct {
    float mouseSensitivityX;
    float mouseSensitivityY;
    float accelerationFactor;
    float decayFactor;
    bool invertY;
    bool rawMouseMode;
    bool nativeUltrahand;
} CameraConfig;

/// 初始化相机控制器
void cameraInit(void);

/// 重置相机当前累积量与速度
void cameraReset(void);

/// 接收来自桌面鼠标的位移增量
void cameraAddMouseDelta(float deltaX, float deltaY);

/// 接收来自移动端触屏划动的位移增量
void cameraAddTouchDelta(float deltaX, float deltaY);
/// 装备选择等输入由游戏原生右摇杆处理。
void cameraSetRightStickMode(bool enabled);

/// 在输入采样时轮询相机输出，如果有主动输入则写入 outRx, outRy 并返回 true
bool cameraPoll(int32_t *outRx, int32_t *outRy);

/// 设置鼠标光标捕获锁定状态
void cameraSetMouseCaptured(bool captured);

/// 获取当前鼠标光标捕获状态
bool cameraIsMouseCaptured(void);

/// 切换鼠标光标捕获状态
void cameraToggleMouseCapture(void);

/// 切换目标锁定状态 (ZL Lock-on Toggle)
void cameraToggleTargetLock(void);

/// 设置目标锁定状态
void cameraSetTargetLock(bool locked);

/// 查询当前是否处于目标锁定状态
bool cameraIsTargetLocked(void);

/// 触发切换注视焦点目标 (+1 为下一个/向右，-1 为上一个/向左)
void cameraSwitchTarget(int direction);

/// 锁定/切怪复合按键事件处理 (pressed: true 为按下, false 为松开)
/// 单击松开切换锁定/解锁状态；若在按住期间左右甩动鼠标，则直接向左/向右切换焦点目标
void cameraHandleLockDragButton(bool pressed);

/// 检查并向锁定切怪状态机送入鼠标横向移动位移
void cameraFeedMouseMovementForTargetSwitch(float deltaX);

/// 查询当前锁敌脉冲附加按键掩码（例如自动按下 ZL）
uint64_t cameraGetTargetLockButtons(void);

/// 获取当前相机配置
CameraConfig cameraGetConfig(void);

/// 更新相机配置
void cameraSetConfig(const CameraConfig *newConfig);

/// 检查并在配置文件变更时自动热重载 (支持 Documents/work/camera_config.json)
/// @param configPath 配置文件绝对路径
/// @return 发生重载返回 true，文件未变或读取失败返回 false
bool cameraCheckAndReloadConfig(const char *configPath);

/// 设置当前是否处于真正进入存档的可控 3D 游戏游玩状态 (排除标题主界面、载入中、暂停菜单)
void cameraSetGameplayActive(bool active);

/// 查询当前是否真正处于可控 3D 游玩状态中
bool cameraIsGameplayActive(void);
bool cameraIsUltrahandActive(void);

/// 记录物理手柄正在输入（手柄最高优先级机制，避免键鼠与手柄冲突）
void cameraNoteGamepadActivity(void);

/// 绑定 CAM4 原生玩家相机回调页 (main+0x3ae4040)
bool cameraBindSharedState(void *statePtr);

/// 每帧清除失效的玩家相机输入；不会覆盖客体坐标。
void cameraFrameSync(void);

/// 最近一次原生玩家相机的角度（弧度）与距离；只用于观测。
void cameraGetAngles(float *outYaw, float *outPitch, float *outDistance);
