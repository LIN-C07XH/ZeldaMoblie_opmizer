# Development guide

**English** | [简体中文](development.zh-CN.md)

Set up the environment first with [Building and running](getting-started.md), and read the [Source map](architecture.md) to find the code you need.

## Starting a change

```sh
git status --short
mkdir -p work/tmp work/logs
export TMPDIR="$PWD/work/tmp/"
export PYTHONDONTWRITEBYTECODE=1
work/venv/bin/python tools/prepare_modules.py --check
```


Quit the game before building. Builds, the full test suite and runtime shader compilation share generated directories, so run them one at a time. Never edit `work/build` by hand.

## Tests

| Goal | Command |
| --- | --- |
| Repository checks: document links, dependency manifests, private paths | `python3 -m unittest discover -s tests -p test_repository.py -v` |
| Install the test dependencies | `work/venv/bin/python -m pip install --no-cache-dir -r requirements/test.txt` |
| Full Python, native and Metal suite | `./script/test.sh` |
| Metal 3 indices, conditional predicates, dispatch batching, constants and MetalFX GPU read-back | `bash script/test_metal3.sh` |
| Build and check loading only | `./script/build_and_run.sh --load-only` |
| Real SDK clock, audio, font and rumble paths | `./script/build_and_run.sh --time-test` (also `--codec-test`, `--font-test`, `--vibration-test`) |

Run the checks related to your change first; run the native and Metal suites when you touch shared graphics, loading or threading. The full `test.sh` needs the prepared local environment and a Metal device, so it is not a resource-free CI job. The complete build log is `work/logs/build.log`.

`test_metal3.sh` uses the older command API and runs on macOS 15 with Xcode 16. It checks shared and private index buffers, preceding GPU writes, nonzero offsets, every pixel of an indexed quad, constant updates before draws, and MetalFX window/4K output. It also checks index reuse and writer invalidation, CPU predicate snapshots and known timestamp values before their completion callbacks, and dependent compute dispatches in both batched and separate encoders. Several tests in the full suite require Metal 4 and macOS 26; the Metal 3 check provides a separate entry point on older systems. Generated shader libraries can contain diagnostic edits: a successful compile alone does not prove that a cached shader still performs its original work. Preserve the edited file and regenerate the affected shader from its SPIR-V when investigating a solid debug color.

Desktop launches retain one directory per run under `work/logs/sessions/`. `native-probe.log`, `performance.tsv` and `latest-session` point to the latest run. Each session includes `runtime.log`, `performance.tsv`, sampled process-group physical memory and system pressure in `memory.jsonl`, and `exit.json` with the exit code or signal. Daily launches default to no timeout; command-line build-and-run probes retain the 120-second default. A `SIGALRM` or native exit 142 identifies a configured timeout. `SIGKILL` alone does not prove an out-of-memory termination; correlate it with macOS DiagnosticReports and system logs. If the supervisor is also killed, its final exit report may be absent, but already-written samples remain.

Software checks cannot replace hands-on checks: what is visible on screen, what you hear and what a controller's rumble feels like must be checked on the real device.

## Scripted input

`ZELDA_INPUT_SCRIPT=<file>` runs an input script after shader preparation; `script/send_input.py` sends more lines to a running game through its FIFO. A script is a list of lines:

| Line | Meaning |
| --- | --- |
| `wait <ms>` | Wait |
| `tap <key> [ms]` / `down <key>` / `up <key>` | Tap, hold or release a host keyboard key; names follow the keyboard mapping |
| `mark <text>` | Write a marker to the log |
| `shot <path>` | Save a screenshot |
| `trace <seconds> <path>`, `dump <frames>` | Record a frame timeline / dump graphics commands |
| `fps <30\|60\|90\|120>`, `render <50..200>` | Change the frame rate target / the internal render scale |

`script/scenes/first-save.input` and `first-save-fast.input` walk the menu to the first save. Scripts know nothing about your save list, so check where you end up. Never use scripts to advance the story or to save.

## Environment variables

| Variable | Purpose |
| --- | --- |
| `ZELDA_TIMEOUT_SECONDS` | Stop the game after N seconds (0 = never, maximum 3600). `build_and_run.sh` defaults to 120; the launchers default to 0 |
| `ZELDA_SETTINGS` | Use another settings file instead of `work/settings.json` |
| `ZELDA_TARGET_FPS`, `ZELDA_METALFX` | Override the saved frame rate target and MetalFX setting for this run |
| `ZELDA_SHARED_FONT` | TrueType font used as the shared system font |
| `ZELDA_SHADER_IR=0`, `ZELDA_SHADER_PREP_ALL=1` | Ignore offline Metal libraries / force a full shader preparation pass |
| `ZELDA_DAY_RATE` | Scale the in-game day/night clock (0–16, 0 pauses it). Frame rate and physics are unaffected |
| `ZELDA_WINDOW_POINTS` | Window size such as `1600x900` |
| `ZELDA_RENDER_SCALE` | Experimental render scale (0.5–2). Clear it for normal play |
| `ZELDA_INDEX_REUSE=0`, `ZELDA_M3_PREDICATE_REUSE=0`, `ZELDA_M3_DISPATCH_BATCH=0` | Disable index reuse, Metal 3 CPU predicate reuse/forwarding, or serial compute batching for controlled comparisons |
| `ZELDA_THREAD_CPU=1`, `ZELDA_IPC_LOG=1` | Diagnostics: per-thread CPU census to `work/logs/threads.tsv`, IPC logging |

