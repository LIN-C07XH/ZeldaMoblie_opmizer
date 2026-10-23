# Source package validation

Validated on 2026-10-23 against upstream totk-mobile commit
`510d95b8da159c0a7da467ad51b10a5a7c7ac133`, with TotK v1.4.3 inputs.

The source package contains 95 override files and fingerprints 547 unchanged
upstream build inputs. It includes the local Metal 3 commits and subsequent
camera, UI, cloud-save, phone-haptics and iOS compatibility changes. No game
resources, compiled application, credentials or saves are distributed.

Validation used a fresh `git archive` of the upstream base, without its `.git`
directory or the four local runtime commits. Only the dependency venv and original
ExeFS inputs were shared; generated modules and build outputs were independent.

- Installation and repeat installation matched the payload bytes and modes.
- Uninstall restored all 692 upstream files and modes and removed newly added
  payload files. Other upstream files remained unchanged.
- Seven installer tests passed: preflight, source fingerprints, legacy backups,
  reinstall/uninstall, local edits, rollback after a failed write, and links.
- The camera, layout, equipment wheel, native UI, filesystem, vibration,
  controller-haptics, phone routing and mock cloud-save native tests passed.
- Font preparation (3), camera trampolines (2), native UI trampolines (4), iOS
  configuration (4), session logging (3), and graphics regression tests (34) passed.
- Standalone Metal 3 index, predicate, dispatch batching, uniform and MetalFX
  tests passed with Metal validation enabled.
- All three guest modules were regenerated from original ExeFS, then the Mac app
  compiled and completed `--load-only`: `LOADED modules=3 unresolved_imports=8`.
  The eight existing unresolved imports are the `glslc*` entries reported by the
  loader; this check does not claim every runtime graphics path is implemented.
- The changed iOS frontend, cloud-save and gamepad Objective-C sources passed
  iOS 17 syntax and API availability checks with the iPhoneOS 18.2 SDK.

The mock cloud-save tests cover exact snapshots and deletions, pre-sync backups,
conflicts, invalid metadata and paths, hashes, links, mounted-save locking,
manual cancellation and a provider request completing after the timeout. They
use temporary local directories rather than a real cloud account.

No game session was launched. No signed iOS build, installation or device test was
performed. The latest DoCommand display behavior, real File Provider downloads
and network synchronization, simultaneous-device behavior, and physical phone
haptics still require user testing. A successful local provider write does not
establish that the provider has uploaded it to its server.

Reproduce the checks after installing into the supported upstream source:

```sh
./run_tests.sh ../totk-mobile
cd ../totk-mobile
work/venv/bin/python script/prepare_graphics.py
work/venv/bin/python -m unittest discover -s tests -p test_graphics.py -v
bash script/test_metal3.sh
work/venv/bin/python tools/prepare_modules.py
./script/build_and_run.sh --load-only
```

The native tests require Apple development tools and the target project's
headers. Unicorn needs JIT permission; Metal tests need access to a GPU; mock
provider tests need the system file coordination service.

## Initial iOS touch controls update (superseded interaction placement)

The title-screen confirm action now has a persistent A-button path independent
of native interaction prompts, camera state, input-device detection and custom
layouts. Confirm, Back, Map, Menu and the mode switch remain available after
loading. The dynamic primary action also falls back to A when a native prompt
is absent. Before gameplay, unrelated action buttons are hidden.

Mobile mode keeps the left virtual stick and passes right-side one-finger pan
through the native camera angle engine, with simultaneous movement permitted.
The persistent mode switch enables the system's two virtual sticks, face buttons,
shoulders and triggers. Supplemental text buttons provide the D-pad and stick
clicks. Fallback output is raw touch-controller HID input; panning and the dynamic
interaction replacement are disabled until switching back. Mode changes clear
held input; stale asynchronous controller connections are discarded.

Text labels replace pictographs, including when loading an old user layout.
Default positions were checked for non-overlap with highlighted buttons on
667x375, 812x375, 874x402 and 1024x768 point surfaces, including safe areas. The
performance panel starts collapsed and its expanded width respects the toolbar.

The actual input implementation passed ASan/UBSan tests for title confirmation
without camera/UI state, four touch axes, short press consumption, independent
button sources, invalid axis values, clearing and physical controller priority.
The existing camera tests also verify touch angle input without mouse capture.
The complete overlay test runner passed after these changes.

