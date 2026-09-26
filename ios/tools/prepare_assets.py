#!/usr/bin/env python3
"""Prepare private TH10 simplified-Chinese assets; no EXEs or absolute paths.

Requires numpy and soundfile 0.13.1 for lossless music conversion.
"""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
if (ROOT / ".local/python").is_dir():
    sys.path.insert(0, str(ROOT / ".local/python"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1048576), b""):
            digest.update(block)
    return digest.hexdigest()


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_name(path.name + ".pending")
    pending.write_bytes(data)
    pending.replace(path)


def write_json(path: Path, value: object) -> None:
    write_atomic(path, (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))


def copy_verified(source: Path, target: Path) -> dict:
    if not source.is_file():
        raise ValueError("Missing required input: " + source.name)
    target.parent.mkdir(parents=True, exist_ok=True)
    expected = sha256(source)
    if not target.is_file() or sha256(target) != expected:
        pending = target.with_name(target.name + ".pending")
        shutil.copyfile(source, pending)
        if sha256(pending) != expected:
            raise ValueError("Copy verification failed: " + source.name)
        pending.replace(target)
    return {"sourceName": source.name, "sha256": expected, "bytes": source.stat().st_size}


def make_codepages() -> tuple[bytes, dict]:
    # FontHost: CP932 then CP936, each 65536 little-endian UTF-16 entries.
    # Windows includes single-byte and private-use mappings absent in GBK.
    if os.name != "nt":
        raise ValueError("Generate codepages.bin on Windows, or supply --tables-from")
    convert = ctypes.windll.kernel32.MultiByteToWideChar
    convert.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_char_p,
                        ctypes.c_int, ctypes.c_wchar_p, ctypes.c_int]
    convert.restype = ctypes.c_int
    result, mappings = bytearray(), []
    for page in (932, 936):
        mapped, table = 0, bytearray()
        for code in range(65536):
            source = bytes([code]) if code < 256 else bytes([code >> 8, code & 255])
            dest = ctypes.create_unicode_buffer(2)
            count = convert(page, 8, source, len(source), dest, 2)
            value = ord(dest[0]) if count == 1 else 0xFFFD
            if count == 1:
                mapped += 1
            table.extend(struct.pack("<H", value))
        result.extend(table)
        mappings.append({"codepage": page, "mappedEntries": mapped,
                         "invalidEntry": "U+FFFD", "sha256": hashlib.sha256(table).hexdigest()})
    for page_index, encoded, expected in ((0, "あ".encode("cp932"), "あ"),
                                           (1, "风".encode("gbk"), "风")):
        code = int.from_bytes(encoded, "big")
        actual = struct.unpack_from("<H", result, page_index * 131072 + code * 2)[0]
        if actual != ord(expected):
            raise ValueError("Codepage sanity check failed")
    return bytes(result), {"generator": "Windows MultiByteToWideChar / MB_ERR_INVALID_CHARS",
                           "format": "CP932 then CP936; 65536 uint16 little-endian entries each",
                           "tables": mappings}


