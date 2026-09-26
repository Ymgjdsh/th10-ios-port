# Private build resources

The first iOS target is fixed to simplified Chinese. It uses this TH10
installation's `th10c.dat` together with its `thbgm.dat`. It does not bundle
`th10.dat`, any EXE, another game's archive, saves, or replays. A traditional
Chinese executable is not evidence that the C++ runtime supports that version.

## Reproducible preparation

Run Python on Windows with NumPy and `soundfile==0.13.1` installed. The script
also detects a private installation of these dependencies in `.local/python`.
Supply your existing game installation and licensed font directory explicitly:

```text
python ios/tools/prepare_assets.py --original <TH10-installation> --font-dir <licensed-font-directory>
```

The default private output is `.local/assets`:

```text
game/th10c.dat
fonts/msgothic.ttc
fonts/simhei.ttf
fonts/codepages.bin
fonts/blend.bin
music/00.flac ... music/17.flac
music-layout.json
music-verification.json
manifest.json
```

`--output` selects another dedicated local directory. The script rejects
unexpected existing files, writes each file atomically, and verifies every
copy. It reads no EXE. It does not delete the user's input files.

The 18 FLAC tracks are encoded directly from the documented TH10 PCM offsets
at 44,100 Hz, stereo, 16-bit. Each track is fully decoded and compared byte for
byte with the original PCM; frame counts and loop offsets are checked. A
repeated preparation validates existing FLAC PCM before reusing a track.
`music-verification.json` contains input PCM and FLAC SHA-256 values.

`codepages.bin` is generated with Windows `MultiByteToWideChar` CP932 and CP936
with `MB_ERR_INVALID_CHARS`, matching the runtime's two 65,536-entry UTF-16
little-endian tables. Invalid entries become U+FFFD. This preserves Windows
single-byte and private-use mappings, which a generic GBK decoder may omit.
Mac builds should reuse the verified prepared tables (`--tables-from` if
re-preparing), not silently regenerate a different mapping.

The current 3.5.1 `FontHost.cpp` loads `blend.bin` but never indexes it. Actual
text composition calls `blend_channel_4444` directly. The generated compatibility
LUT exhaustively evaluates that exact arithmetic for before=0..15,
target=0..255, coverage=0..255. It is **not an extracted original GDI table** and
its existence does not establish pixel identity with Windows GDI. The manifest
records this distinction and hashes the generating script and current host.

`manifest.json` contains only basenames and relative bundle paths, sizes,
hashes, codec versions and verification claims. It deliberately omits private
absolute input paths. It must not be mistaken for gameplay, simulator, or
physical-device validation.

## Independent baseline verification

Keep the upstream reference in a separate private Git checkout. The reference
used for this port is `YomotsuHisami/th10` commit
`0074e589667ecff17dcce34a5f88736b75091d54` (3.5.1, SDL 3.4.2).
After building that checkout with its pinned Emscripten toolchain, run:

```text
python ios/tests/baseline_verify.py --baseline-root <private-upstream-checkout> --original <TH10-installation>
```

This verifies the commit, clean tracked source, all build source hashes,
WebAssembly and loader hashes, private asset inventory, and the music layout
against upstream. It also fully decodes all 18 FLAC files and compares them
with the original PCM again. The unchanged upstream cadence and numerical
truncation tests run through Emscripten/WASI; the report explicitly records
the compiler and dependency-path adaptations. Results are stored privately
in `.local/reports/baseline-verification.json`.

The broader numerical harness also runs with its missing TH08 comparison
lane redirected to TH10; its independent Berkeley SoftFloat reference
expressions are preserved. The 600,000 operand pairs cover three precisions
and four rounding modes. Paired assertions repeat TH10 coverage and must not
be counted as additional independent cases.

The unchanged two-game numerical harness and archive tests depend on absent
TH08 sources. The upstream behavior test depends on its original 2.3 expected
results, which are not included in this source distribution. These gaps must
remain visible; generating expected results from the current iOS candidate
would not establish equivalence. Passing reference tests verifies the
reference setup, not the iOS port, original Windows pixels, or device behavior.

## Rights and source delivery

The game data, music and Microsoft fonts are private local build inputs, not
open-source components. Use only inputs you are entitled to use. Do not include
`.local`, prepared assets, original archives, toolchains, recordings, saves or
signed build products in public source delivery. Preserve the repository's
component notices and license files. Publishing source does not grant rights to
publish the content of the private app bundle.
