#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/.local/tests"
mkdir -p "$OUT"
cd "$ROOT"
xcrun clang++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off portable/check-frame-cadence.cpp -o "$OUT/frame-cadence"
"$OUT/frame-cadence"
echo 'PASS: fixed 60 Hz cadence, 15/20/30/60/90/120/144/165 Hz callbacks, bounded catch-up and reset'
xcrun clang -std=c11 -O2 -DSOFTFLOAT_FAST_INT64 -DINLINE_LEVEL=5 -c th10_web/cpp/rebuild/third_party/softfloat.c -o "$OUT/softfloat.o"
xcrun clang++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off portable/numeric/truncate-check.cpp "$OUT/softfloat.o" -Wl,-dead_strip -o "$OUT/truncate-check"
"$OUT/truncate-check"
xcrun clang++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off ios/tests/touch_policy.cpp -o "$OUT/touch-policy"
"$OUT/touch-policy"
xcrun clang++ -std=c++17 -O2 ios/tests/joystick.cpp -o "$OUT/joystick"
"$OUT/joystick"
xcrun clang++ -std=c++17 -O2 ios/tests/lifecycle.cpp -o "$OUT/lifecycle"
"$OUT/lifecycle"
xcrun clang++ -std=c++17 -O2 th10_web/cpp/game/TextFormat.cpp th10_web/cpp/game/tests/TextFormatSmoke.cpp -o "$OUT/text-format"
"$OUT/text-format"
echo 'PASS: Apple text formatting, rounding modes, numeric locale and native string pointers'
xcrun clang++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off th10_web/cpp/game/tests/NativeLayoutSmoke.cpp th10_web/cpp/game/AnmVm.cpp th10_web/cpp/game/EclContext.cpp th10_web/cpp/game/EclStack.cpp th10_web/cpp/game/Timer.cpp th10_web/cpp/game/GameProgression.cpp th10_web/cpp/game/Arithmetic.cpp "$OUT/softfloat.o" -Wl,-dead_strip -o "$OUT/native-layout"
"$OUT/native-layout"
echo 'PASS: native ANM initialization, ECL high-address return, score statistics and file layouts'
xcrun clang++ -std=c++17 -O2 -fno-fast-math -ffp-contract=off ios/tests/ecl_jump.cpp th10_web/cpp/game/EclInterpreter.cpp th10_web/cpp/game/EclContext.cpp th10_web/cpp/game/EclStack.cpp th10_web/cpp/game/EclProgram.cpp th10_web/cpp/game/EclResources.cpp th10_web/cpp/game/GameMath.cpp th10_web/cpp/game/Arithmetic.cpp "$OUT/softfloat.o" -Wl,-dead_strip -o "$OUT/ecl-jump"
"$OUT/ecl-jump"
