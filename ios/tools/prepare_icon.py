#!/usr/bin/env python3
"""Extract the actual Windows game icon and prepare private iOS AppIcon assets.

Requires Pillow and pefile. The executable is read as data and never executed.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
if (ROOT / ".local/python").is_dir():
    sys.path.insert(0, str(ROOT / ".local/python"))


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_name(path.name + ".pending")
    pending.write_bytes(data)
    pending.replace(path)


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def png_bytes(image) -> bytes:
    target = io.BytesIO()
    image.save(target, format="PNG", optimize=False, compress_level=9)
    return target.getvalue()


def main() -> None:
    import pefile
    import PIL
    from PIL import Image, ImageColor

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True, help="Original game executable (read only)")
    parser.add_argument("--output", type=Path, default=ROOT / ".local/app-icon")
    parser.add_argument("--group", help="RT_GROUP_ICON name or numeric ID, required if several groups exist")
    parser.add_argument("--language", type=int, help="Windows resource language ID, such as 1041")
    parser.add_argument("--background", default="#F2EDE4", help="Opaque iOS background, default warm off-white")
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or output == args.exe.resolve().parent:
        raise ValueError("Choose a dedicated private icon output directory")
    background = ImageColor.getrgb(args.background)
    if len(background) != 3:
        raise ValueError("The iOS icon background must be an opaque RGB color")

    executable = args.exe.read_bytes()
    pe = pefile.PE(data=executable, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
    resources = {}
    for kind in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if kind.id not in (3, 14):  # RT_ICON and RT_GROUP_ICON
            continue
        for name in kind.directory.entries:
            identifier = str(name.name) if name.name is not None else str(name.id)
            for language in name.directory.entries:
                entry = language.data.struct
                data = pe.get_data(entry.OffsetToData, entry.Size)
                if len(data) != entry.Size:
                    raise ValueError("Truncated icon resource")
                resources[kind.id, identifier, language.id] = data
    pe.close()
    candidates = [key for key in resources if key[0] == 14
                  and (args.group is None or key[1] == args.group)
                  and (args.language is None or key[2] == args.language)]
    if len(candidates) != 1:
        raise ValueError("Select exactly one icon group/language: " + repr(candidates))
    group_key = candidates[0]
    group = resources[group_key]
    reserved, kind, count = struct.unpack_from("<HHH", group)
    if reserved != 0 or kind != 1 or count == 0 or len(group) != 6 + count * 14:
        raise ValueError("Invalid RT_GROUP_ICON")

    icon_directory, payload, entries = bytearray(), bytearray(), []
    for index in range(count):
        width, height, colors, reserved, planes, bits, size, identifier = struct.unpack_from("<BBBBHHIH", group, 6 + index * 14)
        resource_key = (3, str(identifier), group_key[2])
        if resource_key not in resources:
            raise ValueError("Missing RT_ICON in selected resource language: " + repr(resource_key))
        data = resources[resource_key]
        if len(data) != size:
            raise ValueError("RT_GROUP_ICON size differs from RT_ICON payload")
        icon_directory.extend(struct.pack("<BBBBHHII", width, height, colors, reserved, planes, bits, size, 6 + count * 16 + len(payload)))
        payload.extend(data)
        entries.append({"id": identifier, "width": width or 256, "height": height or 256,
                        "bits": bits, "bytes": size, "sha256": digest(data)})
    ico = struct.pack("<HHH", 0, 1, count) + icon_directory + payload
    write(output / "original.ico", ico)
    # Pillow selects the largest/highest-depth image from the actual ICO frames.
    with Image.open(io.BytesIO(ico)) as image:
        source = image.convert("RGBA")
    if source.width != source.height:
        raise ValueError("The selected game icon must be square")
    source_png = png_bytes(source)
    write(output / "original.png", source_png)

    images = []
    for idiom, specifications in (
        ("iphone", ((20, (2, 3)), (29, (2, 3)), (40, (2, 3)), (60, (2, 3)))),
        ("ipad", ((20, (1, 2)), (29, (1, 2)), (40, (1, 2)), (76, (1, 2)), (83.5, (2,)))),
        ("ios-marketing", ((1024, (1,)),)),
    ):
        for points, scales in specifications:
            for scale in scales:
                pixels = int(points * scale)
                images.append({"idiom": idiom, "size": f"{points}x{points}", "scale": f"{scale}x",
                               "filename": f"AppIcon-{pixels}.png"})
    catalog = output / "Assets.xcassets"
    appicon = catalog / "AppIcon.appiconset"
    files = {}
    for pixels in sorted({int(float(item["size"].split("x")[0]) * int(item["scale"][0])) for item in images}):
        scaled = source.resize((pixels, pixels), Image.Resampling.LANCZOS)
        opaque = Image.new("RGBA", scaled.size, (*background, 255))
        opaque.alpha_composite(scaled)
        data = png_bytes(opaque.convert("RGB"))
        filename = f"AppIcon-{pixels}.png"
        write(appicon / filename, data)
        files[filename] = {"pixels": pixels, "bytes": len(data), "sha256": digest(data), "mode": "RGB"}
    write(appicon / "Contents.json", json_bytes({"images": images, "info": {"author": "xcode", "version": 1}}))
    write(catalog / "Contents.json", json_bytes({"info": {"author": "xcode", "version": 1}}))
    report = {
        "schema": "th10-ios-private-icon/1", "sourceName": args.exe.name,
        "sourceBytes": len(executable), "sourceSha256": digest(executable),
        "resourceGroup": group_key[1], "resourceLanguage": group_key[2],
        "groupSha256": digest(group), "resources": entries,
        "originalIcoSha256": digest(ico), "transparentPngSha256": digest(source_png),
        "sourceDimensions": list(source.size), "sourceAlphaExtrema": list(source.getchannel("A").getextrema()),
        "rendering": "Original icon pixels, Lanczos scaling, solid RGB background; no redrawing or rounded mask",
        "background": "#" + "".join(f"{value:02X}" for value in background),
        "tools": {"Pillow": PIL.__version__, "pefile": pefile.__version__},
        "files": files,
        "rights": "Private original game resource. Do not include the icon, executable or prepared assets in public source delivery.",
    }
    write(output / "icon-manifest.json", json_bytes(report))
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
