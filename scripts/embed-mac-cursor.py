#!/usr/bin/env python3
"""Embed the locally exported AppKit arrow, retaining its pixels and hotspot."""
from hashlib import sha256
from itertools import groupby
import json
from pathlib import Path
import shutil
import struct
import sys

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1])
metadata = json.loads((source / "mac-arrow.json").read_text())
width, height = metadata["width"], metadata["height"]
raw = (source / "mac-arrow.bgra").read_bytes()
assert len(raw) == width * height * 4
pixels = list(struct.unpack(f"<{width * height}I", raw))
visible = [(i % width, i // width) for i, pixel in enumerate(pixels) if pixel >> 24]
assert visible
# Remove unused transparent margins, keeping a two-source-pixel clear border.
left = max(0, min(x for x, y in visible) - 2)
top = max(0, min(y for x, y in visible) - 2)
right = min(width, max(x for x, y in visible) + 3)
bottom = min(height, max(y for x, y in visible) + 3)
cropped = [pixels[y * width + x] for y in range(top, bottom) for x in range(left, right)]
runs = [(len(list(values)), value) for value, values in groupby(cropped)]
assert [value for count, value in runs for _ in range(count)] == cropped
lines = ["#pragma once", "#include <cstdint>", "", "namespace td::mac_arrow {",
    "// Generated from the installed macOS NSCursor.arrow.image, not hand drawn.",
    f"// Source: {metadata['system']}; PNG SHA256: {sha256((source / 'mac-arrow.png').read_bytes()).hexdigest()}",
    f"inline constexpr int Width={right-left}, Height={bottom-top}, Density={metadata['density']};",
    f"inline constexpr double HotX={metadata['hotX']-left}, HotY={metadata['hotY']-top};",
    "// Run length and premultiplied BGRA pixel pairs.", "inline constexpr uint32_t Runs[]={"]
lines.extend(f"    {count},0x{value:08x}," for count, value in runs)
lines.extend(["};", "} // namespace td::mac_arrow", ""])
(root / "windows-client/src/mac_arrow_data.hpp").write_text("\n".join(lines))
assets = root / "windows-client/assets"
assets.mkdir(exist_ok=True)
for filename in ("mac-arrow.png", "mac-arrow.json"):
    shutil.copyfile(source / filename, assets / filename)
print(f"Embedded system arrow: {right-left}x{bottom-top} at {metadata['density']}x; {len(runs)} runs")
