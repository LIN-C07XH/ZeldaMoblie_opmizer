# TOTK-Mobile 相机无限加速度控制模块 (Camera Overlay)

本模块专为解决 《塞尔达传说：王国之泪》在移植到 Apple Silicon macOS 与 iPadOS/iOS 平台后，原生视角控制过于原始、生硬且转速受限的问题。

---

## 核心痛点与解决原理

1. **原版问题**：
   * **macOS 桌面端**：原版仅支持 `IJKL` 键盘按键与物理手柄控制视角，没有鼠标视角控制；若仅做简单的鼠标映射到右摇杆，由于原版右摇杆存在硬编码的角速度上限（Capped Angular Velocity），快速甩动鼠标时会产生严重的“撞墙感”和滞后。
   * **iOS / iPad 移动端**：原版直接弹出了一个固定右侧虚拟摇杆，在触屏上搓小圆圈转动视角极其不灵活。
2. **无限加速度算法（Unlimited Acceleration Dynamics）**：
   * **非线性冲激模型**：微弱移动时保持像素级精准微调（适合弓箭瞄准）；随着鼠标/触控划屏瞬时速度提升，动态加速度因子 $\text{accel} = 1.0 + \alpha \times \text{speed}$ 介入，实现快甩瞬间完成 180° / 360° 转身，彻底摆脱传统手柄的转速上限。
   * **零延迟衰减阻尼**：位移停止时立即按指数级衰减清零，杜绝漂移与回弹果冻感。

---

## 目录结构

本文件夹采用与 `totk-mobile` 源码完全对应的目录镜像结构，支持直接覆盖安装：

```text
camera_overlay/
├── host/
│   ├── camera_controller.h    # 相机动力学与加速度接口定义
│   ├── camera_controller.inc  # 相机动力学核心算法实现（线程安全）
│   ├── action_layout.h        # Unity 风格 UI 锚点与动态事件驱动按键接口
│   ├── action_layout.inc      # 动态按键与手游级黄金右手人机工学实现
│   ├── screen_aspect.h        # iPad 4:3 到 iPhone 19.5:9 全机型屏幕比例适配
│   ├── screen_aspect.inc      # 视口与黑边消除算法实现
│   ├── action_file_watcher.h  # 内核级 VNODE 文件系统监控（0 轮询、0 每帧解析）
│   ├── action_file_watcher.inc# GCD dispatch_source_vnode 异步钩子实现
│   ├── input.h                # 扩展了相机控制与触控手柄注入的输入头文件
│   ├── input.c                # 覆盖层：将相机瞬时动力学与触控按钮注入客体 HID 采样管线
│   └── graphics.m             # 覆盖层：macOS 鼠标光标捕获、ESC 释放与相对位移监听
├── ios/
│   └── main.m                 # 覆盖层：iPad / iPhone 全屏幕适配、手势与 Unity 锚点原生按键
├── tests/
│   ├── test_camera_controller.c # 相机控制器动力学与配置重载单测
│   └── test_action_layout.c     # 屏幕宽高比计算与 Unity 动态布局单测
├── layout_config.json         # 手游按键布局配置模板（可直接修改热更新）
├── install.sh                 # 一键自动备份并覆盖安装脚本
├── uninstall.sh               # 一键自动还原原版文件脚本
└── README.md
```

---

## 快速使用

### 1. 覆盖安装
在当前目录运行安装脚本即可，脚本会自动创建 `.camera_backup` 备份原版文件，并安全完成覆写：

```bash
cd camera_overlay
./install.sh
```

### 2. 操作说明
* **macOS 桌面端**：
  * **捕获光标**：鼠标左键或右键点击游戏窗口内部，光标自动锁定并隐藏，进入真正的 PC 动作游戏视角控制模式。
  * **释放光标**：随时按下 `ESC` 键，或切换应用窗口，光标自动恢复显示并解锁。
  * **智能锁敌**：鼠标中键（滚轮按下）切换锁定注视（ZL Toggle）。
  * **快速切怪**：鼠标滚轮向上/向下滚动，瞬间切换相邻目标。
  * **移动与操作**：`WASD` 移动，鼠标自由环顾，原有键盘映射与 Xbox 手柄热插拔完全不受影响。
* **iOS / iPad 移动端**：
  * **划屏转动**：右侧虚拟摇杆已自动精简，**整个屏幕右半侧任意区域均可直接滑动旋转视角**，与主流 3D 手游的操作习惯保持完全一致。
  * **手游锁敌按钮 (⌖ 锁)**：放置于攻击键左侧，点击切换锁定目标，锁定激活时呈现金黄色高亮，并伴随 Taptic Engine 触感振动。
  * **手游切怪按钮 (⇄ 切)**：放置于锁定按钮上方，点击瞬间将注视焦点切换至下一只怪物。

### 3. Unity 锚点按键布局与动态热更新
在应用的 `Documents/work/layout_config.json` 中配置按键布局，**内核级文件系统钩子实时监听，保存即热生效（零每帧轮询开销）**：
* 布局借鉴 **Unity UI 锚点体系**（`topLeft`, `bottomRight`, `center` 等），配合 `(offset_x, offset_y)` 实现全屏幕比例绝对人机工学自适应。
* 默认已采用**王者荣耀 / 和平精英 / 原神黄金右手按键布局**：
  * **Y 键（普攻）**：72pt 超大圆按键，置于右下角黄金平 A 区；
  * **B 键（闪避冲刺）**：56pt，位于普攻左下方；
  * **X 键（跳跃）**：52pt，位于普攻正上方偏右（吃鸡跳跃翻墙位）；
  * **A 键（拾取互动）**：50pt，靠近交互物时动态放大并泛起光晕；
  * **ZR 键（射箭）**：48pt，位于跳跃左上方；
  * **ZL 键（锁敌）**：46pt，点击切换注视锁定；
  * **切怪键**：38pt，位于锁敌上方，瞬间切怪；
  * **动态滑翔伞（Glider）**：仅在滞空/高空下落（`in_air`）时从跳跃键上方淡入弹出，点击直接开伞！
* **全设备屏幕自适应**：原生适配 iPad 4:3 传统/Pro、iPad 11" 1.43:1 以及 iPhone 15/16/17 Pro/Pro Max 19.5:9 屏幕。

### 4. 免重启动态调参 (Hot-Reload)
在 iOS 或 macOS 端，只需在应用的 `Documents/work/camera_config.json` 写入以下 JSON 配置即可在运行中**零重启即时热生效**：
```json
{
  "mouse_sens_x": 1.5,
  "mouse_sens_y": 1.5,
  "accel_factor": 0.04,
  "decay_factor": 0.70,
  "invert_y": false,
  "raw_mouse": true
}
```

### 5. 一键还原
若调试完毕希望回到纯原版 `totk-mobile` 状态，只需运行：

```bash
cd camera_overlay
./uninstall.sh
```

---

## 参数调优（可选）

如需微调手感，可直接编辑 `host/camera_controller.inc` 顶部的宏定义：

| 宏名称 | 默认值 | 作用说明 |
| :--- | :--- | :--- |
| `DEFAULT_SENSITIVITY_X` | `1.25f` | 水平基础灵敏度 |
| `DEFAULT_SENSITIVITY_Y` | `1.25f` | 垂直基础灵敏度 |
| `DEFAULT_ACCEL_FACTOR` | `0.035f` | 无限加速度放大系数（越大则快甩时光标转得越快） |
| `DEFAULT_DECAY_FACTOR` | `0.72f` | 停手后的速度衰减率（越小则停下越坚决） |
