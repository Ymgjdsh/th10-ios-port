# Original game application icon

The iOS application uses the icon embedded in the user's original `th10.exe`.
The preparation tool reads `RT_GROUP_ICON` and its exact `RT_ICON` resources;
it never runs the Windows executable and does not redraw or substitute the art.

Install Python dependencies, then prepare the private asset catalog:

```text
python -m pip install Pillow==12.2.0 pefile==2024.8.26
python ios/tools/prepare_icon.py --exe <original-game-directory>/th10.exe
```

The default output is `.local/app-icon/`. It contains `original.ico`, preserving
the original icon frames, and `original.png`, preserving the selected frame's
transparency. `Assets.xcassets/AppIcon.appiconset/` contains all iPhone and iPad
icon sizes required by this iOS 14 application and the 1024-pixel marketing icon.
`icon-manifest.json` records source/resource/output hashes and tool versions;
it contains no absolute source paths.

This installation's `th10.exe` contains one 32 × 32, 32-bit icon in named group
`IDI_ICON3`, resource language 1041. Larger exports necessarily enlarge those
original pixels. They use Lanczos scaling and an opaque warm off-white
background (`#F2EDE4`) to keep the black hat visible. No AI enhancement, redrawing,
cropping, or rounded-corner mask is applied; iOS supplies its normal icon mask.
Use `--background '#RRGGBB'` to choose another opaque background.

`--group` and `--language` select a resource explicitly when an executable has
several groups/languages. `--output` selects a dedicated private directory.
The iOS CMake target reads `.local/app-icon/Assets.xcassets` by default;
`-DTH10_ICON_CATALOG=<private-directory>/Assets.xcassets` overrides that path.
Copy the prepared private catalog to the Mac before configuring the iOS build.

The icon is original game content, not an open-source resource. Keep the EXE,
ICO, catalog and icon manifest in private build inputs. This repository ships
the documentation, the tool and the scaled icon used by the README header; the
remaining icon sizes stay in private build inputs.
