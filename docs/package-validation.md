# Source package validation

Validated on 2026-10-23 against upstream totk-mobile commit
`510d95b8da159c0a7da467ad51b10a5a7c7ac133`, with TotK v1.4.3 inputs.

The source package contains 91 override files and fingerprints 548 unchanged
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
  configuration (4), session logging (3), and graphics regression tests (33) passed.
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

## iOS touch controls update

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
