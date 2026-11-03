# 开发指南

[English](development.md) | **简体中文**

先按 [构建与运行](getting-started.zh-CN.md) 准备环境，再通过 [源码地图](architecture.zh-CN.md) 找到要修改的代码。

## 开始一次修改

```sh
git status --short
mkdir -p work/tmp work/logs
export TMPDIR="$PWD/work/tmp/"
export PYTHONDONTWRITEBYTECODE=1
work/venv/bin/python tools/prepare_modules.py --check
```


构建前先退出游戏。构建、完整测试与运行时着色器编译共用生成目录，请依次执行。不要手工修改 `work/build`。

## 测试

| 目的 | 命令 |
| --- | --- |
| 仓库检查：文档链接、依赖清单、私有路径 | `python3 -m unittest discover -s tests -p test_repository.py -v` |
| 安装测试依赖 | `work/venv/bin/python -m pip install --no-cache-dir -r requirements/test.txt` |
| 完整的 Python、原生与 Metal 测试 | `./script/test.sh` |
| Metal 3 索引、条件判断、计算合批、常量与 MetalFX 的 GPU 回读 | `bash script/test_metal3.sh` |
| 只构建并检查装载 | `./script/build_and_run.sh --load-only` |
| 真实 SDK 的时钟、音频、字体与震动路径 | `./script/build_and_run.sh --time-test`（另有 `--codec-test`、`--font-test`、`--vibration-test`） |

先运行与修改相关的检查；改动共享图形、装载或线程时再跑原生与 Metal 测试。完整的 `test.sh` 需要准备好的本地环境和 Metal 设备，不是无需资源的 CI 任务。完整构建日志在 `work/logs/build.log`。

`test_metal3.sh` 使用旧命令接口，可在 macOS 15、Xcode 16 上运行，检查共享与私有索引缓冲、前序 GPU 写入、非零偏移、索引四边形的每个输出像素、绘制前的常量更新，以及 MetalFX 窗口与 4K 输出。同时验证索引复用与写入失效、CPU 条件值快照、回调执行前的已知时间戳值转发，以及计算任务合批和逐个编码两种路径的输出。完整测试集中的部分测试要求 Metal 4 和 macOS 26；旧系统可使用独立的 Metal 3 检查入口。生成的着色器库可能留有诊断修改：编译成功不代表缓存仍执行原始着色器逻辑。排查整屏调试色时，应先保留修改过的文件，再从对应 SPIR-V 重新生成受影响的着色器。

桌面启动会在 `work/logs/sessions/` 下为每次运行保留独立目录，`native-probe.log`、`performance.tsv` 和 `latest-session` 指向最新一次。目录内包含 `runtime.log`、`performance.tsv`、记录进程组物理内存与系统压力的 `memory.jsonl`，以及记录退出码或信号的 `exit.json`。日常启动默认不限时；命令行构建运行探针仍默认 120 秒。`SIGALRM` 或原生退出码 142 表示配置的超时。仅凭 `SIGKILL` 不能认定内存不足，需结合 macOS DiagnosticReports 和系统日志。如果监控进程也被终止，最终退出报告可能缺失，但已写入的采样仍会保留。

软件检查不能代替实际体验：屏幕上看到的画面、听到的声音和手柄震感都需要在真实设备上确认。

## 脚本输入

`ZELDA_INPUT_SCRIPT=<文件>` 会在着色器准备完成后执行输入脚本；`script/send_input.py` 可通过 FIFO 向运行中的游戏追加指令。脚本由逐行指令组成：

| 指令 | 含义 |
| --- | --- |
| `wait <毫秒>` | 等待 |
| `tap <按键> [毫秒]` / `down <按键>` / `up <按键>` | 轻按、按住或松开宿主键盘按键，名称与键盘映射一致 |
| `mark <文字>` | 在日志中写入标记 |
| `shot <路径>` | 保存截图 |
| `trace <秒> <路径>`、`dump <帧数>` | 记录帧时间线 / 转储图形命令 |
| `fps <30\|60\|90\|120>`、`render <50..200>` | 切换帧率目标 / 内部渲染倍率 |