The iOS 17 / Metal 3 app compiled and linked with the iPhoneOS 18.2 SDK and signing
disabled, in a separate build directory using existing local codec dependencies.
Xcode's missing SDK stat cache was bypassed for this validation with
`CLANG_ENABLE_STAT_CACHE=NO SDK_STAT_CACHE_ENABLE=NO`; no shared cache was deleted.
The unsigned check app is not a signed deployable build. No app was installed or
launched. Physical device layout, simultaneous gestures and controller switching
remain for user acceptance testing.

## Mobile interaction layout and display update

The attack button is now an 80-point primary action moved inward from the screen
edge. Jump and sprint are on its right; secondary actions sit to its left. Real
native interactions use an independent middle-right slot. Without a valid prompt,
that slot is hidden and Confirm stays in the top toolbar, including on the title
screen. Prompt expiration/change releases the old button. No UIKit impact
feedback generator remains in the frontend; game vibration events retain their
existing bridge.

The exact geometry/mappings of the two shipped presets are migrated in memory;
custom layouts and labels are retained and no user file is overwritten. Spacing
checks include a 568x320 small landscape surface and highlighted buttons.

The original renderer fitted a 16:9 image inside the full drawable, independently
of safe areas. Both Metal presenters now use a shared viewport helper. iOS defaults
to equal-scale fill/crop with a persistent Fit switch; Mac retains Fit. Host text
prompts follow the same image transform. Rendering uses full window bounds while
controls retain safe-area margins. Fit preserves the whole original image; Fill
crops excess image and does not increase game FOV.

The display geometry tests include the iPhone 17 Pro rectangular 2622x1206 size
([Apple specifications](https://support.apple.com/en-gb/125090)), tablets and wider
surfaces, point/pixel scaling, centering and invalid input. For a 1920x1080 image
on that phone, Fit produces a 2144x1206 image at x=239; Fill produces 2622x1475 at
y=-135. These are calculated viewport results, not measured device screenshots.

The overlay test runner, all 34 graphics regression tests and the independent
Metal 3 GPU tests passed with this update. The iOS 17 / Metal 3 frontend and the
adapted NVN backend were both recompiled and linked in the unsigned app. The
current Mac app compiled and completed load-only with three modules. Neither
app was installed on a device or started into a game session for this update.
Physical phone gesture, layout and screen-edge acceptance remain with the user.

2026-10-23 输入模式与 iOS 视频修复：滚轮入口停用；装备键按住与非 Chase 状态回退右摇杆，手机装备键支持拖动，普通 ESC 保持捕获。原生 AttentionSender 状态驱动锁定保持与解除。手机闪退记录由已有无线配对的 devicectl 只读取得：20:32 崩溃 UUID 与本地 Metal 4 构建匹配，pso_predict_rest.cold.2 主动 abort；boot.log 明确记录缺失组合 14ba7fd9ad1cc9ac 后尝试 Mac 转换器。构建脚本现在随 iOS 应用补充该组合及片元阶段的 MSL/反射数据，不分发游戏资源进源码仓库。

验证结果：覆盖包完整回归通过，包含 ASan/UBSan 输入/生命周期与 Unicorn 指令锚点测试；34 项图形测试在允许访问 Metal 的环境通过。Mac `--load-only` 构建装载通过（3 模块，既有 8 个 glslc 导入未解析）。Xcode 26.2 / iOS 26 / Metal 4 的独立无签名应用构建通过，包内 4 份视频支持资源共 7380 字节逐字节匹配转换缓存。未安装或启动手机应用，未启动游戏；场景验收与手感仍由用户测试。

## UltraHand camera and control setting

CAM3 adds a third trampoline at the shared Target update, guarded by the exact UltraHand vtable and a live owner/component walk with null checks. Unicorn exercises enabled/disabled callbacks, a different Target subclass, missing owner components and GPR/SIMD/flags restoration. ASan/UBSan checks native parameter bounds, persistent angle accumulators, one-shot consumption, camera transitions, fallback without invoking the new guest parameter API and R-held native input. The complete overlay regression runner passed.

Mac build and load-only validation passed with 3 modules and the existing 8 unresolved glslc imports. The iOS 26 / Metal 4 unsigned check build with Xcode 26.2 passed, including the new long-press control menu and contextual rotation-button code. Modules were regenerated for CAM3. No game or phone application was started; actual UltraHand focus, collision and touch rotation remain for user testing. Existing unrelated uncommitted rendering edits were preserved.