Many other `ZELDA_*` and `DM_*` switches exist for A/B comparisons of individual optimizations. They default to the validated behavior; search for `getenv` in `host/` before relying on one.

## Imported code and dependencies

- `core/` is imported code kept byte-identical: the replacement anchors in `script/prepare_graphics.py`, the shader chain fingerprint and the module fingerprints all depend on its exact bytes. Prefer changing the adapters. If a `core/` file must change, update the anchors, add a proven alias in `script/shader-chain-aliases.json` or regenerate the shader cache, and re-run `prepare.sh`; see [`core/README.md`](../core/README.md).
- Python dependencies are pinned in [`requirements/`](../requirements/): `resources.txt` includes `runtime.txt`, `test.txt` includes `resources.txt`, and `ios-transfer.txt` is a separate environment.
- Every game patch must stay bound to the game version, check the original bytes and keep its manifest verification.

## Ground rules

- **Original quality, normal speed.** Optimizations must not lower resolution, drop effects, skip draws or speed up the game. Never disable a check or fake a success value to make a test pass.
- Source code, comments, UI text, log messages and file names are in English. Documentation is English-first, with a Simplified Chinese copy (`*.zh-CN.md`) kept in sync. Encode required non-ASCII upstream anchors and Unicode test characters as escapes; `tests/test_repository.py` enforces this.
- Change the sources in `host/`, `ios/`, `script/` and `tools/`, never the generated files in `work/build`. Keep `core/` unchanged unless you follow the steps above.
- Never commit game files, keys, saves, logs or build products. `local/` and `work/` are ignored; see [Repository layout](repository.md). Do not run `git clean` in a prepared checkout: it would delete your local data.

## Contributing

1. Keep each commit to one change, with a clear message. Check the staged content with `git diff --cached --check` and `git diff --cached --stat`.
2. Include tests for new behavior. Graphics changes need a Metal read-back test where possible.
3. Describe what you verified and how (unit tests, build, on-screen check, device), and what you did not verify.
4. Update the English and Chinese documentation together.

Contributions are accepted under the [Apache License 2.0](../LICENSE), the license of this repository.

### iOS touch and cutscene regression checks

Touch sticks use UIKit tracking without haptic feedback. Standard actions stay visible while the native UI bridge is idle; context changes enlarge them in place. Native widescreen covers both initial allocation and later resolution selection. The logical crop, view rectangles, UI canvas and player camera share the same aspect; presentation is uniform. SetResolution does not expand the fixed NVN display texture, so the logical crop must remain within its original bounds. A mode change after startup is saved for the next app launch.

`prepare_ios_shader_support.py` bundles every validated offline variant request and its base stages, as well as the movie programs. On-device logs reporting `Captured Zelda ... variant` followed by `Shader translation is required on the Mac` identify missing scene resources, including after a cutscene. Rebuild the support set and app; the phone validates and repairs the matching cache on launch. A successful build verifies coverage and integrity, not a full gameplay run.

Joystick SDK dispatch is checked by `DEVELOPER_DIR=/path/to/Xcode/Contents/Developer bash script/test_ios_touch_controls.sh <booted-simulator-UDID>`. The test calls through UIControl's real tracking selectors, verifying override dispatch, value events, clamping, release and cancellation. C input tests alone cannot detect a misspelled UIKit callback.

## Desktop shader compilation and stutter

Mac retains on-demand MSL compilation and runtime guest-shader translation. It does not require the complete iOS offline IR bundle. Recorded shaders and pipelines still warm at startup. Prefetch retains up to 128 libraries with a 32 MiB source-cost budget, so draws reuse matching library objects instead of requesting source compilation again. Changed source contents invalidate a cached entry.

`host/pipeline_cache.inc` shares the `MTLBinaryArchive` adapter between Mac and iOS. Desktop archives live in `work/shaders/pipeline_cache/` under `DM_MSL_DIR`, isolated by GPU, OS, shader chain and source-transform contract. Missing or corrupt caches fall back to normal compilation. Capture and delayed persistence run on a serial Utility queue, without changing caller-owned descriptors. Mac reads and publishes archives of at most 8 MiB and captures at most 128 pipeline creations per run. Over-budget archives fall back to the system driver cache before entering Metal: macOS 15 can expand large serialized archives into multiple GiB during loading and stall startup. Capture reservations use an atomic counter so draws never wait for the archive mutex. `PIPELINE_CACHE` logs report load, hit, miss and save events; set `ZELDA_PIPELINE_CACHE=0` for comparison.

`ZELDA_SHADER_COMPILE_JOBS` defaults to 2 and clamps to 1–4 for each startup warm-up, library-prefetch and predicted-pipeline queue. Prediction uses Utility priority; a pipeline needed by the current draw moves ahead of queued predictions. Lower concurrency trades cold compilation throughput for CPU capacity for audio, input and the desktop. Previously unseen shaders or variants must still finish compiling before correct drawing, so first encounters can still stall. Draws are not skipped or rendered with substitute shaders.

Run `work/venv/bin/python -m unittest discover -s tests -p test_desktop_updates.py -v` and `bash script/test_metal3.sh`. The desktop tests use temporary directories for cloud lifecycle ordering, compiler concurrency and priority, prefetched-library GPU output, and cross-process archive hits and corruption recovery. They do not access real saves.
