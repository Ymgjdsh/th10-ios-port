<p align="center">
  <img src="docs/app-icon.png" alt="Touhou 10 icon" width="180">
</p>

# TH10 iOS Port

A port of 東方風神録　～ Mountain of Faith by Team Shanghai Alice to iOS using SDL3 and OpenGL ES 3.

This branch carries the iOS work on top of the TH10 3.5.1 C++ reconstruction. Compared to the portable branch, this branch:

- Has native iOS 14+ ARM64 support with a TrollStore installable IPA
- Includes touch controls, with a virtual joystick and Z / X / S buttons
- Fills the entire screen in landscape and keeps the original aspect ratio in portrait
- Adds a native settings panel for controls, display, performance and diagnostics
- Exports a local diagnostic log for device testing

The iOS target is fixed to simplified Chinese. It uses the installation's `th10c.dat` together with `thbgm.dat`, and does not include network multiplayer.

## Building

### Dependencies

- macOS with Xcode 14 or the equivalent iOS 16 SDK
- CMake 3.20 or later
- Python 3

### Assets

Game data, music and fonts are private build inputs and stay out of this repository. Prepare them from an installation and a font directory you are entitled to use:

```sh
python ios/tools/prepare_assets.py --original <TH10-installation> --font-dir <licensed-font-directory>
python ios/tools/prepare_icon.py --exe <original-game-directory>/th10.exe
```

See [ios/ASSETS.md](ios/ASSETS.md) and [ios/ICON.md](ios/ICON.md) for the details.

### iOS

```sh
bash ios/tools/fetch_dependencies.sh
TH10_SDK=iphoneos TH10_DIAGNOSTICS=OFF bash ios/build_ios.sh
python3 ios/tools/package_ipa.py \
  .local/build-iphoneos/RelWithDebInfo-iphoneos/th10.app \
  dist/TH10-iOS14-TrollStore.ipa
```

`TH10_BUILD_NUMBER`, `TH10_BUILD_JOBS`, `TH10_ASSETS` and `TH10_DEPENDENCIES` override their defaults. The IPA is ad-hoc signed for TrollStore, not for the App Store. Simulator builds and the browser runtime are described in [ios/README.md](ios/README.md).

## Controls

The joystick is on the left and Z, X and S are the face buttons: Z is shoot and confirm, X is Bomb and back, and S is focus. The gear button opens the settings panel and the pause button pauses the game or returns from a menu. Swipe and tap in menus, tap in dialogue, two-finger tap to Bomb, two-finger hold to focus and three-finger hold to pause all stay available.

## Todo

- Implement control layout editing, which is still a placeholder entry
- Bring the text rendering closer to the original

## Credits

- The [YomotsuHisami/th10](https://github.com/YomotsuHisami/th10) 3.5.1 C++ reconstruction, used for the game logic, animation, ECL, numeric and audio code this branch is built on
- Team Shanghai Alice for 東方風神録　～ Mountain of Faith
- SDL, SDL_ttf and FreeType for the platform and font layers
- miniaudio, Berkeley SoftFloat and the CC0 eagler-th07 touch policy

## Assets and licensing

This repository does not include the original Touhou executable, archives, music, fonts, replays or saves. The game data, music and fonts are private local build inputs, and a runnable package must be assembled locally from files you are legally allowed to use.

Licensing is component-specific. Keep the notices and licenses beside each bundled component, as they are under `ios/licenses` and the `third_party` directories, and assert no blanket license over the original game or its assets.
