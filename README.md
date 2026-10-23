# TOTK-Mobile 原生玩家相机覆盖层

`totk-mobile` 驱动主游戏；本项目保存需要覆盖到它的相机、输入、UI、Metal 3 渲染修复、Mac/iOS 构建依赖、云存档和手机震动适配。修改应先保存在这里，再通过 `install.sh` 同步。仅支持已校验的 TotK **v1.4.3**。

## 当前实现

鼠标和触屏位移直接转换为角度增量，在游戏线程执行原生 `ExecutePlayerCameraChase` 时消费一次，不经过右摇杆映射，不每帧覆盖相机世界坐标。游戏继续决定角色注视点、距离和后续碰撞处理。

- 普通跟随视角围绕游戏本帧的角色注视点旋转，俯仰有限制。
- 取消触控板双指滚动转视角。鼠标滚轮首次上滚打开盾牌、首次下滚打开武器，继续滚动选择，停止后松开方向键确认；不改变相机距离。
- 独立的原生 PlayerCamera PreUpdate 回调维护生命周期，支持锁敌模式持续按住 ZL。标题、加载、暂停或电影期间不积攒视角输入。
- 松开捕获、切换角色目标/相机、离开游玩状态时丢弃残留输入。
- 锁敌时由游戏控制相机；中键单击切换锁敌，按住中键横向拖动切换目标。
- 原生相机是否可控与系统鼠标捕获分开：攀爬、菜单和过场不会自动释放光标，ESC、窗口失焦或关闭窗口才释放。
- iOS 右侧单指划屏，手势不会从动作按钮或左侧摇杆区域抢走触摸。
- iOS 左侧继续使用系统虚拟摇杆，并单独标记为触摸输入，可与右侧划屏同时操作；外接手柄使用独立输入路径。
- 最近一次主动键鼠、实体手柄或触摸输入决定设备模式。Mac 悬浮指南和游戏内 GuideKeyIcon 按键提示会切换；林克旁边的 DoCommand 保留原始资源，在有效绘制回调中采集键位位置，由宿主层绘制键盘标签。具体覆盖范围见 [ui-input-prompts.md](docs/ui-input-prompts.md)。

iOS 触屏模式隐藏林克旁边的十字提示，复用 `interact_a` 锚点槽位显示原生主动作的本地化文案，并发送实际的 A/B/X/Y/L 按键；对话、拾取、滑翔伞等由游戏当前提示决定。实体手柄操作恢复原版 HUD。配置删除该槽位时保留原版提示。

目前角度注入针对 **Chase 普通追踪分支**。弓瞄、攀爬、骑乘和剧情等其他相机分支保留原生行为，尚未逐个接入鼠标/触屏角度控制。碰撞实际效果和模式转换需要实机验证，单元测试不代表已完成所有场景验收。

## 安装与构建

覆盖层基于 `OJZen/totk-mobile` 的 **`510d95b8da159c0a7da467ad51b10a5a7c7ac133`**，游戏版本为 **v1.4.3**。包含此前在主项目中修复的 Metal 3 渲染、着色器适配、会话日志和 iOS 17 构建依赖，不要求主项目具有本地的四个额外提交。`core/` 保持上游原样。

先下载这个版本的主项目，再把本项目放在同级的 `camera_overlay/` 目录。可以使用 Git：

```sh
git clone https://github.com/OJZen/totk-mobile.git
git -C totk-mobile checkout 510d95b8da159c0a7da467ad51b10a5a7c7ac133
cd camera_overlay
./install.sh ../totk-mobile --check
./install.sh ../totk-mobile
```