`script/scenes/first-save.input` 和 `first-save-fast.input` 会走菜单读取第一个存档。脚本不知道你的存档列表，请核对实际到达的位置。不要用脚本推进剧情或存档。

## 环境变量

| 变量 | 作用 |
| --- | --- |
| `ZELDA_TIMEOUT_SECONDS` | N 秒后结束游戏（0 为不限，最大 3600）。`build_and_run.sh` 默认 120，启动入口默认 0 |
| `ZELDA_SETTINGS` | 使用其他设置文件代替 `work/settings.json` |
| `ZELDA_TARGET_FPS`、`ZELDA_METALFX` | 本次运行覆盖已保存的帧率目标与 MetalFX 设置 |
| `ZELDA_SHARED_FONT` | 作为共享系统字体的 TrueType 字体 |
| `ZELDA_SHADER_IR=0`、`ZELDA_SHADER_PREP_ALL=1` | 忽略离线 Metal 库 / 强制完整准备一轮着色器 |
| `ZELDA_DAY_RATE` | 缩放游戏内昼夜时钟（0–16，0 为暂停），不影响帧率与物理 |
| `ZELDA_WINDOW_POINTS` | 窗口尺寸，例如 `1600x900` |
| `ZELDA_RENDER_SCALE` | 实验性渲染倍率（0.5–2），正常游玩时请清除 |
| `ZELDA_INDEX_REUSE=0`、`ZELDA_M3_PREDICATE_REUSE=0`、`ZELDA_M3_DISPATCH_BATCH=0` | 分别关闭索引复用、Metal 3 CPU 条件复用与值转发、串行计算合批，用于受控对照 |
| `ZELDA_THREAD_CPU=1`、`ZELDA_IPC_LOG=1` | 诊断：逐线程 CPU 统计写入 `work/logs/threads.tsv`；IPC 日志 |

另有许多 `ZELDA_*` 和 `DM_*` 开关用于单项优化的 A/B 对照，默认值即已验证的行为；使用前请在 `host/` 中搜索 `getenv` 确认含义。

## 收录代码与依赖

- `core/` 是保持逐字节一致的收录代码：`script/prepare_graphics.py` 的替换锚点、着色器 chain 指纹和模块指纹都依赖它的原始字节。优先修改适配层。如必须修改 `core/` 文件，需同步更新锚点，在 `script/shader-chain-aliases.json` 中加入经证明的别名或重新生成着色器缓存，并重新运行 `prepare.sh`；见 [`core/README.zh-CN.md`](../core/README.zh-CN.md)。
- Python 依赖固定在 [`requirements/`](../requirements/)：`resources.txt` 引用 `runtime.txt`，`test.txt` 引用 `resources.txt`，`ios-transfer.txt` 是独立环境。
- 每处游戏补丁都必须绑定游戏版本、核对原字节，并保留清单校验。

## 基本原则

- **原画质、正常速度。** 优化不得降低分辨率、删减效果、跳过绘制或加速游戏。不能通过关闭校验或伪造成功值让测试通过。
- 源码、注释、界面文本、日志和文件名使用英文。文档以英文为主，并同步维护简体中文版本（`*.zh-CN.md`）。必要的非 ASCII 上游匹配串和 Unicode 测试字符用转义表示，`tests/test_repository.py` 会检查。
- 修改 `host/`、`ios/`、`script/`、`tools/` 中的源码，不要修改 `work/build` 中的生成文件。除非按上述步骤操作，否则保持 `core/` 不变。
- 不要提交游戏文件、密钥、存档、日志或构建产物。`local/` 和 `work/` 已被忽略，见 [仓库结构](repository.zh-CN.md)。不要在已准备好的检出里运行 `git clean`，它会删除你的本地数据。

## 参与贡献