def make_blend_compatibility() -> bytes:
    # Current FontHost loads this file but does not index it. This complete
    # LUT documents its direct arithmetic, not an extracted GDI table.
    # Index: (before * 256 + target) * 256 + coverage.
    return bytes((before * (255 - coverage) + ((target * 15 + 127) // 255) * coverage + 127) // 255
                 for before in range(16) for target in range(256) for coverage in range(256))


def prepare_music(original: Path, layout: list[dict], out: Path) -> tuple[list[dict], dict]:
    import numpy as np
    import soundfile as sf
    if len(layout) != 18:
        raise ValueError("TH10 requires exactly 18 music tracks")
    expected_end = 16
    for i, track in enumerate(layout):
        if track["offset"] != expected_end or track["length"] <= 0:
            raise ValueError("Music layout is not contiguous at track " + str(i))
        if track["length"] % 4 or track["loop"] % 4 or not 0 <= track["loop"] < track["length"]:
            raise ValueError("Music PCM alignment or loop is invalid")
        expected_end = track["offset"] + track["length"]
    if original.stat().st_size != expected_end:
        raise ValueError("thbgm.dat size does not match the TH10 music layout")
    out.mkdir(parents=True, exist_ok=True)
    reports = []
    with original.open("rb") as source:
        for i, track in enumerate(layout):
            source.seek(track["offset"])
            pcm = source.read(track["length"])
            if len(pcm) != track["length"]:
                raise ValueError("Incomplete music PCM")
            samples = np.frombuffer(pcm, dtype="<i2").reshape(-1, 2)
            target, valid = out / f"{i:02d}.flac", False
            if target.exists():
                try:
                    decoded, rate = sf.read(str(target), dtype="int16", always_2d=True)
                    valid = rate == 44100 and decoded.astype("<i2", copy=False).tobytes() == pcm
                except RuntimeError:
                    pass
            if not valid:
                pending = target.with_name(target.name + ".pending")
                sf.write(str(pending), samples, 44100, subtype="PCM_16", format="FLAC")
                decoded, rate = sf.read(str(pending), dtype="int16", always_2d=True)
                if rate != 44100 or decoded.shape != samples.shape or decoded.astype("<i2", copy=False).tobytes() != pcm:
                    raise ValueError("FLAC PCM differs from original track " + str(i))
                pending.replace(target)
            reports.append({"track": i, "file": target.name, "codec": "flac", "lossless": True,
                            "sampleRate": 44100, "channels": 2, "frames": len(samples),
                            "loopFrame": track["loop"] // 4, "pcmBytes": len(pcm),
                            "encodedBytes": target.stat().st_size,
                            "pcmSha256": hashlib.sha256(pcm).hexdigest(), "encodedSha256": sha256(target)})
            print(f"Music {i + 1}/18: full decoded PCM identical, loop frame {track['loop'] // 4}", flush=True)
    return reports, {"soundfile": sf.__version__, "libsndfile": sf.__libsndfile_version__, "numpy": np.__version__}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", required=True, type=Path, help="This TH10 th10c.dat and thbgm.dat directory")
    parser.add_argument("--font-dir", required=True, type=Path, help="Licensed msgothic.ttc and simhei.ttf directory")
    parser.add_argument("--output", type=Path, default=ROOT / ".local/assets")
    parser.add_argument("--layout", type=Path, default=ROOT / "th10_web/assets/sdl-native/music-layout.json")
    parser.add_argument("--tables-from", type=Path, help="Reuse previously prepared codepages.bin and blend.bin")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == args.original.resolve() or output == ROOT:
        raise ValueError("Output must be a dedicated private build-assets directory")
    allowed = {"game/th10c.dat", "fonts/msgothic.ttc", "fonts/simhei.ttf", "fonts/codepages.bin", "fonts/blend.bin",
               "manifest.json", "music-layout.json", "music-verification.json"}
    allowed.update(f"music/{i:02d}.flac" for i in range(18))
    if output.exists():
        unexpected = [p.relative_to(output).as_posix() for p in output.rglob("*")
                      if p.is_file() and p.relative_to(output).as_posix() not in allowed]
        if unexpected:
            raise ValueError("Unexpected files in output; use a clean directory: " + ", ".join(unexpected[:10]))
    sources = {"game": copy_verified(args.original / "th10c.dat", output / "game/th10c.dat")}
    for name in ("msgothic.ttc", "simhei.ttf"):
        sources[name] = copy_verified(args.font_dir / name, output / "fonts" / name)
    if args.tables_from:
        for name in ("codepages.bin", "blend.bin"):
            sources[name] = copy_verified(args.tables_from / name, output / "fonts" / name)
        codepage_info = {"generator": "reused input; inspect original preparation manifest"}
    else:
        codepages, codepage_info = make_codepages()
        write_atomic(output / "fonts/codepages.bin", codepages)
        write_atomic(output / "fonts/blend.bin", make_blend_compatibility())
    if (output / "fonts/codepages.bin").stat().st_size != 262144:
        raise ValueError("codepages.bin must contain two complete uint16 maps")
    layout = json.loads(args.layout.read_text(encoding="utf-8"))
    write_json(output / "music-layout.json", layout)
    music, versions = prepare_music(args.original / "thbgm.dat", layout, output / "music")
    write_json(output / "music-verification.json", music)
    sources["music"] = {"sourceName": "thbgm.dat", "bytes": (args.original / "thbgm.dat").stat().st_size,
                        "sha256": sha256(args.original / "thbgm.dat")}
    files = {p.relative_to(output).as_posix(): {"bytes": p.stat().st_size, "sha256": sha256(p)}
             for p in sorted(output.rglob("*")) if p.is_file() and p.name != "manifest.json"}
    manifest = {"schema": "th10-ios-private-assets/1", "language": "chs", "sources": sources,
                "files": files, "codepages": codepage_info, "tools": versions,
                "preparationScriptSha256": sha256(Path(__file__)), "layoutSha256": sha256(args.layout),
                "fontHostSha256": sha256(ROOT / "th10_web/cpp/sdl/FontHost.cpp"),
                "blend": {"role": "compatibility input; current FontHost loads but never indexes it",
                          "generatedFrom": "FontHost blend_channel_4444 direct arithmetic",
                          "index": "(before * 256 + target) * 256 + coverage",
                          "entries": 16 * 256 * 256, "originalExtractedGdiTable": False},
                "checks": {"musicPcmByteIdentical": True, "musicTracks": 18, "exeIncluded": False,
                           "japaneseDatIncluded": False, "saveOrReplayIncluded": False},
                "rights": "Private local inputs only. Original content and fonts retain their respective rights."}
    write_json(output / "manifest.json", manifest)
    print(f"Prepared {len(files)} verified files ({sum(x['bytes'] for x in files.values())} bytes).", flush=True)
    print("Output: " + str(output), flush=True)


if __name__ == "__main__":
    main()