也可以下载[对应版本的主项目源码 ZIP](https://github.com/OJZen/totk-mobile/archive/510d95b8da159c0a7da467ad51b10a5a7c7ac133.zip)，解压后将目录路径传给 `install.sh`，无需 Git 或已准备的游戏资源。安装器需要 Python 3.9 及以上；构建环境要求以主项目指南为准。

`payload-files.txt` 列出全部覆盖文件，`overlay-manifest.json` 记录其 SHA-256、权限、上游版本和构建输入。安装前会统一核对；不兼容的上游版本或无法识别的本地改动会停止安装，并指出具体文件。更新本项目源码后，维护者需要运行 `python3 script/update_overlay_manifest.py ../totk-mobile` 更新清单再发布。

安装逐文件备份到目标的 `.camera_backup/`，兼容旧安装器已有备份；重复安装不覆盖最初备份。卸载前同样检查本地改动。配置、游戏资源、密钥和存档不会随安装被覆盖。根目录的 `layout_config.json` 是示例；运行时布局在目标的 `work/layout_config.json` 中配置。

首次使用请按安装后目标项目的 `docs/getting-started.zh-CN.md` 准备 Xcode、Homebrew 依赖和自己的游戏数据。在目标目录执行：

```sh
./script/prepare.sh
./script/prepare_resources.sh local/keys/prod.keys
./script/prepare_shaders.sh
work/venv/bin/python script/prepare_render_resources.py
./script/build_and_run.sh --load-only
```

已经准备好资源的用户，在覆盖后执行：

```sh
cd ../totk-mobile
work/venv/bin/python tools/prepare_modules.py
work/venv/bin/python script/prepare_render_resources.py
./script/build_and_run.sh --load-only
```

`--load-only` 只编译并检查模块装载，不进入游戏。用户自行运行 `./launch.command` 测试。**复制源码不会自动更新已经编译的应用，必须重建。** 缺少 CAM2 相机页或 ZUI143 ABI 4 提示页的旧模块会被拒绝加载。

键鼠提示需要新的 `work/romfs-host.bin`：脚本从用户自己的资源生成字体映射和文本容量补丁，不分发额外字体，不修改 `work/romfs.bin`。Mac 默认启动会检查派生资源；自定义 `ZELDA_ROMFS` 应指向这份文件。iOS 需要重新构建应用，并通过目标的 `script/ios.sh transfer` 更新派生资源。

iOS 默认 Metal 4 / iOS 26；需要 iOS 17 时设置 `ZELDA_IOS_METAL=3`（需要 iPhoneOS 18 及以上 SDK），并按目标项目指南构建。云存档使用系统文件夹选择器，没有新增 CloudKit entitlement；应用签名和原有内存 entitlement 的要求仍由主项目决定。

```sh
# 在 camera_overlay 目录卸载源码覆盖；随后也要重建模块和应用。
./uninstall.sh ../totk-mobile
```

卸载恢复安装前源码并删除原先不存在的文件，保留备份。已运行功能产生的云端快照、书签、本地存档备份和用户配置不随源码卸载删除。

## 操作与配置

Mac 点击游戏窗口捕获鼠标，ESC 或切换窗口释放。标题和菜单仍通过原有键盘/手柄导航。

iOS 默认使用手游模式：左侧虚拟摇杆移动、右侧空白区域单指划屏，位移直接进入原生相机角度引擎；可以边移动边转视角。标题、加载和过场仍遵守原生相机门控。

顶部始终保留“确定／返回／地图／菜单”和模式切换，不依赖 DoCommand、相机活跃状态或布局配置。标题画面和菜单可直接点“确定”。动态交互按钮在没有原生提示时也回退到 A 键，不再因缺少提示而消失。加载完成前禁用游戏按键，加载后恢复。

点击“虚拟手柄”切换到系统双摇杆、A/B/X/Y、肩键和扳机，底部补充方向键、蹲下和望远，按原始手柄语义发送输入。此时右摇杆使用游戏原生视角，关闭右侧划屏和自定义动态按钮，恢复原版交互提示；点“手游模式”切回。选择会保存，切换时释放按键并清除摇杆和待消费视角输入。系统控制器元素依据 [Apple 的虚拟控制器配置](https://developer.apple.com/documentation/gamecontroller/gcvirtualcontroller/configuration/elements)；左右摇杆与补充方向键分别提供。

默认动作标签改为纯文字，按钮之间留出间距并避开顶部工具栏；性能面板默认收起。旧 `work/layout_config.json` 中的 emoji 会在显示时过滤，自定义坐标仍保留。如需采用新的默认排布，可移走该文件或按本项目新的 `layout_config.json` 更新。

滚轮通过原生方向左（盾牌）/方向右（武器）加右摇杆选择装备。第一下确定本次连续滚动的类别，后续反向滚动只改变选择方向；空闲约 0.32 秒且步进处理完后确认。开始导航前留 0.20 秒打开快捷栏，每次步进和归中各至少 0.06 秒，按真实时间计时。快速滚动最多积压 24 步。ESC、其他动作键、手柄活动或失焦会取消滚轮操作。触控板的精细滚动及惯性事件不触发。

`work/camera_config.json` 支持以下配置；`raw_mouse: true` 为线性位移，关闭后才使用可选加速度。

```json
{
  "mouse_sens_x": 3.2,
  "mouse_sens_y": 2.8,
  "accel_factor": 0.035,
  "invert_y": false,
  "raw_mouse": true
}
```

动态动作按钮仍使用 `layout_config.json` 和 Unity 风格九宫格锚点，以安全区中的锚点为基准、偏移定位按钮中心，尺寸单位为 pt。高亮放大后也限制在安全区内。省略 `highlight_event` 或指定 `none` 均不高亮；省略 `visible_event` 表示常驻。无效尺寸、未知映射或事件名会保留上一份配置。iOS 在启动监控时读取已有配置，并在保存或原子替换后重载。重建/隐藏按钮时释放按下时记录的实际按键。

`in_air`、`climbing`、`swimming`、`combat`、`ultrahand` 已连接游戏维护的 CtrlGuide 状态；`interact` 读取真实 DoCommand 黑板中的 A 键提示。iOS 每帧驱动显隐和高亮。退出有效游玩状态或相应原生 UI 帧过期后清除动态事件。这些是控制提示状态，具体含义、版本地址与覆盖范围见 [ui-input-prompts.md](docs/ui-input-prompts.md)。触屏位移沿用当前原生玩家相机角度引擎。

## 云存档与 iPhone 震动

iOS 启动页的 **Link Cloud Folder** 让用户选择“文件”中的目录，并保存安全作用域书签。应用无需 CloudKit 容器或自行收集账号凭据；选择 iCloud Drive 时，用户设备仍需登录 iCloud。其他文件提供程序也可通过系统选择器授权。详见 [Apple 的目录访问说明](https://developer.apple.com/documentation/uikit/providing-access-to-directories)。

启动时最多等待 5 秒拉取；取消或超时后，后台准备任务不能再发布到本地。只有完整校验的目录才能原子替换本地存档，原本地版本保存在目标 `work/cloud-save-backups/`。正在运行的游戏持有存档锁，拉取不会覆盖已挂载的存档。新设备已有不同的非空存档，或本地与云端同时发生修改时，会保留两边并跳过同步；需退出应用后自行备份并解决冲突。

切到后台时只上传游戏**最后一次提交**的存档快照，不把未保存的内存进度当作存档。使用文件协调器、内容哈希和 A/B 两个目录；快照验证成功后才更新元数据。失败保留当前云端版本，不按上传时间判断谁的游戏进度更新。后台执行时限由 iOS 决定，强制退出不能保证触发上传；文件提供程序完成网络上传也可能晚于本地写入。请等待设备云盘同步完成后再切换设备，避免同时游玩；这套目录协议不能提供跨设备的服务端事务。

手机震动把左右声部合并到本机支持的 Core Haptics 马达；触屏与实体手柄输入动态选择输出设备。手机使用 1 秒循环事件，避免使用手柄专用的无限时长；支持引擎重置、错误重建与退避、失焦停止和震动开关。Xbox 的强度补偿只作用于 Xbox 输出。频段到触感的映射是近似值，实际强度和体感仍需真机测试。

## 验证

```sh
# 先安装覆盖层；目标 venv 的测试依赖见 requirements/test.txt。
./run_tests.sh ../totk-mobile
# 独立渲染回归测试（在目标目录，不进入游戏）：
bash script/test_metal3.sh
```

覆盖安装检查包含预检、旧备份迁移、重复安装、卸载往返、权限、局部写入失败回滚和链接拒绝。控制器、布局、滚轮、原生 UI、存档快照、同步冲突与超时、震动路由测试使用 ASan/UBSan；ARM64 跳板用 Unicorn 执行，另有字体资源和构建配置检查。同步测试只用临时模拟目录，不访问真实云盘或真实存档。

构建和离线测试不能代替真机验收。最新 DoCommand 绘制桥接、真实文件提供程序下载/联网同步和 iPhone 马达手感仍由用户实机验证。干净安装与构建记录见 [package-validation.md](docs/package-validation.md)。

实机定位相机问题时运行 `ZELDA_CAMERA_TRACE=1 ./launch.command`，在目标 `work/logs/native-probe.log` 中查看 `[CAMERA_HOOK]`。逆向依据和地址说明见 [camera-hook.md](docs/camera-hook.md)，UI 接入说明见 [ui-input-prompts.md](docs/ui-input-prompts.md)。

来源与许可证见 [NOTICE.md](NOTICE.md) 和 [LICENSE](LICENSE)。