1. 每个提交只做一件事，并写清楚提交说明。用 `git diff --cached --check` 和 `git diff --cached --stat` 检查暂存内容。
2. 新行为要附带测试。图形改动尽量提供 Metal 回读测试。
3. 说明验证了什么、怎样验证（单元测试、构建、画面检查、真机），以及哪些没有验证。
4. 同时更新英文和中文文档。

贡献的代码按本仓库的 [Apache License 2.0](../LICENSE) 授权接收。

### iOS 触控与播片回归检查

触摸摇杆使用 UIKit 跟踪，不触发触感。原生 UI 桥接空闲时，标准动作仍保持可见；上下文变化仅让按钮原地放大。原生宽屏覆盖启动分配和后续分辨率选择；逻辑裁切、视口、UI 画布与玩家相机使用相同比例，呈现层始终等比缩放。逻辑裁切不能超过固定显示纹理；SetResolution 不会扩展 NVN 显示纹理分配。启动后的模式修改在下次打开应用时生效。

`prepare_ios_shader_support.py` 打包全部已验证的离线变体请求、基础阶段及视频程序。手机日志中 `Captured Zelda ... variant` 后出现 `Shader translation is required on the Mac` 表示场景资源缺失，也可能发生在退出播片时。重新生成支持资源并构建应用；手机启动时校验并修复同版本缓存。构建通过能证明覆盖范围与完整性，不能代替完整游玩验收。

通过 `DEVELOPER_DIR=/path/to/Xcode/Contents/Developer bash script/test_ios_touch_controls.sh <已启动模拟器的UDID>` 检查摇杆 SDK 派发。测试使用 UIControl 的真实跟踪选择器，验证重写方法派发、值变更、限幅、松手及取消；只有 C 输入测试无法发现 UIKit 回调名称拼写错误。

## Mac 着色器编译与卡顿

Mac 保留按需编译 MSL 和运行时翻译新客体着色器的能力，不要求生成或打包全部 iOS 离线 IR。启动时仍预热上次记录的着色器与管线。后台预取保留最多 128 个库（源码成本预算 32 MiB），绘制线程直接复用同内容的库，避免再次调用源码编译器；内容变化会使条目失效。

`host/pipeline_cache.inc` 为 Mac 与 iOS 共用的 `MTLBinaryArchive` 适配层。Mac 缓存位于 `work/shaders/pipeline_cache/`（跟随 `DM_MSL_DIR`），按 GPU、系统、翻译链和源码变换契约隔离。缓存缺失或损坏时正常编译，并在低优先级串行队列中捕获、延迟写入新结果。Mac 仅读取和发布不超过 8 MiB 的归档，每次运行最多捕获 128 次管线创建；超过预算时使用系统驱动缓存，避免 macOS 15 加载大归档时产生数 GiB 的临时内存与长时间启动停顿。读取前检查文件大小，捕获请求使用原子计数限流，不在绘制线程等待归档锁。缓存读取不修改调用方的管线描述符。`PIPELINE_CACHE` 日志显示加载、命中、缺失和保存情况；`ZELDA_PIPELINE_CACHE=0` 可关闭，用于对照。

`ZELDA_SHADER_COMPILE_JOBS` 默认 2，限制为 1–4，分别约束启动预热、后台库预取和预测管线队列。预测任务使用 Utility 优先级，绘制马上需要的任务提升到队首。减少并发会延长冷启动或首次编译，但为音频、输入与系统界面留下 CPU 资源。未知的新着色器或新变体仍必须完成编译后才能正确绘制，不能保证首次访问任何场景都没有等待；不会跳过绘制或替换为错误的着色器。

回归测试：`work/venv/bin/python -m unittest discover -s tests -p test_desktop_updates.py -v`，以及 `bash script/test_metal3.sh`。前者使用临时目录测试云同步生命周期、并发上限与任务优先级、库复用的 GPU 输出，以及跨进程管线缓存命中与损坏恢复，不接触实际存档。
