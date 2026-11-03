# ZeldaMoblie_opmizer (ZeldaMobile Optimizer)

[English](#english) | [简体中文](#简体中文)

---

<a name="english"></a>
## English

**ZeldaMoblie_opmizer** is a dedicated overlay optimization suite built upon [`OJZen/totk-mobile`](https://github.com/OJZen/totk-mobile). It is engineered to tailor the gaming experience of *The Legend of Zelda: Tears of the Kingdom* specifically to the natural control habits and performance characteristics of **Apple Mac (macOS)** and **iOS / iPadOS** devices.

Instead of crudely emulating gamepad joysticks or tolerating unoptimized mobile controls, this project hooks directly into the native game engine to deliver PC-grade mouse aiming, ergonomic touch controls, platform-native UI button prompts, high-fidelity Taptic Engine HD Rumble, seamless iCloud cross-save synchronization, and extensive Metal 3 pipeline optimizations.

---

### Key Features

#### 1. Native Mouse SpringArm Camera (Mac Desktop)
* **True SpringArm Control**: Mouse movement translates directly into angular camera deltas inside the engine's native `ExecutePlayerCameraChase`. It directly manipulates the player camera's spring arm rather than simulating a sluggish virtual thumbstick, completely eliminating deadzones, artificial acceleration curves, and steering delays.
* **Intelligent Auto-Fallback**: Seamlessly falls back to native camera handling during bow aiming, climbing, menus, and world map navigation.
* **Seamless Mouse Capture**: Press the backtick/tilde key (`` ` ``) to lock or unlock the mouse cursor without pausing gameplay; `Option + Esc`, window defocus, or opening menus safely release the cursor.
* **Intuitive Quick Swapping**: Hold keys `1`, `2`, or `3` and slide the mouse horizontally to effortlessly cycle through weapons, bows, and materials.
* **Ultrahand Direct Control & Quick Dismantle**: Direct perspective control during Ultrahand manipulation, and a dedicated **`G`** key shortcut to instantly execute a clean 3-swing dismantle (shake) motion.

#### 2. Re-engineered Touch Controls & Swipe Camera (iOS / iPadOS)
* **Say Goodbye to Clunky "Glass Rubbing"**: Action keys (Attack, Sprint, Jump/Glide, Interact, Abilities) are anchored into ergonomic fixed positions that strictly respect hardware safe areas and avoid iOS bottom home gestures.
* **Swipe-to-Look**: Fluid single-finger swiping on the right blank side of the screen turns the camera naturally, allowing simultaneous movement, camera rotation, and combat actions.
* **Dual Virtual Joysticks Mode**: Freely toggle between "Swipe Camera" and traditional "Dual Joysticks" with one tap.
* **Dynamic Feedback**: Buttons automatically enlarge by 10% on active gameplay events and 16% on physical touch down, with no drift in touch centers.

#### 3. Platform-Native In-Game Button Prompts (HUD Adaptation)
* **Dynamic Prompt Switching**: In-game prompts adapt on-the-fly depending on the active input source (Keyboard & Mouse, Touchscreen, or External Gamepad).
* **Mac Keyboard Prompts**: Replaces console button labels with real keyboard keys (`Shift`, `Space`, `E`, `G`, `1/2/3`, `Tab`) rendered directly in Link's ambient `DoCommand` and `GuideKeyIcon` HUDs.
* **Mobile Touch Icons**: Renders intuitive touch glyphs and localized action labels (e.g., "Interact", "Dismantle", "Locked") on iOS.
* **Gamepad Passthrough**: Connecting an external controller immediately restores the authentic console HUD.

#### 4. System Taptic Engine HD Rumble Simulation (iPhone)
* **Core Haptics Integration**: Leverages Apple's native Core Haptics engine and hardware Taptic Engine to faithfully reproduce Nintendo Switch HD Rumble.
* **Low-Amplitude Nonlinear Compensation**: Remaps faint vibration signals (e.g., footsteps, environmental cues, gentle water streams, shrine sensor pulses) with a perceptual compensation curve (mapping `0.1` to `~0.487`), ensuring delicate haptics are physically felt on smartphone linear motors.
* **Touch Filtering**: Touchscreen gestures and virtual joystick movements are strictly excluded from triggering vibrations, eliminating annoying false haptics.

#### 5. Seamless iCloud Cross-Save Synchronization
* **Unified Cross-Platform Sync**: Play on your Mac at home and seamlessly pick up on your iPhone / iPad on the go.
* **Native Security-Scoped Folders**: Utilizes macOS `NSOpenPanel` and iOS `UIDocumentPickerViewController` to bind an iCloud Drive or local "Files" folder, completely bypassing the need for complex personal CloudKit developer containers.
* **Bulletproof Safe-Save Protocol**:
  - Atomic pull on game startup with a 5-second non-blocking safety timeout.
  - Automatic snapshot generation upon app exit or backgrounding.
  - A/B directory atomic rotation, content hashing, and historical version backups to prevent corrupted saves or overwrites.
  - Real-time write-lock conflict detection.

#### 6. Native Metal 4 Architecture & Metal 3 Performance Optimization
* **Native Metal 4 Core**: The project is natively engineered on and optimized for **Metal 4**, leveraging modern unified memory architectures and Apple's latest graphics pipelines for maximum throughput.
* **Extensive Metal 3 Downward Compatibility**: For legacy devices and operating systems (macOS 14/15, iOS 17/18, Xcode 15/16), the project integrates a robust backward-compatible Metal 3 path (`ZELDA_IOS_METAL=3` / `DM_NVN_M3=1`) that resolves critical platform flaws:
  - **Binary Archive & Pipeline Cache Overhaul**: Resolves the severe macOS 15 memory spike issue where expanding massive pipeline caches consumed several gigabytes of RAM and caused prolonged startup freezes. Implements size limits (8 MiB cap) and asynchronous utility queue serialization.
  - **Shader IR Preheating & Metal Toolchain Integration**: Pre-compiles Metal 3.0 / Metal 4 shading language libraries, offline scene variants, and video interfaces (`nv12decode` / `texture_gamma`) on Mac, drastically reducing stuttering during real-time gameplay.
  - **Audio ADPCM Memory Boundary Fix**: Completely resolves random crashes caused by ADPCM chunk alignment and unmapped buffer page overflows on iOS/iPadOS.
  - **Native Pixel-Aligned Widescreen**: Adjusts logical viewports, camera projection, and UI canvases to fit modern bezel-less Apple displays (e.g., iPhone 15/16 Pro, iPad Pro) with exact aspect ratios and zero distortion.

---

### Overlay Architecture & Patching Logic

This repository acts as an **incremental overlay patch** over the upstream base engine. It does not distribute proprietary Nintendo assets or complete game code:

1. **Non-Destructive Patching**: When running `./install.sh <path_to_totk_mobile>`, the script reads `overlay-manifest.json` and `payload-files.txt`, verifies file hashes (SHA-256) and upstream compatibility, and automatically backs up original target files to `.camera_backup/`.
2. **Clean Rollback**: Running `./uninstall.sh <path_to_totk_mobile>` cleanly restores all original files and purges injected modules, leaving user settings, keys, and game saves completely intact.
3. **Module Recompilation**: Because source code patching does not automatically update compiled binaries, target modules must be recompiled after installation.

---

### System & Hardware Requirements

| Platform | Recommended (Native Metal 4) | Downward Compatibility (Metal 3) | Build & Development Toolchain |
| :--- | :--- | :--- | :--- |
| **Mac** | **macOS 26 (Metal 4)**, Apple Silicon | macOS 14 (Sonoma) / macOS 15 (Sequoia) | **Xcode 26 (Recommended)** or Xcode 15/16 (Command Line Tools & Metal Toolchain), Python 3.9+, Homebrew (`cmake`, `pkg-config`, `opus`, `libvpx`) |
| **iOS / iPadOS** | **iOS / iPadOS 26.0+ (Metal 4)** | iOS / iPadOS 17.0+ (`ZELDA_IOS_METAL=3`) | Deployed via Mac with Xcode 26 (or Xcode 16 + iOS 18 SDK) |
| **Game Assets** | Legally dumped *The Legend of Zelda: Tears of the Kingdom* | **v1.4.3** Update (`.nsp` / `.nsz`) | Matched `prod.keys` (minimum 60 GB free disk space recommended) |

---

### Quick Start Guide

#### Step 1: Prepare Upstream Base Project
Clone the supported upstream commit:
```sh
git clone https://github.com/OJZen/totk-mobile.git
git -C totk-mobile checkout 510d95b8da159c0a7da467ad51b10a5a7c7ac133
```

#### Step 2: Apply ZeldaMoblie_opmizer Overlay
Place this repo alongside `totk-mobile`:
```sh
# Directory structure:
#  ├── totk-mobile/
#  └── ZeldaMoblie_opmizer/ (or camera_overlay/)

cd ZeldaMoblie_opmizer
# Verify compatibility
./install.sh ../totk-mobile --check
# Apply overlay patches
./install.sh ../totk-mobile
```

#### Step 3: Compile and Build

**For Mac Desktop:**
```sh
cd ../totk-mobile

# For first-time setup, prepare keys and assets:
./script/prepare.sh
./script/prepare_resources.sh local/keys/prod.keys
./script/prepare_shaders.sh

# Recompile modules and build executable
work/venv/bin/python tools/prepare_modules.py
work/venv/bin/python script/prepare_render_resources.py
./script/build_and_run.sh --load-only

# Launch game
./launch.command
```

**For iOS / iPadOS Device:**
```sh
cd ../totk-mobile
# Configure for Metal 3 and build app package
ZELDA_IOS_METAL=3 ./script/ios.sh build
# Transfer and deploy to connected device
./script/ios.sh install
```

---

<br/>

---

<a name="简体中文"></a>
## 简体中文

**ZeldaMoblie_opmizer**（ZeldaMobile Optimizer）是基于 [`OJZen/totk-mobile`](https://github.com/OJZen/totk-mobile) 开源项目的深度定制化覆盖优化层（Overlay Suite）。本项目致力于彻底解决原项目在移植后只能“硬搬 Switch 手柄逻辑”的痛点，针对 **Mac（macOS）** 与 **iOS / iPadOS** 设备的硬件特性与真实操作习惯进行了全方位的深度改造与体验重构。

通过直接挂钩游戏原生引擎管线，本项目带来了 PC 级原生鼠标 SpringArm 视角控制、现代手游级触控与划屏视角转动、全平台动态按键提示替换、系统级 Taptic Engine 驱动的 HD 震动模拟、iCloud 云存档双端无缝互通，以及针对 Metal 3 / Apple Silicon 的底层渲染与性能优化。

---

### 核心功能亮点

#### 1. 电脑端原生鼠标视角转动（Mac Desktop）
* **原生 SpringArm 悬臂控制**：鼠标输入直接在游戏原生主线程的 `ExecutePlayerCameraChase` 中转化为相机角度增量，直接驱动游戏弹簧悬臂（SpringArm）视角转动。**绝非**粗暴将鼠标位移映射为“模拟右摇杆”，彻底消除了摇杆死区、非线性加速以及转向迟钝感，带来媲美 PC 3A 动作游戏的 1:1 丝滑跟手感。
* **智能视角回退保护**：当进入射箭瞄准、攀爬、地图查看、背包菜单及剧情过场时，系统自动平滑回退至原生相机逻辑，互不干扰。
* **灵活捕获与释放**：单击反引号／波浪号键（`` ` ``，ESC 正下方）即可一键捕获或释放鼠标光标；按下 `Option + Esc`、窗口失焦或呼出系统菜单也会安全释放光标。
* **快捷轮盘选装备**：长按键盘按键 `1`（武器）、`2`（盾牌）、`3`（材料）并左右滑动鼠标即可极速轮播选择，告别繁琐切换。
* **究极手优化与一键拆解**：究极手抓取时提供直连视角，抓取装配物体后按下 **`G`** 键即可自动触发三组高频左右晃动完成快速“拆解”，无需反复剧烈摇晃鼠标。

#### 2. 手机端“搓玻璃”UI 优化与划屏视角（iOS / iPadOS）
* **告别别扭的触屏布局**：深度重构移动端按键九宫格锚点，将攻击、疾跑、跳跃/滑翔、交互与常用能力布置在符合人体工程学的位置，严格避开 iPhone 底部 Home 条与屏幕边缘安全区。
* **单指划屏视角转动**：屏幕右侧空白区域支持单指自然划屏转动视角，轻松实现“左手摇杆跑位 + 右手一边划屏转动视角一边攻击/跳跃”的经典多指协同操作。
* **双摇杆 / 划屏一键切换**：顶部保留快捷切换按钮，用户可根据喜好随时在“右侧划屏模式”与“传统双虚拟摇杆模式”之间即时切换。
* **动态视觉反馈**：按键在事件激活时自动放大 10%，手指按下时放大 16%，触摸中心与基础感应区保持稳定。

#### 3. 目标平台原生按键提示替换（UI Prompt 适配）
* **动态输入源感知**：告别原版游戏混乱的 A/B/X/Y/ZL/ZR 掌机按键认知负担，系统根据当前活跃的输入设备自动切换 HUD 提示：
  - **Mac 键鼠模式**：林克身旁的 `DoCommand` 交互与悬浮指南即时渲染真实电脑键盘按键（如 `Shift` 疾跑、`Space` 跳跃、`E` 交互、`G` 拆解、`1/2/3` 切装备）。
  - **手机触控模式**：按键提示替换为直观的移动端虚拟按钮图标与本地化文本提示（如“交互”、“拆解”、“锁定”）。
  - **实体手柄接入**：接入蓝牙/有线手柄时，瞬间无缝还原原版 Nintendo Switch HUD。

#### 4. iPhone 原生 Taptic Engine 驱动 HD 震动模拟
* **Core Haptics 深度驱动**：全面对接 iOS 系统的 Core Haptics 框架与硬件 Taptic Engine 线性马达，高保真还原 Switch HD Rumble（高精度震动）。
* **低振幅非线性感知补偿**：针对手机横向线性马达的物理特性，开发了专门的振幅感知映射曲线（将游戏微弱的 `0.1` 信号智能放大至 `~0.487`），使细微的脚步、雨水冲刷、风吹草动与神庙感应器脉冲都能清晰被双手感知。
* **手势滤波防误震**：虚拟摇杆拖动与划屏手势严格过滤触感反馈，菜单与游戏切后台自动静音，彻底消除多余劣质震颤。

#### 5. iCloud 云存档跨端无缝同步
* **Mac 与移动端进度互通**：在电脑上探索海拉鲁，出门拿起 iPhone / iPad 即可无缝继续冒险。
* **系统原生安全作用域协议**：通过 macOS `NSOpenPanel` 与 iOS `UIDocumentPickerViewController` 选择“文件”或 iCloud Drive 独立目录，无需配置复杂的个人 CloudKit 开发者凭证。
* **严密的防丢档与防冲突机制**：
  - 游戏启动时执行至多 5 秒原子拉取校验，超时安全跳过，防止卡死。
  - 应用正常退出或切入后台时，自动上传游戏最终提交的存档快照。
  - 采用 A/B 目录原子替换与内容 SHA-256 校验，自动在本地保留多版本备份（`work/cloud-save-backups/`）。
  - 具备并发写锁检测，防止两端同时修改造成数据覆盖。

#### 6. 原生 Metal 4 架构与 Metal 3 兼容性能优化
* **原生 Metal 4 核心开发**：本项目原生基于 **Metal 4** 统一图形计算架构设计开发，默认启用 Metal 4 指令管线与现代统一内存模型，全面释放 Apple 最新一代芯片的极致算力。
* **深度向下兼容优化（Metal 3 模式）**：为处于旧版本生态（macOS 14/15、iOS 17/18、Xcode 15/16）的设备提供了专属的向下兼容层（`ZELDA_IOS_METAL=3` 或 `DM_NVN_M3=1`），并解决了一系列底层核心缺陷：
  - **管线缓存与内存暴涨修复**：彻底修复 macOS 15 在启动时展开庞大管线缓存造成消耗数 GiB 内存并导致系统长时间卡顿的严重缺陷；引入 8 MiB 智能限额、后台 Utility 串行队列捕获与异步预热。
  - **Metal IR 预编译与着色器预热**：支持在 Mac 上通过 Metal Toolchain 离线预编译 Metal 3.0 / Metal 4 着色器 IR，打包 `nv12decode` 与 `texture_gamma` 视频解码接口组合及场景变体，显著消灭真机游玩过程中的编译掉帧。
  - **音频 ADPCM 内存边界越界修复**：彻底修复 iPad / iPhone 运行过程中由于 ADPCM 块对齐与未映射内存页读取导致的随机闪退问题。
  - **原生像素级等比宽屏**：针对全面屏 iPhone（如 iPhone 15/16 Pro）及 iPad 屏幕比例，动态调整渲染视口、相机投射与 UI 原生画布，等比呈现无画面拉伸与无畸变黑边。

---

### 覆盖安装机制与原理

本项目采用**无侵入式增量覆盖包（Overlay Architecture）**设计，不包含也不分发任何商业游戏代码或专有版权资产：

1. **版本清单与安全性校验**：执行 `./install.sh <目标路径>` 时，安装器会基于 `overlay-manifest.json` 与 `payload-files.txt` 对目标项目的文件版本、权限及 SHA-256 进行严格核对。若检测到不兼容的上游版本或未识别的本地变动，会主动终止安装以确保安全。
2. **自动备份机制**：安装过程中涉及被覆盖的文件均会逐一安全备份至目标的 `.camera_backup/` 目录中。
3. **一键干净卸载**：运行 `./uninstall.sh <目标路径>` 可一键完整还原原始代码，并清理增量文件，绝不影响用户的配置文件、游戏密钥（keys）、ROM 资源和个人存档。
4. **重新编译要求**：仅复制源码不会更新已经编译出的机器码二进制，覆盖安装或卸载后必须在目标工程内重新编译模块。

---

### 软硬件环境要求

| 运行平台 | 推荐环境（原生 Metal 4） | 向下兼容支持（Metal 3 模式） | 构建与开发依赖 |
| :--- | :--- | :--- | :--- |
| **Mac 电脑** | **macOS 26**（Apple Silicon） | macOS 14 (Sonoma) / macOS 15 (Sequoia) | **Xcode 26（官方推荐）** 或 Xcode 15/16（含 Command Line Tools 与 Metal Toolchain）、Python 3.9+、Homebrew（`cmake`, `pkg-config`, `opus`, `libvpx`） |
| **iOS / iPadOS** | **iOS / iPadOS 26.0+** | iOS / iPadOS 17.0+（设置 `ZELDA_IOS_METAL=3`） | 需通过 Mac 电脑使用 Xcode 26（或 Xcode 16 + iOS 18 SDK）构建 |
| **游戏与资源** | 合法持有的 Nintendo Switch《塞尔达传说：王国之泪》本体 | **v1.4.3** 更新补丁（`.nsp` / `.nsz`） | 匹配且合法的 `prod.keys`（项目所在磁盘建议保留 60 GB 以上可用空间） |

---

### 快速上手与操作指南

#### 第一步：克隆并检出基准主项目
```sh
git clone https://github.com/OJZen/totk-mobile.git
git -C totk-mobile checkout 510d95b8da159c0a7da467ad51b10a5a7c7ac133
```

#### 第二步：覆盖安装 ZeldaMoblie_opmizer
将本项目目录与 `totk-mobile` 置于同级目录：
```sh
# 目录层级示意：
#  ├── totk-mobile/
#  └── ZeldaMoblie_opmizer/ (即 camera_overlay/)

cd ZeldaMoblie_opmizer
# 预检版本与文件完整性
./install.sh ../totk-mobile --check
# 执行正式覆盖安装
./install.sh ../totk-mobile
```

#### 第三步：构建与启动

**Mac 电脑端：**
```sh
cd ../totk-mobile

# 若为首次配置，请先准备密钥与解包资源：
./script/prepare.sh
./script/prepare_resources.sh local/keys/prod.keys
./script/prepare_shaders.sh

# 重新编译模块并构建目标应用：
work/venv/bin/python tools/prepare_modules.py
work/venv/bin/python script/prepare_render_resources.py
./script/build_and_run.sh --load-only

# 启动游戏：
./launch.command
```

**iOS / iPadOS 移动端：**
```sh
cd ../totk-mobile
# 指定 Metal 3 后端构建 iOS 应用包
ZELDA_IOS_METAL=3 ./script/ios.sh build
# 安装到已连接的 iPhone / iPad 设备
./script/ios.sh install
```

#### 常用快捷键速查（Mac）
- **`~` / `` ` ``（反引号）**：切换鼠标捕获与光标释放
- **`Option + Esc`**：强制释放鼠标指针
- **`1` / `2` / `3` + 鼠标滑动**：快速切换单手武器 / 盾牌 / 材料道具
- **`鼠标中键`**：单击锁定目标；按住拖动切换锁定目标
- **`G` 键**：究极手抓取状态下一键快速拆解组合物体

---

### 鸣谢与开源许可

- 本项目基于 [`OJZen/totk-mobile`](https://github.com/OJZen/totk-mobile) 的卓越工作进行二次适配与优化。
- 更多详细架构与技术设计请参考 [docs/camera-hook.md](docs/camera-hook.md)、[docs/ui-input-prompts.md](docs/ui-input-prompts.md) 与 [docs/saves.zh-CN.md](docs/saves.zh-CN.md)。
- 许可协议遵循 [LICENSE](LICENSE) 与 [NOTICE.md](NOTICE.md)。
