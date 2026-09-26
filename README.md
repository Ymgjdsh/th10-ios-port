<p align="center">
  <img src="docs/app-icon.png" alt="Touhou 10 icon" width="180">
</p>

# TH10 iOS Port

A port of 東方風神録　～ Mountain of Faith by Team Shanghai Alice to iPad and
iPhone, built with SDL 3 and OpenGL ES 3.

This branch carries the iOS work on top of the TH10 3.5.1 C++ reconstruction
([YomotsuHisami/th10](https://github.com/YomotsuHisami/th10), upstream commit
`0074e589667ecff17dcce34a5f88736b75091d54`). The game logic, animation, ECL,
numeric and audio code come from that reconstruction. This branch adds the
native Apple application shell, the touch controls, the settings panel, device
packaging and the touch and rendering work needed to run the game on a real
device.

Compared to the upstream portable and browser builds, this branch:

- Builds a native iOS 14+ ARM64 application bundle instead of a browser page
- Adds touch controls: a virtual joystick plus Z / X / S buttons and gear and
  pause buttons, all sized for landscape play
- Fills the whole screen in landscape and keeps the original 640 x 480 image in
  portrait
- Adds a native settings panel for controls, display, performance, gestures and
  diagnostics
- Records and exports a local diagnostic log for device testing
- Packages the signed bundle as an IPA for TrollStore installation
- Keeps the browser and launcher sources used as the shared upstream reference

The iOS target is fixed to simplified Chinese. It uses the TH10 installation's
`th10c.dat` together with `thbgm.dat`; no other archive, EXE, save or replay is
read. Network multiplayer is not part of this target.

## Status

The current delivery is a simplified-Chinese device test build for iPad mini 5
running iOS 14, installed through TrollStore. Menu, first stage, music, touch
input, settings persistence and diagnostics export have been exercised on the
acceptance device. Full-run, replay and performance acceptance are still open.

The simulator runs software GLES, so simulator frame times are not evidence of
device performance. Text rendering and a few original Windows behaviours are
approximations. The validation and performance reports under `ios/` record what
was actually measured and which claims remain unsupported.

## Building

### Dependencies

- macOS with Xcode 14 or the equivalent iOS 16 SDK and its command line tools
- CMake 3.20 or newer
- Python 3
- SDL 3.4.2 and SDL_ttf 3.2.2, fetched by `ios/tools/fetch_dependencies.sh`

Private build inputs are never committed. They live in `.local/`:

| Path | Purpose |
| --- | --- |
| `.local/assets` | Prepared `th10c.dat`, background music, fonts and the music layout |
| `.local/dependencies` | Pinned SDL and SDL_ttf sources |
| `.local/app-icon` | App icon catalog extracted from the original `th10.exe` |

Prepare the resources on Windows with a licensed install and a licensed font
directory, then copy `.local` to the Mac:

```text
python ios/tools/prepare_assets.py --original <TH10-installation> --font-dir <licensed-font-directory>
python ios/tools/prepare_icon.py --exe <original-game-directory>/th10.exe
```

See [ios/ASSETS.md](ios/ASSETS.md) and [ios/ICON.md](ios/ICON.md) for the exact
inputs, outputs and hashes. The icon tool reads the `RT_GROUP_ICON` resources
from the executable, so the resulting icon is the original game icon.

### iOS

```sh
bash ios/tools/fetch_dependencies.sh
bash ios/tools/native_checks.sh
TH10_SDK=iphonesimulator TH10_DIAGNOSTICS=ON bash ios/build_ios.sh
TH10_SDK=iphoneos TH10_DIAGNOSTICS=OFF bash ios/build_ios.sh
python3 ios/tools/package_ipa.py \
  .local/build-iphoneos/RelWithDebInfo-iphoneos/th10.app \
  dist/TH10-iOS14-TrollStore.ipa
```

`TH10_BUILD_NUMBER`, `TH10_BUILD_DIR`, `TH10_BUILD_JOBS`, `TH10_DEPENDENCIES`
and `TH10_ASSETS` override their defaults. Packaging rejects diagnostic builds,
simulator Mach-O files, resource hash mismatches and bundles containing user
saves. The IPA is ad-hoc signed for TrollStore; it is not an App Store or
development-provisioned package. Full instructions are in
[ios/README.md](ios/README.md).

### Browser and portable targets

The shared renderer, platform layer and launcher still build for the browser:

```sh
python tools/download-emscripten.py
node portable/build.mjs --th10
node portable/serve.mjs
```

Build output goes to `th10_web/artifacts/` and is not tracked.

## Controls

The joystick is on the left in the default layout; Z, X and S are the face
buttons. Z is shoot and confirm, X is Bomb and back, and S is focus. The gear
button opens the settings panel and the pause button pauses the game or returns
from a menu.

Touch gestures stay available next to the buttons:

- Menu: swipe in any direction to move the cursor, tap to select
- Dialogue: tap to advance
- Gameplay: two-finger tap to Bomb, two-finger hold to focus, three-finger hold
  to pause
- Settings: two-finger tap to go back

Rotating the device, backgrounding the app and audio interruption all release
held inputs. Logic runs on the shared fixed 60 Hz scheduler, independent of the
display refresh rate.

## Settings

The gear button opens a native settings panel with four groups.

Operation:

- Movement mode: hybrid joystick or relative drag, drag only, joystick only
- No Button: hide the joystick and buttons while keeping settings and pause
- Free layout editing, stored separately for landscape and portrait
- Z tap-to-hold fire and S tap-to-hold focus
- Auto fire, auto focus, Auto Bomb, and fire while dragging

Joystick and buttons:

- Button opacity, button and joystick size, drag sensitivity, joystick dead zone
- Left-handed layout, haptic feedback

Display and performance:

- 60 or 30 FPS display submission, at 50, 75 or 100 percent output sharpness
- Smooth scaling or sharp pixel edges
- Performance information overlay and an always-visible hitbox marker

Gestures and diagnostics:

- Export the diagnostic log, containing the current and previous run
- Restore default settings and layout, keeping scores and unlocked content
- Developer mode, which exposes invincibility, score, items, power, lives and
  bullet clearing markers during play
- Cheat Code: entering `ymgjdsh` unlocks Extra, the practice stages and all
  music, matching the original game's unlock code

## Diagnostics

The gear button and **Export diagnostic log** write a text file with the current
and previous run. It contains frame timing, upload statistics and error state,
and no game resources, saves or replays. Logs stay on the device, are bounded to
about 1 MiB per run and are never uploaded automatically. The same files are
available in Files under the app's `Diagnostics` directory. After a crash,
restart the game once and export so the previous run is included.

## Repository layout

- `ios/`: native application shell, settings, packaging, validation and reports
- `portable/`: shared GLES renderer, numeric and input code, build orchestration
- `th10_web/`: reconstruction sources, SDL hosts, launcher and browser runtime
- `tools/`: pinned Emscripten installer and its metadata

## Credits

- [YomotsuHisami/th10](https://github.com/YomotsuHisami/th10), the TH10 3.5.1
  C++ reconstruction this port is built on
- Team Shanghai Alice for 東方風神録　～ Mountain of Faith
- SDL, SDL_ttf and their FreeType fork for the platform and font layers
- miniaudio and Berkeley SoftFloat for audio and numeric compatibility
- The CC0 eagler-th07 touch policy used as the reference for touch handling

## Assets and licensing

This repository does not include the original game executable, archives, music,
fonts, saves or replays. A runnable package must be assembled locally from files
you are entitled to use. Publishing this source does not grant rights to publish
the contents of the private application bundle.

Licensing is component specific. Keep the notices in `ios/licenses`, in the
`third_party` directories and beside the bundled launcher fonts with the
components they cover. No blanket license is asserted for the original game or
its assets.

## 中文说明

这是东方风神录（TH10）的 iOS 移植工程，基于 `YomotsuHisami/th10` 的 3.5.1
C++ 重制版本，使用 SDL3 与 OpenGL ES 3，支持 iOS 14 及以上的 ARM64 设备。
界面固定为简体中文，使用汉化版的 `th10c.dat` 与 `thbgm.dat`，不包含网络对战。

真机测试与安装说明见 [ios/DEVICE-TEST.zh-CN.txt](ios/DEVICE-TEST.zh-CN.txt)，
资源与图标准备见 [ios/ASSETS.md](ios/ASSETS.md) 与 [ios/ICON.md](ios/ICON.md)，
验证记录见 [ios/VALIDATION.md](ios/VALIDATION.md)。游戏数据、音乐、字体、存档、
构建产物和本机配置都不在仓库中，需要自行使用有合法来源的文件在本机准备。
