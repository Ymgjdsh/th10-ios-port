#!/usr/bin/env python3
"""Verify an independently checked-out upstream baseline and private assets.

This is not an iOS acceptance test. It never treats candidate output as an
expected result. Reports omit absolute input paths and are private by default.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM_COMMIT = "0074e589667ecff17dcce34a5f88736b75091d54"


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            result.update(block)
    return result.hexdigest()


def run(argv: list[str], cwd: Path, env: dict | None = None) -> str:
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                            encoding="utf-8", errors="replace", timeout=180)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): " + result.stdout + result.stderr)
    return result.stdout.strip()


def provenance(upstream: Path, expected: str) -> dict:
    commit = run(["git", "rev-parse", "HEAD"], upstream)
    if commit != expected:
        raise ValueError("The upstream checkout does not match the explicitly pinned commit")
    dirty = run(["git", "status", "--porcelain", "--untracked-files=no"], upstream)
    if dirty:
        raise ValueError("Tracked baseline sources have been modified: " + dirty)
    build = json.loads((upstream / "th10_web/artifacts/sdl3/build.json").read_text())
    mismatches = [name for name, sha in build["sourceFiles"].items()
                  if digest(upstream / name) != sha]
    if mismatches:
        raise ValueError("Build/source hash mismatch: " + ", ".join(mismatches))
    binary = upstream / "th10_web/artifacts/sdl3/th10-sdl.wasm"
    loader = binary.with_suffix(".mjs")
    if digest(binary) != build["sha256"] or digest(loader) != build["loaderSha256"]:
        raise ValueError("Built module does not match build.json")
    sdk = json.loads((upstream / "tools/emsdk/touhou-sdk.json").read_text())
    if sdk != build["toolchain"]:
        raise ValueError("Toolchain metadata does not match build.json")
    return {"commit": commit, "origin": run(["git", "remote", "get-url", "origin"], upstream),
            "trackedSourceClean": True, "verifiedBuildSourceFiles": len(build["sourceFiles"]),
            "wasmSha256": digest(binary), "wasmBytes": binary.stat().st_size,
            "loaderSha256": digest(loader), "buildJsonSha256": digest(binary.parent / "build.json"),
            "version": build["version"], "sdlVersion": build["sdlVersion"], "toolchain": sdk,
            "scope": "Independent upstream WebAssembly reference only; not an iOS candidate"}


def native_checks(upstream: Path, output: Path) -> dict:
    sdk = upstream / "tools/emsdk"
    env = dict(os.environ, EM_CONFIG=str(sdk / ".emscripten"))
    compiler = [sys.executable, str(sdk / "install/emscripten/emcc.py")]
    compiler_version = run(compiler + ["--version"], upstream, env).splitlines()[0]
    runner = output / "baseline-wasi-runner.mjs"
    runner.write_text("import {readFileSync} from 'node:fs';\n"
                      "import {WASI} from 'node:wasi';\n"
                      "const wasi=new WASI({version:'preview1',args:[],env:{},returnOnExit:true});\n"
                      "const {instance}=await WebAssembly.instantiate(readFileSync(process.argv[2]),"
                      "{wasi_snapshot_preview1:wasi.wasiImport});\n"
                      "process.exitCode=wasi.start(instance);\n", encoding="utf-8")
    tests = []
    for name, sources in [
        ("frame-cadence", ["portable/check-frame-cadence.cpp"]),
        ("numeric-truncate", ["portable/numeric/truncate-check.cpp",
                               "th10_web/artifacts/sdl3/objects/softfloat.o"]),
    ]:
        target = output / ("baseline-" + name + ".wasm")
        run(compiler + ["-O2", "-std=c++17", "-sDEFAULT_TO_CXX=1", "-ffp-contract=off",
                        "-fno-strict-aliasing", *[str(upstream / p) for p in sources],
                        "-sSTANDALONE_WASM=1", "-o", str(target)], upstream, env)
        stdout = run(["node", str(runner), str(target)], upstream)
        tests.append({"name": name, "passed": True, "stdout": stdout,
                      "source": sources[0], "sourceSha256": digest(upstream / sources[0]),
                      "wasmSha256": digest(target)})
    # The published harness tests TH08 and TH10 side by side against Berkeley
    # SoftFloat. This TH10-only checkout has no TH08 source; redirect that lane
    # to TH10 without changing the independently computed SoftFloat oracle.
    # Paired assertions are therefore duplicate TH10 coverage, not extra cases.
    original_harness = upstream / "portable/numeric/verify.cpp"
    source = original_harness.read_text(encoding="utf-8")
    source = source.replace('#include "../../th08_web/cpp/game/Arithmetic.hpp"', '')
    source = source.replace('"../../th10_web/cpp/game/Arithmetic.hpp"',
                            '"' + (upstream / "th10_web/cpp/game/Arithmetic.hpp").as_posix() + '"')
    source = source.replace('"ExactFloat.hpp"',
                            '"' + (upstream / "portable/numeric/ExactFloat.hpp").as_posix() + '"')
    source = source.replace("th08::", "th10::")
    adapted = output / "baseline-numeric-th10.cpp"
    adapted.write_text(source, encoding="utf-8")
    target = output / "baseline-numeric-th10.wasm"
    run(compiler + ["-O2", "-std=c++17", "-sDEFAULT_TO_CXX=1", "-ffp-contract=off",
                    "-fno-strict-aliasing", "-fno-exceptions", "-fno-rtti", str(adapted),
                    str(upstream / "th10_web/cpp/game/Arithmetic.cpp"),
                    str(upstream / "th10_web/artifacts/sdl3/objects/softfloat.o"),
                    "-sSTANDALONE_WASM=1", "--no-entry", "-o", str(target)], upstream, env)
    numeric_runner = output / "baseline-numeric-runner.mjs"
    numeric_runner.write_text("import {readFileSync} from 'node:fs';\n"
                              "import {WASI} from 'node:wasi';\n"
                              "const wasi=new WASI({version:'preview1',args:[],env:{}});\n"
                              "const {instance}=await WebAssembly.instantiate(readFileSync(process.argv[2]),"
                              "{wasi_snapshot_preview1:wasi.wasiImport});\n"
                              "wasi.initialize(instance);const x=instance.exports;const failure=x.verify(50000);\n"
                              "console.log(JSON.stringify({failure,checks:x.checks(),details:Array.from("
                              "new Uint32Array(x.memory.buffer,x.details(),12))}));process.exitCode=failure?1:0;\n",
                              encoding="utf-8")
    numeric_result = json.loads(run(["node", str(numeric_runner), str(target)], upstream))
    tests.append({"name": "numeric-th10-softfloat", "passed": True, **numeric_result,
                  "operandPairs": 600000, "source": "portable/numeric/verify.cpp",
                  "sourceSha256": digest(original_harness), "adaptedSourceSha256": digest(adapted),
                  "wasmSha256": digest(target), "reference": "Berkeley SoftFloat extended-precision operations",
                  "adaptation": "Missing TH08 comparison lane redirected to TH10. Paired assertions duplicate TH10; they are not independent extra cases. SoftFloat reference expressions unchanged."})
    return {"compiler": compiler_version, "node": run(["node", "--version"], upstream),
            "tests": tests,
            "adaptation": "Original test bodies unchanged. Emscripten 6.0.9 supplies WASI instead of unavailable WASI SDK 34; truncation links baseline TH10 SoftFloat object instead of missing TH08 object.",
            "unavailable": [
                {"test": "portable/numeric/verify.mjs", "reason": "TH08 arithmetic source and WASI SDK 34 absent from this TH10-only checkout"},
                {"test": "portable/check-archive-stream.mjs", "reason": "Requires absent TH08 source and original TH08 DAT"},
                {"test": "portable/check-preload-ownership.mjs", "reason": "Requires absent TH08 source and reference animations"},
                {"test": "portable/th10-architecture-behavior.mjs", "reason": "Original architecture-2.3 behavior/Japanese expected-result fixtures are not present; no replacement generated from the candidate"},
                {"test": "portable/th10-architecture-storage.mjs", "reason": "Original recording.rpyx replay fixture is not present"},
                {"test": "portable/check-transition-preload.mjs", "reason": "Previous sdl-release WebAssembly/loader comparison fixtures are not present"},
            ]}


def assets_check(assets: Path, upstream: Path, original: Path | None) -> dict:
    manifest = json.loads((assets / "manifest.json").read_text(encoding="utf-8"))
    if manifest["language"] != "chs":
        raise ValueError("Expected simplified Chinese private assets")
    expected = set(manifest["files"]) | {"manifest.json"}
    actual = {p.relative_to(assets).as_posix() for p in assets.rglob("*") if p.is_file()}
    if actual != expected:
        raise ValueError("Private asset inventory mismatch")
    for name, info in manifest["files"].items():
        path = assets / name
        if path.stat().st_size != info["bytes"] or digest(path) != info["sha256"]:
            raise ValueError("Private asset hash mismatch: " + name)
    layout = json.loads((assets / "music-layout.json").read_text())
    baseline_layout = json.loads((upstream / "th10_web/assets/sdl-native/music-layout.json").read_text())
    if layout != baseline_layout or len(layout) != 18:
        raise ValueError("Music layout differs from independent upstream")
    result = {"passed": True, "files": len(expected), "manifestSha256": digest(assets / "manifest.json"),
              "allManifestHashesMatch": True, "musicLayoutMatchesIndependentUpstream": True,
              "language": "chs", "gameData": [name for name in expected if name.startswith("game/")],
              "originalPcmReverified": False,
              "limitations": ["Compatibility blend LUT is generated arithmetic, not extracted GDI evidence",
                              "Assets and fonts remain private; this check establishes no iOS rendering equivalence"]}
    if original:
        sys.path.insert(0, str(ROOT / ".local/python"))
        import soundfile as sf
        tracks = []
        with (original / "thbgm.dat").open("rb") as source:
            for index, spec in enumerate(layout):
                source.seek(spec["offset"])
                pcm = source.read(spec["length"])
                decoded, rate = sf.read(str(assets / f"music/{index:02d}.flac"), dtype="int16", always_2d=True)
                if rate != 44100 or decoded.shape != (spec["length"] // 4, 2) or decoded.astype("<i2", copy=False).tobytes() != pcm:
                    raise ValueError("Original PCM differs at track " + str(index))
                if spec["loop"] % 4 or not 0 <= spec["loop"] < spec["length"]:
                    raise ValueError("Invalid loop alignment")
                tracks.append({"track": index, "frames": len(decoded), "loopFrame": spec["loop"] // 4,
                               "pcmSha256": hashlib.sha256(pcm).hexdigest()})
        if digest(original / "th10c.dat") != digest(assets / "game/th10c.dat"):
            raise ValueError("Simplified Chinese DAT differs from original input")
        result.update(originalPcmReverified=True, originalChineseDatIdentical=True, tracks=tracks)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-root", type=Path, required=True)
    parser.add_argument("--expected-commit", default=UPSTREAM_COMMIT)
    parser.add_argument("--assets", type=Path, default=ROOT / ".local/assets")
    parser.add_argument("--original", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / ".local/reports")
    args = parser.parse_args()
    upstream, output = args.baseline_root.resolve(), args.output.resolve()
    if upstream == ROOT:
        raise ValueError("Baseline must be a separate upstream checkout")
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / "baseline-verification.json"
    # Invalidate an older success before starting; an interrupted or failed
    # repeat must never leave a stale passing result under the current name.
    report_path.write_text(json.dumps({"schema": "th10-independent-baseline/1", "status": "running"}) + "\n")
    try:
        report = {"schema": "th10-independent-baseline/1", "status": "passed",
                  "provenance": provenance(upstream, args.expected_commit),
                  "checks": native_checks(upstream, output),
                  "assets": assets_check(args.assets.resolve(), upstream, args.original)}
    except Exception as error:
        report_path.write_text(json.dumps({"schema": "th10-independent-baseline/1", "status": "failed",
                                          "errorType": type(error).__name__,
                                          "error": "Verification did not complete; inspect command stderr."}) + "\n")
        raise
    report["claims"] = {"independentBaselineVerified": True, "iosValidated": False,
                         "originalExecutableEquivalent": False, "behaviorGoldenComparisonPassed": False}
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"commit": report["provenance"]["commit"],
                      "sourceFiles": report["provenance"]["verifiedBuildSourceFiles"],
                      "tests": report["checks"]["tests"],
                      "verifiedAssetFiles": report["assets"]["files"],
                      "originalPcmReverified": report["assets"]["originalPcmReverified"]}, indent=2))


if __name__ == "__main__":
    main()
