# TH10 native iOS port

The native target uses the existing TH10 3.5.1 C++ game, SDL 3.4.2,
SDL_ttf 3.2.2 and OpenGL ES 3. It targets ARM64 iOS 14.0 and later.
The first release is fixed to simplified Chinese, with no network multiplayer.
The acceptance device is iPad mini 5 running iOS 14.

## Build on a Mac

Prepare private resources as described in [ASSETS.md](ASSETS.md). Install
Xcode (tested build environment: Xcode 14 / iOS 16 SDK), its command-line tools,
CMake 3.20 or newer, and Python 3. Keep assets and dependencies in `.local`.
Prepare the original EXE icon on Windows using [ICON.md](ICON.md), then copy
the private `.local/app-icon/Assets.xcassets` catalog to the same place on Mac.

```sh
bash ios/tools/fetch_dependencies.sh
bash ios/tools/native_checks.sh
TH10_SDK=iphonesimulator TH10_DIAGNOSTICS=ON bash ios/build_ios.sh
TH10_SDK=iphoneos TH10_DIAGNOSTICS=OFF bash ios/build_ios.sh
python3 ios/tools/package_ipa.py .local/build-iphoneos/RelWithDebInfo-iphoneos/th10.app dist/TH10-iOS14-TrollStore.ipa
```

`TH10_BUILD_JOBS` sets build concurrency (default 3). `TH10_BUILD_NUMBER`,
`TH10_BUILD_DIR`, `TH10_DEPENDENCIES` and `TH10_ASSETS` override their defaults.
Use a separate build directory when switching between diagnostic and release
work. Packaging rejects diagnostic builds, non-device Mach-O, resource hash
mismatches, web runtime payloads and embedded user saves.

The IPA is ad-hoc signed for the user-selected TrollStore installation route.
It is not an App Store or Apple development-provisioned package. Package
validation does not establish successful installation or game acceptance on a
physical device. Simulator evidence must list its actual iOS version.

## Controls and storage

Landscape fills the entire screen with translucent controls over the game.
The iPad mini 5 landscape ratio matches the original 640 x 480 image. Portrait
keeps the original aspect ratio with controls below. The left virtual joystick
provides movement and menu navigation; Z is shoot/confirm, X is Bomb and S is
focus. The top buttons use gear (settings) and pause/back symbols.
Rotation, backgrounding and audio interruption cancel active inputs.
Game logic uses the shared fixed 60 Hz scheduler independent of display rate.
Save data is written in the app's writable SDL preference path under `chs`,
with same-directory temporary-file replacement.

The top-left **gear** button opens the native settings panel. Choose
**导出诊断日志** to share the current and previous run as a text file.
The app also exposes `Diagnostics/current.log` and `previous.log` through
Files / Finder file sharing. Settings and sharing pause gameplay and release
held inputs. Logs are local, bounded to about 1 MiB per run, and never uploaded
automatically. After a crash, restart and export to include the previous run.
A fatal-signal marker is not a full operating-system crash backtrace.

## Source delivery and components

```sh
python3 ios/tools/package_source.py --output dist/TH10-native-source.tar.gz
```

The source archive excludes game data, music, fonts, EXEs, saves, replays,
toolchains, build output and local machine configuration. Private resources in
an IPA are not public source assets. No upload or public release is performed.

- TH10 reconstruction: <https://github.com/YomotsuHisami/th10>, upstream
  `0074e589667ecff17dcce34a5f88736b75091d54` (3.5.1).
- SDL 3.4.2: zlib license, `ios/licenses/SDL.txt`.
- SDL_ttf 3.2.2: `a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b`, zlib license.
- FreeType SDL fork: `9973564cfa63763a3e4ac67c09147899539b1e07`,
  FreeType license (`ios/licenses/FreeType.txt`).
- miniaudio and Berkeley SoftFloat: original notices remain beside their
  sources and are copied into the native bundle's `licenses` directory.
- Touch policy credits its CC0 eagler-th07 source in the shared header.

See the validation report supplied with each build for actual test results
and remaining device acceptance work.
